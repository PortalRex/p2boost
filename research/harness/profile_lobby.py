"""Main-thread sample profile of the split-screen lobby, split by frame length.

profile_lobby.py [seconds] [label] [extra console commands...]

Loads ss_map mp_coop_lobby_3, applies every fix plus the two opt-in caps
(plus any extra commands), measures FPS, then samples the main thread while a
p2boost_trace runs. Each sample is joined to the frame it fell in through the
trace's QPC stamps, so the report shows what the fast frames and the slow
frames spend their time on. Writes artifacts/harness/results/K_profile/<label>.txt.
"""
import bisect
import csv
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, GAME, Game
from run_all import state
from sampler import Sampler, report, summarize

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 15
label = sys.argv[2] if len(sys.argv) > 2 else "best"
extra = sys.argv[3:]
OUT = os.path.join(RESULTS_ROOT, "K_profile")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix partner_view_fps 120"] + extra
g = Game(OUT)


def frames_from_trace(begin, end):
    with open(os.path.join(GAME, "p2boost_metrics.csv")) as handle:
        rows = [r for r in csv.DictReader(handle) if begin <= int(r["qpc"]) <= end]
    return [(int(r["qpc"]), float(r["frame_ms"])) for r in rows]


try:
    g.launch()
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(50)
    g.run("fps_max 0", "p2boost_overlay 0", *BEST)
    time.sleep(5)
    stats, valid, coop = g.measure(8)
    g.log(f"{label}: {stats} valid={valid} coop={coop}")

    sampler = Sampler(g.proc.pid)
    main = sampler.main_thread()
    g.run("p2boost_trace 1")
    time.sleep(1)
    samples = sampler.run([main], seconds, 1000)[main]
    time.sleep(1)
    g.run("p2boost_trace 0")
    time.sleep(1)

    # join samples to frames: a trace row's qpc is the frame's end stamp
    frames = frames_from_trace(samples[0][0] - 10_000_000, samples[-1][0] + 10_000_000)
    ends = [q for q, _ in frames]
    durations = [d for _, d in frames]
    fast, slow, span = [], [], []
    for stamp, chain in samples:
        index = bisect.bisect_left(ends, stamp)
        if index >= len(frames):
            continue
        d = durations[index]
        (fast if d < 1.3 else slow).append((stamp, chain))
        span.append(d)
    sampled_ms = statistics.mean(durations) if durations else 0
    text = [f"== {label}: {stats}",
            f"trace during sampling: {len(durations)} frames, mean {sampled_ms:.3f} ms, median "
            f"{statistics.median(durations) if durations else 0:.3f} ms (the sampler itself slows the game a little)",
            f"samples: {len(samples)} total, {len(fast)} in frames < 1.3 ms, {len(slow)} in frames >= 1.3 ms",
            "\n== ALL FRAMES", report(*summarize(sampler.modules, samples)),
            "\n== FAST FRAMES (< 1.3 ms)", report(*summarize(sampler.modules, fast), top=30),
            "\n== SLOW FRAMES (>= 1.3 ms)", report(*summarize(sampler.modules, slow), top=50)]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, f"{label}.txt"), "w") as handle:
        handle.write("\n".join(text))
    g.log("\n".join(text[:3]))
except Exception as error:
    import traceback
    g.log(f"error: {error!r}\n{traceback.format_exc()}")
finally:
    g.quit()
    g.log("done")
