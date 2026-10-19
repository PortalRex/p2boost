"""Launch the split-screen lobby with the best config and record an elevated ETW CPU profile."""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import RESULTS_ROOT, Game
from run_all import state

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(RESULTS_ROOT, "profile_best")
ETL = os.path.join(REPO, "artifacts", "etw", "lobby3-best.etl")
g = Game(OUT)
try:
    g.launch()
    g.run("sv_lan 1", "ss_map mp_coop_lobby_3")
    time.sleep(45)
    g.run("fps_max 0", "p2boost_overlay 0", *state(depth_fps=120, snd_async=1))
    time.sleep(15)
    g.log("starting WPR capture")
    script = os.path.join(REPO, "research", "scripts", "capture-cpu-profile.ps1")
    proc = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                           "-Output", ETL, "-Seconds", "10"], capture_output=True, text=True, timeout=900)
    g.log(f"wpr exit {proc.returncode}: {proc.stdout.strip()[-300:]} {proc.stderr.strip()[-300:]}")
    g.log(g.run("p2boost_status")[-600:])
except Exception as error:
    g.log(f"error: {error!r}")
finally:
    g.quit()
    g.log("done")
