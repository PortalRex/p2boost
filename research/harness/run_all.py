"""Full measured suite: per-fix contributions, config candidates, launch options, single view."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from harness import RESULTS_ROOT

ROOT = os.path.join(RESULTS_ROOT)
os.makedirs(ROOT, exist_ok=True)

FIXES = {
    "flashlight_flush": "p2boost_fix flashlight_flush",
    "xinput_throttle": "p2boost_fix xinput_throttle",
    "occlusion_skip": "p2boost_fix occlusion_skip",
    "vgui_shadow_skip": "p2boost_fix vgui_shadow_skip",
    "depth_reuse": "p2boost_fix shadow_depth_reuse",
}


def state(enable=1, multicore=1, depth_fps=0, snd_async=0, bones=0, net_graph=1, off=()):
    commands = [f"p2boost_enable {enable}", f"p2boost_fix multicore {multicore}"]
    if not multicore:
        commands.append("mat_queue_mode -1")
    for key, cvar in FIXES.items():
        commands.append(f"{cvar} {0 if key in off else 1}")
    commands += [f"p2boost_fix shadow_depth_fps {depth_fps}", f"snd_mix_async {snd_async}",
                 f"cl_threaded_bone_setup {bones}", f"net_graph {net_graph}"]
    return commands


SUITE_A = [
    ("stock (no p2boost, mat_queue_mode auto)", state(enable=0, multicore=0)),
    ("mat_queue_mode 2 only", state(enable=0, multicore=1)),
    ("all fixes (current defaults)", state()),
    ("all minus flashlight_flush", state(off=("flashlight_flush",))),
    ("all minus xinput_throttle", state(off=("xinput_throttle",))),
    ("all minus occlusion_skip", state(off=("occlusion_skip",))),
    ("all minus vgui_shadow_skip", state(off=("vgui_shadow_skip",))),
    ("all minus depth_reuse", state(off=("depth_reuse",))),
    ("all + shadow_depth_fps 120", state(depth_fps=120)),
    ("all + depth 120 + snd_mix_async 1", state(depth_fps=120, snd_async=1)),
    ("all + depth 120 + snd_async + cl_threaded_bone_setup 1", state(depth_fps=120, snd_async=1, bones=1)),
    ("all + depth 120 + snd_async + net_graph 0", state(depth_fps=120, snd_async=1, net_graph=0)),
]
BEST = [("best (all + depth 120 + snd_mix_async 1)", state(depth_fps=120, snd_async=1))]

if __name__ == "__main__":
    tables = {}
    tables["A split-screen lobby"] = run_suite(os.path.join(ROOT, "A_splitscreen"), "ss_map mp_coop_lobby_3", SUITE_A,
                                              rounds=2, seconds=10, restore=("net_graph",))
    for label, args in (("B default launch", []), ("B -high", ["-high"]), ("B -threads 8", ["-threads", "8"]),
                        ("B +threadpool_affinity 0", ["+threadpool_affinity", "0"])):
        tables[label] = run_suite(os.path.join(ROOT, label.replace(" ", "_")), "ss_map mp_coop_lobby_3", BEST,
                                  rounds=2, seconds=10, extra_args=args)
    tables["C single-view co-op host (map)"] = run_suite(
        os.path.join(ROOT, "C_singleview"), "map mp_coop_lobby_3",
        [("stock (no p2boost, mat_queue_mode auto)", state(enable=0, multicore=0)),
         ("all fixes (current defaults)", state()),
         ("best (all + depth 120 + snd_mix_async 1)", state(depth_fps=120, snd_async=1))],
        rounds=2, seconds=10, require_coop=False)
    json.dump(tables, open(os.path.join(ROOT, "all_tables.json"), "w"), indent=1)
    for label, table in tables.items():
        print("==", label)
        for row in table:
            print(json.dumps(row))
