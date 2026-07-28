# Reverse-engineering report

## Scope and fingerprints

Portal 2 Steam build `23973718`, Windows x86, native Microsoft D3D9:

| Module | SHA-256 |
|---|---|
| engine.dll | `5196EACE78D5F2F47F8CE39A49B0EFD15374A2B996B272642F0493ACE42C24DC` |
| shaderapidx9.dll | `0879D15B4F9CF9F55BB86D874DC88B6EDF4D8DFC825E77D3BD1619A2DF250439` |
| materialsystem.dll | `8B8BBDB77E72A23BEF8E67F0B9800327338E125BA0FBFAE68FEB2DC864AF4FBF` |
| studiorender.dll | `34CD78BDE18BCD96CE4B96D43D4E83649F6DE64E020181F6BB229CDF5E8DB812` |
| stdshader_dx9.dll | `49D1648B3D27150F554AA68F248C3342501AD3F38BC9949AC6CDAE763020E08F` |
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

The engine also conditionally calls `ThreadSleep(1)` at RVA `0x1f9ede` while
`sleep_when_meeting_framerate` is enabled and the configured headroom remains.
A chain-safe prototype replaced only that call with a high-resolution wait,
kept the one-millisecond maximum quantum, and left a 0.25 ms polling tail. In
the same co-op paint demo at `fps_max 360`, the existing patch measured 358.49
FPS with a 4.10 ms p99; the prototype measured 358.48 FPS with a 4.13 ms p99.
It was neutral and has been removed rather than adding an unproven timing hook.

A later live-gameplay prototype used the remaining interval already calculated
at `[ebp-8] - [esi+18h]`, waited once, and kept a bounded 0.15 ms QPC polling
tail. The verified import call is `push 1; call [ThreadSleep]` at RVA
`0x1f9edc`; its surrounding bytes are unique in the supported build. In a
portal-visible 360 FPS co-op lobby run, the prototype reached 357.30 FPS with
a 3.9244 ms p99, compared with 359.84 FPS and 3.7853 ms for the existing
overshoot correction. This second design was also removed. The result rejects
timer substitution as the remaining source of CPU frame-time variance.

A third prototype left the engine's `ThreadSleep(1)` call unchanged and used a
verified wrapper only to mark frames that had actually waited in the limiter.
Those frames could distinguish ordinary scheduler overshoot from renderer work
without relying on the existing 10% inference threshold.

Two complete 360 FPS Laser Relays A/B pairs were run in opposite orders. With
the old policy first, the sleep-aware policy measured 359.50 versus 358.40 FPS
and 4.0307 versus 4.0756 ms p99. Reversing the order reversed the result: the
old policy measured 359.54 versus 358.21 FPS and 4.0377 versus 4.0815 ms p99.
In both pairs, the second map pass won while the first absorbed the cold 35 ms
native-draw burst. The marker is therefore neutral and has been removed. The
live runner retains a `-CandidateFirst` option so future claims can be repeated
in both orders instead of crediting warm-process state to a patch.

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

### Queued-render hitch causality

Real `sp_a2_trust_fling` gameplay retained deterministic frame-time spikes
after the projected-shadow saving. VProf placed the wait in
`CMaterialSystem::EndFrame`, while worker-thread QPC and cycle measurements
showed long wall time with little executing CPU time. Trace-only command-list,
dynamic-buffer, and draw instrumentation then isolated the blocking command:

- the queued command at `materialsystem.dll+0x13e90` dispatches through the
  thunk at `+0x13c60` to render-context vtable slot 219;
- the concrete render-context implementation is `materialsystem.dll+0x2e980`;
- that reaches shader-api vtable slot 136 and `shaderapidx9.dll+0x14a50`, the
  public SDK analogue of `IShaderAPI::ForceHardwareSync`;
- `shaderapidx9.dll+0xeeb0` polls two D3D9 event queries with
  `IDirect3DQuery9::GetData(..., D3DGETDATA_FLUSH)` and a 0.2-second guard.

The adjacent queued command at `materialsystem.dll+0x13d00` reaches
`+0x29fc0`, the presentation path. Across roughly 65,000 dynamic-buffer locks,
the largest lock was 0.092 ms; all draw calls in a representative 12 ms worker
batch totaled about 0.14 ms. Dynamic buffer allocation, draw dispatch, and
portal CPU rendering are therefore not the source of these particular stalls.

