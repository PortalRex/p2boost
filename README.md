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
the default `OFF` configuration.

## Commands

- `p2boost_status` prints hashes/build support, native-D3D9 state, SAR state,
  enabled state, and metric count.
- `p2boost_enable 0|1` switches the verified fix at runtime for A/B testing.
- `p2boost_trace 0|1` enables fixed-size QPC frame, D3D9 resource, portal,
  projected-shadow, queued-worker, concrete hardware-fence, and Present tracing.
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
focus harness is `run-focus-portal-cycles.ps1`. The deterministic
`run-coop-lobby-portals.ps1` runner creates two local players in the co-op
lobby, equips them, places a linked portal pair, restores fixed camera poses,
saves a proof screenshot, runs patched/unpatched captures, and restores the
user's original `fps_max` and `sv_cheats` values.

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

`p2profile.exe` is a research-only x86 user-mode sampler for systems where
kernel ETW collection is unavailable without elevation. Its default mode
records low-overhead main-thread instruction pointers. Explicit `--stack` mode
walks call stacks and is intrusive enough to invalidate timing measurements;
it is only for call-path research. The profiler is never loaded into the game.

Every push and pull request is built, tested, packaged, and uploaded by GitHub
Actions. Pushes to the default branch also update the rolling `nightly`
prerelease and its SHA-256 file; `v*` tags create normal releases.

Only legally installed binaries, Valve's public Source SDK, and SAR's public
repository are valid research inputs. Leaked Source trees and old IDA databases
are explicitly out of scope.
