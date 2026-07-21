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

Skipping, deferring, or prewarming this registry would violate resource
lifetime correctness or merely move the stall. D3D9Ex would require device
creation before a Source plugin can load, so it is not smuggled in as an
unverified proxy or renderer replacement.

## Portal and co-op findings

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

The permanent fix patches one exact engine callsite only after full module hash
and byte-signature validation. Diagnostic callsites and renderer vtable slots
store their previous targets. Removal restores a location only while p2boost
still owns it, so a later hook owner is never overwritten. The pacing target is
outside SAR's client timing hooks, and SAR 1.15.3 was present during A/B tests.
