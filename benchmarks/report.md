# Benchmark report

## Environment

- Intel Core i7-14700KF, NVIDIA RTX 4070 Super
- Portal 2 build 23973718, Windows x86, native Microsoft D3D9
- 2560x1440, 359 Hz, hardware independent flip for fullscreen pacing tests
- SAR 1.15.3 loaded before p2boost
- PresentMon 2.5.1, fixed-camera captures
- Current-user profile kept intact, including `r_portal_use_pvs_optimization 0`
  and SAR's heavy-entity option

## Final pacing A/B

| Scenario | Variant | Average FPS | Median ms | p95 ms | p99 ms | 1% low FPS |
|---|---:|---:|---:|---:|---:|---:|
| SP, 360 target | unpatched | 352.24 | 2.8449 | 3.4416 | 3.7375 | 267.56 |
| SP, 360 target | patched | 360.12 | 2.7804 | 3.3667 | 3.6549 | 273.61 |
| SP, 500 target | unpatched | 483.93 | 2.0607 | 2.5680 | 2.8272 | 353.71 |
| SP, 500 target | patched | 499.88 | 1.9955 | 2.4980 | 2.8269 | 353.74 |
| SP, 999 target | unpatched | 937.17 | 1.0457 | 1.3773 | 1.6460 | 607.53 |
| SP, 999 target, 10% carry | patched | 968.06 | 1.0167 | 1.3238 | 1.7044 | 586.72 |

The correction removes the systematic 2-6% limiter drift at attainable caps.
At 360 it improves p99 by 2.2%; at 500 it improves average throughput by 3.3%
without changing p99. A 999 target is close to renderer throughput in this
scene: median and p95 improve, but p99 does not. This is reported as a limit,
not hidden behind a lower forced cap.

In local split-screen with two placed portals at a 999 target, the patch raised
average throughput from 892.66 to 934.55 FPS, while p99 varied from 1.8334 to
1.8721 ms. The portal-stencil routine itself averaged 0.0549 ms per call and
6.12% of traced CPU time; removing its calls would remove required camera work.

## Co-op lobby with visible linked portals

The added deterministic lobby harness runs two active local players and keeps
both linked portal interiors on screen in both halves. Its proof image is
saved beside every capture. Four portal-view calls occur per rendered frame:
two visible portals times two player views. The current lobby trace averages
0.0815 ms per portal-stencil call.

At `fps_max 999`, linked portals increased median frame time from 1.7085 ms to
2.0676 ms versus the identical view after both portals were fizzled, a roughly
21% throughput cost. Sampling and Ghidra decompilation attribute the work to
distinct client view/world visibility passes; no equivalent-camera duplicate
was found. Temporarily disabling `r_occlusion` did not materially improve p99,
so that cvar remains untouched.

At a 360 target with the linked portals visible, the pacing correction raised
average presentation rate from 350.56 to 358.05 FPS and reduced display
intervals above 4 ms from 7.80% to 6.67%. CPU p99 remained about 3.9 ms, so the
co-op smoothness gate is still open. A 60-second throughput-limited 999 A/B was
neutral within run-order/thermal variance (median 2.1269 ms unpatched and
2.1618 ms patched); the limiter correctly does not hide renderer cost there.

### Projected-shadow cache A/B

VProf and byte-for-byte state tracing proved that the lobby's static projected
light rebuilt its receiver projection every rendered frame. The v0.5.0 cache
removes only identical rebuilds while retaining shadow-depth rendering and all
visual settings.

| Target | Variant | Average FPS | Median ms | p99 ms | Maximum ms | 1% low FPS |
|---|---|---:|---:|---:|---:|---:|
| 999 | unpatched | 414.23 | 2.3789 | 3.1668 | 4.6796 | 315.78 |
| 999 | patched | 538.14 | 1.8310 | 2.6190 | 4.8432 | 381.83 |
| 360 | unpatched | 350.56 | 2.8395 | 3.7745 | 5.5016 | 264.94 |
| 360 | patched | 360.06 | 2.7692 | 3.6634 | 4.0343 | 272.97 |

At the throughput-limited target this is a 29.9% average-FPS increase, 23.0%
median frame-time reduction, 17.3% p99 reduction, and 20.9% improvement in 1%
low FPS. At 360 FPS, display repeats fell from 0.114% to 0.028%. GPU-busy time
was unchanged (approximately 0.89 ms median), confirming a CPU-only saving.

Paused 2560x1440 lossless TGA comparisons measured 44.42 dB PSNR between
patched/unpatched frames versus 43.93 dB between two unpatched control frames;
the patch difference is within the scene's real-time material/particle noise.
Focus testing also confirmed that each resource reacquisition invalidated the
cache and executed a fresh projection before cache hits resumed.

The packaged v0.5.0 build received a final five-second smoke A/B after its
cache storage was reduced to observed handles only. It measured 392.73 versus
518.62 FPS (+32.1%), 1% low 283.94 versus 350.25 FPS (+23.4%), and p99 3.5219
versus 2.8551 ms. The patched maximum was 3.8876 ms with no frame above 8.3 ms.
Both SAR-to-p2boost and p2boost-to-SAR runtime load orders retained a validated
shadow-cache hook and native-D3D9 status.

