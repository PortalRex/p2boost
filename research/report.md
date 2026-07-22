# Reverse-engineering report

## Scope and fingerprints

Portal 2 Steam build `23973718`, Windows x86, native Microsoft D3D9:

| Module | SHA-256 |
|---|---|
| engine.dll | `5196EACE78D5F2F47F8CE39A49B0EFD15374A2B996B272642F0493ACE42C24DC` |
| shaderapidx9.dll | `0879D15B4F9CF9F55BB86D874DC88B6EDF4D8DFC825E77D3BD1619A2DF250439` |
| materialsystem.dll | `8B8BBDB77E72A23BEF8E67F0B9800327338E125BA0FBFAE68FEB2DC864AF4FBF` |
| client.dll | `2FF27C2878C258B2C16A0982E30B0D2F1BAD9D213E42CC778DC2A2925C625137` |
| server.dll | `4C3ECC83BBEA0864660A49322D1D5188CDC00FE43A7EA56F1CA8A0C94356BB4C` |

Ghidra 12.1.2 headless analysis was run from clean projects. Valve's public
Source SDK names and SAR's public ABI were used as analogues. No leaked source
or pre-existing disassembler database was used.

## Proven frame-limiter defect and correction

`engine.dll` RVA `0x1f9e30` accumulates elapsed host-loop time and asks the
frame policy at RVA `0x1f9d40` for the interval derived from the user's
`fps_max`. After producing a frame, instructions at RVA `0x1fa043` always zero
the accumulator. Normal deadline-poll overshoot is therefore discarded every
frame, causing the limiter to drift below its requested rate.

p2boost replaces that verified eight-byte reset with a call that subtracts the
requested interval only when overshoot is below 10% of one frame. It otherwise
performs the original zero. The threshold rejects renderer workload and hitch
debt, so no rapid catch-up frames are introduced. Runtime disable executes the
original zero behavior.

## Device/focus causality

`shaderapidx9.dll` RVA `0x24690` handles lost/reset states and calls RVA
`0x24450` for `NeedsReset` and `OtherAppInit`. Stage timing showed:

- shader-device restore: approximately 0.0005 ms;
- frame fence/query recreation: approximately 0.2-0.8 ms;
- material resource callbacks: approximately 274-317 ms.

The callback target resolves to `materialsystem.dll` RVA `0x11230`, which
restores material shader objects and registered resource owners. After this
reset settles, 30 portal placements produced no frame above 2.8055 ms. The
portal is not lazily allocating the missing 300 ms; the D3D9 device reset is.

A forced restore also produced a reproducible access violation at
`d3d9.dll+0xefd7b` on the material-system queued-render worker. The saved
minidump showed a stale/corrupt D3D object pointer in a shader-api draw chain.
Portal 2's `VMaterialSystem080` slots 116 and 117 resolve to the public SDK's
`Lock`/`Unlock` contract at RVAs `0xeab0` and `0xeba0`. p2boost validates the
vtable targets and prologues and holds this recursive lock across focus
resource reacquisition, preventing the worker from drawing through the device
lifecycle transition.

Exact-QPC sampling of the co-op restore interval then found repeated BSP light
traces in `engine.dll`. Ghidra establishes that RVA `0x1ae7f0` iterates the
static-lighting cache and calls the per-entry computation at `0x1ad1c0`; RVA
`0x1aeae0` reinitializes its arrays before entering the same rebuild. The two
resource callback callsites are `0xf8bb4` and `0xf3adc`. Focus changes neither
map geometry, dynamic-light state, nor entity positions, so those CPU caches
remain valid across this transition.

p2boost intercepts only those two verified direct calls and returns early only
while the validated focus reacquisition call is active. Every normal map,
light, entity, and cache invalidation continues to the original target. A
post-restore sampler found no lighting rebuild in the next two seconds, ruling
out deferred work. The rest of the material resource registry remains intact;
skipping or prewarming it would violate resource lifetime correctness or move
the stall. D3D9Ex would require device creation before a Source plugin can
load, so it is not smuggled in as an unverified proxy or renderer replacement.

## Portal and co-op findings

### Static projected-light rebuild

The engine's hierarchical VProf report exposed a larger steady-state co-op
cost than the portal stencil itself. `CClientShadowMgr::UpdateProjectedTexture`
entered `UpdateBrushShadow`/`BuildFlashlight` once per rendered frame, spending
about 0.565 ms rebuilding BSP and client-leaf receiver projection. Split-screen
then rendered the required shadow-depth views separately; those render calls
are not removed.

