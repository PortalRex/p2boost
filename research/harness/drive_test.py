"""Drive a Portal 2 A/B over the net console (no input injection, no window activation)."""
import ctypes
import csv
import json
import os
import random
import re
import shutil
import socket
import statistics
import subprocess
import sys
import time

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Portal 2"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drive_out")
PORT = 2121
PASSWORD = "p2b" + "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(20))
VARIANTS = [x for x in sys.argv[1].split(";")] if len(sys.argv) > 1 else []
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 2
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
os.makedirs(OUT, exist_ok=True)

kernel32 = ctypes.windll.kernel32


def qpc():
    value = ctypes.c_longlong()
    kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


def log(message):
    line = f"[{time.strftime('%H:%M:%S')}] {message}"
    print(line, flush=True)
    with open(os.path.join(OUT, "log.txt"), "a") as handle:
        handle.write(line + "\n")


class Netcon:
    def __init__(self):
        self.sock = None

    def connect(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
                self.sock.sendall(f"PASS {PASSWORD}\n".encode())
                return True
            except OSError:
                time.sleep(2)
        return False

    def run(self, commands, timeout=30):
        token = "p2netcon_mark_%08x" % random.getrandbits(32)
        payload = "".join(c + "\n" for c in commands) + f"echo {token}\n"
        self.sock.sendall(payload.encode())
        text = b""
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                text += chunk
                decoded = text.decode("ascii", "replace")
                index = decoded.find(token)
                if index >= 0:
                    return decoded[:index]
            except socket.timeout:
                continue
        return text.decode("ascii", "replace")


def stats(rows):
    frames = [float(r["frame_ms"]) for r in rows]
    if not frames:
        return {}
    ordered = sorted(frames)
    pick = lambda q: ordered[min(len(ordered) - 1, int(q * len(ordered)))]
    worst = ordered[-max(1, len(ordered) // 100):]
    return {
        "frames": len(frames),
        "avg_fps": 1000 / statistics.mean(frames),
        "p50": pick(0.5),
        "p99": pick(0.99),
        "max": ordered[-1],
        "low1_fps": 1000 / statistics.mean(worst),
    }


def main():
    if any(p.lower().startswith("portal2") for p in subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower().split()):
        log("portal2.exe is already running; aborting")
        return 1
    args = [os.path.join(GAME, "portal2.exe"), "-novid", "-vulkan", "-netconport", str(PORT), "-netconpassword", PASSWORD]
    log("launching Portal 2")
    game = subprocess.Popen(args, cwd=GAME)
    net = Netcon()
    try:
        if not net.connect(180):
            log("net console never came up")
            return 1
        log("net console connected; waiting for the main menu")
        time.sleep(20)
        # autoexec.cfg already runs plugin_load p2boost; a second load would
        # fail its signature checks against the first copy's patches.
        out = net.run(["p2boost_status"], 60)
        open(os.path.join(OUT, "load.txt"), "w").write(out)
        if "supported build" not in out or "UNSUPPORTED" in out:
            log("p2boost is not loaded as a supported build; aborting: " + out[-400:])
            return 1
        log("p2boost status ok: " + " | ".join(l.strip() for l in out.splitlines() if "depth reuse" in l or "fixes" in l))
        net.run(["ss_map mp_coop_lobby_3"], 10)
        log("loading ss_map mp_coop_lobby_3")
        time.sleep(45)
        net.run(["fps_max 0", "p2boost_overlay 0", "p2boost_trace 1"], 20)
        log("warming up")
        time.sleep(15)

        order = []
        for r in range(ROUNDS):
            order += VARIANTS if r % 2 == 0 else list(reversed(VARIANTS))
        results = []
        for index, variant in enumerate(order):
            commands = [c.strip() for c in variant.split(",") if c.strip()]
            for attempt in range(3):
                net.run(commands, 20)
                time.sleep(3)
                begin = qpc()
                time.sleep(SECONDS)
                end = qpc()
                status = net.run(["p2boost_trace 0", "p2boost_status"], 30)
                time.sleep(1)
                target = os.path.join(OUT, f"seg{index:02d}.csv")
                shutil.copyfile(os.path.join(GAME, "p2boost_metrics.csv"), target)
                with open(target) as handle:
                    rows = [r for r in csv.DictReader(handle) if begin <= int(r["qpc"]) <= end]
                # every measured frame must be the running split-screen session.
                if rows and all(r["simulating"] == "1" and r["coop"] == "1" for r in rows):
                    break
                log(f"segment {index} left the co-op session; reloading the lobby (attempt {attempt + 1})")
                net.run(["ss_map mp_coop_lobby_3"], 10)
                time.sleep(45)
                net.run(["fps_max 0", "p2boost_overlay 0", "p2boost_trace 1"], 20)
                time.sleep(15)
            else:
                log(f"segment {index} never stayed in the session; skipping")
                continue
            result = stats(rows)
            depth = re.search(r"depth reuse\s+(.*)", status)
            result.update(variant=variant, depth=depth.group(1).strip() if depth else "?")
            results.append(result)
            log(json.dumps(result))
        summary = {}
        for result in results:
            summary.setdefault(result["variant"], []).append(result["avg_fps"])
        for variant, values in summary.items():
            log(f"SUMMARY {variant}: mean {statistics.mean(values):.1f} FPS over {len(values)} rounds {['%.1f' % v for v in values]}")
        json.dump(results, open(os.path.join(OUT, "results.json"), "w"), indent=1)
        net.run(["p2boost_trace 0"], 10)
    finally:
        try:
            if net.sock:
                net.run(["quit"], 5)
        except OSError:
            pass
        time.sleep(10)
        if game.poll() is None:
            log("game still running after quit; terminating portal2.exe")
            game.terminate()
        log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
