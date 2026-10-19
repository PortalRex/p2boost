"""Interleaved A/B benchmark over the net console. No input injection.

A suite is a list of (name, [console commands]) variants. Every variant fully
describes its own state. Rounds alternate order (ABBA). Each segment is a
p2boost frame trace sliced by QPC; segments where the session dropped are
re-run after reloading the map.
"""
import csv
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import GAME, Game, qpc


def segment_stats(rows):
    frames = sorted(float(r["frame_ms"]) for r in rows)
    worst = frames[-max(1, len(frames) // 100):]
    return {
        "frames": len(frames),
        "avg_fps": round(1000 / statistics.mean(frames), 1),
        "p50_ms": round(frames[len(frames) // 2], 3),
        "p99_ms": round(frames[min(len(frames) - 1, int(0.99 * len(frames)))], 3),
        "low1_fps": round(1000 / statistics.mean(worst), 1),
    }


def run_suite(out_dir, map_command, variants, rounds=2, seconds=10, extra_args=(), restore=(), require_coop=True,
              settle=45, warmup=15, require_sim=True, screenshot=False):
    g = Game(out_dir)
    results = []
    try:
        g.launch(extra_args=list(extra_args))
        saved = {}
        for cvar in restore:
            match = re.search(r'"%s" = "([^"]*)"' % re.escape(cvar), g.run(cvar))
            if match:
                saved[cvar] = match.group(1)
        g.log(f"saved cvars: {saved}")
        g.run("sv_lan 1", map_command)
        time.sleep(settle)
        g.run("fps_max 0", "p2boost_overlay 0")
        time.sleep(warmup)
        if screenshot:
            g.screenshot("state")
            g.log(g.run("status")[-300:])

        order = []
        for r in range(rounds):
            order += variants if r % 2 == 0 else list(reversed(variants))
        for index, (name, commands) in enumerate(order):
            for attempt in range(3):
                g.run(*commands)
                time.sleep(3)
                g.run("p2boost_trace 1")
                begin = qpc()
                time.sleep(seconds)
                end = qpc()
                status = g.run("p2boost_trace 0", "p2boost_status")
                time.sleep(1)
                with open(os.path.join(GAME, "p2boost_metrics.csv")) as handle:
                    rows = [r for r in csv.DictReader(handle) if begin <= int(r["qpc"]) <= end]
                ok = bool(rows) and (not require_sim or all(r["simulating"] == "1" for r in rows))
                if require_coop:
                    ok = ok and all(r["coop"] == "1" for r in rows)
                if ok:
                    break
                g.log(f"segment {index} ({name}) left the session: " +
                      " | ".join(l.strip() for l in g.run("status").splitlines() if "map" in l.lower())[:200])
                g.run("sv_lan 1", map_command)
                time.sleep(settle)
                g.run("fps_max 0", "p2boost_overlay 0")
                time.sleep(warmup)
            else:
                g.log(f"segment {index} ({name}) never stayed in session; skipped")
                continue
            result = {"variant": name, **segment_stats(rows)}
            depth = re.search(r"depth reuse\s+(.*)", status)
            result["depth"] = depth.group(1).strip() if depth else ""
            results.append(result)
            g.log(json.dumps(result))
        g.run("p2boost_trace 0")
        for cvar, value in saved.items():
            g.run(f'{cvar} "{value}"')
        g.log(f"restored cvars: {saved}")
    except Exception as error:
        g.log(f"error: {error!r}")
    finally:
        g.quit()

    summary = {}
    for result in results:
        summary.setdefault(result["variant"], []).append(result)
    table = []
    for name, _ in variants:
        items = summary.get(name, [])
        if not items:
            table.append({"variant": name, "rounds": 0})
            continue
        table.append({
            "variant": name,
            "rounds": len(items),
            "avg_fps": round(statistics.mean(r["avg_fps"] for r in items), 1),
            "fps_rounds": [r["avg_fps"] for r in items],
            "p99_ms": round(statistics.mean(r["p99_ms"] for r in items), 2),
            "low1_fps": round(statistics.mean(r["low1_fps"] for r in items), 1),
        })
    json.dump({"map": map_command, "extra_args": list(extra_args), "results": results, "table": table},
              open(os.path.join(out_dir, "summary.json"), "w"), indent=1)
    for row in table:
        g.log("TABLE " + json.dumps(row))
    return table
