# p2boost

p2boost is an experimental, open-source, native-Direct3D-9 x86 Source plugin
for Portal 2 Windows build `23973718`. It contains measured, reversible
frame-pacing and focus-restore corrections plus research/test infrastructure
for the portal and co-op paths. It does not replace game DLLs, force cvars,
reduce quality, or use Vulkan/DXVK.

This repository is deliberately fail-closed. The DLL hashes the five relevant
game binaries and validates the exact instruction/import signature before it
installs a hook. An unsupported build or non-Microsoft D3D9 backend remains
loaded for diagnostics but installs no fix.

## Current status

This is a research alpha, not a claim that all acceptance gates are met. The
verified pacing fix preserves the small amount by which the host loop polls
past an `fps_max` deadline instead of throwing that phase information away.
Only overshoot below 10% of one frame is carried; renderer-heavy frames and
real hitches are reset normally, so the patch cannot create catch-up bursts.
`fps_max`, VSync, fullscreen, and refresh policy remain engine-owned.

On focus restore, p2boost serializes the material system across the resource
reacquisition call to prevent the queued-render race observed in a native-D3D9
crash. During that exact call only, it suppresses two CPU static-lighting cache
rebuilds that focus cannot invalidate. Normal map, entity, and lighting cache
invalidation paths still call the original routines. In the co-op lobby this
cut measured synchronous reacquisition from about 564 ms to 251 ms. The
remaining D3D9 resource recreation cost is real and is not hidden or moved.

In portal-visible local co-op, Portal 2 also rebuilt a static projected-light
receiver projection every rendered frame. A five-second trace found 2,237 of
2,238 calls had byte-identical shadow state while consuming about 0.54 ms each.
p2boost caches only that exact state: any changed byte, relevant flashlight
cvar change, map transition, focus transition, or runtime toggle executes the
original rebuild. The original dirty-list cleanup still runs on cache hits.
At `fps_max 999` this raised the lobby average from 414.2 to 538.1 FPS and the
1% low from 315.8 to 381.8 FPS without changing GPU work.
See [research/report.md](research/report.md) and [benchmarks/report.md](benchmarks/report.md).

## Build and install

Requirements are Visual Studio 2022 with the x86 C++ toolchain and CMake 3.24+.

```powershell
.\scripts\bootstrap-tools.ps1
.\scripts\build.ps1
.\scripts\install.ps1
```

Run `.\scripts\format.ps1` before committing C++ changes. The same pinned
`.clang-format` policy is checked by GitHub Actions; use `-Check` for a
non-mutating local verification.

The build is C++17, Win32, `/MT`, CFG-enabled, and exports only
`CreateInterface`. Installation adds an idempotent marked block to
`portal2/cfg/autoexec.cfg`; uninstall removes only that block and p2boost's own
files:

```powershell
.\scripts\uninstall.ps1
```

The normal build compiles permanent per-draw instrumentation out. Reverse-
engineering experiments can opt into it in a separate tree with
`-DP2BOOST_RESEARCH_DRAW_TRACE=ON`; packaging and nightly releases always use
the default `OFF` configuration. Visibility research is split so probes can be
isolated: `P2BOOST_RESEARCH_VISIBILITY_TRACE` times world construction,
`P2BOOST_RESEARCH_PVS_TRACE` adds compressed-PVS expansion, and
`P2BOOST_RESEARCH_RENDERABLE_TRACE` adds client renderable-list stages. These
options propagate to the DLL startup translation unit so the compiled core
hooks are actually installed.

## Commands

- `p2boost_status` prints hashes/build support, native-D3D9 state, SAR state,
  enabled state, overlay-interface availability, and metric count.
- `p2boost_enable 0|1` switches the verified fix at runtime for A/B testing.
- `p2boost_trace 0|1` enables fixed-size QPC frame, D3D9 resource, portal,
  projected-shadow, queued-worker, concrete hardware-fence, and Present tracing.
- `p2boost_overlay 0|1` toggles a compact in-game profiler. Its one-second
  rolling panel shows actual render FPS, current frame time, p99, maximum, a
  48-frame hitch graph, average portal cost, measured render-worker occupancy,
  post-worker main-tail p99, SP/co-op, SAR, and fix state. It uses Portal 2's
  public `VDebugOverlay004` interface,
  so one local panel works for either online co-op role without duplicating
  across local split-screen views. Frames are sampled at the verified
  `CEngine::Frame` completion hook, not the 60 Hz server simulation callback.
  Collection and display stop completely when disabled.
