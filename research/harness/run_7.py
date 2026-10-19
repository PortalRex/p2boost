"""Lobby cost map toward 1000 FPS: measurement-only toggles that remove one kind
of work each, so the remaining main-thread budget is known per subsystem."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT

HUD = ["sar_hud_position", "sar_hud_angles", "sar_hud_velocity", "sar_hud_demo"]
TOGGLES = {
    "r_drawvgui": 1, "cl_drawhud": 1, "net_graph": 1, "r_drawparticles": 1, "r_decals": 2048, "r_drawdetailprops": 1,
    "r_dynamic": 1, "r_shadows": 1, "r_flashlightdepthtexture": 1, "mat_postprocess_enable": 1,
    "r_drawstaticprops": 1, "r_drawtranslucentworld": 1, "r_drawropes": 1, "cl_particle_fallback_base": 0,
    "r_occlusion": 1, "r_portal_stencil_depth": 1, "mat_motion_blur_enabled": 0,
}
base = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120", "p2boost_fix vgui_screen_fps 60"] \
    + [f"{k} {v}" for k, v in TOGGLES.items()] + [f"{c} 1" for c in HUD]


def variant(name, *changes):
    return (name, base + list(changes))


table = run_suite(os.path.join(ROOT, "N_costmap"), "ss_map mp_coop_lobby_3", [
    variant("best"),
    variant("no SAR HUD, no net_graph", "net_graph 0", *[f"{c} 0" for c in HUD]),
    variant("no HUD at all (cl_drawhud 0, r_drawvgui 0, SAR, net_graph)", "cl_drawhud 0", "r_drawvgui 0", "net_graph 0", *[f"{c} 0" for c in HUD]),
    variant("no particles", "r_drawparticles 0"),
    variant("no decals", "r_decals 0"),
    variant("no detail props", "r_drawdetailprops 0"),
    variant("no dynamic lights", "r_dynamic 0"),
    variant("no shadows at all", "r_shadows 0"),
    variant("no flashlight depth", "r_flashlightdepthtexture 0"),
    variant("no post-processing", "mat_postprocess_enable 0"),
    variant("no static props", "r_drawstaticprops 0"),
    variant("no translucent world", "r_drawtranslucentworld 0"),
    variant("no ropes", "r_drawropes 0"),
    variant("vgui_screen_fps 0", "p2boost_fix vgui_screen_fps 0"),
], rounds=2, seconds=8, restore=HUD + list(TOGGLES))
for row in table:
    print(json.dumps(row))
