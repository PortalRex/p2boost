import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT
stock0 = state(enable=0, multicore=0); stock0[stock0.index("mat_queue_mode -1")] = "mat_queue_mode 0"
table = run_suite(os.path.join(ROOT, "D_singleplayer"), "map sp_a2_laser_relays",
    [("stock, mat_queue_mode 0 (single-threaded)", stock0),
     ("stock, mat_queue_mode -1 (auto)", state(enable=0, multicore=0)),
     ("all fixes (current defaults)", state()),
     ("best (all + depth 120 + snd_mix_async 1)", state(depth_fps=120, snd_async=1))],
    rounds=2, seconds=10, require_coop=False, screenshot=True, settle=40)
json.dump(table, open(os.path.join(ROOT, "table_3.json"), "w"), indent=1)
for row in table: print(json.dumps(row))
