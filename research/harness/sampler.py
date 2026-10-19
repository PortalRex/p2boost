"""User-mode sampling profiler for a running portal2.exe: no ETW, no UAC.

Suspends a thread, reads its WOW64 context (EIP, EBP, ESP), resumes it, and
maps EIP (and the EBP return chain, where frames exist) onto the function
tables exported from the Ghidra project (artifacts/etw/functions/<mod>.csv).

sampler.py [seconds] [hz]      profile the main thread of the running game
"""
import bisect
import collections
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FUNCTIONS = os.path.join(REPO, "artifacts", "etw", "functions")

TH32CS_SNAPTHREAD = 0x4
TH32CS_SNAPMODULE = 0x8
TH32CS_SNAPMODULE32 = 0x10
THREAD_RIGHTS = 0x0002 | 0x0008 | 0x0040  # suspend/resume, get context, query information
PROCESS_RIGHTS = 0x0010 | 0x0400  # vm read, query information
WOW64_CONTEXT_CONTROL = 0x00010001
WOW64_CONTEXT_INTEGER = 0x00010002


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wt.DWORD), ("hModule", ctypes.c_void_p), ("szModule", wt.WCHAR * 256),
                ("szExePath", wt.WCHAR * 260)]


class WOW64_FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [("ControlWord", wt.DWORD), ("StatusWord", wt.DWORD), ("TagWord", wt.DWORD),
                ("ErrorOffset", wt.DWORD), ("ErrorSelector", wt.DWORD), ("DataOffset", wt.DWORD),
                ("DataSelector", wt.DWORD), ("RegisterArea", ctypes.c_ubyte * 80), ("Cr0NpxState", wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", wt.DWORD), ("Dr0", wt.DWORD), ("Dr1", wt.DWORD), ("Dr2", wt.DWORD),
                ("Dr3", wt.DWORD), ("Dr6", wt.DWORD), ("Dr7", wt.DWORD), ("FloatSave", WOW64_FLOATING_SAVE_AREA),
                ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD), ("SegDs", wt.DWORD),
                ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD), ("Edx", wt.DWORD), ("Ecx", wt.DWORD),
                ("Eax", wt.DWORD), ("Ebp", wt.DWORD), ("Eip", wt.DWORD), ("SegCs", wt.DWORD), ("EFlags", wt.DWORD),
                ("Esp", wt.DWORD), ("SegSs", wt.DWORD), ("ExtendedRegisters", ctypes.c_ubyte * 512)]


class FILETIME(ctypes.Structure):
    _fields_ = [("low", wt.DWORD), ("high", wt.DWORD)]

    def value(self):
        return (self.high << 32) | self.low


def find_process(name="portal2.exe"):
    import subprocess
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == name:
            return int(parts[1])
    return None


class Modules:
    def __init__(self, pid):
        self.ranges = []  # (base, end, name)
        snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
        entry = MODULEENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        if kernel32.Module32FirstW(snap, ctypes.byref(entry)):
            while True:
                self.ranges.append((entry.modBaseAddr, entry.modBaseAddr + entry.modBaseSize, entry.szModule.lower()))
                if not kernel32.Module32NextW(snap, ctypes.byref(entry)):
                    break
        kernel32.CloseHandle(snap)
        self.ranges.sort()
        self.functions = {}
        for base, end, name in self.ranges:
            path = os.path.join(FUNCTIONS, name + ".csv")
            if os.path.exists(path):
                starts, names = [], []
                for line in open(path):
                    parts = line.strip().split(",")
                    if len(parts) >= 4:
                        starts.append(int(parts[0], 16))
                        names.append((int(parts[1], 16), parts[3]))
                self.functions[name] = (starts, names)

    def locate(self, address):
        for base, end, name in self.ranges:
            if base <= address < end:
                rva = address - base
                table = self.functions.get(name)
                if table:
                    starts, names = table
                    index = bisect.bisect_right(starts, rva) - 1
                    if index >= 0 and rva < names[index][0]:
                        return name, names[index][1]
                return name, f"+0x{rva:x}"
        return "?", f"0x{address:x}"

    def in_code(self, address):
        return any(base <= address < end for base, end, _ in self.ranges)


