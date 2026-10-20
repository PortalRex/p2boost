"""Measured suites behind benchmarks/results.md.

run_all.py [suite ...]   (default: main)

  main    per-fix contributions and config candidates in the split-screen
          lobby, launch options, single-view co-op host
  stock   stock mat_queue_mode 0 vs -1 vs best, split-screen and single view
  sp      one view: map sp_a2_laser_relays
  partner partner_view_fps 0 / 240 / 144 / 120
  l1000   lobby toward 1000 FPS: depth cap phase, host threading, HUD cost
"""
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


LOBBY = "ss_map mp_coop_lobby_3"
STOCK0 = state(enable=0, multicore=0)
STOCK0[STOCK0.index("mat_queue_mode -1")] = "mat_queue_mode 0"
BEST = state(depth_fps=120, snd_async=1)

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
    ("all + depth 120 + snd_mix_async 1", BEST),
    ("all + depth 120 + snd_async + cl_threaded_bone_setup 1", state(depth_fps=120, snd_async=1, bones=1)),
    ("all + depth 120 + snd_async + net_graph 0", state(depth_fps=120, snd_async=1, net_graph=0)),
]


def suite_main(tables):
    tables["A split-screen lobby"] = run_suite(os.path.join(ROOT, "A_splitscreen"), LOBBY, SUITE_A,
                                              rounds=2, seconds=10, restore=("net_graph",))
    best = [("best (all + depth 120 + snd_mix_async 1)", BEST)]
    for label, args in (("B default launch", []), ("B -high", ["-high"]), ("B -threads 8", ["-threads", "8"]),
                        ("B +threadpool_affinity 0", ["+threadpool_affinity", "0"])):
        tables[label] = run_suite(os.path.join(ROOT, label.replace(" ", "_")), LOBBY, best,
                                  rounds=2, seconds=10, extra_args=args)
    tables["C single-view co-op host (map)"] = run_suite(
        os.path.join(ROOT, "C_singleview"), "map mp_coop_lobby_3",
        [("stock (no p2boost, mat_queue_mode auto)", state(enable=0, multicore=0)),
         ("all fixes (current defaults)", state()),
         ("best (all + depth 120 + snd_mix_async 1)", BEST)],
        rounds=2, seconds=10, require_coop=False)


def suite_stock(tables):
    tables["A2 split-screen stock check"] = run_suite(os.path.join(ROOT, "A2_stock"), LOBBY,
        [("stock, mat_queue_mode 0 (single-threaded)", STOCK0),
         ("stock, mat_queue_mode -1 (auto)", state(enable=0, multicore=0)),
         ("best (all + depth 120 + snd_mix_async 1)", BEST)], rounds=2, seconds=10)
    tables["C single-view co-op host (map, 1 player)"] = run_suite(os.path.join(ROOT, "C_singleview"), "map mp_coop_lobby_3",
        [("stock, mat_queue_mode 0 (single-threaded)", STOCK0),
         ("all fixes (current defaults)", state()),
         ("best (all + depth 120 + snd_mix_async 1)", BEST)],
        rounds=2, seconds=10, require_coop=False, require_sim=False, screenshot=True)


def suite_sp(tables):
    tables["D single player"] = run_suite(os.path.join(ROOT, "D_singleplayer"), "map sp_a2_laser_relays",
        [("stock, mat_queue_mode 0 (single-threaded)", STOCK0),
         ("stock, mat_queue_mode -1 (auto)", state(enable=0, multicore=0)),
         ("all fixes (current defaults)", state()),
         ("best (all + depth 120 + snd_mix_async 1)", BEST)],
        rounds=2, seconds=10, require_coop=False, screenshot=True, settle=40)


def suite_partner(tables):
    tables["F partner view cap"] = run_suite(os.path.join(ROOT, "F_split_view2"), LOBBY,
        [(f"best, p2boost_fix partner_view_fps {n}", BEST + [f"p2boost_fix partner_view_fps {n}"]) for n in (0, 240, 144, 120)],
        rounds=2, seconds=10, screenshot=False)


def suite_l1000(tables):
    hud = ["sar_hud_position", "sar_hud_angles", "sar_hud_velocity", "sar_hud_demo"]
    # every variant sets everything it may differ in, so order does not matter
    base = BEST + ["p2boost_fix partner_view_fps 120", "host_thread_mode 0", "r_shadowrendertotexture 1",
                   "net_graph 1"] + [f"{c} 1" for c in hud]

    def variant(name, *changes):
        return (name, base + list(changes))

    tables["L lobby toward 1000 FPS"] = run_suite(os.path.join(ROOT, "L_1000"), LOBBY, [
        variant("best"),
        variant("best, shadow_depth_fps 100 (desync from view 2)", "p2boost_fix shadow_depth_fps 100"),
        variant("best, shadow_depth_fps 60", "p2boost_fix shadow_depth_fps 60"),
        variant("best, host_thread_mode 1", "host_thread_mode 1"),
        variant("best, host_thread_mode 2", "host_thread_mode 2"),
        variant("best, SAR HUD off, net_graph 0", "net_graph 0", *[f"{c} 0" for c in hud]),
        variant("best, r_shadowrendertotexture 0 (measurement only)", "r_shadowrendertotexture 0"),
        variant("best, partner_view_fps 60", "p2boost_fix partner_view_fps 60"),
    ], rounds=2, seconds=10, restore=hud + ["host_thread_mode", "net_graph"])


SUITES = {"main": suite_main, "stock": suite_stock, "sp": suite_sp, "partner": suite_partner, "l1000": suite_l1000}

if __name__ == "__main__":
    names = sys.argv[1:] or ["main"]
    unknown = [name for name in names if name not in SUITES]
    if unknown:
        sys.exit(f"unknown suite {', '.join(unknown)}; choose from {', '.join(SUITES)}")
    tables = {}
    for name in names:
        SUITES[name](tables)
    json.dump(tables, open(os.path.join(ROOT, f"tables_{'_'.join(names)}.json"), "w"), indent=1)
    for label, table in tables.items():
        print("==", label)
        for row in table:
            print(json.dumps(row))
