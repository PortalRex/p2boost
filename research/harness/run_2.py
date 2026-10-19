import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT
stock0 = state(enable=0, multicore=0); stock0[stock0.index("mat_queue_mode -1")] = "mat_queue_mode 0"
tables = {}
tables["A2 split-screen stock check"] = run_suite(os.path.join(ROOT, "A2_stock"), "ss_map mp_coop_lobby_3",
    [("stock, mat_queue_mode 0 (single-threaded)", stock0),
     ("stock, mat_queue_mode -1 (auto)", state(enable=0, multicore=0)),
     ("best (all + depth 120 + snd_mix_async 1)", state(depth_fps=120, snd_async=1))], rounds=2, seconds=10)
tables["C single-view co-op host (map, 1 player)"] = run_suite(os.path.join(ROOT, "C_singleview"), "map mp_coop_lobby_3",
    [("stock, mat_queue_mode 0 (single-threaded)", stock0),
     ("all fixes (current defaults)", state()),
     ("best (all + depth 120 + snd_mix_async 1)", state(depth_fps=120, snd_async=1))],
    rounds=2, seconds=10, require_coop=False, require_sim=False, screenshot=True)
json.dump(tables, open(os.path.join(ROOT, "tables_2.json"), "w"), indent=1)
for label, table in tables.items():
    print("==", label)
    for row in table: print(json.dumps(row))