Two semantics-preserving candidates were tested and rejected. Performing one
flushing query poll followed by non-flushing polls still blocked on the first
poll. Allowing one frame of query slack moved the same backlog to Present
instead of removing it. Targets of 360 and 720 FPS also retained the explicit
fence stalls. None of these experiments ships: moving a hitch, silently
lowering a cap, or weakening the engine's resource-ordering contract is not a
fix. The low-overhead trace-only worker timing remains so future candidates can
be evaluated against the proven path without permanent per-draw hooks.

A later portal-heavy co-op paint capture separated the fence, presentation,
and total worker wall time with concrete, chain-aware hooks. Its deterministic
29.4549 ms worker burst contained 7,540 queued calls, but only 0.0019 ms in
`ForceHardwareSync` and 0.1155 ms in Present. A non-intrusive sampler caught the
same interval blocked in the native D3D9/NVIDIA submission path. A stack-only
research run resolved the caller chain through `materialsystem.dll+0x3ac67`,
`shaderapidx9.dll+0x1ee55/+0x1ee77`, and multiple distinct
`stdshader_dx9.dll` draw leaves. Ghidra shows `shaderapidx9.dll+0x1edb0`
applies the selected shader snapshot before dispatching the actual backend draw
and invalidating that snapshot afterward. This is real draw submission, not a
duplicate explicit fence or Present.

The stack walker suspends the sampled render worker and inflated that research
run to 119 ms, so only its caller chain is retained; its timing is not a
benchmark. A non-flushing event-query prototype was also rejected and removed:
it could not guarantee query completion without either restoring the original
flush or weakening the engine's CPU/GPU ordering and latency contract. The
shipping tracer records only worker, concrete fence, and Present timing. It no
longer clones or timestamps every queued command.

An opt-in `P2BOOST_RESEARCH_DRAW_TRACE` build then hooked the native D3D9 draw
slots only while tracing. The first pass caught a 58.1783 ms worker batch with
47.8942 ms inside 1,504 draws and one 10.5186 ms indexed draw. Repeating the
same demo in the same process reduced the corresponding deterministic batch to
10.1932 ms; its 2,989 draws totaled 4.9082 ms and the longest draw was only
0.3483 ms. This separates a native-driver first-use submission cost from the
repeatable engine workload. Because dummy rendering would only prewarm or move
the first-use work, no prewarming is included.

The repeatable co-op paint batch contained 7,561 queued calls. An initial
command-identity pass associated 2,310 list nodes with
`engine.dll+0xd7840`, an address which Ghidra also finds in creation code for a
six-argument per-surface dynamic-light functor. That correlation is not enough
to identify the list-node payload: disassembly of the observed executor at
`materialsystem.dll+0x13e90` shows a different ABI. It loads only `ECX` from
node offset `+0x14` and tail-jumps through offset `+0x10`.

Consequently, a follow-up interpretation of the raw node bytes as
`dlight_t*`/`msurface2_t*` arguments was invalid and has been removed. A timing
wrapper based on that incorrect six-argument assumption faulted at
`engine.dll+0xd6b15`; the experiment was rejected and never enabled in the
shipping build. The valid result is narrower: the 9.5309 ms repeated batch
spent 4.2320 ms inside native D3D9 draws and 0.0032 ms in the explicit hardware
sync. No light/surface deduplication or culling claim is made until the exact
executor-specific object layout and call ABI are proven.

The executor ABI was subsequently proven independently of those generic list
nodes. Ghidra resolves `engine.dll+0xd7b40` as the concrete `CFunctor6`
executor: callback at `+0x10`, `dlight_t*` at `+0x14`, `msurface2_t*` at
`+0x18`, the copied matrix at `+0x1c`, flags at `+0x4c`, and two booleans at
`+0x50/+0x51`. A research-only, chain-aware hook on both matching functor
vtable slots observed 2,310 calls, one light pointer, and 2,310 distinct
surfaces in the co-op paint setup burst.

That count is not a dynamic-light culling defect. The light pointer is the
all-zero engine sentinel at preferred address `engine.dll+0x50ce70`, the flags
are zero, and only the second update boolean is set. Ghidra identifies the
producer at `engine.dll+0xd8350` as `R_RedownloadAllLightmaps`: it intentionally
enumerates every world surface between the material system's begin/end update
callbacks. Moving the trace window beyond map/demo initialization produces no
such calls. Skipping or culling this setup work would leave incomplete
lightmaps, so no permanent change is made.