- `p2boost_dump_metrics` writes `p2boost_metrics.csv` in the game root, including
  simulation, focus, local-co-op, resource-reacquisition, portal, and
  projected-shadow timings/cache decisions. When renderer-worker tracing has
  been active it also writes `p2boost_metrics_render_workers.csv`, with one row
  per queued render batch (wall time, thread cycles, queued-call count, buffer
  byte totals, fence time, and Present time). It does not timestamp every draw
  command, and the worker hook is a direct forwarder while tracing is off.

No command writes or archives a game cvar.

## Reproducing research

`scripts/run-ghidra.ps1` imports unmodified PEs into a headless Ghidra project
and exports symbols, strings, target decompilation, and callers. Generated
projects/artifacts are ignored because they contain derived game-binary data.
PresentMon capture and analysis scripts are under `scripts/`; the 30-cycle
focus harness is `run-focus-portal-cycles.ps1`. It verifies that the game
covers the selected monitor, alt-tabs away, then fires a fresh primary and
secondary portal through real portal-gun inputs after every restore. The
deterministic `run-coop-lobby-portals.ps1` runner creates two local players in
the co-op lobby, equips them, places a linked portal pair, restores fixed camera
poses, saves a proof screenshot, runs patched/unpatched captures, and restores
the user's original `fps_max` and `sv_cheats` values. The lobby, intro3, and
Laser Relays runners accept `-AlreadyForeground` when an external controller
has already activated the verified game window, avoiding a second focus
mechanism.

The co-op runner also writes a `-status.txt` file around each A/B interval and
captures PresentMon QPC timestamps. This proves which runtime state produced
each file and lets individual hitches align with p2boost metrics. Its final
diagnostic trace always runs with the candidate enabled, independent of A/B
order.

`run-sar-compatibility.ps1` repeatedly constructs both SAR/p2boost load
orders, unloads each plugin while the other remains resident, validates every
p2boost hook/status field and `sar_about`, then closes the game. It discovers
the displayed plugin indexes by name and never assumes that SAR is slot zero.

For the long lifecycle gate, the quick mode preserves real focus loss/restore
and both portal-gun shots while shortening only the idle delay between cycles:

```powershell
.\scripts\run-focus-portal-cycles.ps1 -Cycles 100 -QuickLifecycle
```

After verified restore and synchronous device reacquisition, the connected
portal is scheduled 300 ms later in simulation time. This makes it a stricter
first-use check than the normal profiling schedule.

For a synchronized CPU, GPU, disk, module-load, and frame-time trace, start
Portal 2 from an elevated PowerShell and run:

```powershell
.\scripts\capture-etw.ps1 -Output artifacts\etw\lobby.etl -Profile GeneralProfile -DurationSeconds 15
.\scripts\analyze-etw.ps1 -InputEtl artifacts\etw\lobby.etl
```

The capture script rejects a missing game window, reports WPR/PresentMon exit
codes, and verifies both output files after WPR finishes merging the ETL. The
analyzer exports the trace's image-load rundown so sampled addresses are
converted with the actual per-run ASLR bases rather than inferred offsets.

Interactive benchmark defaults are intentionally short: 10-second A/B
captures and 5 focus/portal cycles. Release-gate reproduction must pass
`-DurationSeconds 60` or `-Cycles 30` explicitly; shortening the smoke defaults
does not change the published acceptance methodology.

`run-gameplay-demos.ps1` replays matched patched/unpatched segments from a
single-player portal/traversal demo and two co-op demos (repulsion gel and
tractor beam), validating the map embedded in Source's accepted `playdemo`
response before every five-second default capture. It restores `fps_max` and leaves
all visual and gameplay cvars alone.

