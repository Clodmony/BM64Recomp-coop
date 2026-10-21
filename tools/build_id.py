#!/usr/bin/env python3
"""The build identity an online session's hello compares (#26, #67).

usage:
  python3 -I tools/build_id.py [--version us|eu] [--number|--files]
      Prints the identity of this checkout, with the files it covers
      (--number: only the value, which src/coop/version.cmake reads;
      --files: only their paths, which the same file configures again on).
  python3 -I tools/build_id.py check [--version us|eu]
      Checks the properties the identity must have: it covers the patches'
      sources and the game's recompiler config, it does not depend on the
      compiled patches binary, and any change to a covered file changes it.
      Prints one line per check and a RESULT line; exit 0 only if all pass.

Why it reads sources, not the compiled patches (#67): the identity used to be
FNV-1a over `patches/patches.bin`, the MIPS binary `make -C patches` builds
with whatever clang the machine has. Measured on the two machines of this fork
at the same commit `18e4c8b`: clang 18.1.3 gave a 46 312-byte binary and
`0xFC44BFB4`, clang 23.1.3 a 46 328-byte one and `0x7F5EB1B6`, so the hello
refused the join although the code was the same. Hashing the sources instead
gives one number per commit on any compiler, while a real difference in the
co-op logic still shows up; code that behaves differently at run time is
caught by the session's per-frame state hash (docs/coop/design.md, *The
session core*).

The same value is compiled into the game at configure time and printed by this
script, so two players can compare their builds without playing (the recipe in
docs/coop/building.md, *The build identity across platforms*).
Standard library only; no game data in this file.
"""
import argparse
import hashlib
import pathlib
import shutil
import sys
import tempfile

# The patches' own sources: every file git tracks under patches/ that the
# build reads (the Makefile holds the compiler flags, so it counts too). The
# build output there (*.o, *.d, patches.bin, patches.elf, patches.map) is
# deliberately left out: it is what #67 found to differ between compilers.
PATCH_PATTERNS = ("*.c", "*.h", "*.ld", "Makefile")

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
MASK = 0xFFFFFFFF


def covered_files(root: pathlib.Path, version: str) -> list[pathlib.Path]:
    """The files the identity covers, sorted by their path in the checkout."""
    paths = []
    for pattern in PATCH_PATTERNS:
        paths += (root / "patches").glob(pattern)
    # The recompiler config of the patches (which functions they replace) and
    # of the game (the hooks); the Europe ones are generated from the US ones.
    for name in (patches_toml(version), rom_toml(version)):
        paths.append(root / name)
    return sorted(paths, key=lambda path: path.relative_to(root).as_posix())


def patches_toml(version: str) -> str:
    return "patches.toml" if version == "us" else f"patches.{version}.toml"


def rom_toml(version: str) -> str:
    return f"bm64.{version}.toml"


def fnv1a(data: bytes, hash_: int = FNV_OFFSET) -> int:
    for byte in data:
        hash_ = ((hash_ ^ byte) * FNV_PRIME) & MASK
    return hash_


def hooks_id(root: pathlib.Path, version: str) -> int:
    """The config half, as src/coop/version.cmake takes it: the first 8 hex
    digits of the SHA-256 of the game's recompiler config."""
    digest = hashlib.sha256((root / rom_toml(version)).read_bytes()).hexdigest()
    return int(digest[:8], 16)


def build_id(root: pathlib.Path, version: str) -> tuple[int, list[pathlib.Path]]:
    files = covered_files(root, version)
    if not files:
        raise SystemExit(f"no files to hash under {root}/patches")
    hash_ = FNV_OFFSET
    for path in files:
        # The path goes in too, so a renamed or added file changes the number.
        hash_ = fnv1a(path.relative_to(root).as_posix().encode(), hash_)
        hash_ = fnv1a(bytes([0]), hash_)
        hash_ = fnv1a(path.read_bytes(), hash_)
        hash_ = fnv1a(bytes([0]), hash_)
    # Then the config half's four bytes, little end first, as the old sum did.
    hooks = hooks_id(root, version)
    hash_ = fnv1a(bytes((hooks >> (i * 8)) & 0xFF for i in range(4)), hash_)
    return hash_, files