The delayed steady-state window instead reproduced one deterministic cold
native-D3D9 submission. At `fps_max 360`, the cold pass reached 9.8429 ms with
8.0993 ms in draw calls and a 6.6628 ms longest indexed draw through the
`VertexLitGeneric` path. Replaying the same segment in the same process reduced
the maximum worker batch to 4.5257 ms and the longest draw to 0.8742 ms. This
confirms first-use driver/resource work rather than a recurring engine
lightmap defect. Dummy rendering or issuing the draw during loading would only
prewarm or move the stall, so neither is included.

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

The preserved cleanup at client preferred address `0x100f5640` was resolved
and timed separately. It searches the manager's two dirty-shadow trees and
clears the renderable flag only when the handle is actually present. On
byte-identical cache hits it costs 0.0002 ms median and 0.0004 ms p99.
Eliding it cannot improve the remaining co-op tail and could leave a renderable
incorrectly dirty, so the exact engine cleanup remains mandatory.

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

Administrator GeneralProfile captures were then repeated without the earlier
empty-kernel-session failure. A 15-second linked-portal SP run recorded 14,371
frames at 958.46 FPS, with 1.7480 ms p99, 2.6934 ms maximum, and no frame above
8.3 ms. The equivalent two-player lobby run recorded 8,442 frames at 562.96
FPS, with 2.7079 ms p99, 3.6233 ms maximum, and again no frame above 8.3 ms.
GPU-busy p99 was only 0.5579 ms in SP and 0.9055 ms in co-op. Neither trace
contained a gameplay-asset read aligned with a hitch.

A low-overhead five-second main-thread sample produced 1,532 active
engine/client/material samples in the linked-portal SP scene and 2,963 in the
equivalent split-screen scene. The approximately 1.93x increase follows the
second player's separately constructed view. A research-only stack walk maps
the hot engine leaves at RVAs `0xe4240`, `0xe70b0`, `0x120bd0`, and `0x1b41f0`
through different BSP/world-list callers, while client RVAs `0xe1fa0`,
`0xe2180`, and `0xe6280` compact and occlusion-filter each view's renderable
list. Ghidra shows distinct camera-dependent visibility state in these chains;
there is no proven cross-view-equivalent result to cache or skip. The stack
walker suspended the main thread for long intervals, so its timing is excluded
from benchmarks and only the caller relationships are retained.

`OtherAppInit` is entered after a focus transition even when the underlying
D3D9 cooperative-level query reports `D3D_OK`; however, the engine has already
executed the corresponding release-side state transition. A prototype that
intercepted only the poll/reacquire side was rejected after a runtime failure
and was removed. No partial lifecycle bypass ships.

## Intro3 first-portal demo reproduction

The 22.433-second `PortalGun_2243_76561198795823814_348449.dem` in
`portal2/a` is replayed with ordinary `playdemo`, not `timedemo`. The first
recorded attack input is not used as the marker because it does not place a
portal. Instead, the capture identifies the first frame with a nonzero portal
render count, which occurs near 15.7--15.9 seconds after PresentMon starts and
corresponds to the successful placement around 14 seconds of demo simulation.

Five independent cold-process captures reproduce the same sequence. Portal
CPU rendering begins at only 0.18--0.23 ms. The following material worker then
spends 38--66 ms, including 16--47 ms in native D3D9 draw submission. A later
queued `materialsystem.dll+0x13c60` command blocks for 55--56 ms in the already
identified hardware-synchronization path, with another 21--23 ms occurrence in
some runs. Present itself can independently cost about 9 ms during this burst.

Resource-creation hooks separate late Source allocation from driver first use.
During map load, one observed batch creates 901 textures, 656 vertex shaders,
and 1,066 pixel shaders roughly thirteen seconds before the portal. The
portal-opening workers create zero resources of those types. The stall is
therefore not a portal-resource allocation bug: it is first-use D3D9 resource
and draw work followed by Source's explicit fence. Authoritative device-state
queries corrected an earlier setter-cache ambiguity. In the latest cold run,
the 66.4278 ms worker spent 35.7085 ms in 535 draws. Its slowest 10.7459 ms draw
used an already-seen shader pair but a first-seen managed 512x512 DXT1 mipmapped
texture. New shader-pair draws accounted for only 0.8855 ms in that worker.

