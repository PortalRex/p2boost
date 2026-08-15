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
- DXVK is zlib-licensed software by Philip Rebohle and contributors.
  `scripts/install-dxvk.ps1` downloads an official DXVK release archive, which
  contains its license text. p2boost itself never vendors a DXVK binary.
