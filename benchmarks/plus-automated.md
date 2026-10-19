# Automated A/B results on the `plus` branch

All runs were driven by `research/harness` over Portal 2's network console
(`-netconport`, no keyboard or window input), measured with `p2boost_trace`
sliced by QPC, interleaved ABBA rounds, `fps_max 0`, spawn position. Hardware:
RTX 5090, Ryzen 7 7800X3D, 2560x1440, DXVK 3.0.2 measured slower here so the
shipped DXVK is active.

Fixes are named as in `p2boost_fix <name> <value>`.

## Split-screen co-op lobby (`ss_map mp_coop_lobby_3`), 2 x 10 s per variant

| Variant | Avg FPS | p99 ms | 1% low |
|---|---:|---:|---:|
| Stock, `mat_queue_mode 0` | 158.3 | 8.28 | 114 |
| Stock, `mat_queue_mode -1` (resolved to multicore at runtime) | 249.2 | 5.42 | 170 |
| All fixes (defaults) | 450.4 | 3.33 | 247 |
| minus `flashlight_flush` | 315.8 | 4.35 | 210 |
| minus `xinput_throttle` | 429.7 | 3.48 | 242 |
| minus `occlusion_skip` | 446.3 | 3.35 | 245 |
| minus `vgui_shadow_skip` | 451.9 | 3.31 | 243 |
| minus `shadow_depth_reuse` | 410.2 | 3.63 | 229 |
| All + `shadow_depth_fps 120` | 485.2 | 3.13 | 256 |
| All + depth 120 + `snd_mix_async 1` (best) | 490-498 | 3.04-3.11 | 247-263 |
| best + `cl_threaded_bone_setup 1` | 492.5 | 3.00 | 265 |
| best + `net_graph 0` | 493.6 | 3.02 | 265 |
| best, launched `-high` | 487.4 | 3.09 | 266 |
| best, launched `-threads 8` | 330.0 | 4.53 | 207 |
| best, launched `+threadpool_affinity 0` | 486.6 | 3.21 | 258 |
| best, no in-world VGUI screens (measurement only) | 554.0 | 2.71 | 282 |
| best, view 2 not rendered (measurement only) | 805.1 | 2.12 | 357 |
| best + `partner_view_fps 240` | 605.4 | 2.71 | 294 |
| best + `partner_view_fps 144` | 681.5 | 2.66 | 295 |
| best + `partner_view_fps 120` | 703.5 | 2.54 | 315 |
| best + partner 120 + `frame_queue_depth 1` (engine flip) | 679.0 | 2.69 | - |
| best + partner 120 + `frame_queue_depth 2` | 755.5 | 2.60 | - |

`p2boost_fix shadow_depth_fps`: 0 = 447.7, 120 = 498.0, 60 = 493.8 (3 x 10 s).

## One view (`map sp_a2_laser_relays`), 2 x 10 s per variant

| Variant | Avg FPS | p99 ms | 1% low |
|---|---:|---:|---:|
| Stock, `mat_queue_mode 0` | 345.9 | 4.31 | 198 |
| Stock, `mat_queue_mode -1` | 592.4 | 2.74 | 290 |
| All fixes (defaults) | 805.1 | 2.06 | 335 |
| All + depth 120 + `snd_mix_async 1` | 953.2 | 1.76 | 409 |

## Notes

- `sv_lan 1` before `ss_map` stops the local listen server dropping the
  client with "No Steam logon" every two minutes when the game is launched
  directly rather than through Steam.
- Loading a co-op map with `map` and one player returns to the menu
  ("Your partner has disconnected"); single-view co-op needs a real partner.
- `research/scripts/capture-cpu-profile.ps1` triggers a UAC prompt on this
  host, so CPU profiles cannot be captured unattended.