def threads_of(pid):
    result = []
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    if kernel32.Thread32First(snap, ctypes.byref(entry)):
        while True:
            if entry.th32OwnerProcessID == pid:
                result.append(entry.th32ThreadID)
            if not kernel32.Thread32Next(snap, ctypes.byref(entry)):
                break
    kernel32.CloseHandle(snap)
    return result


def thread_times(handle):
    creation, exit_, kernel, user = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    kernel32.GetThreadTimes(handle, ctypes.byref(creation), ctypes.byref(exit_), ctypes.byref(kernel), ctypes.byref(user))
    return creation.value(), kernel.value() + user.value()


class Sampler:
    def __init__(self, pid):
        self.pid = pid
        self.process = kernel32.OpenProcess(PROCESS_RIGHTS, False, pid)
        self.modules = Modules(pid)
        self.threads = {}
        for tid in threads_of(pid):
            handle = kernel32.OpenThread(THREAD_RIGHTS, False, tid)
            if handle:
                self.threads[tid] = handle

    def main_thread(self):
        return min(self.threads, key=lambda tid: thread_times(self.threads[tid])[0])

    def busiest(self, count):
        ranked = sorted(self.threads, key=lambda tid: -thread_times(self.threads[tid])[1])
        return ranked[:count]

    def read_dword(self, address):
        value = wt.DWORD()
        read = ctypes.c_size_t()
        if kernel32.ReadProcessMemory(self.process, ctypes.c_void_p(address), ctypes.byref(value), 4, ctypes.byref(read)) and read.value == 4:
            return value.value
        return None

    def sample(self, tid, depth=12):
        handle = self.threads[tid]
        context = WOW64_CONTEXT()
        context.ContextFlags = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER
        if kernel32.SuspendThread(handle) == 0xFFFFFFFF:
            return None
        ok = kernel32.Wow64GetThreadContext(handle, ctypes.byref(context))
        kernel32.ResumeThread(handle)
        if not ok:
            return None
        frames = [context.Eip]
        ebp = context.Ebp
        for _ in range(depth):
            if not ebp or ebp & 3:
                break
            ret = self.read_dword(ebp + 4)
            nxt = self.read_dword(ebp)
            if ret is None or not self.modules.in_code(ret) or nxt is None or nxt <= ebp:
                break
            frames.append(ret)
            ebp = nxt
        return frames

    def run(self, tids, seconds, hz):
        interval = 1.0 / hz
        counts = {tid: collections.Counter() for tid in tids}
        inclusive = {tid: collections.Counter() for tid in tids}
        totals = {tid: 0 for tid in tids}
        deadline = time.perf_counter() + seconds
        next_time = time.perf_counter()
        while time.perf_counter() < deadline:
            for tid in tids:
                frames = self.sample(tid)
                if not frames:
                    continue
                totals[tid] += 1
                counts[tid][self.modules.locate(frames[0])] += 1
                for symbol in set(self.modules.locate(f) for f in frames):
                    inclusive[tid][symbol] += 1
            next_time += interval
            delay = next_time - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
        return counts, inclusive, totals


def report(counts, inclusive, total, top=40):
    lines = [f"samples {total}"]
    by_module = collections.Counter()
    for (module, _), n in counts.items():
        by_module[module] += n
    lines.append("by module: " + ", ".join(f"{m} {100 * n / total:.1f}%" for m, n in by_module.most_common(12)))
    lines.append("self:")
    for (module, symbol), n in counts.most_common(top):
        lines.append(f"  {100 * n / total:5.1f}%  {module}!{symbol}")
    lines.append("inclusive (EBP chain, partial):")
    for (module, symbol), n in inclusive.most_common(top):
        lines.append(f"  {100 * n / total:5.1f}%  {module}!{symbol}")
    return "\n".join(lines)


if __name__ == "__main__":
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10
    hz = float(sys.argv[2]) if len(sys.argv) > 2 else 1000
    pid = find_process()
    if not pid:
        sys.exit("portal2.exe not running")
    sampler = Sampler(pid)
    main = sampler.main_thread()
    counts, inclusive, totals = sampler.run([main], seconds, hz)
    print(report(counts[main], inclusive[main], totals[main]))