## Short real-gameplay coverage

Fresh-process playback of the 40.9-second `sp_a2_trust_fling` demo exercises
movement, portal placement and traversal rather than a fixed camera. At
`fps_max 999`, v0.5.0 measured 944.29 average FPS, 624.41 1% low FPS, and
1.6015 ms p99, versus 900.39 FPS, 586.96 FPS, and 1.7037 ms unpatched. This is
a 4.9% throughput gain and 6.0% p99 reduction. Both variants retained a
roughly 12.4 ms deterministic maximum, which triggered the queued-render
investigation documented in the research report; the residual fence hitch is
not represented as fixed.

After reducing that investigation to its low-overhead trace hooks, three
10-second patched smoke captures covered Trust Fling, co-op repulsion-gel
gameplay, and co-op tractor-beam gameplay. They respectively recorded 9,157,
9,597, and 9,695 presented frames. Their maximum frame times were 4.5652,
3.4945, and 3.8973 ms, with no frame above 8.3 ms. These short runs validate
the packaged hook lifecycle across more varied gameplay; they are not used as
an A/B performance claim because demo segments and transient state were not
identical across those three scenarios.

The automated matched-segment runner was then added and exercised with its
five-second defaults. It restarts each demo at the same tick range for both
variants and verifies the demo header's map before capture:

| Gameplay segment | Variant | Average FPS | p99 ms | Maximum ms |
|---|---:|---:|---:|---:|
| Trust Fling | unpatched | 922.09 | 2.3476 | 4.1779 |
| Trust Fling | patched | 970.14 | 1.3738 | 4.6162 |
| Co-op repulsion gel | unpatched | 937.49 | 2.1810 | 3.1529 |
| Co-op repulsion gel | patched | 978.02 | 2.1300 | 5.5801 |
| Co-op tractor beam | unpatched | 816.83 | 2.6477 | 10.0561 |
| Co-op tractor beam | patched | 845.59 | 2.7361 | 9.3846 |

Average throughput improved by 3.5-5.2% in all three short pairs. Trust Fling
p99 improved materially and repulsion-gel p99 improved slightly; tractor-beam
p99 regressed by 3.3%, while its maximum improved. Separate repetitions caught
occasional 9-31 ms queued-worker events in both portal and co-op gameplay.
This variance is why the runner is retained and why the explicit GPU-fence
path remains an open smoothness issue rather than being hidden by averages.

After adding research-only draw/command classification, the normal Release
build received another five-second patched-only pass through all three demos:

| Gameplay segment | Average FPS | p99 ms | Maximum ms | Frames >8.3 ms |
|---|---:|---:|---:|---:|
| Trust Fling | 962.71 | 1.6099 | 7.0009 | 0 |
| Co-op repulsion gel | 972.20 | 2.1623 | 10.7736 | 1 |
| Co-op tractor beam | 949.77 | 1.4803 | 11.2378 | 1 |

The two co-op maxima confirm that the remaining hitch classes are still
observable in short real gameplay. The release does not claim them as fixed;
the draw trace attributes the paint burst to native D3D9 submission while
separate captures attribute the tractor/fence class to explicit native D3D9
serialization. A tentative dynamic-light list-node interpretation was rejected
after disassembly proved its assumed functor ABI wrong.

A later exact-functor trace separated map setup from steady gameplay. The
2,310-surface callback burst is `R_RedownloadAllLightmaps` during map/demo
initialization, not a steady dynamic-light pass. With tracing delayed until
after initialization, a 360 FPS cold process still contained one 9.8429 ms
worker batch whose longest native draw was 6.6628 ms. Replaying the identical
segment in that process reduced the maximum batch to 4.5257 ms and the longest
draw to 0.8742 ms. This is retained as a first-use native-driver/resource cost;
the project does not hide it with dummy prewarming.

## Short live-gameplay A/B

The new live-input runner replaces demo playback for the primary smoke matrix.
It drives normal movement, jumping, and portal-gun input while linked portal
interiors remain visible. Single-player uses Trust Fling; co-op uses two active
local players in the lobby. Each variant below is a five-second native-D3D9
capture with the user's visual configuration left intact.

| Target | Scene | Variant | Average FPS | p99 ms | Maximum ms | 1% low FPS |
|---:|---|---:|---:|---:|---:|---:|
| 999 | SP Trust Fling | unpatched | 897.09 | 2.3865 | 3.9488 | 419.02 |
| 999 | SP Trust Fling | patched | 943.68 | 2.1110 | 3.9090 | 473.71 |
| 999 | Co-op lobby | unpatched | 469.58 | 4.2633 | 5.9362 | 234.56 |
| 999 | Co-op lobby | patched | 546.38 | 3.1407 | 5.2133 | 318.40 |
| 360 | SP Trust Fling | unpatched | 351.05 | 4.0257 | 6.0486 | 248.40 |
| 360 | SP Trust Fling | patched | 360.12 | 3.8569 | 4.5146 | 259.28 |
| 360 | Co-op lobby | unpatched | 335.98 | 4.9299 | 6.2144 | 202.84 |
| 360 | Co-op lobby | patched | 359.84 | 3.7853 | 4.1052 | 264.18 |

