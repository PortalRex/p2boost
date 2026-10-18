"""Aggregate an `xperf -a dumper` text dump into per-thread CPU profiles.

Usage: python etw_profile.py <dump.txt> <process-name> [--top N] [--thread TID]
           [--module NAME] [--inclusive] [--callers MODULE+RVA]

Every SampledProfile row for the process is attributed to its thread, then to
module+RVA using the authoritative image rundown in the same trace. Stack rows
that follow a sample (same timestamp and thread) provide inclusive attribution.
RVAs are relative to the module's load base and match the preferred-address
labels used by Ghidra once the image base is added.
"""

import argparse
import bisect
import glob
import collections
import os
import pickle
import re
import sys

IMAGE_RE = re.compile(r"^\s*I-(?:DC)?Start,\s*\d+,\s*(.+?) \(\s*(\d+)\),\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+),.*?,\s*\"?([^,\"]+\.(?:dll|exe))\"?", re.I)
SAMPLE_RE = re.compile(r"^\s*SampledProfile,\s*(\d+),\s*(.+?) \(\s*(\d+)\),\s*(\d+),\s*(0x[0-9a-f]+),")
STACK_RE = re.compile(r"^\s*Stack,\s*(\d+),\s*(\d+),\s*(\d+),\s*(0x[0-9a-f]+),")
THREAD_RE = re.compile(r"^\s*T-(?:DC)?Start,\s*\d+,\s*(.+?) \(\s*(\d+)\),\s*(\d+),.*?,\s*(0x[0-9a-f]+),")


class Modules:
	def __init__(self):
		self.ranges = []

	def add(self, begin, end, name):
		self.ranges.append((begin, end, name))

	def finish(self):
		self.ranges.sort()
		self.starts = [r[0] for r in self.ranges]

	def resolve(self, address):
		index = bisect.bisect_right(self.starts, address) - 1
		if index >= 0:
			begin, end, name = self.ranges[index]
			if address < end:
				return name, address - begin
		if address >= 0x80000000:
			return "kernel", 0
		return "?", address


def parse(path, target, modules, samples, thread_start):
	pid = None
	pending = None
	with open(path, "r", errors="replace") as handle:
		for line in handle:
			head = line[:40]
			if "Stack," in head:
				if pending is None:
					continue
				match = STACK_RE.match(line)
				if match and int(match.group(1)) == pending[0] and int(match.group(2)) == pending[1]:
					pending[3].append(int(match.group(4), 16))
				continue
			if "SampledProfile," in head:
				match = SAMPLE_RE.match(line)
				pending = None
				if not match or match.group(2).strip().lower() != target:
					continue
				pid = int(match.group(3))
				pending = (int(match.group(1)), int(match.group(4)), int(match.group(5), 16), [])
				samples.append(pending)
				continue
			if "I-Start," in head or "I-DCStart," in head:
				match = IMAGE_RE.match(line)
				if match and match.group(1).strip().lower() == target:
					name = match.group(5).replace("\\", "/").split("/")[-1].lower()
					modules.add(int(match.group(3), 16), int(match.group(4), 16), name)
				continue
			if "T-Start," in head or "T-DCStart," in head:
				match = THREAD_RE.match(line)
				if match and match.group(1).strip().lower() == target:
					thread_start[int(match.group(3))] = int(match.group(4), 16)

	return pid


