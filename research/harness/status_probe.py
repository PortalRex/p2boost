"""Launch, print p2boost_status at the menu and in the lobby, quit."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game

OUT = os.path.join(RESULTS_ROOT, "S_probe")
g = Game(OUT)
try:
    g.launch()
    g.log("menu: " + " | ".join(l.strip() for l in g.run("p2boost_fix frame_queue_depth").splitlines() if "frame_queue" in l))
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(45)
    g.run("fps_max 0")
    time.sleep(5)
    g.log("lobby: " + " | ".join(l.strip() for l in g.run("p2boost_fix frame_queue_depth").splitlines() if "frame_queue" in l))
    g.log("mqm: " + g.run("mat_queue_mode").strip()[:120].replace("\n", " "))
except Exception as error:
    g.log(f"error: {error!r}")
finally:
    g.quit()
    g.log("done")
