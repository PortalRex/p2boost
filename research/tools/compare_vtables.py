"""Compare vtable slots of RTTI-named classes between two builds of a module.

Usage: python compare_vtables.py <a.dll> <b.dll> <Class>:<slot>,<slot> [...]

Finds each class's primary vtable through its RTTI name and compares the
functions in the given slots with addresses and stack offsets masked. A
differing slot is either the same method compiled differently or a shifted
table; read both bodies to tell which.
"""

import re
import struct
import sys

import capstone

from disasm import offset_to_va, va_to_offset

ADDRESS = re.compile(r"0x[0-9a-f]{6,8}")
STACK = re.compile(r"(ebp|esp) - 0x[0-9a-f]+")


def vtables(data, name):
    """Every primary vtable whose complete-object locator names the class."""
    out = []
    for match in re.finditer(re.escape(b".?AV" + name.encode() + b"@@\0"), data):
        descriptor = offset_to_va(data, match.start()) - 8
        for ref in re.finditer(re.escape(struct.pack("<I", descriptor)), data):
            locator = offset_to_va(data, ref.start()) - 12
            if locator is None or struct.unpack_from("<I", data, ref.start() - 12)[0] != 0:
                continue  # offset != 0: not the primary base
            for use in re.finditer(re.escape(struct.pack("<I", locator)), data):
                out.append(offset_to_va(data, use.start()) + 4)
    return out


def body(data, va, limit=60):
    try:
        offset = va_to_offset(data, va)
    except ValueError:
        return ["<not code>"]
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out = []
    for ins in engine.disasm(data[offset:offset + 0x600], va):
        out.append(STACK.sub(r"\1-X", ADDRESS.sub("ADDR", f"{ins.mnemonic} {ins.op_str}")))
        if ins.mnemonic.startswith("ret") or len(out) >= limit:
            break
    return out


def main():
    left = open(sys.argv[1], "rb").read()
    right = open(sys.argv[2], "rb").read()
    for spec in sys.argv[3:]:
        name, _, slots = spec.partition(":")
        lt, rt = vtables(left, name), vtables(right, name)
        if len(lt) != 1 or len(rt) != 1:
            print(f"{name}: vtables {[hex(v) for v in lt]} vs {[hex(v) for v in rt]}")
            if not lt or not rt:
                continue
        bad = []
        for slot in [int(s) for s in slots.split(",")]:
            lf = struct.unpack_from("<I", left, va_to_offset(left, lt[0] + 4 * slot))[0]
            rf = struct.unpack_from("<I", right, va_to_offset(right, rt[0] + 4 * slot))[0]
            a, b = body(left, lf), body(right, rf)
            if a != b:
                same = sum(x == y for x, y in zip(a, b))
                bad.append(f"{slot} ({same}/{max(len(a), len(b))})")
        print(f"{name}: {len(slots.split(','))} slots, differing: {', '.join(bad) or 'none'}")


if __name__ == "__main__":
    main()
