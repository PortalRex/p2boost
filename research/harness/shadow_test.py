"""Shadow freshness test against the lobby's real shadowed spotlight.

The lobby's one shadowed env_projectedtexture sits at (4096, 4200, 216) aiming
+x, 14.5 deg down, and lights the tower that player 2's spawn view faces. A
spawned cube is frozen inside that beam; moving it sideways sweeps its enlarged
flashlight shadow across the tower. Render-to-texture shadows are disabled so
only the flashlight depth map can produce a shadow. The comparison region is
derived from where the uncapped A and B shots differ. Cap 1 is the positive
control: 0.25 s after a move its depth map is usually still the old one.
"""
import json
import os
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import Game

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shadow_out")
g = Game(OUT)
CUBE_A = "4241 4200 178"
CUBE_B = "4241 4240 178"
CAPS = [0, 120, 1, 0, 120, 1, 0, 120, 1]


def player2(path):
    # player 1's half, next to the lamp; skip SAR's HUD text on both sides
    image = np.asarray(Image.open(path).convert("L")).astype(np.int16)
    return image[: image.shape[0] // 2, 560:2100]


def step(*commands, wait=0.5):
    out = g.run(*commands, timeout=15)
    time.sleep(wait)
    return out


try:
    g.launch()
    g.load_coop_lobby()
    step("sv_cheats 1", "r_shadows 0", "setpos 3928 3352 -447.97", "setang 40 0 0")
    step("ent_create_portal_weighted_cube", wait=2)
    step('ent_fire !picker addoutput "targetname p2t_cube"')
    step("ent_fire p2t_cube DisableMotion", f'ent_fire p2t_cube addoutput "origin {CUBE_A}"', wait=2)
    step("noclip", "setpos 4040 4130 180", "setang 14.5 8 0", wait=2)
    g.log(g.run("p2boost_shadow_depth_views").strip())

    shots = []
    for index, cap in enumerate(CAPS):
        step(f"p2boost_fix shadow_depth_fps {cap}", f'ent_fire p2t_cube addoutput "origin {CUBE_A}"', wait=1.5)
        at_a = g.screenshot(f"r{index}_cap{cap}_A")
        step(f'ent_fire p2t_cube addoutput "origin {CUBE_B}"', wait=0.25)
        at_b = g.screenshot(f"r{index}_cap{cap}_B")
        shots.append((cap, at_a, at_b))
    g.log([l.strip() for l in g.run("p2boost_status").splitlines() if "depth reuse" in l])

    uncapped_a = [player2(a) for cap, a, b in shots if cap == 0]
    uncapped_b = [player2(b) for cap, a, b in shots if cap == 0]
    moved = np.abs(np.mean(uncapped_a, axis=0) - np.mean(uncapped_b, axis=0))
    mask = moved > 25
    g.log(f"shadow-motion region: {int(mask.sum())} pixels")
    Image.fromarray((mask * 255).astype(np.uint8)).save(os.path.join(OUT, "mask.png"))
    mean_a = np.mean(uncapped_a, axis=0)
    mean_b = np.mean(uncapped_b, axis=0)
    results = []
    for cap, at_a, at_b in shots:
        image = player2(at_b)
        to_a = float(np.abs(image - mean_a)[mask].mean()) if mask.any() else -1
        to_b = float(np.abs(image - mean_b)[mask].mean()) if mask.any() else -1
        results.append({"cap": cap, "shot": os.path.basename(at_b), "vs_A": round(to_a, 2), "vs_B": round(to_b, 2),
                        "verdict": "fresh (B)" if to_b < to_a else "stale (A)"})
        g.log(json.dumps(results[-1]))
    json.dump(results, open(os.path.join(OUT, "results.json"), "w"), indent=1)
except Exception as error:
    g.log(f"error: {error}")
finally:
    g.quit()
    g.log("done")
