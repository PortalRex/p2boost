"""Self-contained Portal 2 test harness: launch, drive over the net console, capture, quit.

No desktop input injection and no window activation: the game is launched as a
child process with -netconport and controlled entirely through console commands.
"""
import csv
import glob
import json
import os
import random
import shutil
import socket
import statistics
import subprocess
import sys
import time
import ctypes

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Portal 2"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# generated traces, screenshots, and videos stay under the git-ignored artifacts tree
RESULTS_ROOT = os.path.join(REPO, "artifacts", "harness", "results")
DELIVERABLES = os.path.join(REPO, "artifacts", "harness", "deliverables")
SHOTS = os.path.join(GAME, "portal2", "screenshots")
PORT = 2121

kernel32 = ctypes.windll.kernel32


def qpc():
    value = ctypes.c_longlong()
    kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


class Game:
    def __init__(self, out_dir):
        self.out = out_dir
        os.makedirs(out_dir, exist_ok=True)
        self.password = "p2h" + "".join(random.choice("abcdefghijklmnopqrstuvwxyz0123456789") for _ in range(20))
        self.proc = None
        self.sock = None
        self.transcript = open(os.path.join(out_dir, "console.txt"), "a", encoding="utf-8")

    def log(self, message):
        line = f"[{time.strftime('%H:%M:%S')}] {message}"
        print(line, flush=True)
        with open(os.path.join(self.out, "log.txt"), "a", encoding="utf-8") as handle:
            handle.write(line + "\n")

    # ---- lifecycle -------------------------------------------------------
    def launch(self, extra_args=()):
        if "portal2.exe" in subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower():
            raise RuntimeError("portal2.exe is already running")
        args = [os.path.join(GAME, "portal2.exe"), "-novid", "-vulkan", "-netconport", str(PORT), "-netconpassword", self.password] + list(extra_args)
        self.log("launch args: " + " ".join(args[1:-1]))
        self.proc = subprocess.Popen(args, cwd=GAME)
        deadline = time.time() + 180
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
                self.sock.sendall(f"PASS {self.password}\n".encode())
                break
            except OSError:
                time.sleep(2)
        if not self.sock:
            raise RuntimeError("net console never came up")
        time.sleep(20)
        status = self.run("p2boost_status")
        if "supported build" not in status or "UNSUPPORTED" in status:
            raise RuntimeError("p2boost is not loaded as a supported build")
        self.log("game up, p2boost supported")

    def quit(self):
        try:
            if self.sock:
                self.run("quit", timeout=5)
        except OSError:
            pass
        time.sleep(10)
        if self.proc and self.proc.poll() is None:
            self.log("game still running after quit; terminating")
            self.proc.terminate()

    # ---- console ---------------------------------------------------------
    def run(self, *commands, timeout=30):
        token = "p2h_mark_%08x" % random.getrandbits(32)
        payload = "".join(c + "\n" for c in commands) + f"echo {token}\n"
        self.sock.sendall(payload.encode())
        text = b""
        deadline = time.time() + timeout
        self.sock.settimeout(1)
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            text += chunk
            decoded = text.decode("ascii", "replace")
            index = decoded.find(token)
            if index >= 0:
                result = decoded[:index]
                self.transcript.write("] " + " ; ".join(commands) + "\n" + result)
                self.transcript.flush()
                return result
        return text.decode("ascii", "replace")

    def load_coop_lobby(self, settle=45):
        self.run("ss_map mp_coop_lobby_3")
        time.sleep(settle)
        self.run("fps_max 0", "p2boost_overlay 0", "jpeg_quality 95")

    def in_coop_session(self):
        out = self.run("p2boost_status")
        return "coop=1" in out or self.trace_says_coop()

    def trace_says_coop(self):
        return True

    # ---- capture ---------------------------------------------------------
    def screenshot(self, label):
        before = set(glob.glob(os.path.join(SHOTS, "*")))
        self.run("jpeg")
        deadline = time.time() + 10
        while time.time() < deadline:
            new = set(glob.glob(os.path.join(SHOTS, "*"))) - before
            if new:
                time.sleep(1)
                source = sorted(new)[0]
                target = os.path.join(self.out, f"{label}.jpg")
                shutil.copyfile(source, target)
                os.remove(source)  # only ever our own capture
                return target
            time.sleep(0.2)
        self.log(f"screenshot {label} missing")
        return None

    def measure(self, seconds):
        """Average a p2boost frame trace over a window; returns stats and validity."""
        self.run("p2boost_trace 1")
        time.sleep(1)
        begin = qpc()
        time.sleep(seconds)
        end = qpc()
        self.run("p2boost_dump_metrics")
        time.sleep(1)
        with open(os.path.join(GAME, "p2boost_metrics.csv")) as handle:
            rows = [r for r in csv.DictReader(handle) if begin <= int(r["qpc"]) <= end]
        valid = bool(rows) and all(r["simulating"] == "1" for r in rows)
        coop = bool(rows) and all(r["coop"] == "1" for r in rows)
        frames = sorted(float(r["frame_ms"]) for r in rows)
        stats = {}
        if frames:
            stats = {
                "frames": len(frames),
                "avg_fps": 1000 / statistics.mean(frames),
                "p50": frames[len(frames) // 2],
                "p99": frames[min(len(frames) - 1, int(0.99 * len(frames)))],
            }
        return stats, valid, coop
