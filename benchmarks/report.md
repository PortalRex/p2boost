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

## Focus and portal results

The timestamped windowed native-D3D9 harness completed 30 minimize/restore/
portal-placement cycles. After allowing the synchronous reset to complete, the
100 ms post-portal windows measured:

- median worst frame: 2.1289 ms;
- p95 worst frame: 2.6520 ms;
- absolute worst frame: 2.8055 ms;
- frames above 8.3 ms: 0.

The apparent first-portal stall is therefore the preceding focus reset, not
lazy first-portal rendering. Resource tracing measured roughly 275-317 ms in
the material-system restore callback registry per reset. A fullscreen D3D9
device loss genuinely requires those default-pool resources to be recreated;
skipping or delaying the callbacks would only move the hitch or corrupt state.

## Gate status

- Post-focus portal frame <=8.3 ms: **met** in 30 valid timestamped cycles.
- Co-op 1% low improvement >=35%: **not met**; no quality-neutral redundant work proven.
- p99 <=1.25x median at 360: **not met** (1.3145), though cadence and p99 improve.
- <=2% SP throughput regression: **met**; measured throughput improves.
- 100 map/focus cycles and visual/state equivalence: **not yet completed**.

No release should claim the full original acceptance plan is complete.