A research-only lifecycle prototype called the native D3D9 `PreLoad()` method
after the last mip of each managed texture was unlocked. It did not change
format, pool, mip count, pixels, filtering, or draw order. A cold comparison
still measured a 62.4643 ms portal worker frame followed by a 56.4762 ms wait,
equivalent to the 67.7877/57.3897 ms control. The same first-visible texture
behavior remained, showing that the driver's managed-resource preload is
advisory here or is invalidated by later Source updates. The prototype was
removed completely. D3D9 exposes no pipeline-precompile object; deleting the
fence merely transfers the backlog to Present. These rejected approaches do
not ship.

An adaptive x86 sampler was added for hosts where kernel ETW profiling is not
elevated. It queries process-wide thread CPU deltas without suspending every
thread, then samples only the thread that most recently ran. Across the cold
portal event it found a sustained `nvd3dum.dll` worker burst alongside the
expected studio-render, material-system, ShaderAPI, and D3D9 submissions. No
single Portal gameplay routine or Source per-model loop dominated the event.
The public Alien Swarm client confirms that this engine generation already
groups PC model types into one `DrawModelArray` submission; the recovered
`studiorender.dll+0x4e710` callback is that real batched draw path.

A cold and immediately repeated playback in one unchanged Portal 2 process
then isolated device-local first use. The cold successful portal produced a
64.1448 ms CPU-submission frame followed by a 58.2072 ms wait. The identical
second playback's portal event was 6.0163 ms and had no following 50 ms wait.
Median frame time remained about 1.016 ms and p99 about 1.49 ms in both runs.
The removable cost is therefore cached within the live D3D9 device, but is not
persisted by the runtime or driver across processes. A general correction must
populate the real material/model device state inside an existing loading
transition; fence removal, advisory managed-resource preload, and quality
reductions cannot do so.

The apparent shipped warm-up controls were resolved before considering a
replacement. `mat_forcemanagedtextureintohardware` is registered with default
value `1`, but its `materialsystem.dll` ConVar object has only constructor and
destructor code references. No renderer path reads it. The corresponding
`mat_remoteshadercompile` object in `shaderapidx9.dll` is likewise only
registered and destroyed. `mat_shadercount` is a diagnostic command whose
complete callback prints the two creation counters and resets them;
`mat_spewvertexandpixelshaders` only walks the existing shader collection.
The headless exporter now recovers raw MSVC descriptor pointers when Ghidra
does not create an xref, so these negative results are reproducible.

The slowest 66.4278 ms portal worker independently recorded zero texture,
vertex-shader, and pixel-shader creations. Its 535 real draws consumed
35.7085 ms; twelve first-resource draws consumed 23.0804 ms, while the five
first shader-pair draws consumed only 0.8855 ms. Therefore Source has already
created the relevant D3D objects before the portal event. The remaining cold
cost is native-driver promotion/translation triggered by use of those objects,
not a dormant Source compile queue that can safely be executed earlier.

A stronger managed-resource experiment temporarily raised each completed
texture's priority around `PreLoad()`, then restored the exact old priority.
The cold portal still produced a 37.4421 ms submission, 30.6927 ms follow-up,
57.3335 ms wait, and 21.6415 ms tail. A separate non-drawing experiment bound
each completed managed texture, called `ValidateDevice`, and restored the
previous binding. It created a new cluster of 15--26 ms loading frames while
the portal still produced 53.0841 ms and 59.1647 ms frames. Both prototypes
were removed completely: neither performs driver translation early enough,
and the latter merely moves synchronous work into another visible interval.

A cold-process control repeated the complete demo with the engine default
`r_portal_use_pvs_optimization 1`, restoring the user's `0` value afterward.
The successful portal still produced a 52.7052 ms draw-submission frame followed
by a 57.8891 ms synchronization frame. Portal CPU work remained 0.2088--0.2496
ms. The user's broader-PVS configuration therefore is not the cause of this
first-portal stall and remains untouched.

