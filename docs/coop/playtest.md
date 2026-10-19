# Playtest: co-op Adventure on two PCs

This is how to play the co-op Adventure on two PCs on the same home network,
Linux or Windows on either side (#29). It is an early test build: what is
known not to work yet is listed below, so a playtest can look for the rest.

The kit (`tools/playtest-kit.sh`) has one folder per platform, `linux/` and
`windows/` (also as `windows.zip`). Each is a **portable** folder: settings,
saves and the log stay inside it, and nothing is written elsewhere. These
folders are private builds: copy them to the two PCs by hand and do not
share or upload them.

## What you need

- On each PC, a **Europe** Bomberman 64 ROM of your own (`.z64`, `.n64` or
  `.v64`). This build plays the Europe version only, and both sides must play
  the same one. The kit has no ROM.
- One controller (or the keyboard) per PC.
- Both PCs on the same network.

## Set up each PC

**Linux:** copy the `linux` folder and start the game with `./start.sh` from
a terminal in it. The terminal shows the log, and `coop.log` keeps it.

**Windows:** unzip `windows.zip` and double-click
`windows\BM64Recompiled.exe`. If Windows warns about an unknown app
("Windows protected your PC"), choose *More info*, then *Run anyway*: the
build is not signed. If it says `VCRUNTIME140.dll` or `MSVCP140.dll` is
missing, install Microsoft's *Visual C++ Redistributable* (x64) and start it
again. The log goes to `coop.log` in the folder.

On the first start the launcher asks for the ROM: pick your Europe ROM.

## Start a session

1. Decide who hosts; the host's save is the one you play on.
2. **Find the host's address:** on Linux, `ip -4 addr` (an address such as
   `192.168.1.20`, not `127.0.0.1`); on Windows, `ipconfig` (*IPv4 Address*).
3. **The host** opens **Co-op** in the launcher and presses **Host** (port
   7777). A Windows host asks whether to let the game through its firewall:
   allow it on **private networks**. A Linux host with a firewall must open
   TCP port 7777 (e.g. `sudo ufw allow 7777/tcp`).
4. **The other player** opens **Co-op**, enters the host's address and port
   7777, and presses **Join**.
5. Both games start together once they are connected. If the join is
   refused, the prompt says why (another build, another game version).

## Playing

- The host drives the title, the file select and the menus; the other
  player's game follows. The client plays on a copy of the host's save; its
  own saves are not touched.
- In a story room, player 2 (the client) appears next to player 1 about a
  second after the room's opening (an entry walk, a dialogue) is over, and
  can play at once (#55).
- Each player moves with their own controller. The C-buttons turn and zoom
  your own camera only; each camera follows its own player.
- A player who dies while the other lives is down: a ghost (as in Battle) that
  moves along but lays no bombs, takes no exits and takes no damage (#22).
  The ghost plays again (for one shared life) when the living player finds
  a heart, after a cutscene, at the next stage, or when the stage is
  cleared. When both are down, the room starts over.
- Both games run the same frames: if one PC stalls, both do. If the
  connection is lost, a *Co-op session lost* prompt shows and each game goes
  on alone.

## Known not to work yet

- Player 2's hearts are not shown.
- Enemies still chase a downed player 1's ghost (#49).
- Rooms other than Green Garden stage 1 are untested with two players.
- No rejoining a lost session; restart both games.
- Joining needs a keyboard to type the host's address (a controller moves
  through the Co-op menu, but there is no on-screen keyboard).
- The other uses of the camera in stages (some enemies or objects may
  read it) were not checked; a desync there shows in the log.

## What to report

Each game keeps its log in `coop.log` in its folder, and the one of the run
before in `coop.prev.log`. A Windows game that crashes writes the crash
(its exception and where it happened) into its log first.

**The logs travel by themselves:** when the two games connect, each sends
the other its `coop.prev.log`, the log of its run before, and the other keeps
it in `coop-logs/` in its folder. So after a crash, start the game again and
join once more: the crashed run's log lands in the host's `coop-logs/`.
Nothing else is sent, and only to the PC you connect to.

Otherwise keep **both** `coop.log` files (one from each PC). Note what you
did when something went wrong, and the time. In the logs, look for:

- `coop: session desync frame=…`: the two games no longer agree (the most
  important line).
- `coop: session lost …`: the connection ended, and why.
- `coop: session refused …`: the join was refused, and why.
- `coop: crash: …`: a crash on Windows, with its place in the program
  (`BM64Recompiled.exe+0x…`), which the `.pdb` in the kit resolves.

Screenshots and clips show the game's graphics: keep them to yourselves,
never in a GitHub issue.
