# Building, verifying and running the co-op fork

Upstream's [BUILDING.md](../../BUILDING.md) still describes the US build.
This page holds what the co-op fork adds: the Europe build, the verify and run
scripts, what can be checked without a ROM, and the workarounds a newer
toolchain needs.

## Versions

| Version | ROM | Build | Verified (2026-10-18) |
|---|---|---|---|
| US 1.0 | `BOMBERMAN64`, `NBME` | `-DBM64_VERSION=us` (default, upstream's build) | compile only: no US ROM on the development machine |
| Europe | `BOMBERMAN64E`, `NBMP`, revision 0 | `-DBM64_VERSION=eu` | `tools/verify.sh --full --version eu` (recompiled and linked), `tools/run.sh`: boots, plays the attract demo's stages for 100 s, game time 100.0 % of wall-clock time |

One executable runs one version: a Europe build accepts only the Europe ROM,
a US build only the US one. Online co-op runs in lockstep, so both players
need the same version.

## The pipeline and where the ROM comes in

| Step | Input | Output (gitignored) | Needs the ROM |
|---|---|---|---|
| `tools/unpack_rom.py <rom>` | the US or Europe ROM | `bm64.decomp.<us\|eu>.z64`, `bm64.<us\|eu>.z64` in scratch | yes |
| `tools/europe.py configs` (Europe only) | the US configs, `BM64Syms/` | `bm64.eu.toml`, `aspMain.eu.toml`, `patches.eu.toml`, `dump.eu.toml`, `data_dump.eu.toml` | no |
| `make -C patches` | `patches/*.c`, headers from `lib/sf64`, `lib/rt64`, `lib/N64ModernRuntime` | `patches/patches.elf` | no |
| `./N64Recomp patches.toml` (Europe: `patches.eu.toml`) | `patches.elf`, the symbol files | `RecompiledPatches/`, `patches/patches.bin` | no |
| `./N64Recomp bm64.us.toml` (Europe: `bm64.eu.toml`) | `bm64.decomp.<us\|eu>.z64` | `RecompiledFuncs/` | yes |
| `./RSPRecomp aspMain.toml` (Europe: `aspMain.eu.toml`) | `bm64.decomp.<us\|eu>.z64` | `rsp/aspMain.cpp` | yes |
| `cmake --build … --target BM64Recompiled` | all of the above, `src/`, `lib/` | the executable | yes, to link |

The recompiler reads the **decompressed** ROM: the ROM as is, followed by
every overlay unpacked (the image Bomberhackers/bm64's `bm64decompress`
builds). `tools/unpack_rom.py` makes it from either release, in any byte
order, with the standard library only:

```bash
python3 -I tools/unpack_rom.py "<folder>/Bomberman 64 (Europe).n64"
# ~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.eu.z64  the decompressed image
# ~/dev-scratch/BM64Recomp-coop/rom/bm64.eu.z64         the ROM in .z64 order
```

It detects the version from the header, refuses any other ROM (another game,
another revision, a damaged one: the header checksum is recomputed), checks
every overlay's unpacked size against the table (`tools/bm64_overlays.txt`,
offsets and sizes from `bm64decompress`, the same for both versions) and
compares the Europe image with its known SHA-1. The decompilation publishes no
hash for the US image, so a US image is only printed. The output stays in
scratch: the ROM folder is read-only, the checkout gets none of it (`*.z64`
is gitignored; the pre-commit hook and the owner's push guard refuse ROMs).
`tools/verify.sh` links the image into the checkout for the recompiler. The
game itself is given the normal ROM, never the decompressed one.

## The Europe build

The Europe code has the US layout, with three differences that matter to the
build:

- **Five boot functions** are elsewhere: `n64main` is 4 bytes longer (it sets
  `osTvType` to PAL), so `Main_SetPanOverride` and `Main_GetTvType` start 4
  later, and 0x60 bytes of padding sit before `osInitialize`, so it and
  `osSetThreadPri` start 0x60 later. With US bounds the recompiled boot code
  jumps to labels that do not exist and misses `osInitialize`, which the
  runtime replaces by name.
- **Main data from US 0x802A2F78 and all of main bss sit lower** (8, then
  0x10 bytes; [design.md](design.md), *Addresses*). The patches use US
  symbol names; recompiled against the Europe symbol file they reach the
  Europe addresses. Five `.rodata` symbols in US 0x802A2F64–0x802A2F78 have
  no known Europe address and are left out, so a patch that uses one fails
  to recompile instead of reading the wrong word.
- **PAL timing**, see *Europe speed* below.

`tools/europe.py` holds these differences. `configs` writes the Europe
configs into the checkout root, each derived from its US counterpart so that
upstream changes carry over; all five are gitignored (`*.eu.toml`):
`bm64.eu.toml` (reads `bm64.decomp.eu.z64` and `dump.eu.toml`, plus the PAL
hook), `aspMain.eu.toml`, `patches.eu.toml` (resolves symbols in the two
files below), `dump.eu.toml` (`BM64Syms/dump.toml` with the Europe function
bounds) and `data_dump.eu.toml` (`BM64Syms/data_dump.toml` with the Europe
addresses). `check <image>` tests them on the Europe image, read only:

- `functions`: with the Europe bounds every function ends in `jr $ra`, but
  the secure-call stubs and ten known ones (exception code, tail jumps);
- `tables`: each of the 221 jump tables the code uses (main code and
  overlays) is where its symbol's Europe address says;
- `bss`: the boot path clears main bss at the Europe addresses;
- `patches`: every data address a patched function uses is where its Europe
  original uses it (145 of 147; the other two, which only the patches use,
  are used by other Europe functions);
- `sites`: each of the 68,255 `[[patches.instruction]]` sites holds the kind
  of word the patch expects, and the PAL hook site the expected code.

With US addresses (no shifts) `tables`, `bss` and `patches` fail; without
the function fixes `functions` fails.

The build chooses its version when configuring:

```bash
python3 -I tools/europe.py configs
ln -sf ~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.eu.z64 bm64.decomp.eu.z64
./N64Recomp bm64.eu.toml && ./RSPRecomp aspMain.eu.toml
echo eu > RecompiledFuncs/bm64_version.txt
cmake -S . -B <build folder> -G Ninja -DBM64_VERSION=eu …
cmake --build <build folder> --target BM64Recompiled
```

`src/coop/version.cmake` reads `BM64_VERSION` (`us` by default, so upstream's
build is unchanged): a Europe build recompiles the patches with
`patches.eu.toml` and registers only the Europe ROM in `src/main/main.cpp`
(`BOMBERMAN64E`, `game_id` `bm64_eu`, so it keeps its own stored ROM and
saves; `mod_game_id` stays `bm64`, the id the mod menu lists). The checkout's
recompiled output holds one version at a time: configuring stops when
`RecompiledFuncs/bm64_version.txt` names the other version, and a stamp per
version in `RecompiledPatches/` makes a build recompile the patches when the
other version ran last. `tools/verify.sh` does all of this.

## tools/verify.sh

The one check of a branch. Run it in the background and read the summary:

```bash
tools/verify.sh                  # every stage; the ROM stages skip without a ROM
tools/verify.sh --quick          # submodules, configs, patches, europe: about a minute
tools/verify.sh --full           # a missing ROM is a failure, not a skip
tools/verify.sh --version eu     # the Europe build (default: the decompressed
                                 # image that exists, US first, else US)
```

Stages: `submodules`, `configs` (`tools/europe.py configs`, then the US and
Europe configs parse), `n64recomp`, `patches`, `europe` (Europe only:
`tools/europe.py check` on the image), `recomp`, `build`. Logs and
`summary.txt` go to `~/dev-scratch/BM64Recomp-coop/verify/<stamp>/`, the
summary's first line names the version; the CMake folder stays in
`~/dev-scratch/BM64Recomp-coop/build/<checkout name>/` (Europe:
`<checkout name>-eu/`) so later runs are incremental. Exit code 0 only if no
stage failed.

Without the ROM, `build` compiles every object file `BM64Recompiled` links
(549 on 2026-10-18: host code in `src/`, librecomp, ultramodern, RT64, RmlUi,
`RecompiledPatches`) except the ROM's own output and
`src/main/register_overlays.cpp`, which includes
`RecompiledFuncs/recomp_overlays.inl`. CMake refuses to configure without
`RecompiledFuncs/*.c` and `rsp/aspMain.cpp`, so the script writes empty stubs
there for the run and deletes them afterwards. A compile-only pass says nothing
about linking or running.

CI (`.github/workflows/coop-ci.yml`) runs the same script without a ROM, so
it checks the US build compile-only.

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

## Running the game: tools/run.sh

The game keeps saves, config and mods in `~/.config/BM64Recompiled` on
Linux, which is the owner's. A test run never uses it and never stops a game
the owner is running. `tools/run.sh` starts a built executable unattended:

```bash
tools/run.sh --version eu --until-overlay 0x20 --timeout 60   # boot check
tools/run.sh --version eu --timeout 100 --env BM64_COOP_LOG_FRAMES=50
```

Each run gets a folder `~/dev-scratch/BM64Recomp-coop/runs/<stamp>-<version>/`
that holds everything it writes: an empty `portable.txt` (so
`zelda64::get_app_folder_path()`, `src/game/config.cpp`, keeps config, saves
and mods there), the stored ROM `bm64_<version>.z64` (what the launcher writes
after a ROM is chosen; taken from `bm64.<version>.z64` in scratch), links to
`assets/` and `recompcontrollerdb.txt` in the checkout, `game.log` (stdout and
stderr) and `summary.txt`. The script stops only the process it started
(TERM, then KILL) and afterwards checks that nothing under
`~/.config/BM64Recompiled` was created or changed.

Options: `--version`, `--until-overlay ID` (success once the game loads that
overlay), `--timeout` (default 60 s), `--exe`, `--rom`, `--env NAME=VALUE`
(repeatable), `--audio` (sound on the default device; by default SDL's dummy
driver, which consumes audio at the real rate). Exit status: 0 overlay
reached (or ran until the timeout without `--until-overlay`), 1 timeout
before the overlay, 2 the game exited or crashed, 3 the owner's config
folder changed, 4 bad setup. The game opens its window on the desktop; there
is no headless mode.

Debug switches, all off unless set (`src/coop/debug.cpp`):

- `BM64_COOP_AUTOSTART=1`: when the launcher first shows, do what its Start
  button does (start the stored ROM, hide the launcher). `tools/run.sh` sets it.
- `BM64_COOP_LOG_OVERLAYS=1`: one line `coop: overlay 0xID` per overlay load,
  from a hook at the loader's entry (`func_8022691C`, `bm64.us.toml`).
  `tools/run.sh` sets it. The boot path loads 0x20 (title and main menu); left
  alone, the game then cycles through the attract demo (0x1B) and its stages
  (0x38, 0x50, 0x58, …) and back to 0x20 (the overlay map is in
  [design.md](design.md)).
- `BM64_COOP_LOG_FRAMES=N`: every N game frames, from a hook on
  `func_8023A208`, which the main loop calls once per frame:
  `coop: frame <frames> <ms> <retraces> <audio frames> <audio queue µs> <audio skips>`.
- `BM64_COOP_PAL60=1`: turn the Europe build's PAL pacing off.

Captures (screenshots, clips) are fine for debugging but stay in the run
folder: never in commits, issues or pull requests.

## Europe speed

The game runs one frame per VI retrace (its scheduler, `__scMain`, passes each
retrace on to the game loop and the audio thread), and the Europe release is
made for a 50 Hz VI. ultramodern's VI runs at a fixed 60 Hz
(`lib/N64ModernRuntime/ultramodern/src/events.cpp`), so unchanged the Europe
build runs 20 % fast. The fix is a hook that `tools/europe.py` adds to
`bm64.eu.toml` in `__scMain`, after it receives a message: one retrace
message (VIDEO_MSG) in six becomes message 0, which the loop ignores
(`coop_pal_skip_retrace`, `src/coop/debug.cpp`). The game, its scheduler
clients and the audio then see 50 retraces a second. The screen is still
presented at 60 Hz (each game frame shows for one or two VIs).

Measured on 2026-10-18 with `tools/run.sh --version eu --timeout 100 --env
BM64_COOP_LOG_FRAMES=50`, over the 93 s after the first 10 s (title, attract
demo and its stages), audio on SDL's dummy driver:

| | game frames/s | retraces/s | game time / wall-clock | audio from the game | host audio queue | host sample skips |
|---|---|---|---|---|---|---|
| without the fix (`BM64_COOP_PAL60=1`) | 60.00 | 60.00 | 120.0 % | 37,433 frames/s | 83–105 ms | 1095 |
| with the fix | 50.00 | 50.00 | 100.0 % | 32,416 frames/s | 8–17 ms | 0 |

Game time is game frames at 50 per second. Without the fix the game makes
17 % more audio than plays, and the host drops samples whenever its queue
passes 100 ms; with it the queue stays short and nothing is dropped.
