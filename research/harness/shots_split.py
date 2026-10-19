import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state
OUT = os.path.join(RESULTS_ROOT, "G_split_shots")
g = Game(OUT)
try:
    g.launch()
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3"); time.sleep(45)
    g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 95", *state(depth_fps=120, snd_async=1)); time.sleep(5)
    for cap in (0, 120):
        g.run(f"p2boost_split_view2_fps {cap}"); time.sleep(2)
        g.screenshot(f"static_cap{cap}")
        g.run("cmd2 +forward", "cmd2 +left"); time.sleep(0.7)
        g.screenshot(f"moving_cap{cap}_a"); time.sleep(0.5)
        g.screenshot(f"moving_cap{cap}_b")
        g.run("cmd2 -forward", "cmd2 -left"); time.sleep(2)
    g.log([l.strip() for l in g.run("p2boost_status").splitlines() if "remote view" in l])
except Exception as e:
    g.log(f"error: {e!r}")
finally:
    g.quit()
