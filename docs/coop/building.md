# Building and verifying the co-op fork

Upstream's [BUILDING.md](../../BUILDING.md) still describes the full build.
This page holds what the co-op fork adds: the verify script, what can be
checked without a ROM, and the workarounds a newer toolchain needs.

## The pipeline and where the ROM comes in

| Step | Input | Output (gitignored) | Needs the ROM |
|---|---|---|---|
| `make -C patches` | `patches/*.c`, headers from `lib/sf64`, `lib/rt64`, `lib/N64ModernRuntime` | `patches/patches.elf` | no |
| `./N64Recomp patches.toml` | `patches.elf`, `BM64Syms/*.toml` | `RecompiledPatches/`, `patches/patches.bin` | no |
| `./N64Recomp bm64.us.toml` | `bm64.decomp.us.z64` | `RecompiledFuncs/` | yes |
| `./RSPRecomp aspMain.toml` | `bm64.decomp.us.z64` | `rsp/aspMain.cpp` | yes |
| `cmake --build … --target BM64Recompiled` | all of the above, `src/`, `lib/` | the executable | yes, to link |

`bm64.decomp.us.z64` is the **decompressed** 1.0 US ROM: build
[Bomberhackers/bm64](https://github.com/Bomberhackers/bm64) from a dumped US
cartridge to get it. The game itself is then given the normal US ROM in its
launcher. A Europe ROM does not fit `bm64.us.toml`.

Keep the decompressed ROM outside the repository:
`~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.us.z64`, or set
`BM64_DECOMP_ROM`. `tools/verify.sh` links it into the checkout as
`bm64.decomp.us.z64` (`*.z64` is gitignored; the pre-commit hook and the
owner's push guard refuse ROMs anyway).

## tools/verify.sh

The one check of a branch. Run it in the background and read the summary:

```bash
tools/verify.sh            # every stage; the ROM stages skip without a ROM
tools/verify.sh --quick    # submodules, configs, patches: about a minute
tools/verify.sh --full     # a missing ROM is a failure, not a skip
```

Stages: `submodules`, `configs` (the three TOML files parse), `n64recomp`,
`patches`, `recomp`, `build`. Logs and `summary.txt` go to
`~/dev-scratch/BM64Recomp-coop/verify/<stamp>/`; the CMake folder stays in
`~/dev-scratch/BM64Recomp-coop/build/<checkout name>/` so later runs are
incremental. Exit code 0 only if no stage failed.

Without the ROM, `build` compiles every object file `BM64Recompiled` links
(549 on 2026-10-18: host code in `src/`, librecomp, ultramodern, RT64, RmlUi,
`RecompiledPatches`) except the ROM's own output and
`src/main/register_overlays.cpp`, which includes
`RecompiledFuncs/recomp_overlays.inl`. CMake refuses to configure without
`RecompiledFuncs/*.c` and `rsp/aspMain.cpp`, so the script writes empty stubs
there for the run and deletes them afterwards. A compile-only pass says nothing
about linking or running.

CI (`.github/workflows/coop-ci.yml`) runs the same script without a ROM.

## N64Recomp

`tools/n64recomp.sh` builds N64Recomp and RSPRecomp at the commit upstream's
CI pins (`N64RECOMP_COMMIT` in `.github/workflows/validate.yml`) into
`~/dev-scratch/BM64Recomp-coop/n64recomp/<commit>/`, once, and symlinks both
into the repository root, where the TOML files and `CMakeLists.txt` call them.

## Newer toolchains

Upstream's CI builds with clang-15 on Ubuntu 22.04. With clang 16+ and
libstdc++ 15 two things break, and `tools/verify.sh` works around both
without touching upstream files:

- clang makes `-Wincompatible-pointer-types` an error, which
  `patches/required_patches.c` trips; the patches are compiled with
  `-Wno-error=incompatible-pointer-types` (`PATCHES_C_COMPILER`), the clang-15
  behaviour.
- RmlUi's debugger uses `uint32_t` without including `<cstdint>`; the host
  build adds `-include cstdint` (`VERIFY_CXX_FLAGS`).

## Running the game

Not run by the co-op fork yet. When a session does: the game keeps saves,
config and mods in `~/.config/BM64Recompiled` on Linux, which is the
owner's. A test run starts the game from a working folder under scratch
that holds `assets/`, `recompcontrollerdb.txt` and an empty `portable.txt`:
`zelda64::get_app_folder_path()` (`src/game/config.cpp`) then keeps saves,
config and mods in the current directory. A test run never stops a game the
owner is running.
