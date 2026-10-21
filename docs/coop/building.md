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
need the same version. The launcher's message for a refused ROM names the
version the build wants (`src/ui/ui_launcher.cpp`, #11): a US build given the
Europe ROM says it needs the NTSC-U version, as upstream's does. A Europe
build given the US ROM gets librecomp's "not the correct game", since its
header name is compared over the 12 characters of `BOMBERMAN64E`; the Europe
build adds that it runs the Europe (PAL) version only. Read from the code and
compiled, not shown in a run: the harness cannot choose a ROM in the launcher.

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

### The patched functions against their Europe originals (#11)

`check` proves that the patches reach the right data, not that the Europe
originals do what the US code the patches replace does. Read on 2026-10-19
(the Europe disassembly against the decompilation's C, which builds the US
ROM byte for byte, and against the patches):

- **Per frame:** `func_80225840` (the main loop), `Gfx_InitGfx`,
  `Gfx_Render` and `Gfx_EndRender` are the US code instruction for
  instruction but for the address shifts above (`D_802A5300`/`D_802B36D0`
  appear as 0x802A52F0/0x802B36C0, `D_802AC5C0` as 0x802AC5B0, the gfx
  queues and buffer IDs 0x10 lower, `D_802A4840` as 0x802A4830). The
  Europe game draws the same 320×240 frame: framebuffers 0x25800 bytes
  apart, scissor 320×240, the motion-blur copy over 240 lines, screen
  coordinates 8, 6, 304×228 set on the eighth frame, the same four
  `osViSetSpecialFeatures` calls. `func_80242270` (the HUD) has the patch's
  constants (8 time digits from 10,000,000, 2 gem digits, digit row 22 to
  40) and reads the session block at 0x802AC600 (US 0x802AC610).
- **All 59 patched functions** have the US size in Europe (the Europe bounds
  differ only for the five boot functions above, none of them patched), and
  a scan of their immediates found no timing constant (no 50 or 60 frame
  rate, no PAL line count).
- `Gfx_Render`'s patch receives the stale message when `validCount >
  msgCount`, the US and Europe code when `validCount >= msgCount`: upstream's
  change, the same in both builds.

No difference beyond addresses was found, so no patch changes for Europe.
PAL in the Europe release is the scheduler's 50 Hz retrace (*Europe speed*)
and `osTvType`, which `n64main` sets to 0 (PAL); the render path is the US
one.

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
tools/verify.sh --scenarios      # also the game's test scenarios (below);
                                 # needs the linked build, opens the game window
```

Stages: `submodules`, `configs` (`tools/europe.py configs`, then the US and
Europe configs parse), `harness` (`tools/test_scenario.py`, `scenario.py`'s
checks on fake runs' logs, #46), `n64recomp`, `patches`, `europe` (Europe only:
`tools/europe.py check` on the image), `recomp`, `build`, and with
`--scenarios` `scenarios` (*The test harness*, below). Logs and
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

## The Windows build

`tools/build-windows.sh [--version us|eu]` cross-compiles the Windows x64
build on Linux (#29), after `tools/verify.sh --full` has made
`RecompiledFuncs/` for that version. Output:
`~/dev-scratch/BM64Recomp-coop/build/win-<version>/` (`BM64Recompiled.exe`,
its `.pdb`, `SDL2.dll`, `dxcompiler.dll`, `dxil.dll`). How it is put
together (`tools/windows-cross.cmake`):

- clang-cl and lld-link against Microsoft's CRT and Windows SDK, splatted by
  [xwin](https://github.com/Jake-Shadle/xwin) into
  `~/dev-scratch/BM64Recomp-coop/win/sdk` (`XWIN_SDK`; fetching them accepts
  Microsoft's licence; never commit them). `RelWithDebInfo`, as upstream's
  Windows CI. Windows finds headers and libraries in any case; the script
  links the four spellings the sources use that the SDK lacks.
- The patches are built by the Linux clang as in the Linux build. Both builds
  have the same build identity and can play together — since #67 that holds
  for any compiler, because the identity is hashed from the sources
  (*The build identity across platforms*, below).
- Build-time tools built for Windows (`file_to_c`) run in Wine. The shaders
  are compiled by RT64's Windows `dxc.exe` (1.7, what upstream's Windows
  builds use) in Wine through `tools/dxc-wine.sh`, which turns the Unix paths
  into Wine paths; the script points the generated `build.ninja` at it.
  RT64's Linux `dxc` is 1.8 and its DXIL differs (a larger `PSV0` part),
  which Wine's D3D12 takes but a real Windows' might not.
- RT64's SDL 2.26.3 headers define `_m_prefetch`, a builtin in newer clang;
  the toolchain sets their guard `__PRFCHWINTRIN_H`.
- The MSVC STL stops a compiler it holds too old (`error STL1000`): the CRT
  xwin splats from the release channel (14.44) asks for clang 19, Ubuntu
  24.04 packages clang 18, and the channel offers no older CRT. For a clang
  below 19 the toolchain sets the STL's own switch,
  `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`, and says so when configuring
  (#29). Installing a newer clang instead would also change the clang that
  builds the patches: that changes the patches' binary, but since #67 no
  longer the build identity.

What the cross build needs beyond the Linux build, and what it does not have
on a fresh Ubuntu 24.04: `clang-cl` (the clang package ships the driver but
not that name: `ln -s "$(command -v clang)" ~/.local/bin/clang-cl`), `wine64`,
`xwin` (a release binary from its repository, kept outside the checkout) and
the splat itself:

```bash
xwin --accept-license --cache-dir ~/dev-scratch/BM64Recomp-coop/win/xwin-cache \
    splat --output ~/dev-scratch/BM64Recomp-coop/win/sdk    # ~630 MB, a few minutes
```

`tools/build-windows.sh` names what of this is missing and stops with exit 4
before it builds anything.

`tools/run.sh --exe <…>.exe` runs it in Wine with its own prefix
(`~/dev-scratch/BM64Recomp-coop/win/prefix`, never the owner's), and
`tools/scenario.py --exe-client <…>.exe` makes it a session's client
(`--exe` the host): the Linux and Windows builds against each other on one
machine. Its log lines end in CR LF; `run.sh` and `scenario.py` take both.
Every build writes its log to `coop.log` in its app folder as well (#57,
[design.md](design.md), *The playtest's logs*): a Windows build started
without a console (from Explorer) there alone, one with a redirect (the
harness in Wine) to both. `WIN_BUILD=<folder>` builds into another folder
than `build/win-<version>/`, which belongs to the checkout that configured it
first (a worktree of its own needs its own).

`tools/playtest-kit.sh` makes portable folders of both builds for a playtest
on two PCs (`docs/coop/playtest.md`): builds of the game, copied by hand,
never uploaded.

### The build identity across platforms

A session's hello refuses a peer whose build identity differs (#26), so two
players need builds that agree. Since #67 it is FNV-1a over the **sources**
the patches are built from — every `patches/*.c`, `*.h`, `*.ld` and the
`Makefile` with its flags, each with its path — then over the recompiler
config of the patches (`patches.toml`, which functions they replace) and the
first 8 hex digits of the SHA-256 of the game's one (`bm64.<version>.toml`,
which holds the hooks). `tools/build_id.py` computes it, `src/coop/version.cmake`
runs the script when configuring and compiles the number in, and
`build_id()` in `src/coop/session.cpp` only reports it. So one commit gives
one number on every machine, and `tools/verify.sh`'s `buildid` stage
(`tools/build_id.py check`) keeps that property: the compiled
`patches/patches.bin` is left out, and a change to any covered file changes
the number.

**Why the sources and not the binary** (#67): until then the identity was
FNV-1a over `patches/patches.bin`, which `make -C patches` builds with
whatever clang the machine has. The fork's two machines measured, at the same
commit `18e4c8b` and the same Europe config: clang 18.1.3 a 46 312-byte
binary and `0xFC44BFB4`, clang 23.1.3 a 46 328-byte one and `0x7F5EB1B6`. The
hello refused the join although the code was the same, so the two people
could not have played together at all. One machine shows it too: the same
sources compiled with an added `-O1` gave a 32 788-byte binary and
`0xD447083F`. A difference that reaches the game's behaviour is still caught,
by the session's per-frame state hash (`coop: session desync`).

The Linux build and the cross-compiled Windows build of one checkout match as
well, which was measured before the change (#29) and now holds by
construction:

- **Measured** (#29, 2026-10-20, one Windows machine with WSL2, Ubuntu 24.04,
  clang 18, Europe, the identity of the day `0xFC44BFB4`): the Linux build
  reports `coop: session build <identity>` and the cross-compiled Windows
  build, run **natively on Windows 11**, the same number. Both sides of each
  session then reached `coop: session lockstep from frame 0`.
- The Windows pair played 3600 frames in lockstep (two portable folders on
  127.0.0.1): the same state hash at frame 0 (`199C12E7`) and at frame 3600
  (`AE6DD052`), no `coop: session desync` and no `coop: session lost`, the
  same twelve overlay loads on both sides, about 49 frames a second at PAL
  50. The Windows runs logged no audio skips, where the same machine's WSL2
  runs log them (#66).
- The Windows build and the Linux build **played against each other** on one
  machine, both ways (*A session between the Windows and the Linux build*,
  below).
- **Not tested:** a build made with Visual Studio (upstream's `BUILDING.md`).
  Another clang version does change the patches' binary (#67, measured on the
  two machines); it no longer changes the identity.

The identity is logged only when a hello happens, so a game that boots alone
never prints it. Two ways to read it without playing:

```bash
# in a session (what the hello compared), from either side's log
grep "coop: session build" <run folder>/game.log

# offline, from the checkout the build was made from (the same number, since
# the build compiles in what this prints); --number gives the value alone
python3 -I tools/build_id.py --version eu
```

Two players compare those numbers before a playtest: both run the offline
command in a checkout of the same commit, with the same `--version`. They
must be equal — if they are not, the two are not on the same commit, or one
of them has an edit in `patches/` or in a config (the `--version eu` configs
are generated, so `tools/europe.py configs` must have run on both).

The game's build, the Windows cross build and the script all read the same
files, so the version a player holds can be read without building anything.
A build whose `tools/build_id.py` failed logs the identity `0x00000000` and
CMake says so with a warning; such a build only ever matches another one in
the same state.

To run the Windows build **natively on Windows** rather than in Wine, make a
portable folder of it on the Windows side (as `tools/playtest-kit.sh` does,
plus the stored ROM): `BM64Recompiled.exe`, `SDL2.dll`, `dxcompiler.dll`,
`dxil.dll`, `assets/`, `recompcontrollerdb.txt`, an empty `portable.txt` and
the ROM as `bm64_<version>.z64`. The switches `tools/run.sh` would set are
plain environment variables (`BM64_COOP_AUTOSTART=1`,
`BM64_COOP_LOG_OVERLAYS=1`, `BM64_COOP_SESSION=…`), and the folder keeps
everything the run writes, so no setting of the person's own is touched. Such
a run opens a window: Windows has no headless wrapper here.

### A session between the Windows and the Linux build

The playtest's real pair is one player on Windows and one on Linux (#29). On a
Windows machine with WSL2 both builds are there, so the pair can be played
without a second PC: the native Windows build in a portable folder on the
Windows side, the Linux build through `tools/run.sh` inside WSL2. The two go
through different network stacks, WSL2's NAT between them.

- The **Windows side joins the WSL2 host** at `127.0.0.1`: WSL2's localhost
  relay carries it to the listening game inside WSL2.
- The **WSL2 side joins the Windows host** at the address of WSL2's default
  route (`ip route | grep default`, the Windows host on the NAT network).
  This needed no firewall rule of its own here, so it says nothing about what
  a host on a real LAN needs: there, Windows asks (`playtest.md`, *Start a
  session*).

Each side is started on its own, as a playtest player would be, with the same
switches `tools/scenario.py` would set (*The test harness*): the scripts of
`tools/scenarios/session-lockstep.txt`, its host part on the host and its
`#client` steps on the client, `BM64_COOP_WARP=0x29:0x13`,
`BM64_COOP_LOG_HASH=1` and `BM64_COOP_LOG_FRAMES=300` on both. Afterwards the
two logs are compared with `tools/compare_logs.py` (below). A game started
without a console keeps its log in `coop.log` in its folder; started with its
output redirected, as a script does, the log is on **stderr**.

**Measured** (#29, 2026-10-20, Europe, one Windows machine with WSL2 on
Ubuntu 24.04):

- Both ways, 448 frames of the `session-lockstep` scripts: the same build
  identity `0xFC44BFB4` on both sides, `coop: session lockstep from frame 0`,
  the same state hash on **every** frame both logged, the same two overlay
  loads, no `coop: session desync` and no `coop: session lost` before the
  scripts ended.
- A longer run with the WSL2 build hosting, 3551 frames hashed by both: all
  equal. About **49.4 frames a second** on either side at PAL 50. Audio: 4
  skips on the WSL2 side, which runs with SDL's dummy driver (`run.sh`
  without `--audio`), 0 on the Windows side, which plays on the real device.
- **Still not run:** two real machines on a LAN, the internet with a
  forwarded port, and macOS.

### Comparing the logs of two machines: tools/compare_logs.py

`tools/scenario.py` compares the two sides itself (`#check same-hashes`), but
only for two games it starts on one machine. A session across machines leaves
one log on each PC instead:

```bash
python3 -I tools/compare_logs.py <host log> <client log>
```

It cuts both logs at `coop: input end` (once one game is stopped the other
loses the session), and reports the build identity of each side, the state
hash of every frame both logged with the first one that differs, the overlay
loads, the frames a second and audio skips of each side
(`BM64_COOP_LOG_FRAMES`), and any `coop: session desync`, `lost` or `refused`.
Exit 0 only when the identities agree, no frame differs and no such event was
logged; two logs without `BM64_COOP_LOG_HASH` (a playtest build) still give
the rest. Either side's `game.log`, `coop.log` or captured stderr will do,
with Unix or Windows line ends.

## Running the game: tools/run.sh

The game keeps saves, config and mods in `~/.config/BM64Recompiled` on
Linux, which is the owner's. A test run never uses it and never stops a game
the owner is running. `tools/run.sh` starts a built executable unattended:

```bash
tools/run.sh --version eu --until-overlay 0x20 --timeout 60   # boot check
tools/run.sh --version eu --timeout 100 --env BM64_COOP_LOG_FRAMES=50
```

Each run gets a folder `~/dev-scratch/BM64Recomp-coop/runs/<stamp>-<version>-<suffix>/`
that holds everything it writes: an empty `portable.txt` (so
`zelda64::get_app_folder_path()`, `src/game/config.cpp`, keeps config, saves
and mods there), the stored ROM `bm64_<version>.z64` (what the launcher writes
after a ROM is chosen; taken from `bm64.<version>.z64` in scratch), links to
`assets/` and `recompcontrollerdb.txt` in the checkout, `game.log` (stdout and
stderr, gamescope's and Wine's lines among them), the game's own `coop.log`
(its lines only, #57) and `summary.txt`. The script stops only the process it started
(TERM, then KILL) and afterwards checks that nothing under
`~/.config/BM64Recompiled` was created or changed.

Options: `--version`, `--until-overlay ID` (success once the game loads that
overlay), `--until-log TEXT` (success once a line of the log starts with
TEXT), `--timeout` (default 60 s), `--exe`, `--rom`, `--env NAME=VALUE`
(repeatable), `--input FILE` (an input script, copied into the run folder;
*The test harness*, below), `--saves DIR` (start with a copy of DIR, e.g. an
earlier run's `saves/`, as the run's save folder), `--audio` (sound on the default device; by
default SDL's dummy driver, which consumes audio at the real rate),
`--window` and `--wrapper gamescope|xvfb` (the headless display, below). Exit
status: 0 overlay reached or log line seen (or ran until the timeout
without either), 1 timeout before them, 2 the game exited or crashed, 3 the
owner's config folder changed, 4 bad setup. The game runs headless, in a
640x480 display of its own, so no window opens on the desktop (owner,
2026-10-18); `--window` opens one. Each run is its own process group, which
the stop ends whole (the wrapper and Wine start children of their own).

Which wrapper gives that display is chosen per machine (#63): `gamescope
--backend headless` when `gamescope` is on `PATH`, else `env -u
WAYLAND_DISPLAY xvfb-run -a -s "-screen 0 640x480x24"` (Ubuntu 24.04 packages
no gamescope, `apt install xvfb` gives the other), else the run fails with
exit 4 naming both. `--wrapper gamescope|xvfb` forces one, which is how a
machine with both compares them; the run's summary line names the one used
(`display: xvfb`, or `window`). Under Xvfb, `WAYLAND_DISPLAY` must go: with
it SDL takes the desktop's Wayland display and opens a window there instead
of drawing into Xvfb.

Debug switches, all off unless set (`src/coop/debug.cpp`):

- `BM64_COOP_AUTOSTART=1`: when the launcher first shows, do what its Start
  button does (start the stored ROM, hide the launcher). `tools/run.sh` sets it.
- `BM64_COOP_LOG_OVERLAYS=1`: one line `coop: overlay 0xID` per overlay load,
  from a hook at the loader's entry (`func_8022691C`, `bm64.us.toml`).
  `tools/run.sh` sets it. The boot path loads 0x20 (first the logos, later
  the title and main menu); left alone, the game then cycles through the attract demo (0x1B) and its stages
  (0x38, 0x50, 0x58, …) and back to 0x20 (the overlay map is in
  [design.md](design.md)).
- `BM64_COOP_LOG_FRAMES=N`: every N game frames, from a hook on
  `func_8023A208`, which the main loop calls once per frame:
  `coop: frame <frames> <ms> <retraces> <audio frames> <audio queue µs> <audio skips>`.
- `BM64_COOP_PAL60=1`: turn the Europe build's PAL pacing off (and its
  refresh-rate hint back to 60, *Presenting 50 game frames a second*).
- `BM64_COOP_CRASH=1`: crash on purpose when the title loads (a test of the
  crash log, #29).

**Z skips the story intro** (#77). It is not a switch and needs nothing set:
start a new game the normal way — title, main menu, Adventure, **pick a
file** — and then press **Z on the controller** while that file's opening
cutscene (overlay 0x1A) plays. The game goes to the world map (0x21) at once.

Where it deliberately does **nothing** (the person running the session,
2026-10-21): before a file is picked. In the logos, in the attract demo and in
the title's main menu, Z is ignored, so the file select and the Co-op menu are
reached the normal way and **the file picked is the file played**
(`skip-intro-title`). In a room Z stays the game's own button
(`skip-intro-room`), and in an **online session** the skip is refused
(`coop: skip intro: not in an online session`), since the step is one
machine's alone and the other game would play on ([design.md](design.md),
*The session core*).

Not to be confused with `BM64_COOP_NO_DEMO=1` (#49, #51, above), which keeps
the attract demo from ever starting so a *scripted* run's warped room plays
with its enemies. Z is for the runs by hand.

Two things about how it works. The button is read where the game reads its
pads (`recomp::get_n64_input`, `src/game/controls.cpp`), not from this
machine's keyboard, so a scenario presses it with an ordinary `pad 0 Z` step.
And the step is the input script's `scene` step (`func_8023AE6C`) **in the
running session**, not `BM64_COOP_WARP`'s: the warp starts a new file
(`func_8023DA04`) and would throw away the file the person picked. A cutscene
can still win a scene change of its own, so the skip keeps asking while the
intro plays (at most 6 steps, 60 frames apart) until the world map loads. Each
step logs `coop: skip intro: to the world map, keeping the file (step N)`.

**The hint on screen.** Nobody can guess the button, so while the story intro
plays the game shows **PRESS Z TO SKIP**, low and centred
(`patches/coop_intro_hint.c`). It is drawn the way the co-op HUD draws its
icons (#53, `coop_hud.c`): the text is painted once into an RGBA5551 texture
from a 5×7 pixel font, white with a black outline so it reads over any scene,
and a draw object of the game's own pool (`func_8022A328`) puts it on screen
at twice that size. It only draws — no game state is written, so the
lockstep's hash is untouched.

When it shows is decided on the host side, in the same place as the skip
itself (`in_story_intro()` and no online session, `src/coop/harness.cpp`), and
only a *change* is passed to the patch (the per-frame hook's bit 8), which
registers the draw object and frees it again. So it appears with the opening
cutscene, goes away when the world map loads or Z was pressed, and **never
appears in an online session**, where the skip is refused anyway. It logs
`coop: hint on x=68 y=196` and `coop: hint off` (and
`coop: hint none: no free draw object` if the pool is full), which is what the
scenarios check: `skip-intro` wants the pair around the cutscene,
`skip-intro-title` and `skip-intro-room` that it never appears.

A run's own `general.json` turns background input off, so a controller in
use elsewhere does not press the test game's buttons; a key or button
pressed while a test game has the focus still reaches it.

Captures (screenshots, clips) are fine for debugging but stay in the run
folder: never in commits, issues or pull requests.
Capture only the game's window: a capture of the active window takes
whatever window has the focus once the game loses it. A headless run is
captured through its own gamescope: the game process (its working folder is
the run folder) has `GAMESCOPE_WAYLAND_DISPLAY` in its environment, and
`XDG_RUNTIME_DIR=/run/user/$(id -u) GAMESCOPE_WAYLAND_DISPLAY=<that display>
gamescopectl screenshot <run folder>/shot.png` saves what it shows (#53).

## The test harness

`src/coop/harness.cpp` drives and observes the game in an unattended run.
Everything is off unless a switch asks for it:

- `BM64_COOP_INPUT=<file>` (`tools/run.sh --input`): an input script drives
  pads 0–3, which all report connected; the real keyboard and controller are
  ignored. One step per line, `#` starts a comment:
  - `overlay ID`: wait for the next load of that overlay (the n-th such step
    waits for the n-th load);
  - `wait N`: wait N game frames;
  - `pad N BUTTONS [X Y]`: from now on pad N holds the buttons (`A`, `B`, `Z`,
    `START`, `L`, `R`, `DU` `DD` `DL` `DR`, `CU` `CD` `CL` `CR`, joined with
    `+`, or `-` for none) and the stick at X, Y in −1..1;
  - `probe LABEL`: log `coop: probe LABEL`, a `coop: state` line (overlay,
    S, flow flags, lives), a `coop: file current=N` line (the file the game
    would save into, `D_802A1DB4`: 0–2, or −1 from the boot until the file
    select sets it — a save takes it unchecked, so −1 writes below the EEPROM
    mirror, which is what `skip-intro` checks, #77), the stick reading of pads 0 and 1, a `coop:
    camera` line (its eye, its yaw and pitch in tenths of a degree and its
    distance from the point it looks at) and one `coop: player slot=…` line per player in play
    (controlType, hearts, bombs, fire, flags, position in tenths of a world
    unit, `ghost` 1 for the ghost flag, bit 0x200 of +0x68, and the model's
    `alpha`, 255 for a living player and 128 for a ghost, #22) and, with `BM64_COOP_LOG_PLAYERS=1`, a `coop: item from=SLOT
    type=T pos=…` line for the item a hit knocked out last while it is in
    the room (#36);
  - `tile SLOT X Z`: put the player in that slot on tile X, Z of the room at
    the floor level it stands on (`func_802511EC`, as a room entry does), and
    log `coop: tile slot=… from=… to=…`. With player 2 on, a tile step of
    slot 0 is a room entry: player 2 stays frozen and cannot be hurt until
    it is placed next to player 1 (#36; at once once player 1 has been free
    for 60 frames, #55); walk player 1 instead when player 2 must stay where
    it is;
  - `hearts SLOT N`: set the player's hearts to N (0–9), as a heart pickup
    does, and log `coop: hearts slot=… from=… to=…`;
  - `save FILE`: save the live progress block into file 0–2 and the EEPROM,
    as the pause menu's save does (`func_802947AC`), and log
    `coop: save file=… used=… time=…` (the file's "used" bit and play time);
  - `drop SLOT`: knock a heart out of the player, as a hit that leaves
    hearts does (`func_8027B39C`; it flies off in a random direction), and
    log `coop: drop step slot=…` (#22, `ghost-heart`);
  - `scene ID [ENTRY]`: go to overlay ID with entry code ENTRY (default
    0x10, a stage start from the world map) in the running session, as a
    room's exit does (`func_8023AE6C`), and log `coop: scene overlay=…`
    (#22, `ghost-rooms`);
  - `freeze 0|1`: freeze or release the story player as a cutscene does
    (`func_802452E8`, with the idle animation), and log `coop: freeze step
    on=…` (#22, `ghost-scene`);
  - `log TEXT`, and `end`, which logs `coop: input end`.

  A bad line logs `coop: input error …` and the script does not run. The
  script steps once per game frame, from the per-frame hook of
  `func_8023A208`; a pad change reaches the game's pad history (Europe
  `D_802AC418`, filled by `func_8023A104` at the top of the next frame) one
  frame later, as a press (the edge the menus read) in its first frame.

  **Driving the menus** (#31, `title-menu`): the boot's first load of overlay
  0x20 only shows the logos and goes on to the opening demo (0x1B) whatever
  is pressed: START there does not open the main menu. START in the demo
  (60 frames after its load) brings back the title, a second load of 0x20:
  wait for it with a second `overlay 0x20` step, then START opens the main
  menu and A (60 frames later) takes its first entry, Adventure, into the
  file select (0x1F). In a run without saves, A picks the first file and A
  again starts it: the new file's opening cutscene (0x1A). A tap is
  `pad 0 START`, `wait 4`, `pad 0 -`. Nothing in the game's own menus cuts
  that path short, and the menus have to be driven as above. What can be cut
  is the **opening cutscene** at the end of it: **Z** there goes to the world
  map (*Z skips the story intro*, above, and the `skip-intro` scenarios, which
  drive exactly this path and then press Z, #77).
- `BM64_COOP_WARP=ID[:ENTRY[:FILE]]`: 30 frames after the title (overlay
  0x20) loads, start a story session as the file select does, for a new file
  (`func_8023DA04(0)`) or, with FILE, by loading file 0–2
  (`func_80293EA0`, `func_80293E1C`, `func_8023DA04(1)`; it logs the file's
  "used" bit and play time), and ask for overlay ID with entry code ENTRY
  (default 0x10, the code the world map starts a stage with;
  `func_8023AE6C`). Room
  0x28 opens with the tutorial's dialogue, which waits for A; room 0x29 with
  entry 0x13 (room 0x28's exit) starts with player 1's short entry walk and
  then plays.
  The title, as the warp leaves it, starts the attract demo
  (`func_80297ADC`, #49, #51): the warped room runs as the demo, story flag
  0x4 set, so the map's enemy tiles become start points for slots 1–3
  (`func_8026F5E8`: no enemies), the timer is off and START goes back to
  the title.
- `BM64_COOP_NO_DEMO=1`: the attract demo's setup is skipped (a hook at
  `func_80297ADC`), so a warped room plays as a game's does: its enemies
  spawn, and player 1 is held longer at the start (room 0x29: still frozen
  345 frames after the load, free at 600; as the demo, 300 were enough).
  The enemy scenarios use it (`enemy-ghost`,
  `enemy-range`, `session-enemy-range`); the older ones run without enemies.
  The enemy lines (`coop: enemy aim|spawn|despawn|clear …`) come from
  `patches/coop_enemy.c` whenever player 2 is on ([design.md](design.md),
  *Enemies and two players*).
- `BM64_COOP_LOG_PLAYERS=1`: one line per room init, spawn (and the model
  asset it gets), placement on a tile, bomb, hit, the heart a hit knocks
  out (`drop … item=4`), an item picked up (`pickup … item=T`), an item a
  player passes through (`pass … item=T`: a heart for a full player, #36,
  any item for a ghost, #22), death, a ghost's hit (`ghost hit`), cutscene
  freeze, scene load, stage clear and room teardown, from hooks in
  `bm64.us.toml`.
- `BM64_COOP_P2=1|4`: player 2 in that slot in story rooms, driven by pad 1
  ([design.md](design.md), *Player 2 in a story room*). Its own lines
  (`coop: place …`, `trigger`, `down … ghost`, `revive … why=…`, `still
  down`, `stays down`, `heart found`, `ghost fell`) come from
  `patches/coop_player2.c` whenever it is on.
- `BM64_COOP_CAMERA=SLOT`: the camera follows the player in that slot (0–4)
  while it plays (an actor, controlType 1 or 2), and the game's choice (S)
  otherwise; `-1` is always the game's choice. Without it, a live session's
  client follows player 2 in slot 4, any other game the game's choice
  ([design.md](design.md), *The camera*). Each change logs `coop: camera
  follows slot=…` (`patches/coop_camera.c`). A camera that follows slot 4
  takes its C-buttons from pad 1, player 2's (#33).

Patches print with `recomp_printf` (stdout), the host with stderr; with any
of these switches stdout is line-buffered, so both stay in order in
`game.log`. The warp, the probes and the tile steps run in
`patches/coop_harness.c`, called from the frame hook on a copy of the
registers with the stack pointer moved down. Such a copy must point its odd
float registers at itself (`c.f_odd`, as every co-op hook in `bm64.us.toml`
does): copied as is, they stay the original context's, and double
arithmetic in a game function called on the copy reads the wrong halves
(#13).

A **scenario** (`tools/scenarios/*.txt`) is an input script with `#env`
lines (switches for the game) and `#check` lines. `tools/scenario.py` runs
each through `tools/run.sh` (until `coop: input end`) and checks the log:

```bash
tools/scenario.py --version eu p2-slot4                       # one scenario
tools/scenario.py --version eu tools/scenarios/*.txt          # all of them
tools/scenario.py --version eu --env BM64_COOP_P2=0 p2-slot4  # override an #env
tools/scenario.py --version eu --jobs 1 tools/scenarios/*.txt # one after the other
```

**Several at a time (#71).** `--jobs N` runs N scenarios in parallel;
without it a third of the machine's cores, at most 6, and 1 with `--exe`
(Wine shares one prefix). Each run already has its own run folder
(`run.sh`'s `mktemp -d`) and a session its own free port, so the lanes only
fill the waiting: the game paces itself at 50 frames a second whatever the
CPU could do. Measured on 16 cores, Europe build, the 32 scenarios
(43 runs), **383/383 checks every time**:

| `--jobs` | the stage | the runs' own time together | the longest run |
|---|---|---|---|
| 1 | 11:47 | 636 s | 38.9 s |
| 4 | 3:14 | 653 s | 39.3 s |
| 6 | 2:19 | 675 s | 39.7 s |
| 8 | 1:52 | 701 s | 43.2 s |

The runs themselves barely stretch (+6 % at six lanes), which is what says
the lanes change no check's result: a check reads the game's frames, not the
clock. Eight lanes passed here too; the default stays a third of the cores
so a session scenario's two games and a busy machine keep their headroom.
The output of a scenario is printed whole and in the order the scenarios
were named, so a log reads as before.

Checks: `log REGEX`, `no-log REGEX`, `after REGEX1 REGEX2` (a match of
REGEX2 after the first match of REGEX1), `not-after REGEX1 REGEX2` (none),
`moved P1 P2 SLOT [MIN]` and
`still P1 P2 SLOT [MAX]` (distance between two probes, in world units),
`delta P1 P2 SLOT AXIS OP VALUE` (the move along one axis), `near PROBE A B
MAX [MIN]` (two slots' distance at one probe; a tile is 100 units),
`nearer PROBE camera A B` (the point the camera looks at is nearer slot A
than slot B along the floor), `camera PROBE NAME OP VALUE` (the camera's
`yaw`, `pitch` in degrees or its `distance` at PROBE), `field
PROBE SLOT NAME OP VALUE`, `file PATH` and `no-file PATH` (a file the run
left, a glob relative to its run folder: `saves/bm64_*.bin`), `contains PATH
REGEX` (such a file has a matching line). The output is
one line per check, `N/M checks passed`
and a RESULT line; exit 0 only if all passed. A scenario takes 15–35 s.

A log line the checks read that does not parse (a probe's `coop: player` or
`coop: camera eye=` line, a `coop: hash` line), as when two games wrote into
one `game.log`, is a failed check `log line parses` that names the line; a
check that raises (a text field compared with a number) or a scenario that
crashes `scenario.py` is a failed check with the error, and the run goes on
with the next scenario (#46). `tools/test_scenario.py` tests this on fake
runs' logs without the game (`tools/verify.sh`'s `harness` stage).

### Session scenarios: two games on one machine

An online session ([design.md](design.md), *The session core*) is tested
with two games on this machine, a host and a client on a free local port.
The session's own switches (`src/coop/session.cpp`):

- `BM64_COOP_SESSION=host:PORT` or `join:HOST:PORT`: be the session's host or
  its client; with `tools/run.sh`'s autostart the launcher waits for the other
  side, with its prompts (`BM64_COOP_CONNECT_TIMEOUT`, default 60 s), then
  starts the game, alone if none came. Without the autostart the launcher's
  Start runs the game, which waits at its first pad read.
- `BM64_COOP_LAUNCHER=host:PORT` or `join:HOST:PORT`: instead of Start, fill
  the launcher's Co-op menu with it and press Host or Join (#28,
  [design.md](design.md), *Hosting and joining*); no time limit. If it does
  not connect, the menu comes back and the switch presses Start.
- `BM64_COOP_CANCEL_AFTER=S`: press the waiting prompt's Cancel after S
  seconds.
- `BM64_COOP_DELAY=N`: the input delay in frames, 1–60 (the host's counts,
  default 2).
- `BM64_COOP_SEED=N`: seed the game's random generator with N; also without
  a session, where two runs of the same script then hash the same frames.
- `BM64_COOP_LOG_HASH=N`: log `coop: hash frame=… hash=…` every N frames
  (the state hash of `patches/coop_session.c`).
- `BM64_COOP_BUILD_ID=N`: claim build identity N in the hello (a test of the
  build check, `session-build`).

A session's client plays on a copy of the host's save,
`saves/coop-session/<game id>.bin` in its own folder; its own save file is
never written. A scenario's `#saves-host NAME` (or `#saves-client`) runs
scenario NAME first, alone and with its checks, and starts that side with
the `saves/` folder NAME's run left (`run.sh --saves`): `session-save` gives
the host the file `save-file` makes, and the client none.

The client's camera follows player 2, the host's player 1
(`BM64_COOP_CAMERA` above; `session-camera` checks both and the hashes).

A scenario with a `#session` line runs both games at once (`tools/run.sh
--name host` and `--name client`, so their run folders differ). With a
`#launcher` line both start the session from the launcher's Co-op menu
(`BM64_COOP_LAUNCHER` in place of `BM64_COOP_SESSION`; `session-launcher`). The scenario
is the host's script; `#client STEP` lines are the client's script (its pad
0 is pad 1, player 2, in both games), `#env-host` and `#env-client` set one
side's environment, `#check-client` checks the client's log, and `#check
same-hashes` compares the two logs' `coop: hash` lines frame by frame (for a
session across two machines, which this cannot start, there is
`tools/compare_logs.py` instead: *Comparing the logs of two machines*). Both
logs are cut at their `coop: input end`, since the session is lost as soon
as one game is stopped; both scripts should end on the same frame. A tile
step runs on one side only, outside the lockstep: `session-desync` uses one
to part the two games on purpose.

```bash
tools/scenario.py --version eu session-lockstep   # both games, about 15 s
```

**One gamepad, two games** (#29, read in the code, not played): the game sets
SDL's `JOYSTICK_ALLOW_BACKGROUND_EVENTS` and its own *background input* option
is on by default (`src/main/main.cpp`, `src/ui/ui_config.cpp`), so two games
on one machine both take the events of the same controller — one stick would
move both players. `tools/run.sh` writes a `general.json` with the option off
for that reason, and a hand test of two games on one machine (#33) has to do
the same in each game: then only the game whose window has the focus reads the
pad.

## Europe speed

The game runs one frame per VI retrace (its scheduler, `__scMain`, passes each
retrace on to the game loop and the audio thread), and the Europe release is
made for a 50 Hz VI. ultramodern's VI runs at a fixed 60 Hz
(`lib/N64ModernRuntime/ultramodern/src/events.cpp`), so unchanged the Europe
build runs 20 % fast. The fix is a hook that `tools/europe.py` adds to
`bm64.eu.toml` in `__scMain`, after it receives a message: one retrace
message (VIDEO_MSG) in six becomes message 0, which the loop ignores
(`coop_pal_skip_retrace`, `src/coop/debug.cpp`). The game, its scheduler
clients and the audio then see 50 retraces a second.

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

### Presenting 50 game frames a second (#11)

Read in `lib/rt64` and `lib/N64ModernRuntime` on 2026-10-19:

- **What is presented when.** The build runs RT64 in its *present early*
  mode (`src/main/rt64_render_context.cpp`): a frame is presented when the
  game's display list for it is done, not on the runtime's VI. The runtime's
  60 Hz VI only paces the game (the PAL pacing above). Without frame
  generation the Europe build presents 50 frames a second and the display
  shows each for a whole number of its refreshes: on a 60 Hz display one in
  five frames stays for two.
- **The refresh-rate hint.** `gEXSetRefreshRate`, sent every frame from
  `Gfx_InitGfx`, tells RT64 the rate the game makes frames at
  (`viOriginalRate`; without it RT64 guesses from the VI history, assuming
  60 Hz, its "TODO: PAL support"). RT64 compares it with its target rate
  (the refresh-rate setting: *Original* 0, *Manual* default 60, or
  *Display*): when the target is higher it generates the frames in between
  (`rt64_workload_queue.cpp`; untagged transforms are matched automatically,
  `G_EX_ID_AUTO`), weights them by the two rates, and paces the presents at
  the target rate (`rt64_present_queue.cpp`). Upstream sent 60. A Europe
  build now sends 50 (60 with `BM64_COOP_PAL60=1`), from the host
  (`coop_game_frame_rate`, `src/coop/debug.cpp`, address in
  `patches/syms.ld`), since the patches are the same compiled code for both
  versions; it logs `coop: refresh rate <N>` once. The US build sends 60 as
  before.
- **What changes for Europe.** With the hint at 60, a 60 Hz target generated
  nothing (50 presents a second, as above), and a 120 Hz target generated two
  frames per game frame, 100 a second, weighted as if the game ran at 60.
  With 50, a 60 Hz target generates 6 frames for every 5 game frames, and
  120 Hz 12 for 5, at even steps. The *Original* setting generates nothing
  either way.
- So presenting at an even 60 (or the display's rate) needs no runtime edit:
  it is RT64's frame generation, driven by the hint. Whether it looks right,
  whether scrolling still judders and whether the generated frames show
  artefacts is a judgement by eye on a real display, which no headless run
  makes: that is the owner's.

Checked by run: scenarios `europe-rate` (`coop: refresh rate 50`) and
`europe-rate-pal60` (60 with the PAL pacing off), Europe build only.