At the throughput-limited 999 target, the patch improves SP average FPS by
5.2% and SP p99 by 11.5%. Co-op average FPS improves by 16.4%, p99 by 26.3%,
and 1% low FPS by 35.7%. At the 360 target, co-op p99 improves by 23.2% and
1% low FPS by 30.2%. None of the eight live captures contains a frame above
8.3 ms. The 999 co-op sample clears the original 35% 1%-low target once, but
the release gate remains provisional until repeated longer runs reproduce it.

A clean reinstall then ran the combined co-op-to-SP 360 matrix end to end. The
patched co-op repeat measured 359.55 FPS, 3.7712 ms p99, and 4.2334 ms maximum;
the patched SP repeat measured 360.06 FPS, 3.9090 ms p99, and 4.3959 ms maximum.
Again, neither scene produced a frame above 8.3 ms.

The analyzer now removes PresentMon's undefined first interval separately for
each process/swap chain. A trace beginning between presents previously produced
one artificial interval approximately equal to the complete capture duration;
that boundary row is not a game frame. No interior or final samples are trimmed.

## Focus and portal results

The original timestamped native-D3D9 harness completed 30 minimize/restore/
portal-placement cycles. After allowing the synchronous reset to complete, the
100 ms post-portal windows measured:

- median worst frame: 2.1289 ms;
- p95 worst frame: 2.6520 ms;
- absolute worst frame: 2.8055 ms;
- frames above 8.3 ms: 0.

The apparent first-portal stall is therefore the preceding focus reset, not
lazy first-portal rendering. In the portal-visible co-op lobby, the unpatched
resource reacquisition averaged about 564 ms. Sampling isolated two redundant
CPU static-lighting cache rebuilds inside that interval. With the focus
synchronization and both scoped lighting skips enabled, three direct restores
measured 245.36, 246.31, and 261.11 ms (250.93 ms average), a 55.5% reduction.
A subsequent 30-cycle stability run averaged 258.75 ms, with 296.06 ms p95 and
297.02 ms maximum. No static-lighting work appeared in the two seconds after
the reacquisition interval, so the removed work was not deferred to the first
portal frame.

Frozen-camera before/after-focus image differences were within the temporal
and JPEG noise of a no-focus control pair. A complete lossless fixed-tick image
equivalence suite is still outstanding. The remaining roughly 250-280 ms is
genuine D3D/studio-render resource recreation and driver work; p2boost does not
skip, defer, or prewarm those callbacks.

An additional 20-second portal-visible lobby throughput A/B at `fps_max 999`
was neutral: 472.47 FPS unpatched versus 470.38 FPS patched (-0.44%). Median
frame time was 2.0321 versus 2.0335 ms and p99 was 3.1350 versus 3.1899 ms.
This is within the 2% throughput-regression gate and expected because the new
hooks are inactive outside focus reacquisition.

The final v0.4.0 binary received another portal-visible 20-second smoke A/B
after packaging changes. It measured 467.02 FPS unpatched and 466.18 FPS
patched (-0.18%). Patched p99 improved from 3.3256 to 3.2363 ms, 1% low from
300.70 to 308.99 FPS, and display repeats from 0.45% to 0.19%. A single
8.4888 ms patched frame remains; this result confirms no throughput regression
but does not claim that ordinary steady-state hitches are solved.

## Synchronized ETW hitch capture

A 15-second GeneralProfile trace of the deterministic two-player lobby with a
linked portal pair visible recorded 6,623 frames:

| Metric | Result |
|---|---:|
| Average FPS | 441.82 |
| Median | 2.1692 ms |
| p95 | 3.1012 ms |
| p99 | 3.5445 ms |
| Maximum | 8.4279 ms |
| 1% low | 282.13 FPS |
| p99 / median | 1.634 |

There was one frame above 8.3 ms and none above 16.7 ms. That frame contained
only 1.59 ms of GPU-busy work and no coincident Portal asset read. ETW samples
place its main-thread work in client renderable filtering and engine BSP
visibility traversal. This confirms a remaining intermittent CPU visibility
burst, but not a redundant call: each local player and each portal has distinct
view state. A permanent cache or skip would therefore be speculative and is
not included in the current binary.

## Gate status

- Post-focus portal frame <=8.3 ms: **met** in 30 valid timestamped cycles.
- Co-op 1% low improvement >=35%: **provisional**; the short live 999 run
  improves by 35.7%, but longer repeated live runs are still required.
- p99 <=1.25x median at 360: **not met**; the live SP and co-op ratios are
  1.3898 and 1.3648 respectively, though both cadence and p99 improve.
- <=2% SP throughput regression: **met**; measured throughput improves.
- 100 map/focus cycles and lossless visual/state equivalence: **not yet completed**.

No release should claim the full original acceptance plan is complete.
