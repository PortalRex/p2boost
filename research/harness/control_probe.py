import os
import re
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import Game

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "control_out")
g = Game(OUT)


def rendered():
    out = g.run("p2boost_status")
    match = re.search(r"depth reuse.*?(\d+) rendered, (\d+) reused", out)
    return (int(match.group(1)), int(match.group(2))) if match else (-1, -1)


def half(path):
    return np.asarray(Image.open(path).convert("L")).astype(np.int16)[:720, 560:2100]


try:
    g.launch()
    g.load_coop_lobby()
    g.run("sv_cheats 1", "r_shadows 0", "setpos 3928 3352 -447.97", "setang 40 0 0")
    time.sleep(1)
    g.run("ent_create_portal_weighted_cube")
    time.sleep(2)
    g.run('ent_fire !picker addoutput "targetname p2t_cube"')
    time.sleep(1)
    g.run("ent_fire p2t_cube DisableMotion", 'ent_fire p2t_cube addoutput "origin 4241 4200 178"')
    g.run("noclip", "setpos 4040 4130 180", "setang 14.5 8 0")
    time.sleep(2)

    for cap in (0, 1, 120):
        g.run(f"p2boost_shadow_depth_fps {cap}")
        time.sleep(1)
        before = rendered()
        time.sleep(3)
        after = rendered()
        g.log(f"cap {cap}: rendered +{after[0] - before[0]}, reused +{after[1] - before[1]} over 3 s")

    g.run("p2boost_shadow_depth_fps 0")
    shots = {}
    for depth in (1, 0):
        g.run(f"r_flashlightdepthtexture {depth}", 'ent_fire p2t_cube addoutput "origin 4241 4200 178"')
        time.sleep(1.5)
        shots[f"A{depth}"] = g.screenshot(f"depth{depth}_A")
        g.run('ent_fire p2t_cube addoutput "origin 4241 4240 178"')
        time.sleep(1.5)
        shots[f"B{depth}"] = g.screenshot(f"depth{depth}_B")
    for depth in (1, 0):
        diff = np.abs(half(shots[f"A{depth}"]) - half(shots[f"B{depth}"]))
        g.log(f"r_flashlightdepthtexture {depth}: A vs B mean {diff.mean():.2f}, pixels>25 {int((diff > 25).sum())}")
    g.run("r_flashlightdepthtexture 1")
except Exception as error:
    g.log(f"error: {error}")
finally:
    g.quit()
    g.log("done")
