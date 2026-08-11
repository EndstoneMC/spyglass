"""Cut a libhat byte pattern for a Bedrock client function, and prove it is unique.

    uv run --no-project --with capstone scripts/cut_signature.py \
        --pe  <Minecraft.Windows.exe> \
        --pdb <directory holding Minecraft.Windows.pdb> \
        --symbol "Packet::readNoHeader"

Wildcarding policy, so a pattern survives a relink of the same source:

  * rip-relative displacements    -- every global moves
  * rel32 call/jmp targets        -- every callee moves
  * immediates of 4 bytes or more -- absolute addresses and frame sizes
  * displacements off rsp/rbp     -- the frame layout shifts with register allocation

Displacements off any other base register are kept: those are structure offsets,
and they are exactly the signal that identifies the function.

Without --pdb the symbol is not resolved and --rva must be given instead, which is
how a pattern gets verified against a build whose symbols are unavailable.
"""

from __future__ import annotations

import argparse
import ctypes
import struct
import sys
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RBP, X86_REG_RIP, X86_REG_RSP

_SYMOPT_FAIL_CRITICAL_ERRORS = 0x00000200
_SYMOPT_EXACT_SYMBOLS = 0x00000400
_SYMOPT_NO_PROMPTS = 0x00080000


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.ULONG),
        ("TypeIndex", wintypes.ULONG),
        ("Reserved", ctypes.c_ulonglong * 2),
        ("Index", wintypes.ULONG),
        ("Size", wintypes.ULONG),
        ("ModBase", ctypes.c_ulonglong),
        ("Flags", wintypes.ULONG),
        ("Value", ctypes.c_ulonglong),
        ("Address", ctypes.c_ulonglong),
        ("Register", wintypes.ULONG),
        ("Scope", wintypes.ULONG),
        ("Tag", wintypes.ULONG),
        ("NameLen", wintypes.ULONG),
        ("MaxNameLen", wintypes.ULONG),
        ("Name", ctypes.c_char * 1),
    ]


_NAME_OFFSET = SYMBOL_INFO.Name.offset
_ENUM_CB = ctypes.WINFUNCTYPE(wintypes.BOOL, ctypes.POINTER(SYMBOL_INFO), wintypes.ULONG, ctypes.c_void_p)


def resolve_symbol(pdb_dir: Path, pe_path: Path, symbol: str) -> tuple[int, int]:
    """Returns (rva, size) for the one function matching `symbol`."""
    dh = ctypes.WinDLL("dbghelp.dll")
    k32 = ctypes.WinDLL("kernel32.dll")
    k32.GetCurrentProcess.restype = wintypes.HANDLE
    dh.SymSetOptions.argtypes = [wintypes.DWORD]
    dh.SymSetOptions.restype = wintypes.DWORD
    dh.SymInitialize.argtypes = [wintypes.HANDLE, wintypes.LPCSTR, wintypes.BOOL]
    dh.SymInitialize.restype = wintypes.BOOL
    dh.SymLoadModuleEx.argtypes = [
        wintypes.HANDLE, wintypes.HANDLE, wintypes.LPCSTR, wintypes.LPCSTR,
        ctypes.c_ulonglong, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
    ]
    dh.SymLoadModuleEx.restype = ctypes.c_ulonglong
    dh.SymEnumSymbols.argtypes = [
        wintypes.HANDLE, ctypes.c_ulonglong, wintypes.LPCSTR, _ENUM_CB, ctypes.c_void_p,
    ]

    process = k32.GetCurrentProcess()
    dh.SymSetOptions(_SYMOPT_EXACT_SYMBOLS | _SYMOPT_FAIL_CRITICAL_ERRORS | _SYMOPT_NO_PROMPTS)
    if not dh.SymInitialize(process, str(pdb_dir).encode(), False):
        raise SystemExit("SymInitialize failed")
    base = dh.SymLoadModuleEx(process, None, str(pe_path).encode(), None, 0, 0, None, 0)
    if base == 0:
        raise SystemExit("SymLoadModuleEx failed; is the PDB beside the PE?")

    hits: list[tuple[int, int]] = []

    @_ENUM_CB
    def callback(psym, _size, _ctx):
        sym = psym.contents
        hits.append((sym.Address - base, sym.Size))
        return True

    dh.SymEnumSymbols(process, base, symbol.encode(), callback, None)
    if not hits:
        raise SystemExit(f"{symbol}: no such symbol in the PDB")
    if len(hits) > 1:
        listed = ", ".join(f"0x{rva:x}({size}B)" for rva, size in hits)
        raise SystemExit(f"{symbol}: overloaded, pick one with --rva/--size: {listed}")
    return hits[0]


