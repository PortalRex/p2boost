# Research code

This directory is intentionally outside the release target.

- `runtime/native_d3d9` preserves the renderer, resource, fence, visibility,
  portal, and server probes used to isolate native-D3D9 stalls.
- `scripts` contains the matching ETW, PresentMon, gameplay-route, focus, and
  SAR test harnesses.
- `benchmarks/native-d3d9-report.md` is the complete pre-Vulkan benchmark log.
- `ghidra` contains the reproducible headless analysis exporter.
- `report.md` records accepted and rejected hypotheses, signatures, xrefs, and
  causality evidence.

None of these sources is compiled into `p2boost.dll`. The optional
`P2BOOST_BUILD_RESEARCH_TOOLS` CMake switch builds only the standalone sampler;
it does not enable any in-game research hook.