Valve's public Source SDK 2013 implementation documents that projective lights
are created without cached vertices because they are expected to change every
frame, alongside a FIXME suggesting optional caching for static entity lights.
Portal 2's `C_EnvProjectedTexture` still calls `UpdateProjectedTexture(...,
true)` every simulation pass even when it does not update the flashlight
state. Ghidra maps the Portal 2 routines to `client.dll` RVAs `0xf81a0`
(public update), `0xf8100` (internal update), `0xf7d20` (brush update), and
`0xf6440` (flashlight projection).

A trace-only vtable hook copied the full 0x114-byte Portal 2 shadow record
before every call while always executing the original function. In five
seconds, 2,237 of 2,238 records were byte-identical and the calls consumed
1,199.066 ms total. The permanent fix therefore skips only a byte-identical
projection rebuild. It still calls RVA `0xf5640` to preserve the public
function's dirty-list removal. The exact state cache includes the relevant
`r_flashlightmodels` value and invalidates on any state change, cvar change,
map transition, focus transition, plugin toggle, load, or unload.

Changing `r_flashlightmodels` twice produced two measured cache misses, and
each focus resource reacquisition produced an uncached rebuild before hits
resumed. The cache occupies storage only for observed projected-light handles;
it does not reserve the engine's full 16-bit handle space.

`client.dll` RVA `0x1ee590` is the profiled
`CViewRender::ViewDrawScene_PortalStencil` routine. Both verified callers (RVAs
`0x285c91` and `0x2c57db`) are traced. With local split-screen and two active
portals it averaged 0.0549 ms per call and 6.12% of traced CPU time. Calls track
the active view pipelines; no duplicate with equivalent camera/view state was
found. Removing them would reduce visual work rather than correct redundancy.

The user's `r_portal_use_pvs_optimization 0` and SAR heavy-entity option remain
untouched and are treated as separately measurable configuration costs.

The co-op lobby follow-up used two fixed split-screen cameras with both linked
portal interiors visible. User-mode main-thread samples were exported as
module/RVA call stacks and fed back into headless Ghidra. The largest client
paths are the ordinary view/world-list chain (`client.dll` RVAs `0x1edfa0`,
`0x1f1460`, and `0x1f1190`) and the distinct portal-stencil calls. Engine hot
leaves at RVAs `0xe4240`, `0x120bd0`, and `0x1249f0` build/mark BSP surface and
visibility data. Disabling hardware occlusion for an isolated A/B did not
improve p99. None of these calls had equivalent camera state, so removing one
would be a rendering-quality or visibility change.

A subsequent synchronized 15-second GeneralProfile ETW and PresentMon capture
recorded 6,623 portal-visible lobby frames. The only frame above 8.3 ms was
8.4279 ms. Its GPU busy time was 1.59 ms; 6.79 ms was classified as GPU wait,
so the frame was a CPU submission/work burst rather than GPU saturation.
Main-thread samples mapped by the ETL image-load rundown enter the client
renderable filtering path around preferred address `0x100e205a` and engine BSP
leaf/surface traversal around `0x10124adb`/`0x10124da2`. Ghidra resolves these
to normal view-dependent visibility marking feeding portal rendering.

Kernel disk events recorded no Portal asset reads during that capture. The
only Portal file traffic was small periodic writes to `workshop_log.txt` and
the active demo, neither aligned with the 8.43 ms frame. This invalidates an
earlier filesystem-streaming hypothesis that came from mapping samples with
inferred module bases and unreliable x86 stack-unwind frames. All retained
sample addresses are now rebased from each ETL's authoritative image-load
rundown. The visibility burst is a real remaining target, but the two
split-screen cameras have different view state; no pass will be cached or
skipped until instrumentation proves identical inputs and output equivalence.

The sampler also observed periodic XInput/SetupAPI and Steam-overlay frames.
They are recorded as a possible external hitch contributor, not patched:
controller discovery and SAR/overlay hooks are outside a proven
gameplay-neutral engine correction.

`OtherAppInit` is entered after a focus transition even when the underlying
D3D9 cooperative-level query reports `D3D_OK`; however, the engine has already
executed the corresponding release-side state transition. A prototype that
intercepted only the poll/reacquire side was rejected after a runtime failure
and was removed. No partial lifecycle bypass ships.

## Hook compatibility

Permanent fixes patch exact engine/shader callsites only after full module-hash
and byte-signature validation. Renderer diagnostic vtable hooks store their
previous targets. Removal restores a location only while p2boost still owns
it, so a later hook owner is never overwritten. The targets are outside SAR's
public client timing hooks, and SAR 1.15.3 was present during A/B tests.
