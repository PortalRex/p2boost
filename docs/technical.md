# p2boost technical notes

This document explains every change p2boost makes to Portal 2: what problem
it addresses, how the problem was found, what exactly is patched, why the
output stays the same, which command controls it, and what it measured. The
[README](../README.md) is the user-facing summary.

All addresses are preferred-image addresses in the supported Steam build
(`23973718`). Production code never uses them directly; every patch site is
found through a byte signature that must match exactly once, and every
offset it relies on is part of that signature or of a signature-verified
function body.

## Contents

- [How p2boost patches the game](#how-p2boost-patches-the-game)
- [Measuring performance](#measuring-performance)
- [Performance changes](#performance-changes)
- [Display and convenience features](#display-and-convenience-features)
- [Why Vulkan only](#why-vulkan-only)
- [Investigated and rejected](#investigated-and-rejected)
- [Known remaining costs](#known-remaining-costs)
- [Source layout](#source-layout)

## How p2boost patches the game

p2boost is an ordinary Source server plugin (`plugin_load p2boost`). On load
it:

1. Fingerprints the running modules and confirms the Vulkan chain
   (`shaderapivk.dll` → `dxvk_d3d9.dll` → `vulkan-1.dll`). An unsupported
   build installs nothing and reports the failed check in `p2boost_status`.
2. Installs each feature independently. A signature that does not match
   exactly once disables only that feature and is listed under "issues".
3. Uses three hook primitives (`src/memory/hook.h`): a VMT/function-pointer
   swap that remembers the previous owner, a call-site rewrite, and an
   in-place byte patch. Each records the bytes it expects and the bytes it
   wrote, and removal restores a location only while p2boost still owns it,
   so SAR or another plugin hooking later is never overwritten. SAR may load
   before or after p2boost.

`p2boost_enable 0|1` is the global switch: every performance change follows
it, so one command compares stock and p2boost. Each change is also an entry
in one table in `src/core/plugin.cpp`, controlled with
`p2boost_fix <name> <value>` and on by default. Section headings below give
each fix's name. Display features (gamma, viewport scale, `r_intensity`, flat
textures) are user settings and do not follow the switch.

## Measuring performance

Portal 2's frame is CPU-bound on the main thread in the scenes that matter,
so small per-frame savings are real but noisy. All numbers in this document
come from `scripts/bench.ps1` unless stated otherwise:

- It launches Portal 2 itself (`-novid -high -vulkan -netconport`), drives it
  through the net console (which works from the main menu, unlike RCON),
  loads the split-screen co-op lobby with `ss_map mp_coop_lobby_3`, and
  uncaps with `fps_max 0` plus `sar_frametime_uncap 1`.
- Each named variant is a list of console commands. Variants are measured in
  interleaved rounds whose order alternates (ABBA…), so heat and background
  activity cannot favor one side. Results are average FPS with a 95%
  confidence interval and a per-round paired delta against the first variant,
  plus median, 1% low, and PresentMon CPU/GPU busy time.
- PresentMon stalls the game for up to ~120 ms about two seconds before its
  trace session ends. The runner records three extra seconds and measures
  only the requested window.
- `p2boost_status` is saved before and after the rounds, so each feature's
  counters prove it ran.
- `-ProfileSeconds N` adds an elevated WPR CPU profile per variant (about
  8 kHz sampling). `research/tools/etw_profile.py` splits it by thread,
  module, function (using Ghidra function ranges from
  `research/ghidra/scripts/ExportFunctionRanges.java`), and caller chain.
  Valve code is built without frame pointers, so only about 20% of samples
  carry full stacks; exclusive per-function samples are reliable, inclusive
  totals are not.

The retail build contains VProf scopes but never records them, so VProf is
not usable for inclusive timing. Subsystem costs were instead measured by
diffing profiles taken with one subsystem disabled (for example
`r_flashlightdepthtexture 0`).

Test system for the numbers below: i7-14700KF, RTX 4070 SUPER, 2560x1440,
DXVK 3.0.2. Absolute FPS varies between sessions; compare deltas from the
same run.

## Performance changes

### Approved DXVK replacement

**Problem.** Portal 2 ships a 2020-era DXVK whose per-draw translation cost
dominates CPU-bound scenes. GPU utilization stays low while the CPU feeds
draws.

**Change.** p2boost accepts exactly one replacement besides the shipped
binary: official DXVK 3.0.2 x86
(`44a2e749694128710cce3a545c954bd9d986dd6a8ea61bb08d931c2ef0190488`). Release
zips include it, and `scripts/install-dxvk.ps1` and `restore-dxvk.ps1` swap
it on an existing install after verifying hashes and backing up the shipped
file. `p2boost_status` reports `dxvk_shipped` and the exact hash.

**Result.** Split-screen lobby: 2.03x frame rate (3.94 → 1.94 ms mean) with
GPU busy time unchanged, so the whole saving is CPU translation cost. A light
1400 FPS scene is unchanged.

### Multicore rendering (`multicore`)

**Problem.** Portal 2's `mat_queue_mode -1` auto-detection resolves to 0
(single-threaded) on modern CPUs, and Portal Stories: Mel maps force 0 again
through a `logic_auto` after spawn.

**Change.** p2boost runs `mat_queue_mode 2` through the engine's client
command path, so the change callback runs, and re-asserts it every frame.
The first time it raises the value, it remembers the game's own value and
restores it when `p2boost_enable 0`, `p2boost_fix multicore 0`, or plugin unload
turns forcing off. `mat_queue_mode` is archived, so a config that already
saved 2 is left alone.

**Result.** One CPU-bound scene: 150 FPS at mode 0 vs 260 FPS at mode 2.

### Frame limiter overshoot (`frame_limiter`)

**Problem.** `CEngine::Frame` accumulates elapsed time and compares it with
the `fps_max` interval. After producing a frame it zeroes the accumulator
(engine RVA `0x1fa043`), discarding the normal overshoot of the deadline
poll, so the real rate drifts about 2.2% below `fps_max`.

**Change.** That reset is replaced by a call that subtracts one interval
when the overshoot is under 10% of a frame and otherwise zeroes as before,
so real hitches still restart the phase and no catch-up frames occur.

**Result.** `fps_max 360`: 352 → 360 FPS in both A/B orders.

### Projected-shadow cache (`shadow_cache`)

**Problem.** `C_EnvProjectedTexture` calls
`CClientShadowMgr::UpdateProjectedTexture` every client update, which
rebuilds BSP and client-leaf receiver projection for each projected light
(about 0.57 ms per frame in co-op), even when nothing changed. In a trace,
2,237 of 2,238 rebuilds had byte-identical input.

**Change.** The update (client VMT slot 24) is skipped when the complete
276-byte light record and the flashlight-model input are identical to the
last full rebuild. The engine's dirty-list cleanup (client `0x100f5640`)
still runs. Lights projected through the client leaf system stay cached
until the light changes or is destroyed, or a map, focus, model-transfer,
plugin, or A/B boundary invalidates all entries; the leaf system already
moves receivers between lit leaves as their bounds change. Lights bound to a
target entity are rebuilt after every completed entity update, because their
receivers come from that entity's live child hierarchy. The leafless
flashlight mode bypasses the cache.

**Result.** Split-screen lobby: +6.4% and +6.6% average FPS in the two A/B
orders.

### Flashlight material-bucket flush (`flashlight_flush`)

**Problem.** `CShadowMgr::ClearAllFlashlightMaterialBuckets` (engine
`0x101222c0`) runs `CMaterialsBuckets::Flush` for every flashlight, twice
per world-list draw, for every view. `Flush` calls
`CUtlLinkedList<…, unsigned short, true>::RemoveAll` on the bucket's surface
list. For a multilist, Valve's `RemoveAll` scans every slot ever allocated
to rebuild the free list, so it costs the list's high-water mark, not its
current size. In the lobby this one function was 7.9% of the main thread.

**Change.** The function is replaced through a verified entry jump. For
each flashlight it increments the flush count as before, frees only the
elements on the chains of the sort IDs used since the last flush, resets
the list header, and calls the engine's own `RemoveAll` for the used-sort-ID
list.

**Why it is exact.** The decompiled `AddElement` (engine `0x10124450`)
always makes a new element the head of its sort ID's chain and records the
sort ID the first time it is used after a flush. Nothing else adds to the
list, so the used chains contain exactly the elements the full scan would
free. One exception: the buckets and every sort ID start with a flush count
of −1, so elements added before the very first flush are not recorded. That
first flush therefore uses the stock scan. The drawing code walks chains,
not slot indices, so a different free-list order cannot change output. A
unit test compares the result against a transcription of Valve's tier1 list
code over 200 randomized add/flush rounds.

**Result.** +10.6% ± 1.3 (lobby).

### Absent XInput controller probes (`xinput_throttle`)

**Problem.** Portal 2's `inputsystem.dll` `PollXDevices` (`0x10006ea0`)
calls `XInputGetState` for all four slots on every input poll, which is
every frame. With no controller attached each call fails, and under Steam
the call runs through the overlay's XInput hooks into SetupAPI/CfgMgr device
enumeration on the main thread. Valve's later inputsystem (CS:GO) re-probes
absent controllers only every 0.5–20 seconds.

**Change.** inputsystem's stored `XInputGetState` pointer is replaced. A slot
that last returned `ERROR_DEVICE_NOT_CONNECTED` returns that same result
without calling the driver until one second has passed. Connected
controllers are read on every poll exactly as before.

**Why it is exact.** `PollXDevices` acts on a not-connected result only for a
slot it still considers connected, and an absent slot reports nothing.
The only difference is that a newly plugged controller is noticed within one
second instead of one frame.

**Result.** +10.7% ± 1.5 (lobby, no controller). Together with the bucket
flush: +17.8% ± 0.8.

### Occlusion pass on maps without occluders (`occlusion_skip`)

**Problem.** `CClientLeafSystem::BuildRenderablesList` runs
`ExtractOccludedRenderables` (client `0x100e21c0`) for every view. For each
studio model and static prop it calls the engine's `IsOccluded`, which takes
a mutex, rebuilds the occluder edge list from `host_state.worldbrush`, and
returns false when that list is empty. The list comes only from the BSP
occlusion lump (`func_occluder` brushes), and that lump is empty in all 117
shipped single-player, co-op, and DLC maps. The pass never removes anything,
but the client loop alone cost 4.3% of the main thread in the lobby.

**Change.** The single call site in `BuildRenderablesList` goes through a
wrapper that returns the input count unchanged when
`worldbrush->numoccluders` is zero, and calls the original otherwise.
Workshop maps that do have occluders keep the stock pass.

**Why it is exact.** With no occluders every `IsOccluded` returns false, so
the stock function copies each entry onto itself and returns the same
count.

**Result.** +2.9% ± 0.8 (lobby, with the other fixes on).

### In-world VGUI screens in shadow-depth views (`vgui_shadow_skip`)

**Problem.** Every flashlight shadow-depth pass drew `C_VGuiScreen` entities
(in-world panels such as the lobby's wall displays). Each draw laid out and
painted the screen's panel tree, text included, through vguimatsurface into
a depth-only render target: 3.2% of the main thread in the lobby.

**Change.** `C_VGuiScreen::DrawModel` (renderable VMT slot 9) returns
"nothing drawn" while the current view is `VIEW_SHADOW_DEPTH_TEXTURE`. All
other views draw the screens normally.

**Visual effect.** The panels sit flush on walls, so they no longer occlude
projected light, and this is not visible in shipped maps.

**Result.** Lobby: 400 → 430 FPS.

### Same-frame shadow-depth reuse (`shadow_depth_reuse`)

**Problem.** Each split-screen view, and a held `+remote_view`, runs
`ComputeShadowDepthTextures` and re-renders the depth map of every selected
flashlight, 19.5% of the main thread in the split-screen lobby. A depth map
is rendered from the light's point of view, so the second view renders the
same image again.

**Change.** `CViewRender::UpdateShadowDepthTexture` (VMT slot 29) records,
per depth texture, the frame number and the written fields of the light's
`CViewSetup`, and skips the render when both match. To make each light land
in the same texture in every view, `ProjectedShadowCache` hands the selected
lights over in handle order when all of them fit in the depth slots and none
needs the high-resolution range. Which lights receive shadows never changes.

**Result.** Split-screen lobby: 430 → 470 FPS, with shadows identical in both
views when each player stands in a spotlight. Single-view online co-op does
not render a second view, so it gains only with `+remote_view` held.

### Exact shadow-depth culling (`shadow_cull`)

**Problem.** Portal 2 selects the shadow-depth lights for a view by
comparing the axis-aligned box around each light's frustum with the camera
frustum. A long rotated light can pass that test without its volume touching
the view, which costs a complete extra depth render.

**Change.** After the stock selection, the hook in the depth-list builder
(client `0x100f1650`) runs a separating-axis test between the light's finite
frustum and the view frustum. It drops a light only when the separation is
more than 0.25 units, keeps singular and uncertain cases, and clears a culled
light's depth slot exactly as the stock selector does.

**Caveat.** Only the main view of each player builds a depth list. Portal,
monitor, and reflection views reuse those maps, so a light visible only
through a portal can lose its shadow while this is on.

**Result.** In the lobby it culled no light and measured +0.6% (within noise).
It is on by default so every optimization is active; set
`p2boost_fix shadow_cull 0` if a shadow seen through a portal ever disappears.

### +remote_view picture cap (`remote_view_fps`)

**Problem.** Holding `+remote_view` in co-op makes `CViewRender::Render`
render a complete second scene for the partner's picture every frame.

**Change.** `RenderView` (VMT slot 6) renders the remote slot at most N times
per second (default 60). After a real render a queued device call copies the
slot from the back buffer, and the skipped frames redraw that copy. Your own
view still renders every frame. Local split-screen players are never
affected, and the cap is inactive while `mat_viewportscale` is below 1.

**Visual effect.** The partner picture updates at 60 FPS instead of your
frame rate. Set `p2boost_fix remote_view_fps 0` to render it every frame.

**Result.** Online co-op with remote view held: 270 → 400 FPS at 60.

### Flat-texture hook overhead

The optional flat-texture mode hooks `IMaterial::Bind` on the material-system
render workers. While the mode was off, the hook still string-classified
every bound material, about 12% of each render worker. It now calls through
immediately while disabled. This is a fix to p2boost's own overhead, so it
has no toggle.

## Display and convenience features

These do not change performance and do not follow `p2boost_enable`.

- **`mat_monitorgamma` on DXVK.** The DXVK path ignores the engine's gamma
  ramp. p2boost applies the curve in a final full-screen pixel-shader pass
  (`src/shaders/monitor_gamma.hlsl`) to the frame handed to DXVK and restores
  the untouched back buffer after presentation, so retained loading frames
  cannot accumulate the correction. At 2.2 (neutral) no pass runs.
- **`r_intensity <0..2>`.** A Quake-style RGB multiplier applied in the same
  pass before the gamma curve. At 1.0 no pass runs.
- **`mat_viewportscale` / `mat_viewportupscale`.** Only `FCVAR_CHEAT` is
  removed, and the original flags are restored on unload. Each scaled 3D
  view, including each local split-screen slot and the remote view, is
  expanded into its own full-resolution slot before native-resolution VGUI
  paints.
- **`ss_force_primary_fullscreen 1` viewport fix.** The hidden secondary slot
  becomes a 0-size viewport under scaling, which `CommitSetViewports` in
  `shaderapivk.dll` silently drops, leaving the previous full-screen viewport
  active. Both zero-size guards are patched to pass, and the viewport is then
  clamped to one pixel so the hidden slot stays clipped as intended.
- **Flat textures (`p2boost_flat_textures`, default 0).** Replaces
  color-bearing world/model samplers with their dominant tone and normal
  maps with a neutral texture. Alpha and self-illumination masks stay exact;
  UI, video, particles, render targets, lightmaps, and cubemaps are excluded
  by shader and texture-group classification. Its VGUI exclusion chains
  `CEngineVGui::Paint` (`VEngineVGui001` slot 14).
- **Profiler overlay (`p2boost_overlay`) and tracing (`p2boost_trace`;
  stopping it writes `p2boost_metrics.csv`).** Frames are timed at the verified `CEngine::Frame`
  completion, not the 60 Hz `GameFrame` callback. The overlay is drawn in each
  local client's final VGUI paint, which also works for Orange on a remote
  co-op connection.
- **Demo provenance.** When a demo starts recording, p2boost issues one
  ordinary `echo` with its version, renderer, master-switch state, and
  flat-texture state, so a playback shows whether p2boost was loaded. No
  custom demo message or simulation data is added.
- **NVIDIA alt-tab profile.** Some NVIDIA systems show a black screen after
  alt-tabbing the Vulkan renderer. If the adapter behind DXVK is NVIDIA,
  p2boost sets the `portal2.exe` application profile (never the global
  profile) through NVAPI DRS: Vulkan/OpenGL present method `0x00000001`
  (layered on DXGI) and flags `0x000802A5`. If a value was missing it asks
  for one restart. If the normal write is denied it re-runs its own export
  elevated through 32-bit `rundll32`. Other vendors are skipped without
  touching the driver. Uninstalling p2boost does not reset the profile.
- **`p2boost_update`.** Downloads the latest release on a worker thread,
  verifies the published SHA-256, and swaps the DLL next to `portal2.exe`
  (the old copy is kept as `p2boost.dll.old-auto`). The new DLL loads on the
  next start.

## Why Vulkan only

Native-D3D9 tracing reproduced the first connected-portal hitch inside
NVIDIA's first-use D3D9 submission and the engine fence after it. Portal CPU
work, dynamic-buffer locks, portal geometry, visibility, and resource creation
were each too small to explain it, and removing or deferring the fence only
moved the stall to `Present`. The `-vulkan` path (shaderapivk + DXVK)
eliminates that hitch on the target system, so the release validates that
module chain and carries no native D3D9 renderer hooks.

## Investigated and rejected

Recorded so they are not retried without new evidence. Details are in
[research/report.md](../research/report.md).

- **2D UI as the bottleneck.** An early profile put half the main thread in
  vguimatsurface. It came from a session left in the main menu. In a clean
  lobby, turning off all VGUI (`r_drawvgui 0`) gains only about 1%.
- **VProf.** The retail build has scopes but records nothing.
- **Threaded bone setup** (`cl_threaded_bone_setup`) was slower: 517 vs
  500 FPS.
- **`D3DCREATE_MULTITHREADED`, timer-based limiter waits, extra render
  workers, PVS-decompression caching, portal mesh caching, and
  managed-texture preloading** were each measured as neutral or as only
  moving a stall.
- **Engine threading options that ship disabled on PC**, measured in the
  lobby (4 interleaved rounds): `host_threaded_sound 1` +0.8% ± 0.2,
  `r_threaded_shadow_clip 1` −0.1%, `r_queued_decals 1` +0.1%,
  `r_queued_post_processing 1` ±0. Too small to justify changing engine
  threading behavior.
- **Reusing shadow-depth maps across frames.** A deliberately unsafe test
  build reused any depth map whose light setup matched the previous frame,
  ignoring moving objects. It reused no more than same-frame reuse already
  does, because the lobby's lights change their setup every frame, so a
  correct version (which would also have to track moving objects and depth
  bias) would gain nothing here.
- **Main-thread kernel time** in ETW profiles (~6–8%) is largely ETW's own
  user-stack walking (`RtlpWalkFrameChain`, `RtlpLookupFunctionEntryForStackWalks`)
  plus GPU-driver work, not something the game does.

## Known remaining costs

Measured in the split-screen lobby with every change above on (about
600 FPS), using diagnostic switches that change visuals and are not fixes:

| Disabled for measurement | FPS gain |
|---|---:|
| Projected-texture shadow depth maps (`r_flashlightdepthtexture 0`) | +13.6% ± 2.4 |
| The whole client shadow manager (`r_shadows 0`, includes the above) | +15.8% ± 2.6 |
| All 2D VGUI (`r_drawvgui 0`) | +0.8% ± 1.0 |

Before same-frame depth reuse, the depth maps cost +26.9%. A profile diff of
the remaining depth cost (about 230 µs of main-thread time per frame) shows:

- About 74 µs: the engine rebuilding a CPU-side dynamic mesh of every opaque
  world surface for each depth pass (`Shader_WorldShadowDepthFill` and
  `Shader_WorldZFillSurfChain_Single`, engine `0x100e4240`, `0x100e4120`,
  `0x100e70b0`, `0x100e7d90`). Valve's later engine draws that fill from
  static, position-only vertex buffers instead (`r_csm_static_vb`,
  `Shader_DrawDepthFillChainsStatic`); porting it could save the vertex-copy
  part (about 40 µs, ~2.5%).
- About 25 µs: frustum culling of each depth view's renderable list.

Other remaining main-thread costs are necessary per-view work:
`ComputeTranslucency` (about 78 µs per frame across the normal views), the
renderable frustum cull, BSP world-list building, and in-world VGUI screens.
The VGUI screens spend part of their time in `CFontTextureCache`'s per-glyph
RB-tree lookups (vguimatsurface `0x100150e0`), where a faster cache might
save 1–2%.

## Source layout

- `src/core`: performance changes, runtime verification, metrics, overlay,
  and the plugin entry point.
- `src/engine`: module and interface context, typed Source ABI wrappers,
  engine data layouts (`tier1.h`, `material_buckets.h`, `client_shadow.h`,
  `client_view.h`), the console bridge, and the per-game signature/offset
  profile (`game_profile.h`).
- `src/features`: display adjustments, remote view, demo provenance, and flat
  textures.
- `src/memory`: pattern scanning and chain-aware hook primitives.
- `src/platform`: D3D9 layout, window handling, and the NVIDIA profile bridge.
- `src/util`: timing, math, and projected-frustum helpers.
- `scripts`: build, format, install, package, DXVK install/restore, the
  benchmark runner (`bench.ps1`) and its game-control helpers, and
  `bootstrap-tools.ps1`.
- `research`: the profiling and Ghidra tools and the archived
  reverse-engineering log (see `research/README.md`). Nothing there is
  linked into `p2boost.dll`.
- `tests`: native unit tests for patch primitives, signatures, and the exact
  algorithms above.
