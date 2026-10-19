"""Research: screenshots of the lobby's big movie screen under each vgui cache mode."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

OUT = os.path.join(RESULTS_ROOT, "P_vguimode")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120", "p2boost_fix vgui_screen_fps 60"]
g = Game(OUT)
try:
    g.launch()
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(50)
    g.run("fps_max 999", "p2boost_overlay 0", "jpeg_quality 92", *BEST)
    time.sleep(6)
    for mode, maximum in ((0, 100000), (1, 100000), (2, 100000), (0, 600)):
        g.log(g.run(f"p2boost_x_vguimode {mode} {maximum}").strip()[-80:])
        time.sleep(3)
        g.screenshot(f"lobby_mode{mode}_max{maximum}")
    g.log("panels: " + " | ".join(l.strip() for l in g.run("echo").splitlines() if "vgui screen panel" in l))
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
