import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT
best = state(depth_fps=120, snd_async=1)
table = run_suite(os.path.join(ROOT, "F_split_view2"), "ss_map mp_coop_lobby_3",
    [(f"best, p2boost_fix split_view2_fps {n}", best + [f"p2boost_fix split_view2_fps {n}"]) for n in (0, 240, 144, 120)],
    rounds=2, seconds=10, screenshot=False)
for row in table: print(json.dumps(row))