def load_functions(directory):
	tables = {}
	for path in glob.glob(os.path.join(directory, "*.csv")):
		rows = []
		with open(path) as handle:
			for line in handle:
				start, end, entry, label = line.strip().split(",", 3)
				rows.append((int(start, 16), int(end, 16), int(entry, 16), label))
		rows.sort()
		tables[os.path.basename(path)[:-4].lower()] = ([r[0] for r in rows], [(r[1], r[2], r[3]) for r in rows])
	return tables


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("dump")
	parser.add_argument("process")
	parser.add_argument("--top", type=int, default=40)
	parser.add_argument("--thread", type=int, default=0)
	parser.add_argument("--module", default="")
	parser.add_argument("--inclusive", action="store_true")
	parser.add_argument("--callers", default="")
	parser.add_argument("--bucket", type=int, default=0, help="round RVAs down to this power-of-two size")
	parser.add_argument("--window", default="", help="start:end seconds relative to the first sample")
	parser.add_argument("--timeline", action="store_true", help="print per-second module mix of the busiest thread")
	parser.add_argument("--chains", default="", help="print the most common caller chains below this module+RVA (or module)")
	parser.add_argument("--depth", type=int, default=14)
	parser.add_argument("--functions", default="", help="directory of <module>.csv function ranges from ExportFunctionRanges.java")
	args = parser.parse_args()

	target = args.process.lower()
	modules = Modules()
	samples = []  # (timestamp, tid, address, [stack addresses])
	thread_start = {}

	cache = f"{args.dump}.{target}.pkl"
	if os.path.exists(cache) and os.path.getmtime(cache) >= os.path.getmtime(args.dump):
		with open(cache, "rb") as handle:
			pid, modules.ranges, samples, thread_start = pickle.load(handle)
	else:
		pid = parse(args.dump, target, modules, samples, thread_start)
		with open(cache, "wb") as handle:
			pickle.dump((pid, modules.ranges, samples, thread_start), handle)

	modules.finish()
	if not samples:
		print("no samples for", target)
		return 1

	origin = samples[0][0]
	duration_us = samples[-1][0] - origin
	print(f"pid={pid} samples={len(samples)} span={duration_us / 1e6:.3f}s")
	if args.timeline:
		busiest = collections.Counter(s[1] for s in samples).most_common(1)[0][0]
		seconds = collections.defaultdict(collections.Counter)
		for s in samples:
			if s[1] == busiest:
				seconds[int((s[0] - origin) / 1e6)][modules.resolve(s[2])[0]] += 1
		print(f"\n== timeline of thread {busiest} ==")
		for second in sorted(seconds):
			counts = seconds[second]
			total = sum(counts.values())
			print(f"{second:>4}s {total:>6} " + ", ".join(f"{m} {100 * c / total:.0f}%" for m, c in counts.most_common(4)))
	if args.window:
		begin, end = (float(v) for v in args.window.split(":"))
		samples = [s for s in samples if begin <= (s[0] - origin) / 1e6 < end]
		print(f"window {begin}-{end}s: {len(samples)} samples")

	per_thread = collections.Counter(s[1] for s in samples)
	print("\n== samples per thread ==")
	for tid, count in per_thread.most_common(12):
		start = thread_start.get(tid)
		where = "%s+0x%x" % modules.resolve(start) if start else "?"
		modcount = collections.Counter(modules.resolve(s[2])[0] for s in samples if s[1] == tid)
		top = ", ".join(f"{m} {100 * c / count:.0f}%" for m, c in modcount.most_common(5))
		print(f"{tid:>7} {count:>7} ({100 * count / len(samples):5.1f}%) start={where}  [{top}]")

	chosen = [s for s in samples if not args.thread or s[1] == args.thread]
	label = f"thread {args.thread}" if args.thread else "all threads"
	total = len(chosen)
	mask = ~(args.bucket - 1) if args.bucket else ~0

	functions = load_functions(args.functions) if args.functions else {}

	def key(address):
		name, rva = modules.resolve(address)
		table = functions.get(name)
		if table:
			index = bisect.bisect_right(table[0], rva) - 1
			if index >= 0 and rva < table[1][index][0]:
				end, entry, label = table[1][index]
				return f"{name}!{label}"
		return f"{name}+0x{rva & mask:x}"

	print(f"\n== exclusive by module ({label}, {total}) ==")
	for name, count in collections.Counter(modules.resolve(s[2])[0] for s in chosen).most_common(20):
		print(f"{count:>7} {100 * count / total:5.1f}% {name}")

	print(f"\n== exclusive by address ({label}) ==")
	exclusive = collections.Counter(key(s[2]) for s in chosen if not args.module or modules.resolve(s[2])[0] == args.module)
	for name, count in exclusive.most_common(args.top):
		print(f"{count:>7} {100 * count / total:5.1f}% {name}")

	if args.inclusive:
		print(f"\n== inclusive by frame ({label}) ==")
		inclusive = collections.Counter()
		for s in chosen:
			seen = set()
			for address in s[3]:
				k = key(address)
				if k.startswith("kernel") or k in seen:
					continue
				if args.module and not k.startswith(args.module + "+"):
					continue
				seen.add(k)
				inclusive[k] += 1
		for name, count in inclusive.most_common(args.top):
			print(f"{count:>7} {100 * count / total:5.1f}% {name}")

	if args.callers:
		print(f"\n== callers of {args.callers} ==")
		callers = collections.Counter()
		for s in chosen:
			frames = [key(a) for a in s[3]]
			for index, frame in enumerate(frames):
				if frame == args.callers and index + 1 < len(frames):
					callers[" <- ".join(frames[index + 1:index + 4])] += 1
					break
		for name, count in callers.most_common(args.top):
			print(f"{count:>7} {name}")
	if args.chains:
		print(f"\n== caller chains below {args.chains} ==")
		chains = collections.Counter()
		for s in chosen:
			frames = [key(a) for a in s[3]]
			for index, frame in enumerate(frames):
				if frame == args.chains or frame.split("+")[0] == args.chains:
					chains[" <- ".join(f for f in frames[index:index + args.depth] if not f.startswith("kernel"))] += 1
					break
		for chain, count in chains.most_common(args.top):
			print(f"{count:>7} {chain}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
