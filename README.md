# p2boost [![Build and release](https://github.com/aIIison/p2boost/actions/workflows/build-release.yml/badge.svg)](https://github.com/aIIison/p2boost/actions/workflows/build-release.yml)

**A drop-in performance plugin for Portal 2 on Windows** (Steam build
`23973718`, Vulkan only). Portal 2's render path is CPU-bound: most of the work
runs on one core, the GPU sits idle, and the bundled 2020-era DXVK adds large
per-draw translation cost. p2boost removes that waste — a frame-pacing
correction for `fps_max`, a projected-shadow cache, exact shadow-depth frustum
culling, and an approved DXVK 3.0.2 replacement that doubles frame rate on
CPU-bound scenes — without changing visuals, input, or simulation.

**Install in under a minute:**

1. Download `p2boost-nightly-win32.zip` from the latest nightly release.
2. Extract it into your `Portal 2` game folder: `p2boost.dll` goes next to
   `portal2.exe`, and `bin\dxvk_d3d9.dll` goes into `bin\`.
3. Add `-vulkan` to Portal 2's Steam launch options.
4. Check `p2boost_status` in the console to confirm everything is active.

That is the entire release layout — the zip contains the plugin DLL, the
approved DXVK replacement, and their licenses, already in the right folders.
No launcher, no installer, no config edits. The plugin loads through the
ordinary Source `plugin_load p2boost` command, which `scripts/install.ps1` adds
to your autoexec when you build from source.

> [!WARNING]
> **p2boost is not currently approved for Portal 2 speedrun submissions.** The
> moderator agreement is that it remains disallowed until it has been fully
> verified. Do not record a run with p2boost loaded if you may want to submit
> it: a run made before a future approval remains ineligible even after the
> plugin is approved. Unloading the plugin later does not make that recording
> eligible.

## Features

- More consistent `fps_max` frame delivery without changing simulation, input,
  VSync, fullscreen, or refresh-rate behavior.
- A client-update-scoped projected-shadow cache that skips duplicate
  high-refresh rebuilds, plus opt-in exact frustum rejection for projected
  shadow maps outside the main camera (`p2boost_shadow_cull`). Model-instance
  transfers invalidate p2boost's receiver cache after the supported engine
  performs Valve's model-shadow cleanup.
- Working `mat_monitorgamma` and `mat_viewportscale` output on DXVK, including
  independent local split-screen viewports and remote-view composition. The same
  final-frame pass accepts Quake-style `r_intensity` brightness scaling (neutral
  at its default `1.0`).
- Optional alpha-safe flat textures through `p2boost_flat_textures`, disabled
  by default and kept separate from the performance fixes. Its VGUI exclusion
  chains the verified engine paint callback; it never intercepts render-view
  geometry or water-volume queries.
- A toggleable frame-time overlay and bounded CSV tracing for diagnosing real
  hitches instead of relying only on average FPS. The overlay is drawn directly
  in each local client's final VGUI paint, including Orange on a remote co-op
  connection.
- Replay-visible demo provenance and compatibility with SAR in either plugin
  load order.
- Automatic NVIDIA application-profile setup for the Vulkan alt-tab black
  screen. AMD, Intel, and other adapters are detected and skipped quietly.

No cvars are forced. There is no mandatory graphics reduction, dummy prewarming,
demo or simulation change, replacement game DLL, or D3D9 proxy.

## Verified performance changes

The release contains two independently measured, gameplay-neutral performance
changes and one conservative projected-shadow selection correction:

- The frame limiter carries ordinary sub-frame deadline overshoot instead of
  discarding it every frame. It still resets phase after real renderer work or
  a hitch and continues to respect `fps_max`, VSync, fullscreen, and refresh
  settings.
- The projected-shadow cache skips byte-identical receiver-projection rebuilds.
  It keeps the engine's dirty-list cleanup, shadow-depth rendering, lighting,
  and all visual settings. Lights projected through the client leaf system stay
  cached until the light is destroyed or its record changes, or a map, focus,
  model-transfer, plugin, or A/B transition occurs. The leaf system already
  moves receivers between lit leaves whenever their bounds change. Lights bound
  to a target entity read that entity's live child hierarchy, so a full vanilla
  rebuild still follows every completed entity update for them. The engine's
  leafless flashlight mode bypasses the cache entirely. This uses `VClient016` rather than a
  listen-server callback, so host and remote Orange receive the same lifetime
  boundary. The supported engine already calls
  `IShadowMgr::RemoveAllShadowsFromModel` inside `ChangeInstance`, as prescribed
  by Valve's ragdoll fix. p2boost observes a successful transfer only to
  invalidate its own separate client receiver cache; it does not duplicate the
  engine cleanup.
- Portal 2's active shadow-depth list uses the axis-aligned box surrounding each
  projected-light frustum. A long rotated box can overlap the camera box even
  when the actual convex volumes do not intersect, causing a complete extra
  shadow-depth scene render that cannot affect a pixel. p2boost retains all of
  Portal 2's existing visibility, priority, split-screen, and shadow-limit
  decisions, then applies a separating-axis test to the two finite frusta. It
  keeps uncertain, singular, and near-touching cases, and clears a culled
  light's depth texture exactly as the stock selector does. This test is
  **off by default** (`p2boost_shadow_cull 1` enables it): only the main view
  (and each split-screen player's main view) builds a depth list.
  `ViewDrawScene_PortalStencil`, monitor, and reflection views reuse the main
  view's depth maps, so a light visible only through a portal can lose its
  shadow map when the test is on. It is not benchmark-verified.

The DXVK compatibility layer also restores two shipped display controls. A
final gamma pass makes `mat_monitorgamma` functional, while the missing viewport
transition expands `mat_viewportscale` before full-resolution UI is painted.
Both paths remain dormant at their default values. The separate flat-texture
command is an explicit visual option rather than a claimed performance fix.
`r_intensity` reuses the same final-frame pass as an explicit brightness
multiplier and is likewise dormant at its default `1.0`.

With `ss_force_primary_fullscreen 1`, the hidden secondary slot is a 1x1 box at
the corner; viewport scaling reduces it to a 0-size rect that the shader API's
`CommitSetViewports` silently swallows, leaving the previous fullscreen
viewport active and painting the hidden 3D scene stretched across the screen.
p2boost patches both zero-size guards to unconditional so the degenerate
viewport reaches the device, then clamps it to one pixel so the hidden slot
stays clipped exactly as the engine intends.

## Why Vulkan only

Native-D3D9 tracing reproduced the first connected-portal hitch inside NVIDIA's
first-use D3D9 submission and the following engine fence. Portal CPU work,
dynamic-buffer locks, portal geometry, visibility work, and resource creation
were individually too small. Attempts to remove or defer the fence only moved
the same stall to `Present`.

Portal 2's `-vulkan` path loads `shaderapivk.dll`, its shipped
`DXVK_D3D9.DLL`, and `vulkan-1.dll`. It eliminates the objectionable native-D3D9
portal hitch on the target system, so the release validates that exact module
chain and no longer carries native renderer hooks.

## NVIDIA alt-tab profile

Some NVIDIA systems show a black screen after alt-tabbing the Vulkan renderer.
On plugin load, `p2boost.dll` reads the vendor of the adapter backing Portal 2's
live DXVK device. It checks these two values in the `portal2.exe` application
profile only when that adapter is NVIDIA:

| NVIDIA setting | Value |
|---|---:|
| Vulkan/OpenGL present method | `0x00000001` (prefer layered on DXGI swapchain) |
| Vulkan/OpenGL present method flags | `0x000802A5` (treat DXVK as native) |

The DLL uses NVIDIA's public NVAPI DRS interface and changes only Portal 2's
application profile, never the global profile. If either value is missing, it
applies both required values, shows one restart message, and asks Portal 2 to
close. If NVIDIA denies a normal-process write, the DLL invokes its own exported
setup entry through Windows' 32-bit `rundll32` with elevation; no helper file is
installed. The next launch sees the completed profile and continues without a
prompt. If NVAPI remains unavailable or the elevated write fails, the plugin
leaves the game open and reports the exact operation and status code through
`p2boost_status`.

AMD, Intel, software, and other non-NVIDIA adapters return `not_applicable`.
They do not initialize NVAPI, request elevation, change a driver profile, or
print a setup-failure warning. No documented AMD equivalent of these private
NVIDIA profile values is applied.

Because the setting is now owned directly by the DLL, uninstalling p2boost does
not reset the Portal 2 application profile.

## Approved DXVK replacement

Portal 2 ships a 2020-era DXVK build whose per-draw translation cost keeps the
CPU-bound render path from feeding the GPU. On CPU-limited systems this shows up
as low GPU utilization and frametime jitter at high `fps_max` values. A newer
DXVK with a parallel command stream and lower overhead directly attacks that
path, so p2boost accepts one exact replacement binary alongside the shipped
one:

| `bin\dxvk_d3d9.dll` | SHA-256 |
|---|---:|
| Shipped (Valve) | `7bf7c95d81ff33b951ff985037401ff5a792474f1e3b9c5d2f521affb92b9073` |
| DXVK 3.0.2 (x86) | `44a2e749694128710cce3a545c954bd9d986dd6a8ea61bb08d931c2ef0190488` |

Only the exact hash above is accepted; no approximate DXVK version check
exists. `p2boost_status` prints `dxvk_shipped=0` and the exact `dxvk_hash` when
the replacement is active so an A/B comparison always names the binary that
produced it. The nightly zip and tagged releases already contain the approved
DXVK 3.0.2 x86 binary in `bin\`, so a zip install does not need the script
below; the scripts exist for restoring the shipped binary or updating the
replacement on an existing game:

Install or restore it with the two small scripts:

```powershell
.\scripts\install-dxvk.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
.\scripts\restore-dxvk.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
```

Measured on the split-screen lobby scene (the same CPU-bound profile that
limits the shipped binary), the replacement doubles frame rate (2.03x) and
halves mean frame time (3.94 ms to 1.94 ms) with GPU-busy time unchanged, so
the saving is entirely in DXVK's per-draw CPU cost. In a light 1400 FPS demo
scene the two binaries are within noise, confirming the replacement's benefit
is specific to CPU-bound render paths. See [benchmarks/report.md](benchmarks/report.md).

`install-dxvk.ps1` backs up the shipped binary to
`bin\dxvk_d3d9.dll.p2boost-original`, downloads the official GitHub release,
verifies the exact hash above before copying, and refuses to overwrite any
unrecognized binary. `restore-dxvk.ps1` restores the backed-up shipped build.
The rest of the fingerprint (`engine.dll`, `client.dll`, `shaderapivk.dll`,
and the other shipped modules) is unchanged and still required; `shaderapivk`
imports only the ordinary D3D9 export surface, which DXVK 3.0.2 provides.

> [!WARNING]
> Some antivirus products flag DXVK binaries (especially 32-bit ones). The
> installed file's SHA-256 must match the official release hash above. Verify
> the DXVK replacement before recording a speedrun, exactly as you would
> verify any other plugin state.

## Build and install

Requirements are Visual Studio 2019 or newer, CMake 3.24 or newer, an x86
toolchain, and the Windows SDK's x86 `fxc.exe` shader compiler. The build script
selects the installed Visual Studio generator and keeps architecture-specific
outputs separate.

```powershell
.\scripts\build.ps1
.\scripts\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
```

Add `-vulkan` to Portal 2's Steam launch options. The installer copies
`p2boost.dll` and its symbols into the game root beside `portal2.exe` (the
same folder the community drops `sar.dll` into, which is also where Source's
plugin loader finds it first) and adds one managed `plugin_load p2boost` block
to `portal2/cfg/autoexec.cfg`. Profile setup happens inside the DLL on its first
load.

To remove the plugin:

```powershell
.\scripts\uninstall.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
```

## Commands

- `p2boost_status` prints the build fingerprint, DXVK/Vulkan state (including
  whether the approved replacement DXVK is active and its exact hash),
  NVIDIA-profile state, enabled fixes, projected-shadow cache hits and
  exact-frustum culls, display-pass counts, demo-marker readiness, SAR state,
  overlay availability, and metric count.
- `p2boost_enable 0|1` provides a reversible runtime A/B switch.
- `p2boost_flat_textures 0|1` replaces proven color-bearing world/model samplers
  with their dominant tone and substitutes a valid neutral texture for normal/
  bump samplers. The mode stays active while paused and follows learned textures
  into flashlight/shadow override passes. Base alpha and dedicated self-
  illumination masks remain exact, preserving cutouts, transparency, and
  emissive shapes. Blend controls, lighting lookups, video frames, particles,
  render targets, lightmaps, and cubemaps remain original. Shader and texture-
  group classification protects HUD, console, menus, cursors, and SAR overlays
  without relying on material-name exceptions.
- `p2boost_overlay 0|1|2` selects off, the full top-right profiler, or a
  larger FPS-only counter. The compact counter stays white and turns red only
  while the rolling one-second window contains an 8.3 ms-or-longer hitch. The
  full view shows actual rendered FPS, current/p95/p99/max frame time, a
  64-frame graph, one-second hitch count, and shadow-cache activity.
- `p2boost_trace 0|1` records bounded frame and shadow-cache metrics.
- `p2boost_dump_metrics` writes `p2boost_metrics.csv` in the game root.
- `p2boost_shadow_cull 0|1` toggles the experimental exact projected-shadow
  depth culling described above. It defaults to `0`; with no argument it
  prints the current value.
- `p2boost_update [force]` downloads the latest GitHub release in the
  background, verifies its SHA-256 against the published checksum, and swaps
  the installed `p2boost.dll` next to the game executable (the previous copy is
  kept as `p2boost.dll.old-auto`). The running image stays valid until the game
  is restarted. Pass `force` to reinstall the current version. Updates run on a
  worker thread and never block the render loop. The release repository is
  private during development, so the updater authenticates with the
  `GITHUB_TOKEN` (or `GH_TOKEN`) environment variable when present; set it in
  your environment before launching the game to update from a private
  repository, or leave it unset once the repository is public.
- `r_intensity <0.0..2.0>` scales final-frame RGB brightness before the
  monitor-gamma curve, matching Quake Live's command of the same name. The
  default `1.0` is neutral and costs no extra pass; values above `1.0`
  brighten and values below darken. It is a display tweak, not a performance
  change, and is independent of `p2boost_enable`.

`mat_monitorgamma` now changes only the frame handed to DXVK and restores the
untouched back buffer after presentation, so retained loading frames cannot
accumulate the correction. A value of 2.2 is neutral and therefore incurs no
extra pass. `mat_viewportscale` and `mat_viewportupscale`
have only `FCVAR_CHEAT` removed, retain every other engine flag and bound, and
are restored to their original flags when p2boost unloads. With upscaling
enabled, each scaled 3D view is expanded into its own full-resolution slot
before native-resolution VGUI. This includes local split-screen and the remote
view used by `+remote_view`.

## Compatibility and safety

The plugin supports only the fingerprinted Windows x86 Steam build. A mismatch
leaves every hook disabled and reports the exact failed module. Release hooks
use validated byte signatures rather than preferred RVAs, preserve previous
VMT targets, and restore only patches they still own.

SAR may load before or after p2boost. p2boost does not alter timing, input,
network, demo, entity, or simulation semantics.

When a demo starts recording, p2boost inserts one ordinary replay-visible echo
containing its version, renderer, master-fix state, and flat-texture state. This
lets a later playback show whether the recording used p2boost without adding a
custom demo message or changing simulation data.

## Source layout

- `src/core` contains the benchmark-proven performance fixes, runtime
  verification, metrics, overlay, and plugin entry point.
- `src/engine` contains the single module/interface context, typed Source ABI
  wrappers, console bridge, and the build-23973718 reversed findings used by
  release code.
- `src/features` contains optional display, provenance, and flat-texture
  features. Flat-texture material classification, pixel codecs, resource
  ownership, and hook callbacks live in separate translation units.
- `src/memory` contains executable-memory, pattern, and chain-aware hook
  helpers, including the only production wrapper around loader module exports.
- `src/platform` contains D3D9 layout, window handling, and the in-process
  NVIDIA application-profile bridge. AMD adapters bypass NVIDIA setup cleanly.
- `src/util` contains shared timing, math, and projected-frustum helpers.
- `research` contains Ghidra scripts, old native-D3D9 probes, benchmark routes,
  and rejected-experiment notes. Nothing there is linked into `p2boost.dll`.
- `third_party/nvapi` contains NVIDIA's MIT-licensed public build interface.

The compact Vulkan PortalGun smoke runner is
`scripts/run-portalgun-demo.ps1`. It plays the exact 22.433-second intro3 demo
and reports the 12-18 second successful-portal window instead of treating the
runner's first attack input as a placed portal. `-FpsMax 0` selects an uncapped
run and enables `sar_frametime_uncap 1` for the measurement window.

See [benchmarks/report.md](benchmarks/report.md) for current Vulkan validation
and [research/report.md](research/report.md) for the archived reverse-engineering
record.
