"""Main-thread sample profile of the split-screen lobby in the best config.

profile_lobby.py [seconds] [label]

Loads ss_map mp_coop_lobby_3, applies every fix plus the two opt-in caps,
measures FPS, then samples the main thread and the busiest other threads
with sampler.py. Writes artifacts/harness/results/K_profile/<label>.txt.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state
from sampler import Sampler, report

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 12
label = sys.argv[2] if len(sys.argv) > 2 else "best"
extra = sys.argv[3:]
OUT = os.path.join(RESULTS_ROOT, "K_profile")
BEST = state(depth_fps=120, snd_async=1) + ["p2boost_fix split_view2_fps 120"] + extra
g = Game(OUT)

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
    others = [tid for tid in sampler.busiest(6) if tid != main][:3]
    counts, inclusive, totals = sampler.run([main] + others, seconds, 1500)
    text = [f"== {label}: {stats}", f"main thread {main}", report(counts[main], inclusive[main], totals[main])]
    for tid in others:
        text += [f"\n== thread {tid}", report(counts[tid], inclusive[tid], totals[tid], top=15)]
    with open(os.path.join(OUT, f"{label}.txt"), "w") as handle:
        handle.write("\n".join(text))
    g.log("\n".join(text[:3]))
except Exception as error:
    g.log(f"error: {error!r}")
finally:
    g.quit()
    g.log("done")
