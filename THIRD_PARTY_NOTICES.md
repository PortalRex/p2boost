# Third-party notices

p2boost contains no Valve, SAR, Ghidra, PresentMon, or JDK binaries and no
proprietary Source code. The only vendored binary is NVIDIA's public x86 NVAPI
import library under its MIT license.

- Portal 2 and Source are trademarks of Valve Corporation. Shipped Portal 2
  binaries are analyzed locally and are never redistributed.
- Source SDK 2013 is used as a public structural analogue under Valve's SDK
  license; no SDK code is copied into the DLL.
- SourceAutoRecord (SAR) is public GPL-3.0 software. Its public Portal 2 ABI and
  hook behavior were studied for interoperability; no SAR code is linked or
  copied into p2boost.
- Ghidra is Apache-2.0 software from the NSA.
- PresentMon is MIT-licensed software from Intel.
- Eclipse Temurin is GPLv2 with the Classpath Exception.
- NVIDIA's public NVAPI headers and x86 import library are MIT-licensed. Their
  complete license is included at `third_party/nvapi/LICENSE`.
- DXVK is zlib-licensed software by Philip Rebohle and contributors. The
  drop-in release package (produced by `scripts/package-dropin.ps1`) contains
  the official DXVK x86 `d3d9.dll` renamed to `bin\dxvk_d3d9.dll`, together
  with its complete license text as `LICENSE.dxvk.txt`.
  `scripts/install-dxvk.ps1` downloads the same official DXVK release archive
  for in-place updates; p2boost itself never vendors or modifies a DXVK binary.
