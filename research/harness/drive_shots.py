"""Capture in-game screenshots for a visual A/B over the net console (no input injection)."""
import glob
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import drive_test as D

SHOTS = os.path.join(D.GAME, "portal2", "screenshots")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shots_out")
os.makedirs(OUT, exist_ok=True)


def shot(net, label):
    before = set(glob.glob(os.path.join(SHOTS, "*")))
    net.run(["jpeg"], 10)
    deadline = time.time() + 10
    while time.time() < deadline:
        new = set(glob.glob(os.path.join(SHOTS, "*"))) - before
        if new:
            time.sleep(1)
            source = sorted(new)[0]
            target = os.path.join(OUT, f"{label}.jpg")
            shutil.copyfile(source, target)
            os.remove(source)  # our own capture; keep the user's screenshot folder clean
            D.log(f"shot {label}")
            return target
        time.sleep(0.2)
    D.log(f"shot {label} missing")
    return None


def main():
    if "portal2.exe" in subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower():
        D.log("portal2.exe already running; aborting")
        return 1
    args = [os.path.join(D.GAME, "portal2.exe"), "-novid", "-vulkan", "-netconport", str(D.PORT), "-netconpassword", D.PASSWORD]
    game = subprocess.Popen(args, cwd=D.GAME)
    net = D.Netcon()
    try:
        if not net.connect(180):
            return 1
        time.sleep(20)
        status = net.run(["p2boost_status"], 30)
        if "supported build" not in status or "UNSUPPORTED" in status:
            D.log("p2boost not ready; aborting")
            return 1
        net.run(["ss_map mp_coop_lobby_3"], 10)
        time.sleep(45)
        net.run(["fps_max 0", "p2boost_overlay 0", "jpeg_quality 95", "cl_drawhud 1"], 10)
        time.sleep(10)

        # 1. static: same spot, cap off vs on (twice each, interleaved)
        for label, cap in (("static_cap0_a", 0), ("static_cap120_a", 120), ("static_cap0_b", 0), ("static_cap120_b", 120)):
            net.run([f"p2boost_fix shadow_depth_fps {cap}"], 10)
            time.sleep(2)
            shot(net, label)

        # 2. motion: player 2 walks forward and turns; shots mid-motion
        for cap in (0, 120):
            net.run([f"p2boost_fix shadow_depth_fps {cap}"], 10)
            time.sleep(1)
            net.run(["cmd2 +forward", "cmd2 +left"], 10)
            for index in range(3):
                time.sleep(0.6)
                shot(net, f"motion_cap{cap}_{index}")
            net.run(["cmd2 -forward", "cmd2 -left"], 10)
            time.sleep(2)

        # 3. reload the lobby with the cap on; shadows must come back
        net.run(["p2boost_fix shadow_depth_fps 120", "ss_map mp_coop_lobby_3"], 10)
        time.sleep(45)
        net.run(["fps_max 0"], 10)
        time.sleep(5)
        shot(net, "reload_cap120")
        net.run(["p2boost_fix shadow_depth_fps 0"], 10)
        time.sleep(2)
        shot(net, "reload_cap0")
        D.log(net.run(["p2boost_status"], 30)[-1500:])
    finally:
        try:
            net.run(["quit"], 5)
        except OSError:
            pass
        time.sleep(10)
        if game.poll() is None:
            game.terminate()
        D.log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
