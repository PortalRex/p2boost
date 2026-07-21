# Benchmark report

## Environment

- Intel Core i7-14700KF, NVIDIA RTX 4070 Super
- Portal 2 build 23973718, Windows x86, native Microsoft D3D9
- SAR 1.15.3 loaded before p2boost
- PresentMon 2.5.1, 60-second captures
- Current-user profile: `fps_max 999`, `r_portal_use_pvs_optimization 0`, and
  `sar_always_transmit_heavy_ents 1` in co-op

These are initial fixed-scene measurements, not the complete release matrix.

| Scenario | Variant | Median ms | p99 ms | p99/median | 1% low FPS |
|---|---:|---:|---:|---:|---:|
| SP, 360 target | unpatched | 2.8392 | 3.4473 | 1.2142 | 290.08 |
| SP, 360 target | patched | 2.8399 | 3.4078 | 1.2000 | 293.44 |
| Co-op, user config | unpatched | 1.0622 | 2.5769 | 2.4260 | 388.06 |
| Co-op, user config | patched | 1.0717 | 2.0055 | 1.8713 | 498.63 |
| Co-op, clean PVS/heavy settings, unpatched | 1.0722 | 1.9962 | 1.8618 | 500.95 |

The 360-target improvement is small. The co-op A/B showed a 28.5% 1% low
improvement, below the requested 35% gate and nearly identical to the clean
configuration baseline. This does not prove a co-op renderer fix.

The automated 30 focus-cycle harness completed, but its first version issued
an invalid synthetic `attack` command and included minimized intervals in the
distribution; those numbers are rejected rather than reported as portal-stall
evidence. System-wide WPR CPU sampling was unavailable in the non-elevated test
session (`0xc5585011`).

## Gate status

- Post-focus portal frame <= 8.3 ms: **not yet validly measured**
- Co-op 1% low improvement >= 35%: **not met** (28.5% initial A/B)
- p99 <= 1.25x median at 360 target: **met in sampled SP scene**
- <=2% SP throughput regression: **met in sampled SP scene**
- 100 map/focus cycles and visual/state equivalence: **not yet completed**

No release should claim the full plan is complete until the remaining gates
are reproduced and the portal/co-op causes have isolated permanent fixes.