`run-portalgun-demo.ps1` is the dedicated intro3 first-portal regression. It
plays the complete 22.433-second demo from `portal2/a`, verifies the embedded
`sp_a1_intro3` header, and captures enough load margin to include the successful
portal placement around 14 seconds of demo time. The first nonzero portal-render
metric identifies the placement; an earlier attack input is not mistaken for a
successful portal. The runner saves playback, frame, engine, and render-worker
artifacts, restores settings, and closes Portal 2 by default after every run.
Pass `-KeepGameOpen` only for an explicitly interactive follow-up. The shared
shutdown helper waits after forced termination, preventing a mapped research
DLL from leaking into the next run. Research captures may add
`-SampleMainThread` for one-millisecond main-thread instruction samples or
`-SampleAdaptive` for coarse process-wide worker attribution. The modes are
mutually exclusive and use the same QPC clock as PresentMon and p2boost.
Every intro3 run also stores an x86 `-modules.csv` snapshot so absolute stack
addresses can be reduced to stable DLL RVAs after the game closes.

`run-live-gameplay.ps1` is the primary short performance check. It records
matched single-player Laser Relays and two-player lobby runs while genuine
player commands move and fire both portal guns. The SP route starts at the
normal spawn, rides the elevator, and walks under collision into the far side
of the main relay room for twenty seconds. It then fires a linked portal pair
on separate walls and leaves a portal interior visible. It does not use noclip,
a position override, or in-capture RCON traffic. Proof screenshots are saved
beside the CSVs. The default 24-second duration includes the complete route.
Start the game with `+ss_map mp_coop_lobby_2` before a combined run, then use:

```powershell
.\scripts\run-live-gameplay.ps1 -Scenario Both -FpsMax 360
.\scripts\run-live-gameplay.ps1 -Scenario Both -FpsMax 999
.\scripts\run-live-gameplay.ps1 -Scenario Both -FpsMax 360 -CandidateFirst
.\scripts\run-live-gameplay.ps1 -Scenario Coop -DurationSeconds 10 -PatchedOnly
```

The runner restores `fps_max`, `sv_cheats`, all held inputs, tracing, and the
enabled patch state even after a failed capture. PresentMon's undefined first
interval on each swap chain is excluded by `analyze-presentmon.ps1`; otherwise
that trace-boundary sample can be misreported as a multi-second game hitch.
Release comparisons run both normal and `-CandidateFirst` orders so cold
native-driver work cannot be silently credited to the second variant.

`run-laser-trace.ps1` is the research-metrics version of the same grounded SP
route. It records the complete portal-mesh and server laser-think timers
alongside the queued-render worker, native draw, fence, and presentation
metrics. It also restores every held input and setting and closes Portal 2 by
default, including after a failed capture.

`run-sphere-peek.ps1` reproduces the Sphere Peek chamber transition and faith
plate from the normal spawn. It clears the entrance panel with a one-second
left input, walks into the main room, then moves left for only twelve ticks
before continuing forward onto the catapult. The runner verifies Portal 2 is
the foreground process, writes a chronological top-hitch report, and can align
research-only main-thread samples to PresentMon with `-SampleMainThread`.

```powershell
.\scripts\run-sphere-peek.ps1 -Variant Both -DurationSeconds 23
.\scripts\run-sphere-peek.ps1 -Variant Patched -SampleMainThread
```

`run-laser-etw.ps1 -Scenario SpherePeek` performs the same grounded route
inside an elevated GeneralProfile trace. The ETW helper reclaims interrupted
WPR and PresentMon sessions before starting, so a stale global recorder cannot
silently leave the artifact directory empty.

`p2profile.exe` is a research-only x86 user-mode sampler for systems where
kernel ETW collection is unavailable without elevation. Its default mode
records low-overhead main-thread instruction pointers. `--thread <tid>` targets
one known worker, while `--adaptive` watches process-wide thread CPU deltas and
samples only the thread that most recently consumed CPU. Explicit `--stack`
mode walks call stacks and is intrusive enough to invalidate timing
measurements; it is only for call-path research. The profiler is never loaded
into the game.

Every push and pull request is built, tested, packaged, and uploaded by GitHub
Actions. Pushes to the default branch also update the rolling `nightly`
prerelease and its SHA-256 file; `v*` tags create normal releases.

Only legally installed binaries, Valve's public Source SDK, and SAR's public
repository are valid research inputs. Leaked Source trees and old IDA databases
are explicitly out of scope.
