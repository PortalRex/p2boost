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

Ghidra 12.1.2 headless analysis was run from clean projects. Public Source SDK
2013 names and SAR's public ABI were used only as analogues. No leaked source or
pre-existing disassembler database was used.

## Proven frame-limiter path

`engine.dll` function RVA `0x1f9e30` calls the build's frame-policy function at
RVA `0x1f9d40`. When `fps_max`, `sleep_when_meeting_framerate`, and
`sleep_when_meeting_framerate_headroom_ms` policy chooses to yield, the host
loop calls `ThreadSleep(1)` at RVA `0x1f9ede` through IAT RVA `0x3445f8`.

One millisecond is a large quantum relative to 2.778 ms at 360 FPS and about
2 ms at 500 FPS. p2boost hooks the IAT entry, checks the return address, and
uses a high-resolution waitable timer only for this callsite. Other engine
sleep callers chain unchanged. This removes no simulation, rendering, or
presentation work.

## Device/focus path under investigation

`shaderapidx9.dll` RVA `0x24690` handles lost/reset device states and calls
resource reacquisition at RVA `0x24450` for paths labelled `NeedsReset` and
`OtherAppInit`. Reacquisition immediately walks registered callbacks. Current
evidence has not yet shown whether the post-focus first-portal spike is a lazy
portal render target, material, query, or driver allocation. Pre-rendering a
dummy portal would only move the stall and is therefore prohibited.

## Portal/co-op path under investigation

`client.dll` RVA `0x1ee590` corresponds to
`CViewRender::ViewDrawScene_PortalStencil`. It allocates/refcounts several view
objects per invocation, but no redundant invocation has yet been proven safe
to remove. Local split-screen also creates two visibility/render consumers;
public SAR's `sar_always_transmit_heavy_ents` and the user's
`r_portal_use_pvs_optimization 0` are separately measurable configuration
costs, not candidate engine fixes.

## Hook compatibility

The permanent hook modifies an engine import slot rather than SAR-owned client
or renderer targets. It stores the previous pointer, forwards all unrelated
calls, and restores only when the slot still points to p2boost. Portal 2's
branch-specific plugin and ConCommand ABIs are matched, including
`ClientFullyConnect`; command registration steals the engine's own command
vtable in the same interoperability pattern documented by public SAR.