@dataclass(frozen=True)
class Image:
    data: bytes
    base: int
    sections: list[tuple[str, int, int, int, int]]

    @classmethod
    def load(cls, path: Path) -> Image:
        data = path.read_bytes()
        pe_off = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_off : pe_off + 4] != b"PE\0\0":
            raise SystemExit(f"{path}: not a PE image")
        count = struct.unpack_from("<H", data, pe_off + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
        base = struct.unpack_from("<Q", data, pe_off + 24 + 24)[0]
        sec_off = pe_off + 24 + opt_size
        sections = []
        for i in range(count):
            at = sec_off + i * 40
            name = data[at : at + 8].rstrip(b"\0").decode(errors="replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, at + 8)
            sections.append((name, vaddr, vsize, rawptr, rawsize))
        return cls(data, base, sections)

    def section(self, name: str) -> tuple[int, int, int]:
        for entry in self.sections:
            if entry[0] == name:
                return entry[1], entry[3], entry[4]
        raise SystemExit(f"no {name} section")

    def offset_of(self, rva: int) -> int:
        for _name, vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        raise SystemExit(f"rva 0x{rva:x} is outside every section")


def volatile_ranges(insn) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for op in insn.operands:
        if op.type == X86_OP_MEM:
            unstable = op.mem.base in (X86_REG_RIP, X86_REG_RSP, X86_REG_RBP)
            if unstable and insn.disp_size:
                ranges.append((insn.disp_offset, insn.disp_size))
        elif op.type == X86_OP_IMM and insn.imm_size >= 4:
            ranges.append((insn.imm_offset, insn.imm_size))
    return ranges


def cut(image: Image, rva: int, length: int, budget: int) -> str:
    code = image.data[image.offset_of(rva) : image.offset_of(rva) + length]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    tokens: list[str] = []
    for insn in md.disasm(code, image.base + rva):
        if len(tokens) >= budget:
            break
        keep = [True] * insn.size
        for off, size in volatile_ranges(insn):
            for i in range(off, min(off + size, insn.size)):
                keep[i] = False
        tokens.extend(f"{b:02X}" if keep[i] else "?" for i, b in enumerate(insn.bytes))

    while tokens and tokens[-1] == "?":
        tokens.pop()
    return " ".join(tokens)


def matches(image: Image, pattern: str, section: str = ".text") -> list[int]:
    vaddr, rawptr, rawsize = image.section(section)
    blob = image.data[rawptr : rawptr + rawsize]
    tokens = pattern.split()
    wanted = [None if t == "?" else int(t, 16) for t in tokens]

    anchor = wanted[0]
    found: list[int] = []
    start = blob.find(bytes([anchor]))
    while start != -1:
        if start + len(wanted) <= len(blob) and all(
            w is None or blob[start + i] == w for i, w in enumerate(wanted)
        ):
            found.append(vaddr + start)
        start = blob.find(bytes([anchor]), start + 1)
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pe", type=Path, required=True)
    parser.add_argument("--pdb", type=Path, help="directory holding the PDB")
    parser.add_argument("--symbol")
    parser.add_argument("--rva", type=lambda v: int(v, 16))
    parser.add_argument("--size", type=int, default=0)
    parser.add_argument("--budget", type=int, default=72, help="maximum pattern bytes")
    parser.add_argument("--verify", help="verify this pattern instead of cutting a new one")
    args = parser.parse_args()

    image = Image.load(args.pe)

    if args.verify:
        hits = matches(image, args.verify)
        print(f"{len(hits)} match(es): {[hex(h) for h in hits]}")
        sys.exit(0 if len(hits) == 1 else 1)

    if args.rva is not None:
        rva, size = args.rva, args.size or args.budget * 2
    elif args.symbol and args.pdb:
        rva, size = resolve_symbol(args.pdb, args.pe, args.symbol)
        print(f"{args.symbol}: rva=0x{rva:x} size={size}")
    else:
        raise SystemExit("give either --rva or both --pdb and --symbol")

    pattern = cut(image, rva, size, args.budget)
    hits = matches(image, pattern)
    print(f"\npattern ({len(pattern.split())} bytes):\n{pattern}")
    print(f"\n{len(hits)} match(es) in .text: {[hex(h) for h in hits]}")
    if hits != [rva]:
        print("REJECTED: a pattern must match its own function and nothing else", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
