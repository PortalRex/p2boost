"""Lobby A/B toward 1000 FPS: schedule phase, threaded host, HUD cost, RTT shadows."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT

HUD = ["sar_hud_position", "sar_hud_angles", "sar_hud_velocity", "sar_hud_demo"]
# every variant sets everything it may differ in, so order does not matter
base = state(depth_fps=120, snd_async=1) + ["p2boost_fix partner_view_fps 120", "host_thread_mode 0",
                                             "r_shadowrendertotexture 1", "net_graph 1"] + [f"{c} 1" for c in HUD]


def variant(name, *changes):
    return (name, base + list(changes))


table = run_suite(os.path.join(ROOT, "L_1000"), "ss_map mp_coop_lobby_3", [
    variant("best"),
    variant("best, shadow_depth_fps 100 (desync from view 2)", "p2boost_fix shadow_depth_fps 100"),
    variant("best, shadow_depth_fps 60", "p2boost_fix shadow_depth_fps 60"),
    variant("best, host_thread_mode 1", "host_thread_mode 1"),
    variant("best, host_thread_mode 2", "host_thread_mode 2"),
    variant("best, SAR HUD off, net_graph 0", "net_graph 0", *[f"{c} 0" for c in HUD]),
    variant("best, r_shadowrendertotexture 0 (measurement only)", "r_shadowrendertotexture 0"),
    variant("best, split_view2_fps 60", "p2boost_fix partner_view_fps 60"),
], rounds=2, seconds=10, restore=HUD + ["host_thread_mode", "net_graph"])
for row in table:
    print(json.dumps(row))
