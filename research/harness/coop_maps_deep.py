"""Five co-op maps, captured deep inside each chamber: both players are placed
at the exit (or the puzzle element farthest from the spawn) looking back over
the puzzle, then vanilla vs the 700-config is measured and one frame of each
is captured.

coop_maps_deep.py [--test] [map ...]
  --test  load each map, place the players, screenshot, no measurement
"""
import csv
import json
import math
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bsp_spawns import look_angles, summary
from harness import RESULTS_ROOT, GAME, Game, qpc
from run_all import state

MAPS = [
    ("mp_coop_rat_maze", "Course 1: Team Building, Rat Maze"),
    ("mp_coop_infinifling_train", "Course 2: Mass and Velocity, Infinifling"),
    ("mp_coop_wall_block", "Course 3: Hard-Light Surfaces, Bridge Block"),
    ("mp_coop_paint_red_racer", "Course 5: Mobility Gels, Red Racer"),
    ("mp_coop_catapult_catch", "Course 6: Art Therapy, Catapult Catch"),
]
VANILLA = state(enable=0, multicore=0) + ["p2boost_fix split_view2_fps 0"]
VANILLA[VANILLA.index("mat_queue_mode -1")] = "mat_queue_mode 0"
OPT = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120"]
OUT = os.path.join(RESULTS_ROOT, "J_coop_maps_deep")
TEST = "--test" in sys.argv
selected = [a for a in sys.argv[1:] if not a.startswith("--")]
if selected:
    MAPS = [m for m in MAPS if m[0] in selected]


# maps without an exit door entity: the last checkpoint spawn in the map
ANCHORS = {"mp_coop_wall_5": (-2092.0, -1624.0, -127.0)}
# second player's own spot where the side offset would leave the platform
ANCHORS_P2 = {"mp_coop_wall_5": (-2095.0, -1819.0, -127.0)}
# how far to step from the anchor toward the puzzle; the exit corridor of
# rat_maze and the raised exit of infinifling_train need more than the default
STEPS = {"mp_coop_rat_maze": 360.0, "mp_coop_infinifling_train": 400.0, "mp_coop_wall_5": 0.0}


def placement(map_name):
    """eye position for player 1 and 2 and the view angles, from the BSP."""
    spawns, doors, puzzle, start, centroid = summary(map_name)
    if map_name in ANCHORS:
        anchor = ANCHORS[map_name]
    elif doors:
        anchor = max(doors, key=lambda d: math.dist(d[0], start))[0]
    else:
        anchor = max(puzzle, key=lambda p: math.dist(p, start))
    # step from the anchor toward the puzzle so the players stand inside the
    # chamber, and put player 2 64 units to the side
    step = STEPS.get(map_name, 160.0)
    dx, dy = centroid[0] - anchor[0], centroid[1] - anchor[1]
    length = math.hypot(dx, dy) or 1.0
    ux, uy = dx / length, dy / length
    p1 = (anchor[0] + step * ux, anchor[1] + step * uy, anchor[2] + 8)
    p2 = (p1[0] - 64 * uy, p1[1] + 64 * ux, p1[2])
    if map_name in ANCHORS_P2:
        p2 = (ANCHORS_P2[map_name][0], ANCHORS_P2[map_name][1], ANCHORS_P2[map_name][2] + 8)
    pitch, yaw = look_angles((p1[0], p1[1], p1[2] + 64), centroid)
    return p1, p2, max(-30.0, min(10.0, pitch)), yaw


def measure(seconds=8):
    g.run("p2boost_trace 1")
    time.sleep(1)
    begin = qpc()
    time.sleep(seconds)
    end = qpc()
    g.run("p2boost_trace 0")
    time.sleep(1)
    with open(os.path.join(GAME, "p2boost_metrics.csv")) as handle:
        rows = [r for r in csv.DictReader(handle) if begin <= int(r["qpc"]) <= end]
    if not rows or not all(r["simulating"] == "1" and r["coop"] == "1" for r in rows):
        return None
    frames = sorted(float(r["frame_ms"]) for r in rows)
    return {"avg_fps": 1000 / statistics.mean(frames), "p99_ms": frames[min(len(frames) - 1, int(0.99 * len(frames)))]}


g = Game(OUT)
results = {}
if os.path.exists(os.path.join(OUT, "scenes.json")):
    results = json.load(open(os.path.join(OUT, "scenes.json")))

try:
    g.launch()
    for map_name, title in MAPS:
        p1, p2, pitch, yaw = placement(map_name)
        g.log(f"{map_name}: p1 {p1} p2 {p2} pitch {pitch:.1f} yaw {yaw:.1f}")
        g.run("disconnect")
        time.sleep(6)
        g.run("sv_lan 1", f"ss_map {map_name}")
        time.sleep(50)
        g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92", "sv_cheats 1")
        time.sleep(8)
        # leave the arrival elevators first; a teleport from inside one is undone
        g.run("cmd1 +forward", "cmd2 +forward")
        time.sleep(4)
        g.run("cmd1 -forward", "cmd2 -forward")
        time.sleep(1)
        g.run(f"cmd1 setpos {p1[0]:.0f} {p1[1]:.0f} {p1[2]:.0f}", f"cmd1 setang {pitch:.1f} {yaw:.1f} 0",
              f"cmd2 setpos {p2[0]:.0f} {p2[1]:.0f} {p2[2]:.0f}", f"cmd2 setang {pitch:.1f} {yaw:.1f} 0")
        time.sleep(4)
        # the positions stay; this removes the "cheats are enabled" banner
        g.run("sv_cheats 0")
        time.sleep(1)
        g.log("positions: " + " | ".join(line for line in g.run("cmd1 getpos", "cmd2 getpos").splitlines() if "setpos" in line))
        if TEST:
            g.run("fps_max 999", "p2boost_overlay 2")
            time.sleep(2)
            g.screenshot(f"{map_name}_test")
            continue
        runs = {"vanilla": [], "optimized": []}
        for label, commands in (("vanilla", VANILLA), ("optimized", OPT), ("optimized", OPT), ("vanilla", VANILLA)):
            g.run(*commands)
            time.sleep(3)
            result = measure()
            if result:
                runs[label].append(result)
            g.log(f"{map_name} {label}: {result}")
        g.run("fps_max 999")
        for label, commands in (("vanilla", VANILLA), ("optimized", OPT)):
            g.run(*commands, "p2boost_overlay 2")
            time.sleep(3)
            g.screenshot(f"{map_name}_{label}")
        g.run("p2boost_overlay 0")
        results[map_name] = {
            "title": title,
            "vanilla": statistics.mean(r["avg_fps"] for r in runs["vanilla"]) if runs["vanilla"] else None,
            "optimized": statistics.mean(r["avg_fps"] for r in runs["optimized"]) if runs["optimized"] else None,
            "vanilla_p99": statistics.mean(r["p99_ms"] for r in runs["vanilla"]) if runs["vanilla"] else None,
            "optimized_p99": statistics.mean(r["p99_ms"] for r in runs["optimized"]) if runs["optimized"] else None,
            "rounds": (len(runs["vanilla"]), len(runs["optimized"])),
        }
        g.log(json.dumps({map_name: results[map_name]}))
        json.dump(results, open(os.path.join(OUT, "scenes.json"), "w"), indent=1)
except Exception as error:
    g.log(f"error: {error!r}")
finally:
    g.quit()
    g.log("done")
