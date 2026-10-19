"""User-mode sampling profiler for a running portal2.exe: no ETW, no UAC.

Suspends a thread, reads its WOW64 context (EIP, EBP, ESP), resumes it at
once, and only then walks the EBP return chain from a copy of the stack. Each
sample also records QueryPerformanceCounter, so samples can be joined to the
per-frame QPC stamps of a p2boost_trace CSV and split by frame duration.

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
STACK_BYTES = 1024


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


def qpc():
    value = ctypes.c_longlong()
    kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


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
        self.bases = [r[0] for r in self.ranges]
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

    def module(self, address):
        index = bisect.bisect_right(self.bases, address) - 1
        if index >= 0:
            base, end, name = self.ranges[index]
            if address < end:
                return base, name
        return None, None

    def locate(self, address):
        base, name = self.module(address)
        if name is None:
            return "?", f"0x{address:x}"
        rva = address - base
        table = self.functions.get(name)
        if table:
            starts, names = table
            index = bisect.bisect_right(starts, rva) - 1
            if index >= 0 and rva < names[index][0]:
                return name, names[index][1]
        return name, f"+0x{rva:x}"

    def in_code(self, address):
        return self.module(address)[1] is not None


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
        self.stack = (ctypes.c_ubyte * STACK_BYTES)()
        self.context = WOW64_CONTEXT()

    def main_thread(self):
        return min(self.threads, key=lambda tid: thread_times(self.threads[tid])[0])

    def busiest(self, count):
        ranked = sorted(self.threads, key=lambda tid: -thread_times(self.threads[tid])[1])
        return ranked[:count]

    def sample(self, tid, depth=16):
        """(qpc, [eip, caller, caller's caller...]); the thread is suspended only
        for the context read."""
        handle = self.threads[tid]
        context = self.context
        context.ContextFlags = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER
        if kernel32.SuspendThread(handle) == 0xFFFFFFFF:
            return None
        stamp = qpc()
        ok = kernel32.Wow64GetThreadContext(handle, ctypes.byref(context))
        kernel32.ResumeThread(handle)
        if not ok:
            return None
        frames = [context.Eip]
        ebp, esp = context.Ebp, context.Esp
        # copy the top of the stack after resuming: frames above the current
        # one rarely move within the microseconds this takes
        read = ctypes.c_size_t()
        base = esp & ~0xF
        if ebp >= base and ebp - base < STACK_BYTES - 8 and kernel32.ReadProcessMemory(
                self.process, ctypes.c_void_p(base), self.stack, STACK_BYTES, ctypes.byref(read)):
            words = (wt.DWORD * (read.value // 4)).from_buffer(self.stack)
            for _ in range(depth):
                index = (ebp - base) // 4
                if index + 1 >= len(words):
                    break
                nxt, ret = words[index], words[index + 1]
                if not self.modules.in_code(ret) or nxt <= ebp or nxt - base >= read.value:
                    break
                frames.append(ret)
                ebp = nxt
        return stamp, frames

    def run(self, tids, seconds, hz):
        """returns {tid: [(qpc, frames), ...]}"""
        interval = 1.0 / hz
        samples = {tid: [] for tid in tids}
        deadline = time.perf_counter() + seconds
        next_time = time.perf_counter()
        while time.perf_counter() < deadline:
            for tid in tids:
                result = self.sample(tid)
                if result:
                    samples[tid].append(result)
            next_time += interval
            delay = next_time - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
        return samples


def summarize(modules, samples):
    """self and inclusive counters over a list of (qpc, frames)."""
    counts, inclusive = collections.Counter(), collections.Counter()
    for _, frames in samples:
        counts[modules.locate(frames[0])] += 1
        for symbol in set(modules.locate(f) for f in frames):
            inclusive[symbol] += 1
    return counts, inclusive, len(samples)


def report(counts, inclusive, total, top=40):
    if not total:
        return "no samples"
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
    samples = sampler.run([main], seconds, hz)
    print(report(*summarize(sampler.modules, samples[main])))
