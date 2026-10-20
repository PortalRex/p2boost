# Research tools

Nothing here is compiled into `p2boost.dll`. These are the tools used to find
and verify the changes described in [docs/technical.md](../docs/technical.md).

- `scripts/capture-cpu-profile.ps1` records an elevated WPR CPU profile of the
  running game and dumps it to text; `scripts\bench.ps1 -ProfileSeconds N`
  calls it per variant.
- `tools/etw_profile.py` aggregates that dump per thread, module, function,
  and caller chain (`--functions`, `--chains`, `--timeline`, `--window`).
- `scripts/ghidra-analyze.ps1` imports the current game binaries into
  headless Ghidra projects (`ghidra/projects/current-*`, not tracked) and
  exports the function ranges `etw_profile.py --functions` uses.
- `scripts/ghidra-describe.ps1` resolves profile RVAs to functions and
  optionally decompiles them from those projects.
- `ghidra/scripts` holds the Ghidra scripts behind the above:
  `ExportFunctionRanges.java`, `DescribeRvas.java`, and `FindScalarUses.java`
  (lists instructions that use given struct offsets).
- `tools/disasm.py` disassembles PE bytes at preferred addresses and finds
  byte signatures (`--find`), used to verify every signature is unique.
- `tools/check_signatures.py <portal 2 root> <mod root>` matches every
  profile signature in both installs' modules, and
  `tools/compare_functions.py <a.dll> <b.dll> "<signature>"` diffs one
  function between two builds with addresses masked, and
  `tools/compare_vtables.py` does the same for numbered interface slots;
  together they show which profile fields a mod must override.

`harness/` drives the game unattended over the net console, without input
injection or elevation:

- `harness.py` launches, drives, captures, and quits the game; `bench.py`
  runs interleaved suites measured with `p2boost_trace`.
- `run_all.py [main|stock|sp|partner|l1000]` holds the suites behind
  [benchmarks/plus-automated.md](../benchmarks/plus-automated.md).
- `sampler.py` is a user-mode main-thread sampler (no ETW, no UAC);
  `profile_lobby.py` profiles the split-screen lobby with it.
- `coop_maps.py`, `coop_maps_deep.py` (spawns from `bsp_spawns.py`), and
  `capture_compare.py` capture before/after frames; `make_slider*.py` and
  `compose.py` turn them into a slider page or video.
- `partner_test.py` and `shadow_test.py` check the two caps' visual
  behavior: the partner picture following `in_forceuser`, and shadow
  freshness under a stationary light.

Run `scripts\bootstrap-tools.ps1` once to fetch PresentMon, a JDK, and Ghidra
into `.tools`. `report.md` is the archived reverse-engineering log from the
earlier native-D3D9 and Vulkan investigations; some files it mentions were
removed after the work concluded and remain in git history.
