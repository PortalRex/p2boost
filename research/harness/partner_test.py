"""partner_view_fps checks in the split-screen lobby: default cap, mat_viewportscale,
and the cap following in_forceuser."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

OUT = os.path.join(RESULTS_ROOT, "Q_partner")
BEST = state(depth_fps=120, snd_async=1)
g = Game(OUT)


def fix_line():
    return " | ".join(l.strip() for l in g.run("p2boost_fix partner_view_fps").splitlines() if "partner_view" in l)


try:
    g.launch()
    g.log(fix_line())
    g.log("in_forceuser: " + g.run("in_forceuser").strip()[:160].replace("\n", " "))
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92", *BEST)
    time.sleep(6)

    for fps in (120, 0, 120):
        g.run(f"p2boost_fix partner_view_fps {fps}")
        time.sleep(3)
        stats, valid, coop = g.measure(8)
        g.log(f"scale 1, partner_view_fps {fps}: {stats} valid={valid} coop={coop}")
    g.log(fix_line())

    g.run("mat_viewportscale 0.5")
    time.sleep(3)
    for fps in (0, 120):
        g.run(f"p2boost_fix partner_view_fps {fps}")
        time.sleep(3)
        stats, valid, coop = g.measure(6)
        g.log(f"scale 0.5, partner_view_fps {fps}: {stats} valid={valid} coop={coop}")
        g.run("fps_max 999")
        time.sleep(2)
        g.screenshot(f"viewportscale_{fps}")
        g.run("fps_max 0")
    g.log(fix_line())
    g.run("mat_viewportscale 1", "fps_max 999")
    time.sleep(2)

    # the cap follows in_forceuser: at 1 FPS the partner half freezes while
    # that player walks; the controlled half keeps moving.
    g.run("sv_cheats 1", "p2boost_fix partner_view_fps 1")
    for user, mover in ((1, "cmd1"), (0, "cmd2")):
        g.run(f"in_forceuser {user}")
        time.sleep(2)
        g.run(f"{mover} +forward")
        time.sleep(1.2)
        g.screenshot(f"forceuser{user}_a")
        time.sleep(0.4)
        g.screenshot(f"forceuser{user}_b")
        g.run(f"{mover} -forward")
        time.sleep(1)
    g.run("in_forceuser 0", "sv_cheats 0", "p2boost_fix partner_view_fps 120")
    g.log(fix_line())
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
