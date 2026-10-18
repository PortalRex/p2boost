"""Disassemble x86 code from a PE file at preferred virtual addresses.

Usage: python disasm.py <image.dll> <va>[:<bytes>] [...]
       python disasm.py <image.dll> --find "<hex bytes with ??>"

Requires the capstone package. Addresses are preferred-image addresses
(for example 0x100e6650), matching Ghidra listings.
"""

import re
import struct
import sys

import capstone


def sections(data):
	pe = struct.unpack_from("<I", data, 0x3C)[0]
	count = struct.unpack_from("<H", data, pe + 6)[0]
	optional = struct.unpack_from("<H", data, pe + 20)[0]
	base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
	table = pe + 24 + optional
	out = []
	for index in range(count):
		size, rva, _, raw = struct.unpack_from("<IIII", data, table + index * 40 + 8)
		out.append((rva, size, raw))
	return base, out


def va_to_offset(data, va):
	base, table = sections(data)
	rva = va - base
	for start, size, raw in table:
		if start <= rva < start + size:
			return rva - start + raw
	raise ValueError(hex(va))


def offset_to_va(data, offset):
	base, table = sections(data)
	for start, size, raw in table:
		if raw <= offset < raw + size:
			return base + start + offset - raw
	return None


def main():
	data = open(sys.argv[1], "rb").read()
	if sys.argv[2] == "--find":
		pattern = b"".join(b"." if token == "??" else re.escape(bytes([int(token, 16)])) for token in sys.argv[3].split())
		for match in re.finditer(pattern, data, re.S):
			print(hex(offset_to_va(data, match.start())))
		return
	engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
	for request in sys.argv[2:]:
		address, _, length = request.partition(":")
		address = int(address, 16)
		length = int(length or "64")
		offset = va_to_offset(data, address)
		for instruction in engine.disasm(data[offset:offset + length], address):
			print(f"{instruction.address:08x}  {instruction.bytes.hex(' '):<24} {instruction.mnemonic} {instruction.op_str}")
		print()


if __name__ == "__main__":
	main()
