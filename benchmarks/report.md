# Vulkan validation report

## Target

- Portal 2 Windows x86 build `23973718`
- Portal 2's shipped `shaderapivk.dll` and `DXVK_D3D9.DLL`
- NVIDIA RTX 4070 Super, 2560x1440 at 359/360 Hz
- SAR loaded alongside p2boost

## Backend and driver profile

A live module snapshot verified `shaderapivk.dll`, `DXVK_D3D9.DLL`, and
`vulkan-1.dll` in the same Portal 2 process. `p2boost_status` then reported
`supported=1`, `dxvk_vulkan=1`, and both release hooks installed.

The original standalone NVAPI validation read these effective defaults:

```text
present_method=0x00000002
present_flags=0x00000000
```

The validation changed only the `portal2.exe` profile to `0x00000001` and
`0x000802A5`, verified both values, and confirmed that the global profile stayed
untouched. Version 0.8 performs that same application-profile check directly
inside `p2boost.dll` during plugin load. A successful change produces a restart
message and cleanly closes Portal 2 before the Vulkan swapchain is created.

## Intro3 PortalGun demo

The Vulkan runner uses
`a/PortalGun_2243_76561198795823814_348449.dem`, verifies the embedded
`sp_a1_intro3` header, and reports the 12-18 second interval around the actual
successful portal placement. Initial fresh-process captures confirmed normal
playback and no native-D3D9-scale 50-60 ms submission/fence stall. Their order
was affected by Vulkan pipeline warming, so they are retained as smoke data,
not as a patched/unpatched throughput claim.

## Frame limiter A/B

Each process played the complete PortalGun demo once before measurement, then
captured the same 12-18 second interval at `fps_max 360`. Both A/B orders were
run in separate processes:

| Order | Variant | Average FPS | p99 ms | Maximum ms | Frames >=8.3 ms |
|---|---|---:|---:|---:|---:|
| off then on | off | 352.15 | 3.3831 | 4.7448 | 0 |
| off then on | on | 360.01 | 3.2819 | 3.9403 | 0 |
| on then off | on | 359.98 | 3.4796 | 4.1899 | 0 |
| on then off | off | 352.07 | 3.3243 | 4.5309 | 0 |

The correction removes the same deterministic 2.2% limiter drift in both
orders under DXVK/Vulkan. Tail differences are small and mixed in the reverse
pair, so the claim is the repeatable cap accuracy/throughput result rather than
an unsupported p99 improvement.

## Projected-shadow cache A/B

The deterministic local split-screen lobby used two fixed cameras with a
linked portal pair visible. Five-second captures ran both A/B orders at
`fps_max 999`:

| Order | Variant | Average FPS | Median ms | p99 ms | Maximum ms |
|---|---|---:|---:|---:|---:|
| off then on | off | 177.51 | 4.8511 | 15.9581 | 18.1776 |
| off then on | on | 188.80 | 4.5720 | 15.4510 | 20.0518 |
| on then off | on | 504.35 | 1.8970 | 3.1472 | 3.9733 |
| on then off | off | 473.16 | 2.0367 | 3.2264 | 4.7116 |

The first pair still contained substantial Vulkan/display warm-up, which is
why its absolute rate differs from the later reverse pair. The candidate gains
6.36% and 6.59% average FPS respectively despite the order reversal. The clean
reverse pair also improves median frame time by 6.9%. Trace metrics recorded
continuous cache hits, and Vulkan GPU-busy time stayed approximately 1.75-1.77
ms median in the reverse pair, consistent with a CPU-only saving.

## Focus, portals, and lifecycle

With the NVIDIA profile active, the fullscreen Laser Relays harness completed
five focus-loss/restores and fired both real portal-gun inputs after every
verified restore. Every connected-portal event window contained presented
frames, the game remained responsive through the run, and no black-screen
recovery failure occurred.

The 100 ms post-portal windows measured 10.7291 ms median worst-frame and
11.0915 ms absolute worst-frame; three of five cycles exceeded 8.3 ms. This is
far below the earlier native-D3D9 50-60 ms submission/fence stall but is not
reported as a perfect hitch-free gate.

The final package lifecycle removed all p2boost files and the managed autoexec
block, reinstalled the release, matched the installed DLL hash to the Release
build, and restored the active `plugin_load p2boost` line.
