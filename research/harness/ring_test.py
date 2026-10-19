"""frame_queue_depth A/B in the split-screen lobby with screenshots and counters."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

MAP = sys.argv[1] if len(sys.argv) > 1 else "ss_map mp_coop_lobby_3"
OUT = os.path.join(RESULTS_ROOT, "R_ring")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix partner_view_fps 120"]
g = Game(OUT)


def fix_line():
    return " | ".join(l.strip() for l in g.run("p2boost_fix frame_queue_depth").splitlines() if "frame_queue" in l)


try:
    g.launch()
    g.log(fix_line())
    g.run("sv_lan 1", MAP)
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92", *BEST)
    time.sleep(6)
    g.log(fix_line())
    for depth in (1, 2, 1, 2, 1, 2):
        g.run(f"p2boost_fix frame_queue_depth {depth}")
        time.sleep(3)
        stats, valid, coop = g.measure(8)
        g.log(f"frame_queue_depth {depth}: {stats} valid={valid} coop={coop}")
        g.log(fix_line())
    g.run("fps_max 999")
    for depth in (1, 2):
        g.run(f"p2boost_fix frame_queue_depth {depth}")
        time.sleep(3)
        g.screenshot(f"lobby_depth{depth}")
    # a map change exercises the drain and mode-switch path
    g.run("p2boost_fix frame_queue_depth 2", "disconnect")
    time.sleep(6)
    g.run("sv_lan 1", "ss_map mp_coop_doors")
    time.sleep(45)
    g.run("fps_max 999")
    time.sleep(3)
    g.screenshot("doors_depth2")
    g.log(fix_line())
    g.log("errors: " + " | ".join(l.strip() for l in open(os.path.join(OUT, "console.txt"), errors="replace").read().splitlines()[-400:] if "Error" in l or "crash" in l.lower())[:300])
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