def missing(root: pathlib.Path, version: str) -> str | None:
    for name in (patches_toml(version), rom_toml(version)):
        if not (root / name).is_file():
            hint = ""
            if version != "us":
                hint = f" (python3 -I tools/europe.py configs writes {name})"
            return f"{name} is not in the checkout{hint}"
    return None


def show(root: pathlib.Path, version: str, number_only: bool, files_only: bool) -> int:
    gone = missing(root, version)
    if gone is not None:
        print(f"build id: {gone}", file=sys.stderr)
        return 4
    if files_only:
        for path in covered_files(root, version):
            print(path.relative_to(root).as_posix())
        return 0
    value, files = build_id(root, version)
    if number_only:
        print(f"0x{value:08X}")
        return 0
    print(f"build id 0x{value:08X} ({version})")
    print(f"  {len(files)} files, config 0x{hooks_id(root, version):08X}")
    for path in files:
        print(f"    {path.relative_to(root).as_posix()}")
    return 0


def copy_covered(root: pathlib.Path, version: str, into: pathlib.Path) -> None:
    for path in covered_files(root, version):
        target = into / path.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)


def check(root: pathlib.Path, version: str) -> int:
    gone = missing(root, version)
    if gone is not None:
        print(f"build id: {gone}", file=sys.stderr)
        return 4
    results = []

    def result(name: str, ok: bool, detail: str) -> None:
        results.append(ok)
        print(f"{name}: {'ok' if ok else 'FAILED'} ({detail})")

    value, files = build_id(root, version)
    names = [path.relative_to(root).as_posix() for path in files]

    wanted = sorted(path.name for path in (root / "patches").glob("*.c"))
    held = sorted(name.split("/")[-1] for name in names if name.endswith(".c"))
    result("sources", wanted == held and bool(wanted),
           f"{len(files)} files, every patches/*.c covered" if wanted == held
           else f"covers {held}, patches/ holds {wanted}")

    result("config", rom_toml(version) in names and patches_toml(version) in names,
           f"{rom_toml(version)} and {patches_toml(version)} covered")

    result("stable", build_id(root, version)[0] == value,
           f"twice the same number, 0x{value:08X}")

    binary = root / "patches" / "patches.bin"
    if binary.is_file():
        result("binary", "patches/patches.bin" not in names,
               f"{binary.stat().st_size} bytes of compiled patches left out "
               "(the compiler differed between the two machines, #67)")
    else:
        result("binary", "patches/patches.bin" not in names,
               "the compiled patches are left out (none built here)")

    # Every covered file must count: change one byte in a copy of each, and the
    # number must change. This is what makes the identity a check at all.
    with tempfile.TemporaryDirectory(prefix="bm64-build-id-") as tmp:
        copy = pathlib.Path(tmp)
        copy_covered(root, version, copy)
        if build_id(copy, version)[0] != value:
            result("copy", False, "a copy of the covered files gives another number")
        else:
            changed = []
            for path in covered_files(copy, version):
                original = path.read_bytes()
                path.write_bytes(original + b"// coop: build id check")
                if build_id(copy, version)[0] == value:
                    changed.append(path.relative_to(copy).as_posix())
                path.write_bytes(original)
            result("sensitive", not changed,
                   f"a change in any of the {len(files)} files changes the number"
                   if not changed else f"ignored a change in {changed}")

    passed = sum(1 for ok in results if ok)
    print(f"{passed}/{len(results)} checks passed")
    print("RESULT: " + ("ALL PASSED" if passed == len(results) else "FAILURES"))
    return 0 if passed == len(results) else 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("mode", nargs="?", default="show", choices=("show", "check"))
    parser.add_argument("--version", default="us", choices=("us", "eu"))
    parser.add_argument("--number", action="store_true")
    parser.add_argument("--files", action="store_true")
    parser.add_argument("-h", "--help", action="store_true")
    args = parser.parse_args(argv)
    if args.help:
        print(__doc__.strip())
        return 0
    root = pathlib.Path(__file__).resolve().parent.parent
    if args.mode == "check":
        return check(root, args.version)
    return show(root, args.version, args.number, args.files)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
