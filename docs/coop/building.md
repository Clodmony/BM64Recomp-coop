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
tools/verify.sh --scenarios      # also the game's test scenarios (below);
                                 # needs the linked build, opens the game window
```

Stages: `submodules`, `configs` (`tools/europe.py configs`, then the US and
Europe configs parse), `n64recomp`, `patches`, `europe` (Europe only:
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
overlay), `--until-log TEXT` (success once a line of the log starts with
TEXT), `--timeout` (default 60 s), `--exe`, `--rom`, `--env NAME=VALUE`
(repeatable), `--input FILE` (an input script, copied into the run folder;
*The test harness*, below), `--saves DIR` (start with a copy of DIR, e.g. an
earlier run's `saves/`, as the run's save folder), `--audio` (sound on the default device; by
default SDL's dummy driver, which consumes audio at the real rate). Exit
status: 0 overlay reached or log line seen (or ran until the timeout
without either), 1 timeout before them, 2 the game exited or crashed, 3 the
owner's config folder changed, 4 bad setup. The game opens its window on the desktop; there
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
Capture only the game's window: a capture of the active window takes
whatever window has the focus once the game loses it.

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
    S, flow flags, lives), the stick reading of pads 0 and 1, and one
    `coop: player slot=…` line per player in play (controlType, hearts,
    bombs, fire, flags, position in tenths of a world unit);
  - `tile SLOT X Z`: put the player in that slot on tile X, Z of the room at
    the floor level it stands on (`func_802511EC`, as a room entry does), and
    log `coop: tile slot=… from=… to=…`;
  - `save FILE`: save the live progress block into file 0–2 and the EEPROM,
    as the pause menu's save does (`func_802947AC`), and log
    `coop: save file=… used=… time=…` (the file's "used" bit and play time);
  - `log TEXT`, and `end`, which logs `coop: input end`.

  A bad line logs `coop: input error …` and the script does not run. The
  script steps once per game frame, from the per-frame hook of
  `func_8023A208`.
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
- `BM64_COOP_LOG_PLAYERS=1`: one line per room init, spawn (and the model
  asset it gets), placement on a tile, bomb, hit, death, cutscene freeze and
  room teardown, from hooks in `bm64.us.toml`.
- `BM64_COOP_P2=1|4`: player 2 in that slot in story rooms, driven by pad 1
  ([design.md](design.md), *Player 2 in a story room*). Its own lines
  (`coop: place …`, `trigger`, `down`, `respawn`) come from
  `patches/coop_player2.c` whenever it is on.

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
```

Checks: `log REGEX`, `no-log REGEX`, `after REGEX1 REGEX2` (a match of
REGEX2 after the first match of REGEX1), `not-after REGEX1 REGEX2` (none),
`moved P1 P2 SLOT [MIN]` and
`still P1 P2 SLOT [MAX]` (distance between two probes, in world units),
`delta P1 P2 SLOT AXIS OP VALUE` (the move along one axis), `near PROBE A B
MAX [MIN]` (two slots' distance at one probe; a tile is 100 units), `field
PROBE SLOT NAME OP VALUE`, `file PATH` and `no-file PATH` (a file the run
left, a glob relative to its run folder: `saves/bm64_*.bin`). The output is
one line per check, `N/M checks passed`
and a RESULT line; exit 0 only if all passed. A scenario takes 15–35 s.

### Session scenarios: two games on one machine

An online session ([design.md](design.md), *The session core*) is tested
with two games on this machine, a host and a client on a free local port.
The session's own switches (`src/coop/session.cpp`):

- `BM64_COOP_SESSION=host:PORT` or `join:HOST:PORT`: be the session's host or
  its client; the game waits for the other side at its first pad read
  (`BM64_COOP_CONNECT_TIMEOUT`, default 60 s) and plays alone if none comes.
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

A scenario with a `#session` line runs both games at once (`tools/run.sh
--name host` and `--name client`, so their run folders differ). The scenario
is the host's script; `#client STEP` lines are the client's script (its pad
0 is pad 1, player 2, in both games), `#env-host` and `#env-client` set one
side's environment, `#check-client` checks the client's log, and `#check
same-hashes` compares the two logs' `coop: hash` lines frame by frame. Both
logs are cut at their `coop: input end`, since the session is lost as soon
as one game is stopped; both scripts should end on the same frame. A tile
step runs on one side only, outside the lockstep: `session-desync` uses one
to part the two games on purpose.

```bash
tools/scenario.py --version eu session-lockstep   # both games, about 15 s
```

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
