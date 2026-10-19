"""A/B and screenshots for the VGUI screen cache in the split-screen lobby.

cache_test.py [map command]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

MAP = sys.argv[1] if len(sys.argv) > 1 else "ss_map mp_coop_lobby_3"
OUT = os.path.join(RESULTS_ROOT, "M_vgui_cache")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120"]
g = Game(OUT)
try:
    g.launch()
    status = g.run("p2boost_status")
    g.log("status: " + " | ".join(l.strip() for l in status.splitlines() if "vgui_screen" in l or "error" in l.lower()))
    g.run("sv_lan 1", MAP)
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92", *BEST)
    time.sleep(8)
    for round_ in range(2):
        for fps in (0, 60, 0, 60):
            g.run(f"p2boost_fix vgui_screen_fps {fps}")
            time.sleep(3)
            stats, valid, coop = g.measure(8)
            g.log(f"vgui_screen_fps {fps}: {stats} valid={valid} coop={coop}")
            if round_ == 0:
                g.log(" | ".join(l.strip() for l in g.run("p2boost_fix vgui_screen_fps").splitlines() if "vgui_screen" in l))
    g.run("fps_max 999")
    for fps in (0, 60):
        g.run(f"p2boost_fix vgui_screen_fps {fps}")
        time.sleep(3)
        g.screenshot(f"lobby_vgui_{fps}")
    g.run("p2boost_fix vgui_screen_fps 60")
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