One later queued command was separately resolved with a fresh Ghidra project to
`valve_avi.dll+0x3f90`. It performs a real Bink frame decode and updates the
movie's three texture planes. A research-only attempt to pipeline that decode
through the asynchronous functions already exported by the shipped
`binkw32.dll` changed in-game video playback while leaving the portal event at
approximately 55--57 ms. The experiment was rejected in full; no Bink hook or
movie-frame scheduling change ships.

Playback completes with the expected `sp_a1_intro3` header and 1,346 demo ticks.
The standalone runner disables tracing, restores `fps_max` and patch state, and
fully closes the game even when analysis fails so research hooks cannot remain
active after a benchmark.

### Occlusion-query lifecycle exclusion

Headless Ghidra analysis recovered the material-system query owner rather than
assuming portal rendering was responsible. `materialsystem.dll` keeps four
native ShaderAPI queries in every logical query node. `0x10039680` creates the
four handles, `0x10039cf0` destroys them when the logical node is released, and
`0x100396f0` destroys active handles during shader-object/device release. The
CPU node itself is retained on a free list. Calls from focus/device restore run
through `0x100396b0`, which recreates handles for the active list. These
addresses are analysis labels for the fingerprinted binary; the runtime trace
uses validated interface slots and contains no absolute address.

Research-only chain-aware hooks measured both ShaderAPI lifecycle calls over a
complete intro3 playback. The run created 96 queries in 16 batches: 13.6967 ms
total, with a 3.5111 ms worst individual creation. It destroyed 28 queries in
two batches: 0.0072 ms total and 0.0019 ms worst. No query was created at the
15.8-second successful first portal. Pooling native handles would complicate
device reset and cannot remove the measured portal stall, so no query-lifetime
change ships.

### Portal mesh and laser-simulation exclusions

Headless analysis resolves `client.dll` preferred address `0x102c5ee0` to
`CPortalRender::CreateMeshForPortals`. Its two callers are the normal stencil
portal path and the early-Z portal path. Both pass the current view and portal
set, so retaining geometry across changed views would first require an
equivalence proof rather than an assumed frame cache.

A research-only signature hook measured the complete mesh builder during a
30-second intro3 playback. It observed 29,092 calls and 64,547 input portal
entries, but only 59.4025 ms of total CPU time. The average call cost was
2.042 microseconds and the largest complete-frame contribution was 0.1000 ms.
No mesh cache can materially remove the 38--55 ms first-portal worker stall,
so no lifecycle or geometry change ships.

The expensive-room hypothesis on `sp_a2_laser_relays` was tested separately.
Ghidra resolves `server.dll` preferred address `0x103b5a40` to
`CPortalLaser::StrikeThink`. It clears the retained beam path, resolves the
live source, transforms the beam through portals, traces it, and updates its
touch links. A grounded 24-second route rode the elevator into the main room,
continued forward through the room, fired both portal colors, and retained a
connected portal on screen.

The trace observed 967 laser-think calls over 668 rendered frames. They cost
30.1226 ms in total, averaged 31.151 microseconds, and contributed at most
0.1690 ms to one frame. The portal-opening interval still contained a
42.3111 ms queued-render batch: 35.0566 ms was inside 429 native D3D9 draws,
including an 11.1299 ms longest draw. The following worker blocked for
16.4363 ms in the already identified explicit hardware fence. Portal CPU
rendering, mesh construction, laser simulation, world-surface construction,
and GPU-busy time were individually small in the corresponding presented
frames. This reproduces the cold driver-submission/fence sequence on a second
real gameplay route and rules out skipping server laser logic as a valid fix.

The dedicated runner writes both PresentMon and exact-QPC p2boost metrics,
releases every queued input, restores the original cvars, and closes Portal 2
after the benchmark by default.

## Sphere Peek chamber-entry reproduction

The Sphere Peek route now starts from the ordinary `sp_a2_sphere_peek` spawn,
walks three seconds toward the entrance panel, moves left for one second,
crosses the main chamber, then moves left for exactly twelve ticks before
continuing forward. That final 0.2-second adjustment is the narrow line that
hits the faith plate; longer left inputs walk past it. Position tracing
confirmed the launch at `-1949.60 1568.21 437.02` and the subsequent landing.

