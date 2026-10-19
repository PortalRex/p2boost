import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import Game

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "light_out")
g = Game(OUT)
try:
    g.launch()
    g.load_coop_lobby()
    time.sleep(3)
    out = g.run("p2boost_shadow_depth_views")
    views = re.findall(r"origin (\S+) (\S+) (\S+) angles (\S+) (\S+) (\S+) fov (\S+) frame (\d+)", out)
    for view in views:
        g.log("light view " + " ".join(view))
    g.run("sv_cheats 1")
    for index, view in enumerate(views[:4]):
        x, y, z, pitch, yaw, roll, fov, frame = view
        g.run(f"setpos {x} {y} {float(z) - 64:.1f}", f"setang {pitch} {yaw} 0")
        time.sleep(1.5)
        g.screenshot(f"along_light{index}")
except Exception as error:
    g.log(f"error: {error}")
finally:
    g.quit()
    g.log("done")
