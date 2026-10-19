import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import run_suite
from run_all import state, ROOT
best = state(depth_fps=120, snd_async=1)
table = run_suite(os.path.join(ROOT, "E_bounds"), "ss_map mp_coop_lobby_3",
    [("best", best + ["p2boost_x_skip_view2 0", "p2boost_x_vgui_screens 1"]),
     ("best, no in-world VGUI screens", best + ["p2boost_x_skip_view2 0", "p2boost_x_vgui_screens 0"]),
     ("best, view 2 not rendered", best + ["p2boost_x_skip_view2 1", "p2boost_x_vgui_screens 1"]),
     ("best, view 2 not rendered, no VGUI screens", best + ["p2boost_x_skip_view2 1", "p2boost_x_vgui_screens 0"])],
    rounds=2, seconds=10)
for row in table: print(json.dumps(row))
