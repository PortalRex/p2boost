"""Ring bisect: run the reimplemented frame flip with only the engine's two contexts
(x_ring 1), then with the private third (x_ring 0); measure each."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

OUT = os.path.join(RESULTS_ROOT, "R_ring")
MODE = int(sys.argv[1]) if len(sys.argv) > 1 else 1
g = Game(OUT)


def fix_line():
    return " | ".join(l.strip() for l in g.run("p2boost_fix frame_queue_depth").splitlines() if "frame_queue" in l)[:200]


try:
    g.launch()
    g.run(f"p2boost_x_ring {MODE}", "p2boost_fix frame_queue_depth 1")
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", *state(depth_fps=120, snd_async=1), "p2boost_fix partner_view_fps 120")
    time.sleep(5)
    g.log(f"x_ring {MODE}: " + fix_line())
    for depth in (2, 1, 2, 1):
        g.run(f"p2boost_fix frame_queue_depth {depth}")
        time.sleep(3)
        stats, valid, coop = g.measure(8)
        g.log(f"x_ring {MODE} depth {depth}: {stats} valid={valid} coop={coop}")
        g.log(fix_line())
except Exception as error:
    g.log(f"error: {error!r}")
finally:
    g.quit()
    g.log("done")
