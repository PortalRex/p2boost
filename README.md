# p2boost

p2boost is an experimental, open-source, native-Direct3D-9 x86 Source plugin
for Portal 2 Windows build `23973718`. It currently contains one measured,
reversible frame-pacing correction and the research/test infrastructure needed
to investigate the portal-reset and co-op paths without replacing game DLLs,
forcing cvars, reducing quality, or using Vulkan/DXVK.

This repository is deliberately fail-closed. The DLL hashes the five relevant
game binaries and validates the exact instruction/import signature before it
installs a hook. An unsupported build or non-Microsoft D3D9 backend remains
loaded for diagnostics but installs no fix.

## Current status

This is a research alpha, not a claim that all acceptance gates are met. The
coarse `ThreadSleep(1)` call in Portal 2's `fps_max` host-loop path has been
replaced, at that caller only, by a high-resolution waitable-timer wait. The
engine still decides whether and how long to wait, so `fps_max`, VSync,
fullscreen, and refresh policy remain engine-owned. The hook chains through the
previous import target and restores it only if it still owns the slot.

The focus/portal-resource and redundant co-op-render causes are documented but
not patched because the current evidence does not yet isolate a safe permanent
change. See [research/report.md](research/report.md) and
[benchmarks/report.md](benchmarks/report.md).

## Build and install

Requirements are Visual Studio 2022 with the x86 C++ toolchain and CMake 3.24+.

```powershell
.\scripts\bootstrap-tools.ps1
.\scripts\build.ps1
.\scripts\install.ps1
```

The build is C++17, Win32, `/MT`, CFG-enabled, and exports only
`CreateInterface`. Installation adds an idempotent marked block to
`portal2/cfg/autoexec.cfg`; uninstall removes only that block and p2boost's own
files:

```powershell
.\scripts\uninstall.ps1
```

## Commands

- `p2boost_status` prints hashes/build support, native-D3D9 state, SAR state,
  enabled state, and metric count.
- `p2boost_enable 0|1` switches the verified fix at runtime for A/B testing.
- `p2boost_trace 0|1` enables the fixed-size QPC frame trace.
- `p2boost_dump_metrics` writes `p2boost_metrics.csv` in the game root, including
  simulation, focus, and local-co-op state.

No command writes or archives a game cvar.

## Reproducing research

`scripts/run-ghidra.ps1` imports unmodified PEs into a headless Ghidra project
and exports symbols, strings, target decompilation, and callers. Generated
projects/artifacts are ignored because they contain derived game-binary data.
PresentMon capture and analysis scripts are under `scripts/`; the 30-cycle
focus harness is `run-focus-portal-cycles.ps1`.

Only legally installed binaries, Valve's public Source SDK, and SAR's public
repository are valid research inputs. Leaked Source trees and old IDA databases
are explicitly out of scope.