Early captures accidentally ran while another application owned the
foreground window. Those results are discarded: Portal 2's background message
wait dominated the main-thread samples and can produce misleading 60 Hz-like
overlay behavior. Every grounded gameplay runner now activates Portal 2 and
verifies its foreground process ID before accepting a capture.

A corrected 23-second foreground run recorded 21,724 frames at 944.81 FPS,
with a 1.0295 ms median, 1.8812 ms p99, and 6.9219 ms maximum. The repeatable
maximum occurs about 8.2 seconds into capture while entering the chamber,
before the twelve-tick catapult adjustment. It is CPU-busy: GPU busy was only
1.5143 ms, while the portal renderer recorded zero work, the projected-shadow
cache skipped an unchanged update, and no focus reacquisition occurred. A
separate GeneralProfile trace of the same grounded route recorded no gameplay
file read during the transition.

Static ownership narrows the four synchronized CPU samples further. The
server instruction belongs to `CPortalGameMovement` vtable slot 12. That
method resets a retained collision enumerator, resolves the live player/entity
handle, transforms current bounds through portal movement state, and submits a
world-space box to the spatial partition. The route changes those inputs while
the player crosses the chamber, and the frame contains no duplicate invocation
with equal bounds.

The engine instruction at preferred address `0x100e80fd` is inside the
world-surface mesh builder at `0x100e7d90`. It has one direct caller,
`0x100e9770`, reached through the world-list path only for the corresponding
draw-flag combination. The routine enumerates the current view's selected
surface lists, material properties, and mesh capacity before emitting geometry.
Its inputs change as new BSP leaves enter the camera view. The bit-buffer sample
belongs to a shared copy primitive with five independent callers, while the CRT
`memset` sample is likewise insufficient to identify one owner. None establishes
equivalent input or redundant output, so these samples do not justify skipping
movement collision, world geometry, or memory initialization.

PresentMon and the low-overhead main-thread sampler were then started together
and correlated using their shared QPC timestamps. Sampling increased the
maximum to 8.0124 ms, so that timing is research-only, but it isolated four
non-system instructions inside the exact frame: `server.dll+0x33d840` and
`engine.dll+0x285aec`, `engine.dll+0x304d9a`, and `engine.dll+0xe80fd`.
Headless Ghidra resolves the engine samples to bit-buffer copying, CRT memory
clearing, and the world-surface mesh construction loop respectively. The
server sample constructs transformed entity bounds before a virtual spatial
query. This establishes a mixed simulation/world-list burst rather than a
portal-only or GPU-only stall. None of these operations is skipped yet: the
trace has not proved duplicate inputs or equivalent cached output, and doing
so without that proof could change collision, visibility, or rendered
geometry.

## Hook compatibility

Permanent fixes patch exact engine/shader callsites only after full module-hash
and byte-signature validation. Renderer diagnostic vtable hooks store their
previous targets. Removal restores a location only while p2boost still owns
it, so a later hook owner is never overwritten. The targets are outside SAR's
public client timing hooks, and SAR 1.15.3 was present during A/B tests.

An active-render unload stress test exposed a worker that could still return
through `p2boost.dll` after Source had detached the plugin. The renderer hook
now restores the queued-worker target first, tracks in-flight batches, and
requires a 50 ms zero-callback grace interval before removing downstream
hooks. Ten consecutive unload/reload cycles passed while the co-op paint demo
was rendering with tracing enabled.

## Grounded Laser Relays reproduction

Visual validation rejected the earlier automated Trust Fling route: its old
camera and movement path could look through or leave level geometry, so none
of its captures are retained as performance evidence. A replacement route on
`sp_a2_laser_relays` starts at the normal spawn, uses collision and ordinary
forward/portal inputs, and visibly enters the relay room. Input events are
scheduled before capture, avoiding the 33.7 ms false hitch previously caused
by repeated RCON packets during measurement.

The first short grounded A/B reproduced one attributable entry spike: 20.2059
ms unpatched and 10.2092 ms patched. That route stopped near the room entrance,
so its numbers are retained only as the initial reproduction. The runner now
holds forward for five additional seconds to cross the expensive relay area.

The extended 13-second A/B reached the deeper relay platforms and saved a
matching proof image. Unpatched recorded three frames above 8.3 ms and a
12.9386 ms maximum; patched recorded no frame above 8.3 ms and a 5.5315 ms
maximum. Average throughput improved from 612.68 to 731.02 FPS, while 1% low
improved from 318.40 to 352.10 FPS. Median CPU-busy time fell from 1.5708 to
1.2947 ms.

