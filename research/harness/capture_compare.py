"""Capture screenshot sequences (live FPS counter on) for vanilla vs optimized, two scenes."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state
ROOT = os.path.join(RESULTS_ROOT, "H_compare2")
VANILLA = state(enable=0, multicore=0); VANILLA[VANILLA.index("mat_queue_mode -1")] = "mat_queue_mode 0"
OPT = state(depth_fps=120, snd_async=1)
SCENES = [("split", "ss_map mp_coop_lobby_3", ["p2boost_split_view2_fps 120"]),
          ("single", "map sp_a2_laser_relays", [])]
for scene, map_command, extra in SCENES:
    g = Game(os.path.join(ROOT, scene))
    try:
        g.launch()
        g.run("sv_lan 1", map_command); time.sleep(45)
        g.run("fps_max 999", "jpeg_quality 92"); time.sleep(10)
        for label, commands in (("vanilla", VANILLA + ["p2boost_split_view2_fps 0"]), ("optimized", OPT + extra)):
            g.run(*commands, "p2boost_overlay 2"); time.sleep(4)
            for i in range(14):
                g.screenshot(f"{label}_{i:02d}"); time.sleep(0.45)
            g.log(f"{scene} {label} captured")
    except Exception as e:
        g.log(f"error: {e!r}")
    finally:
        g.quit()
