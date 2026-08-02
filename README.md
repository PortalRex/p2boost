# p2boost

p2boost is a Windows x86 Source plugin for Portal 2 build `23973718`. Version
0.8 is Vulkan-only and expects Portal 2's shipped DXVK renderer. Launch the game
with `-vulkan`; native D3D9 is deliberately rejected.

The release contains two independently measured, gameplay-neutral changes:

- The frame limiter carries ordinary sub-frame deadline overshoot instead of
  discarding it every frame. It still resets phase after real renderer work or
  a hitch and continues to respect `fps_max`, VSync, fullscreen, and refresh
  settings.
- The projected-shadow cache skips only byte-identical receiver-projection
  rebuilds. It keeps the engine's dirty-list cleanup, shadow-depth rendering,
  lighting, and all visual settings. The cache invalidates on map, focus,
  plugin, and A/B state transitions.

No cvars are forced. There is no mandatory graphics reduction, dummy prewarming,
demo or simulation change, replacement game DLL, or D3D9 proxy. The separate
flat-texture command is an explicit visual option and is disabled by default.

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
On plugin load, `p2boost.dll` checks these two values in the `portal2.exe`
application profile:

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

Because the setting is now owned directly by the DLL, uninstalling p2boost does
not reset the Portal 2 application profile.

## Build and install

Requirements are Visual Studio 2022, CMake 3.24 or newer, and an x86 toolchain.

```powershell
.\scripts\build.ps1
.\scripts\install.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
```

Add `-vulkan` to Portal 2's Steam launch options. The installer copies
`p2boost.dll` and its symbols and adds one managed `plugin_load p2boost` block
to `portal2/cfg/autoexec.cfg`. Profile setup happens inside the DLL on its first
load.

To remove the plugin:

```powershell
.\scripts\uninstall.ps1 -GameRoot 'D:\SteamLibrary\steamapps\common\Portal 2'
```

## Commands

- `p2boost_status` prints the build fingerprint, DXVK/Vulkan and NVIDIA-profile
  state, enabled fixes, SAR state, overlay availability, and metric count.
- `p2boost_enable 0|1` provides a reversible runtime A/B switch.
- `p2boost_flat_textures 0|1` replaces admitted world/model albedo with its
  alpha-weighted average color while preserving cutout and transparency data.
  HUD, VGUI, SAR, render-target, lightmap, and tiny crosshair textures remain
  untouched.
- `p2boost_overlay 0|1` shows actual rendered FPS, current/p95/p99/max frame
  time, a 64-frame graph, one-second hitch count, and shadow-cache activity.
- `p2boost_trace 0|1` records bounded frame and shadow-cache metrics.
- `p2boost_dump_metrics` writes `p2boost_metrics.csv` in the game root.

## Compatibility and safety

The plugin supports only the fingerprinted Windows x86 Steam build. A mismatch
leaves every hook disabled and reports the exact failed module. Release hooks
use validated byte signatures rather than preferred RVAs, preserve previous
VMT targets, and restore only patches they still own.

SAR may load before or after p2boost. p2boost does not alter timing, input,
network, demo, entity, or simulation semantics.

## Source layout

- `src/core` contains the two release fixes, runtime verification, metrics,
  overlay, and plugin entry point.
- `src/engine` contains typed wrappers for the minimal public Source ABI, the
  console bridge, and only the reversed fields/signatures used by release code.
- `src/memory` contains the small chain-aware hook and pattern helpers.
- `src/platform` contains the in-process NVIDIA application-profile bridge.
- `research` contains Ghidra scripts, old native-D3D9 probes, benchmark routes,
  and rejected-experiment notes. Nothing there is linked into `p2boost.dll`.
- `third_party/nvapi` contains NVIDIA's MIT-licensed public build interface.

The compact Vulkan PortalGun smoke runner is
`scripts/run-portalgun-demo.ps1`. It plays the exact 22.433-second intro3 demo
and reports the 12-18 second successful-portal window instead of treating the
runner's first attack input as a placed portal.

See [benchmarks/report.md](benchmarks/report.md) for current Vulkan validation
and [research/report.md](research/report.md) for the archived reverse-engineering
record.