The final route follows the user's full reproduction: twenty seconds of
ordinary forward movement takes the elevator into the expensive chamber, then
real primary and secondary attacks open linked portals on separate walls. A
24-second A/B recorded 646.06 FPS unpatched and 812.89 FPS patched. The 1% low
rose from 267.17 to 386.16 FPS, median CPU-busy time fell from 1.3892 to
1.1134 ms, and neither variant exceeded 8.3 ms. The patched route's slowest
one-second interval averaged 586.1 FPS; the connected-portal end view averaged
more than 900 FPS. This is the current valid SP gameplay result; discarded
Trust Fling and out-of-bounds noclip captures are not used in any comparison.

## Profiler validation and connected-portal focus test

The first overlay implementation sampled `IServerPluginCallbacks::GameFrame`,
which is the simulation callback and therefore reported approximately 60 FPS
at an actual render rate above 800 FPS. Frame collection now occurs after the
verified `CEngine::Frame` completion call. In a simultaneous five-second
capture, PresentMon reported 837.52 FPS, 1.0922 ms median, and 3.0146 ms p99;
p2boost reported 842.55 FPS, 1.0816 ms median, and 2.8466 ms p99. The small
boundary difference is expected because the two collectors start separately.

The original display backend also used `Con_NXPrintf`, whose global console
rows duplicated both panels over the orange half of Portal 2's vertical
split-screen layout and could silently fail for an online orange client. The
replacement binds the exact public `VDebugOverlay004` interface registered by
this build and uses its normalized screen-text API. Visual SP and local co-op
tests show one stable top-left panel, while `p2boost_status` separately reports
`overlay_available` and `overlay` so a remote co-op installation failure is no
longer silent. Portal and render-worker collection now remains active when the
overlay is enabled even if full CSV tracing is off. Summed worker CPU time is
presented as render-thread utilization and call count rather than misleading
wall-clock milliseconds.

The focus harness now rejects a window that does not cover its monitor. Before
each cycle it fizzles both prior portals, alt-tabs away, restores the game, and
uses delayed engine inputs to fire a new primary and secondary portal. This
keeps RCON parsing out of the measured connected-portal frame. Five patched
smoke cycles had a 1.8167 ms median post-connection maximum, 1.9831 ms worst
frame, and zero cycles above 8.3 ms. The disabled-patch control measured
1.9313 ms median and 2.2449 ms worst, also with no cycle above 8.3 ms.

The full 30-cycle patched release gate then measured a 1.7618 ms median,
2.1820 ms p95, and 2.5227 ms worst post-connection frame. No cycle exceeded
8.3 ms. The harness duration includes its measured RCON setup latency, so the
final event and analysis window remain inside the PresentMon capture.

An elevated 25-second GeneralProfile trace of the same elevator route recorded
728.51 FPS at the extra tracing overhead, 3.7184 ms p99, 6.8840 ms maximum,
and no frame above 8.3 ms. GPU-busy p99 was 1.1074 ms. Exclusive CPU samples
were distributed across the native NVIDIA D3D9 driver (26.74%), client view and
render-list work (13.45%), engine BSP/world work (10.87%), shader API (6.49%),
and material submission (4.60%). There was no gameplay-asset read; the only
Portal file traffic was the active demo write. This leaves no single large
semantics-preserving operation to remove: the remaining dominant paths submit
real draws or build camera-dependent visibility. Further patches require proof
of equivalent state or duplicated output before any work is skipped.

## Existing multicore paths

A production runtime inventory found that Portal 2 already enables its queued
material-system selection, threaded particles, queued ropes, and parallel
server entity packing. Enabling the remaining PC-disabled
`cl_threaded_bone_setup` path was tested in fresh processes on the grounded
split-screen lobby route. The stock path measured 517.55 FPS, a 1.8558 ms
median, 3.0708 ms p99, and 325.65 FPS 1% low. Parallel bone setup measured
500.40 FPS, a 1.8926 ms median, 3.2055 ms p99, and 311.96 FPS 1% low.
Dispatch and synchronization overhead exceed the small bone workload at these
frame times, so p2boost does not enable or conceal that cvar.

