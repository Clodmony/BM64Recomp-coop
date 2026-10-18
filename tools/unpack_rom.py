#!/usr/bin/env python3
"""Unpack a Bomberman 64 ROM into the decompressed image the recompiler reads.

usage: python3 -I tools/unpack_rom.py <rom> [<out folder>]

<rom> is the US (BOMBERMAN64, NBME) or Europe (BOMBERMAN64E, NBMP) release in
any byte order (.z64, .n64, .v64). The image is written to
<out folder>/bm64.decomp.<us|eu>.z64 (default ~/dev-scratch/BM64Recomp-coop/rom),
never next to the ROM or into the checkout, and the ROM itself in .z64 order to
<out folder>/bm64.<us|eu>.z64: the file the game stores and checks, which
tools/run.sh hands it. Any other ROM is refused.

The image is what Bomberhackers/bm64's tools/bm64decompress builds: the ROM in
big-endian order as is, then every overlay table entry appended, LZSS entries
decompressed, stored entries padded to 16 bytes. The overlay table (offsets and
sizes only) is tools/bm64_overlays.txt; it holds for both versions.

Checks: the header checksum is recomputed (a damaged or modified ROM fails),
every decompressed entry must have the table's size, and the image's SHA-1 is
compared with the known one where there is one (KNOWN_IMAGE_SHA1).
Standard library only.
"""
import hashlib
import os
import struct
import sys

VERSIONS = {
    # (internal name, game code): version
    ("BOMBERMAN64", "NBME"): "us",
    ("BOMBERMAN64E", "NBMP"): "eu",
}

# SHA-1 of the decompressed image, from a run of this script on a known-good
# ROM. The decompilation publishes only the hash of its rebuilt compressed ROM,
# so the US image has none yet.
KNOWN_IMAGE_SHA1 = {
    "eu": "3769a7af0aac3aeb285ba63f03e936879db6ba82",
}

DEFAULT_OUT = os.path.expanduser("~/dev-scratch/BM64Recomp-coop/rom")
TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bm64_overlays.txt")


def fail(msg):
    sys.exit(f"unpack_rom: {msg}")


def to_big_endian(b):
    magic = b[:4]
    if magic == b"\x80\x37\x12\x40":  # .z64
        return bytearray(b)
    if magic == b"\x37\x80\x40\x12":  # .v64, byte-swapped halfwords
        out = bytearray(len(b))
        out[0::2], out[1::2] = b[1::2], b[0::2]
        return out
    if magic == b"\x40\x12\x37\x80":  # .n64, little-endian words
        out = bytearray(len(b))
        for i in range(4):
            out[i::4] = b[3 - i::4]
        return out
    fail("not an N64 ROM (unknown byte order)")


def header_crc(rom):
    """The boot checksum (CIC-NUS-6102) over 1 MiB after the boot code."""
    m = 0xFFFFFFFF
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
    for (d,) in struct.iter_unpack(">I", rom[0x1000:0x101000]):
        if (t6 + d) & m < t6:
            t4 = (t4 + 1) & m
        t6 = (t6 + d) & m
        t3 ^= d
        s = d & 0x1F
        r = ((d << s) | (d >> (32 - s))) & m
        t5 = (t5 + r) & m
        t2 ^= r if t2 > d else t6 ^ d
        t1 = (t1 + (t5 ^ d)) & m
    return t6 ^ t4 ^ t3, t5 ^ t2 ^ t1


def lzss(rom, ofs):
    n = struct.unpack_from(">I", rom, ofs)[0]
    ofs += 4
    win = bytearray(1024)
    wo = 958
    out = bytearray()
    flag = 0
    while len(out) < n:
        if not flag & 0x100:
            flag = 0xFF00 | rom[ofs]
            ofs += 1
        if flag & 1:
            v = rom[ofs]
            ofs += 1
            out.append(v)
            win[wo] = v
            wo = (wo + 1) % 1024
        else:
            b1, b2 = rom[ofs], rom[ofs + 1]
            ofs += 2
            o = ((b2 & 0xC0) << 2) | b1
            for _ in range((b2 & 0x3F) + 3):
                v = win[o]
                win[wo] = v
                wo = (wo + 1) % 1024
                out.append(v)
                o = (o + 1) % 1024
        flag >>= 1
    return bytes(out[:n])


def read_table():
    rows = []
    with open(TABLE) as f:
        for line in f:
            line = line.split("#", 1)[0].split()
            if line:
                rows.append((line[0] == "1", *(int(x, 16) for x in line[1:4])))
    return rows


def main():
    if len(sys.argv) not in (2, 3) or sys.argv[1] in ("-h", "--help"):
        print(__doc__.split("\n\n")[1])
        sys.exit(2)
    rom_path = sys.argv[1]
    out_dir = os.path.abspath(sys.argv[2] if len(sys.argv) == 3 else DEFAULT_OUT)
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if out_dir == repo or out_dir.startswith(repo + os.sep):
        fail(f"refusing to write into the checkout ({out_dir}); pick a scratch folder")
    if os.path.realpath(os.path.dirname(os.path.abspath(rom_path))) == os.path.realpath(out_dir):
        fail("refusing to write next to the ROM; pick a scratch folder")

    with open(rom_path, "rb") as f:
        rom = to_big_endian(f.read())
    name = rom[0x20:0x34].decode("ascii", "replace").strip(" \0")
    code = rom[0x3B:0x3F].decode("ascii", "replace")
    version = VERSIONS.get((name, code))
    if version is None:
        fail(f"not a supported Bomberman 64 ROM: internal name {name!r}, game code {code!r} "
             f"(supported: US BOMBERMAN64/NBME, Europe BOMBERMAN64E/NBMP)")
    if rom[0x3F] != 0:
        fail(f"{name}: revision {rom[0x3F]}, only revision 0 (1.0) is supported")
    crc = header_crc(rom)
    if crc != struct.unpack_from(">II", rom, 0x10):
        fail(f"{name}: header checksum does not match the contents (damaged or modified ROM)")

    img = bytearray(rom)
    for i, (compressed, ofs, size, usize) in enumerate(read_table()):
        if compressed:
            blob = lzss(rom, ofs)
            if len(blob) != usize:
                fail(f"overlay entry {i}: decompressed {len(blob):#x} bytes, table says {usize:#x}")
        else:
            blob = bytes(rom[ofs:ofs + size])
            blob += b"\0" * ((-len(blob)) % 16)
        img += blob

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, f"bm64.{version}.z64"), "wb") as f:
        f.write(rom)
    out_path = os.path.join(out_dir, f"bm64.decomp.{version}.z64")
    with open(out_path, "wb") as f:
        f.write(img)
    sha1 = hashlib.sha1(img).hexdigest()
    known = KNOWN_IMAGE_SHA1.get(version)
    status = "no known hash to compare" if known is None else ("matches the known image" if sha1 == known else "DIFFERS from the known image")
    print(f"{out_path}: {version} ({name}), {len(img):#x} bytes, sha1 {sha1}: {status}")
    if known is not None and sha1 != known:
        sys.exit(1)


main()
