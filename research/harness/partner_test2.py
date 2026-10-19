"""partner_view_fps follows in_forceuser: at a 1 FPS cap the partner's half keeps
its old picture for up to a second after that player is teleported, while the
controlled half changes at once."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

OUT = os.path.join(RESULTS_ROOT, "Q_partner")
g = Game(OUT)
try:
    g.launch()
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(50)
    g.run("fps_max 999", "p2boost_overlay 0", "jpeg_quality 92", *state(depth_fps=120, snd_async=1))
    time.sleep(6)
    g.run("sv_cheats 1", "p2boost_fix partner_view_fps 1")
    time.sleep(2)
    for user, mover, pos in ((1, "cmd1", "3928 3200 -447.97"), (0, "cmd2", "3928 3650 -447.97")):
        g.run(f"in_forceuser {user}")
        time.sleep(2.5)
        g.run(f"{mover} setpos {pos}")
        time.sleep(0.25)
        g.screenshot(f"teleport_user{user}_soon")
        time.sleep(1.5)
        g.screenshot(f"teleport_user{user}_later")
        g.log(f"in_forceuser {user}: {mover} teleported; " + " | ".join(l.strip() for l in g.run("cmd1 getpos", "cmd2 getpos").splitlines() if "setpos" in l))
    g.run("in_forceuser 0", "sv_cheats 0", "p2boost_fix partner_view_fps 120")
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