The current user profile sets `r_paintblob_max_number_of_threads 1` while the
engine default is `4`. This was kept separate from engine-patch results as
required. On the five-second co-op paint playback, one worker measured
978.08 FPS with a 2.1427 ms p99; four workers measured 976.15 FPS with a
2.1337 ms p99. That interval provides no material throughput benefit from the
additional paint workers, so the user value is neither changed nor blamed for
the remaining hitches. These results also show why adding threads generically
is not a correction: Portal 2's dominant renderer and driver paths are already
concurrent, and short jobs can become slower when split.

The fixed two-camera, linked-portal lobby trace also measures the existing
material render worker directly. Across 1,250 steady frames, frame time was
1.7229 ms median and 2.7408 ms p99. The single ordered render worker occupied
1.4967 ms median and 1.8057 ms p99, while all four portal-stencil calls
together occupied only 0.2484 ms median and 0.4015 ms p99. Worker time has
only 0.1685 correlation with whole-frame time, and portal time has 0.2969
correlation. The remaining tail is therefore not an idle main thread that can
be fixed by dispatching the same D3D9 command stream to another worker.
D3D9 immediate-context state and draw order remain serial even if command
construction is moved between CPU threads.

The QPC endpoints prove that this worker is already pipelined with the next
main-thread frame. It begins 0.0388 ms median before the preceding
`CEngine::Frame` completion marker, then finishes 0.2499 ms median before the
current marker. At p99 the post-worker main-thread remainder grows to 1.2407
ms while worker wall time is still only 1.8057 ms. A second submission worker
cannot hide that remainder and would instead contend for the same ordered
D3D9 device. The profiler overlay now computes worker occupancy against the
actual sampled frame interval and displays this post-worker main-tail p99;
its previous division by a presumed exact one-second window was inaccurate
during startup and short collection windows.

Static analysis then evaluated the more plausible modern design: build the
two player views and their portal views concurrently, followed by ordered
material submission. This build cannot do so locally. Client preferred
address `0x101f1190` saves and replaces the global current-view pointer
`DAT_10937f40`, mutates the shared view object's flags, and calls multiple
render-context methods before restoring that state. The engine visibility
path at preferred address `0x101249f0` mutates each shared BSP surface's
generation field and compares it with the global generation
`DAT_1061532c`. Its output lists and counters are likewise owned by the
current world-list builder. Two concurrent calls would race the active view,
surface visitation, and output storage.

Making that safe would require replacing the complete client/engine
world-list contract with per-view state, merging visibility results in a
deterministic order, and auditing every downstream renderable and occlusion
consumer. An atomic change to the surface generation alone is insufficient:
it could cause one view to treat a surface visited by another view as its own
result. That is an engine rewrite, not a chain-safe plugin hook, and it risks
view-dependent omissions, nondeterministic demos, and SAR incompatibility.
No parallel-view prototype is shipped without an isolated state-equivalence
proof.

## Grounded render-handoff validation

A fresh production build was exercised through the full grounded Laser Relays
route with the elevator, main room, two portal shots, and a connected portal
interior visible. The overlay reported real render completion rates near
900 FPS rather than the 60 Hz simulation rate. Its worker percentage and
post-worker tail agreed with the timestamped CSV trace.

The cold portal sequence produced 21.6935, 36.3260, and 24.5148 ms presented
frames. The corresponding queued material batches took 19.3305, 36.3047, and
24.4925 ms, while the post-worker main-thread tails were only 0.476, 0.351,
and 0.240 ms. On the largest frame, 35.6328 ms was spent in the native D3D9
`Present` call; the recorded hardware fence was 0.0014 ms. Portal CPU work was
0.0624--0.1695 ms across the three frames.

Repeating the same fizzle-and-reopen sequence in the same process produced a
3.5742 ms maximum, 1.7860 ms p99, and no frame above 8.3 ms. This isolates the
large event to cold driver submission/presentation, not portal simulation,
visibility construction, the Source hardware fence, or an idle main thread.
Adding another Source worker cannot parallelize an ordered D3D9 `Present`.

The hitch analyzer now aligns the bounded metric interval to the PresentMon
capture and reports queued-worker and post-worker-main timing explicitly.
This makes render handoff, main tail, portal work, GPU work, and focus
reacquisition independently visible instead of classifying every CPU-busy
frame alike.
