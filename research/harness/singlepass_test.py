"""Research A/B of the engine's single-pass flashlight mode in the lobby."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

MAP = sys.argv[1] if len(sys.argv) > 1 else "ss_map mp_coop_lobby_3"
OUT = os.path.join(RESULTS_ROOT, "O_singlepass")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120", "p2boost_fix vgui_screen_fps 60"]
g = Game(OUT)
try:
    g.launch()
    g.run("sv_lan 1", MAP)
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92", *BEST)
    time.sleep(8)
    for value in (0, 1, 1, 0, 1, 0):
        g.log(g.run(f"p2boost_x_singlepass {value}").strip()[-120:])
        time.sleep(4)
        stats, valid, coop = g.measure(8)
        g.log(f"singlepass {value}: {stats} valid={valid} coop={coop}")
    g.run("fps_max 999")
    for value in (0, 1):
        g.run(f"p2boost_x_singlepass {value}")
        time.sleep(3)
        g.screenshot(f"lobby_singlepass_{value}")
    g.run("p2boost_x_singlepass 0")
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
