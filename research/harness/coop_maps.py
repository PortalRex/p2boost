"""Five co-op maps in split-screen: measure vanilla vs the 700-config, capture one frame of each."""
import csv
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, GAME, Game, qpc
from run_all import state

MAPS = [
    ("mp_coop_doors", "Course 1: Team Building, Doors"),
    ("mp_coop_fling_3", "Course 2: Mass and Velocity, Flings"),
    ("mp_coop_wall_intro", "Course 3: Hard-Light Surfaces, Bridge Intro"),
    ("mp_coop_tbeam_redirect", "Course 4: Excursion Funnels, Funnel Redirect"),
    ("mp_coop_catapult_catch", "Course 6: Art Therapy, Catapult Catch"),
]
VANILLA = state(enable=0, multicore=0) + ["p2boost_fix partner_view_fps 0"]
VANILLA[VANILLA.index("mat_queue_mode -1")] = "mat_queue_mode 0"
OPT = state(depth_fps=120, snd_async=1) + ["p2boost_fix partner_view_fps 120"]
OUT = os.path.join(RESULTS_ROOT, "I_coop_maps")
# optional map names on the command line re-run only those and merge into scenes.json
if sys.argv[1:]:
    MAPS = [m for m in MAPS if m[0] in sys.argv[1:]]
g = Game(OUT)
results = {}
if os.path.exists(os.path.join(OUT, "scenes.json")):
    results = json.load(open(os.path.join(OUT, "scenes.json")))


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


try:
    g.launch()
    for map_name, title in MAPS:
        # ss_map issued inside a running co-op map shuts the server down and
        # drops the client; it only works from the menu.
        g.run("disconnect")
        time.sleep(6)
        g.run("sv_lan 1", f"ss_map {map_name}")
        time.sleep(50)
        g.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 92")
        time.sleep(8)
        # walk both players out of the arrival elevators, then stand still
        g.run("cmd1 +forward", "cmd2 +forward")
        time.sleep(4)
        g.run("cmd1 -forward", "cmd2 -forward")
        time.sleep(1)
        # player 1 ends up against the wall it walked into; turn it around
        # (cl_yawspeed 210 deg/s) so its half shows the room
        g.run("cmd1 +left")
        time.sleep(180 / 210)
        g.run("cmd1 -left")
        time.sleep(3)
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
