"""Compare one function between two builds of a module, instruction by instruction.

Usage: python compare_functions.py <a.dll> <b.dll> "<signature>" [length]

Finds the signature in both images, disassembles up to `length` bytes
(default 0x600) from each match, stopping at the padding after a return, and
prints the instructions that differ once absolute addresses are masked. What
remains are differences in struct offsets, immediates, and code shape, which
a mod profile must cover.
"""

import re
import sys

import capstone

from disasm import offset_to_va, va_to_offset

ADDRESS = re.compile(r"0x1[0-9a-f]{7}")


def listing(path, signature, length):
    data = open(path, "rb").read()
    pattern = b"".join(b"." if "?" in token else re.escape(bytes([int(token, 16)])) for token in signature.split())
    matches = [match.start() for match in re.finditer(pattern, data, re.S)]
    if len(matches) != 1:
        sys.exit(f"{path}: signature matched {len(matches)} times")
    start = offset_to_va(data, matches[0])
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    offset = va_to_offset(data, start)
    out = []
    for instruction in engine.disasm(data[offset:offset + length], start):
        text = f"{instruction.mnemonic} {instruction.op_str}"
        # stop at the padding after the function's last return.
        if instruction.mnemonic == "int3" and out and out[-1][1].startswith("ret"):
            break
        out.append((instruction.address - start, ADDRESS.sub("ADDR", text)))
    return out


def main():
    length = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x600
    left = listing(sys.argv[1], sys.argv[3], length)
    right = listing(sys.argv[2], sys.argv[3], length)
    differences = 0
    for (offset_a, text_a), (offset_b, text_b) in zip(left, right):
        if text_a != text_b or offset_a != offset_b:
            differences += 1
            print(f"+{offset_a:04x} {text_a:48} | +{offset_b:04x} {text_b}")
    print(f"{differences} differing instructions of {min(len(left), len(right))}")


if __name__ == "__main__":
    main()
