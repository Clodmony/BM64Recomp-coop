# Story mode co-op: the single-player audit and the design

Issue #3. Battle mode already runs four players; the Adventure (story) mode is
written for one. This page records where and how story mode assumes a single
player, and proposes a co-op approach for each assumption. It is the base for
the co-op issues that follow.

**Status (2026-10-18).** Everything here was read from a disassembly of the
**Europe** release, named with the US symbols in `BM64Syms/`. The spike (#4,
*The follow-up spike* below) ran parts of it on the Europe build; the rest is
read, not run. Every finding is to be re-checked on a US build. "Read" means
the instructions show it; "inferred" means an interpretation beyond them.

**Scope (owner, 2026-10-18).** The work targets **online** co-op. Local couch
co-op (split-screen or a shared screen) is dropped for now; its notes stay
below, marked, for when it comes back. The other proposals (player 2 in slot
4, shared lives) are proposals until the spike (#4) confirms them.

## How this was read

- **Disassembly.** The Europe ROM, unpacked like the US decompressed ROM, was
  disassembled section by section with spimdisasm (boot, main code at
  0x80225800, and all 95 overlay sections, which all load at 0x80043000). The
  function layout matches US; main-code bss does not (next section).
- **Calls from overlays into main code.** Overlays never call or address main
  code directly. Each overlay starts with a table of 8-byte stubs,
  `jr $zero; addiu $t0, $zero, 0xTTII`, which jump through the secure-call
  table `TT` (the decompilation's *zerojmp*), entry `II`. `BM64Syms` names
  them `<target>__0x<area>__secure_call`; in the Europe ROM all 68108 named
  stubs decode to the same target for the same table entry. The counts below
  resolve every stub from its encoding, including stubs spimdisasm left as
  data. Table 0x10 is the player-helper table (`func_80245CDC`,
  `func_802512C0`, `func_8024557C`, `func_802457D8`, `func_80243DDC`, …).
- **Overlay data access.** No overlay has a `%hi/%lo` reference to main-code
  data: overlays reach game state only through main helpers and the pointers
  they return. That is what makes most of the co-op work possible from main
  code alone.

### Addresses: the Europe bss is 0x10 lower

Main-code `.bss` sits **0x10 lower in Europe than in US**, and so does main
`.data`/`.rodata` from US 0x802A3020 on. Europe's main `.rodata` has two
4-byte constants fewer somewhere in US 0x802A2F64–0x802A2F78, so from US
0x802A2F78 its symbols sit 8 lower, and after the next file's 16-byte
alignment (US 0x802A3020) 0x10 lower through the end of main bss. Main
`.data` and `.rodata` below US 0x802A2F64 are at the same addresses, and so
is low bss (0x80063000–0x800BEA60: `g_Players`, `g_EnemySlots`, the save
block). Functions are at the same addresses but for five boot functions
(`n64main` is 4 bytes longer; `osInitialize` and `osSetThreadPri` start
0x60 later). Proof: upstream's US patches of `func_80242270` (HUD),
`func_80243A50` and `func_80225840` against the same Europe functions (US
`D_802AC610`, `D_802AC770`, `D_802AC884`, `D_802AC5C0` appear as Europe
0x802AC600, 0x802AC760, 0x802AC874, 0x802AC5B0), the bss clear range (US
0x802A5300–0x802B36D0, Europe 0x802A52F0–0x802B36C0), and all 68 main jump
tables. `tools/europe.py` holds this map, generates the Europe symbol files
from it and checks it on the Europe image (`check`: 221 jump tables, the bss
clear, the 147 data references of the patched functions; see
[building.md](building.md), *The Europe build*).

So a US symbol name printed on a main-bss operand of the Europe disassembly
names a **different** variable. This page gives main-bss globals by their
**US address**, with the Europe one in brackets; patches use the US address.
Functions are given by name (the same in both builds).

Two statements in the issue change with this:

- Lives and credits are not fields of the block at `D_802AC5C0`. They are
  +0x14 and +0x18 of the story session block at US 0x802AC610 (Europe
  0x802AC600), which upstream's cheats already call `D_802AC624` (lives) and
  `D_802AC628` (credits). The address the issue gives, `D_802AC5C0`+0x64, is
  the same US address, 0x802AC624.
- There is no 16-bit stage ID at `D_802AC5C0`+0x16. What picks a stage is
  the **overlay ID**, a 16-bit value the game keeps in a word at US
  0x802AC5D4 (see the game flow below); US 0x802AC5D6 is that word's low
  half.

## Overlay map

Overlays are chosen by a 16-bit overlay ID, not by a stage number.
`func_8022691C(id)`, called only from boot (`func_8023A3E0`) and from the main
process `func_8023DB0C`, looks the ID up in the bank table at ROM 0x30800
(`gOverlaySizes` in BM64Syms), then in the bank's header file, decompresses the file to
0x80043000 and keeps the dispatcher the overlay returns.
`func_802268F4(code)` sends it event codes: 2 init, the entry code, 4, 5 after
the fade, 3 every frame, 0xC on leaving, 0xB on stage clear.

| Sections | Overlay IDs | Role |
|---|---|---|
| ovl_1_subovl_1–4 | 0x14–0x17 | debug maps ("debmap0 : Mapchange happened!") |
| ovl_2_subovl_1, 2 | 0x19, 0x1A | story cutscenes; 0x1A is the opening after a new file |
| ovl_2_subovl_3, 4 | 0x1C, 0x1D | ending director (staff roll) and final ending scene |
| ovl_2_subovl_5 | 0x1B | attract/intro demo |
| ovl_2_subovl_6 | 0x24 | Controller Pak manager (boot with START held) |
| ovl_3_subovl_1 | 0x20 | title / main menu ("Adventure / Battle / Custom / Options") |
| ovl_3_subovl_2 | 0x21 | Adventure world map and stage select |
| ovl_3_subovl_3, 7 | 0x23, 0x22 | Battle setup menu, battle results |
| ovl_3_subovl_4, 5, 6 | 0x25, 0x26, 0x1F | Custom (parts), Options, file select |
| ovl_4_subovl_1–10 | 0x28–0x31 | Green Garden: stage 1 rooms 0x28–0x2C, stage 3 rooms 0x2D–0x30, stage 2 "Friend or Foe?" rival stage 0x31 |
| ovl_5_subovl_1–7 | 0x38–0x3E | Blue Resort: stage 1 0x38–0x3B, stage 3 0x3C–0x3D, rival stage "VS Artemis" 0x3E |
| ovl_6_subovl_1–15 | 0x48–0x56 | Red Mountain: stage 1 0x48–0x4F, stage 3 0x50–0x55, rival stage "VS Orion" 0x56 |
| ovl_7_subovl_1–10 | 0x58–0x61 | White Glacier: stage 1 0x58–0x5D, stage 3 0x5E–0x61 |
| ovl_8_subovl_1–13 | 0x68–0x74 | Black Fortress: stage 1 0x68–0x6D, stage 3 0x6E–0x73, rival stage "VS Altair" 0x74 (three rivals) |
| ovl_9_subovl_1–5 | 0x78–0x7E | Rainbow Palace (hidden world): stage 1 0x78–0x79, stage 3 0x7B–0x7C, rival stage "Final Battle!" 0x7E |
| ovl_9_subovl_6 | 0x62 | White Glacier rival stage "VS Regulus" (stored in bank 9) |
| ovl_10_subovl_1, 2 | 0x88, 0x89 | bosses: Green Garden, Blue Resort |
| ovl_11_subovl_1, 2 | 0x8A, 0x8B | bosses: Red Mountain, White Glacier |
| ovl_12_subovl_1, 2 | 0x8C, 0x8D | bosses: Black Fortress, Rainbow Palace |
| ovl_13_subovl_1 | 0x9B | unknown, debug-like; nothing requests it |
| ovl_14_subovl_1–10 | 0x90–0x99 | Battle arenas |

Read: the IDs, the menus' text, the transitions between IDs and the two
identical world tables (`D_802A04C8` in main, `D_ovl3_2_8004A19C` on the world
map: 6 worlds × 4 stages → first overlay ID). Inferred: the stage names come
from the order of the game's stage-name list; the rival stages are confirmed by
their `func_8025DC60` calls and the bosses by size. Each story stage is several
**rooms**, one overlay per room.

## Game flow and how a mode is chosen

The main process `func_8023DB0C` loops forever. When a scene change is
pending it fades out, calls `func_8023D0F8` (mode and player setup), loads the
overlay, sends the init events and fades in. Every scene change goes through
`func_8023AE6C(id, entry, fade)` (called from 69 overlay sections), which
fills the flow block at US 0x802AC5D0 (Europe 0x802AC5C0, US name
`g_LevelLoader`):

| US address | Field |
|---|---|
| 0x802AC5D0 | flags: 0x1 scene change pending, 0x2 death retry pending, 0x4 battle, 0x8 rival stage, 0x20 boss stage, 0x10/0x80 frozen (death, stage clear), 0x40 paused |
| 0x802AC5D4, 0x802AC5D8 | overlay ID and entry code to load |
| 0x802AC5DC, 0x802AC5E0 | retry point: the ID and entry code a death reloads (`func_80243810` sets it) |
| 0x802AC5E4 | fade flags |
| 0x802AC5E8, 0x802AC5EC | world (0–5) and stage (0–3), derived from the ID |
| 0x802AC610 | story session block (below), returned to overlays by `func_8023A840` |
| 0x802AC660 | battle configuration block, returned by `func_8023A834` |

Story or battle is decided **on every load from the overlay ID** by
`func_8023D0F8` (read): IDs 0x90–0x99 set flag 0x4 (battle); an ID in a world
range is a story room (world, stage and room derived; flag 0x8 on rival
stages, 0x20 on boss stages); anything else is a menu or cutscene.

## Single-player assumptions, with the proposed approach

### 1. Who is a player: decided at every scene load

`func_8023D0F8` (0x8023D168–0x8023D1B8, read) clears `controlType` (+0x24)
and +0x68 of slots 0–3, then sets `g_Players[0].controlType = 1` and
`g_Players[4].controlType = 0`. Battle IDs then set slots 0–3 from the battle
configuration (US 0x802AC664 + 4·slot: 0 off, 1 human, 2 computer). In story
mode **every room change, retry and continue leaves exactly one human, slot
0.**

The room's init then calls `func_80245CDC`, the shared player init: every
boss, every battle arena and every stage room but three (ovl_5_subovl_1,
ovl_5_subovl_6, ovl_7_subovl_1) call it, 89 calls in 83 sections. It sets
the story slot (below) to 0, starts the player process `func_8024FFF0`, and
calls `g_SpawnPlayer(slot)` for **every slot below 5 whose controlType is not
0**. The spawn and update are already multi-player; only the slot reset makes
story single-player.

*Approach:* a hook at the end of the story slot reset in `func_8023D0F8` that
sets the co-op slot human when a co-op session runs. Cheap. Not flag 0x4: it
turns on battle timers, battle death rules and an extra enemy allocation.

### 2. The story slot S: one global behind most player access

US 0x802AC894 (Europe 0x802AC884) holds **S, the slot of the story player**
(-1 = none). It is -1 at boot (`func_80243BF0`) and at stage teardown
(`func_80245868`), 0 at stage init. `func_80243DDC` returns it (249 overlay
calls, 67 sections), `func_8024585C(s)` sets it. 27 main functions read it.
Story code reaches "the player" in three ways (read):

1. **Wrappers that act on `g_Players[S]`** (all in table 0x10):

   | Function | Overlay calls | Does |
   |---|---|---|
   | `func_802512C0(x, y, z)` | 222 / 66 sections | place S on a tile (room entry, teleports); worker `func_802511EC(slot, …)` |
   | `func_802452E8(on)` | 182 / 52 | freeze or release S for a cutscene: flags 0x2010000 (control lock and damage guard), idle animation |
   | `func_8024557C(out)` | 132 / 51 | S's tile coordinates |
   | `func_80245280(on)` | 117 / 42 | freeze or release S without the animation |
   | `func_8024A824(…)` | 114 / 58 | cutscene/warp set-up: camera focus on S's tile, flags on S |
   | `func_80245F74(dir)` | 89 / 56 | set S's facing |
   | `func_802457D8(x0, z0, x1, z1)` | 70 / 48 | is S inside a tile rectangle (exit and event triggers) |
   | `func_802506B8(script)` | 32 / 25 | scripted walk of S (blocks until done) |
   | `func_80250DC8` | 32 / 23 | one action step of S |
   | `func_802456FC(…)` | 28 / 16 | rectangle test plus floor level |
   | `func_80250E2C(wait)` | 27 / 16 | warp prelude: camera on S, waits for S |
   | `func_802510C8` | 22 / 14 | release S from a carried state |
   | `func_80243DEC(on)`, `func_802443A0(on)`, `func_8025068C(v)` | 14, 6, 5 | flags and fields of S |

2. **Slot-taking helpers fed with S.** Overlays usually call
   `func_80243DDC()` once and pass the slot on: `func_802443F4(slot)` (the
   player's actor, 169 overlay calls), `func_80250338(slot, out)` (world
   position, 139), `func_802444B8(slot, anim)`, `func_80243D2C(slot)` (hearts),
   `g_AddPlayerBombCount(slot, n)`. 39 overlay globals keep a copy of S, and
   about 112 call sites cache S's position every frame for enemies and bosses
   to aim at.
3. **Slot 0 by constant.** Some overlay sites pass 0 directly: 34 calls of
   `func_802443F4(0)`, 15 of `func_802444B8(0, …)`, 15 of `func_80243D2C(0)`.
   Main code also has fixed slot-0 writes (sections 6 and 7 below).

The game already switches S temporarily: rival-bomber code does
`func_8024585C(1); func_802452E8(on); func_8024585C(0)`, and `func_8023C170`
applies the freeze to every player in a loop the same way. Nothing found
sleeps between the switch and the restore. Every restore writes 0, so **S
must stay 0 = player 1**, or those writes must change.

*Approach:* keep S = 0 = player 1 as the story player. Make the behaviours
that must include player 2 player-aware in **main code** (the wrappers, the
triggers, the enemy AI), not in the 50+ overlays. Overlay sites that hard-code
slot 0 stay player 1's: acceptable for a first version, listed per behaviour
below.

### 3. Player 2's slot: 4, not 1

Story stages already host AI bombers. `func_8025DC60(slot, type, x, z)`
(called only by the six rival stages) spawns a rival with controlType 2 into
**slot 1** (ovl_4_subovl_10, ovl_5_subovl_7, ovl_6_subovl_15, ovl_9_subovl_6),
**slots 1 and 2** (ovl_9_subovl_5, "Final Battle!") or **slots 1, 2 and 3**
(ovl_8_subovl_13), points its AI at slot 0, and
restores S = 0. Its AI process `func_802553D4` switches S to 1. A human in
slot 1 would be overwritten there. In a rival stage, a death of any slot
other than 0 counts as a rival defeated (`func_8023A870`, `func_8023F26C`),
which could end the fight as won when player 2 dies.

**Slot 4** is spawned by `func_80245CDC`, updated by `func_8024FFF0` (both
loop over 5 slots), and zeroed by `func_8023D0F8`; no story code uses it (no
constant 4 found). The per-slot player arrays in main code have 5 entries. Two
things are missing for slot 4 (read):

- **Model.** `g_SpawnPlayer` chooses model asset 0x4A–0x4D for slots 0–3; for
  slot 4 it reads a stale register. It needs a model case (a patch of
  `g_SpawnPlayer`, or re-assigning the parts after the spawn).
- **Input.** There are only four pad buffers. The player update
  `func_8024E088(slot)` reads pad = slot for controlType 1, so slot 4 would
  read past them. For controlType 2 it reads the **command record**
  `g_ComputerPlayerMovement` (US 0x802ACF08 + 0x10·slot: stick vector f32[3],
  held buttons +0xC, newly pressed +0xE), which has 5 entries. Running player
  2 as controlType 2 and filling record 4 from pad 1 each frame (one hook
  before `func_8024E088`) gives slot 4 input with no change to the update.

The command record looked like the natural input point for **online** co-op
(each side sends the record of its human slot). The session core (#21) sends
the pads instead, one step earlier, and both machines fill the record from
pad 1 with the same code (*The session core*, below).

*Approach:* player 2 in slot 4 as controlType 2, input through the command
record, model case added. Cheap to moderate. Battle-only arrays sized 4 (the
battle configuration, the out flags at US 0x802AC714) must stay unused by
slot 4 or be checked. The spike (below) checks slot 4; slot 1 in a stage
without rivals is its first step because it needs no input or model change.

### 4. Spawning and placing player 2

`g_SpawnPlayer(slot)` allocates the actor (`g_InitLevelClass`), places it at
the slot's `spawnLocation` (+0x0), assigns the model and a shadow, and resets
per-slot state. It does **not** set hearts, bombs, fire or controlType
(read). Story rooms then place S with `func_802512C0`; no story code writes
another slot's `spawnLocation`.

*Approach:* when the co-op slot spawns, give it hearts (the game sets only
slot 0's, to 1, in `func_8023DA04`; bombs and fire are already reset to 2 for
all five slots by `func_80277E1C`), and patch
`func_802511EC` so that placing slot 0 also places player 2 on a free tile
next to it. Every room entry and story teleport (222 calls of
`func_802512C0`) then carries player 2 along. Cheap, plus a walkable-tile
check.

### 5. Camera

The camera process `func_80235D7C` (one for all modes) follows, when mode bit
0x10 is set (`D_8008F428`), either an object set by `func_80235FD8` (7 calls,
bosses) or **`func_802443F4(func_80243DDC())`, player S's actor** (read). Its
C-button control reads pad 0 (`func_80235934`). Stage init snaps it to S
(`func_80237664`); `func_802362D4` / `func_802362B8` set and clear mode bits
(45 and 147 overlay calls). Battle clears the follow bits and leaves the
camera static. No code frames several players. The camera state is a set of
single globals; cutscenes write it through `func_8024A824` and `func_80250E2C`.

*Approach:* online, each machine draws its own view, so its camera should
follow its own player. Patch the target choice in `func_80235D7C` (one
function) to a "camera slot" that is a per-machine setting, not game state.
Whether the camera feeds back into the simulation (enemy activation, the stick
vector) must be ruled out on a build, or the two machines diverge.

*Local co-op (dropped for now):* a shared screen framing both players
(target midpoint, distance from their separation, a leash) is the same one-function patch. **Split-screen** needs a
second camera state and a second render of the scene per frame: the camera
globals exist once and the HUD draws once per frame. That is the expensive
part of the local mode and needs its own investigation.

### 6. HUD

The story stage HUD is one draw callback, `func_80242270`: the clock (elapsed
time, red when over the target time) and the gem counter, read from the
session block. It reads **no player slot** (read). Hearts, bombs and fire are
not shown as numbers in a stage. The stage title card and the miss screen
(`func_8023AF5C`, `func_8023E1D8`) show lives and **slot 0's** heart state
(`g_Players[0].heartCount`, by constant). Battle registers a different HUD,
`func_80241BB0`, which loops over slots 0–3 and draws a face per slot from the
same icon sheet (asset 0x130, "dead" variant at +8).

*Approach:* with shared gems and time the story HUD is already right. Add a
per-player panel (face, hearts) as a **second draw object** registered next to
the story HUD in `func_8023D0F8` (`func_8022A328`, the same call the game
uses), using the battle faces. Upstream already patches `func_80242270`
(widescreen), so co-op does not edit it. Cheap to moderate.

### 7. Hearts, lives, credits, game over

Per player already (read): hearts (+0x14), bombs (+0x1C), fire (+0x20) and
bomb types (+0x18) live in `g_Players[slot]`. The damage handler
`func_80248148(slot, cause)` takes a heart from that slot; item pickups
(`func_802797BC`) find the slot whose actor touched the item. Stored once:
the session block at US 0x802AC610 holds difficulty (+0x10), **lives**
(+0x14, US 0x802AC624, 2 at start), **credits** (+0x18, US 0x802AC628, 3 at
start, +1 per 50 gems, saved), **gems** (+0x1C), the target and elapsed time
(+0x28, +0x30) and play time (+0x38). Overlays never touch lives, credits or
gems: main code only.

The death chain (read):

1. At 0 hearts `func_80248148` calls `func_8023A870(slot, cause)`.
2. Battle (flag 0x4), or a rival stage (flag 0x8) and slot ≠ 0: the slot is
   out (US 0x802AC714 + 4·slot = 2), no retry.
3. Story otherwise, **for any slot**: flags |= 0x92 (freeze, retry pending)
   and the death process `func_8023F26C` starts. In the story case it takes
   **slot 0's actor and slot 0** (0x8023F3A4–0x8023F3BC): a player 2 death
   plays and ends as player 1's death.
4. At lives 0 `func_8023E1D8` runs: Continue (credits −1, lives back to 2) or
   game over, and it rolls back the gold cards collected in the stage.
   Otherwise the **retry point reloads**: the room being played, from the
   entry it was entered by. On the reload `func_8023D0F8` takes one life and
   sets `g_Players[0]` hearts to 1.
5. Further slot-0 constants: hearts := 1 on continue (0x8023E724) and on the
   pause menu's retry (`func_802968BC`), remote bombs lost on death
   (`func_80277DF4`, `g_Players[0]` only), the session start
   `func_8023DA04`.

*Approach (recommended):* a **shared lives and credits pool**, one counter,
which fits the single save and an online host that owns it. While the partner
is alive, a dying player is **downed and respawns** instead of reloading the
room: patch `func_8023A870` so the co-op slot takes a branch like the battle
one, spends a shared life, and respawns next to the partner after a delay
(`func_8023EB28` is the game's own one-slot respawn). When the last player
dies, the original path runs. The slot-0 constants in step 5 apply to every
co-op slot (small patches or post-hooks). Moderate; the main co-op gameplay
work. Per-player lives would need a second counter and two numbers on the
cards: possible, not recommended first.

### 8. Exits, goals, gold cards, items

- **Exits and room triggers test S only** (read): `func_802457D8` /
  `func_802456FC` (98 overlay calls) test S's tile against a rectangle, then
  the overlay requests the room change with `func_8023AE6C`. The next room's
  init respawns every slot, so a transition already brings every player
  along; only the trigger and the exit fade (on S's actor) look at player 1.
  *Approach:* patch the rectangle tests to return true when any co-op player
  is inside (cheap, three small functions). Whether exits need both players
  inside is a design choice; "any player" is the cheap default.
- **Items: any player** (read): `func_802797BC` loops over slots 0–4 and gives
  fire, bombs, bomb types and hearts to the slot that touched the item; gems
  go to the shared counter. Exceptions: the "got item" notice only for S, and
  the heart item treats slot 0 differently in rival stages. The item's
  starting direction uses slot 0's position (cosmetic).
- **Boss reward** (`func_80233E3C`, 3 boss overlays) flies to S; the item it
  drops can be picked up by anyone.
- **Gold cards**: no dedicated pickup was found. The stage clear
  (`func_8023FD08`) awards cards from the shared counters, and the save holds
  them as 120 shared flags (5 per stage). Who collected a card does not
  matter for a shared campaign.
- **Stage clear** waits for player S to stand idle (`func_8023AB90`) and uses
  slot 0's actor: fine with player 1 as S.

### 9. Enemies and bosses

- **Damage reaches any slot** (read in the player update, inferred for the
  setter): `func_8024FFF0` runs `func_8024E088(slot)` for every slot, which
  turns that player's actor hit bits into `func_80248148(slot, cause)`. Enemy
  contact, boss attacks and blasts therefore hurt player 2 as they would in
  battle. Who sets the hit bits was not traced.
- **Main-code enemy AI chases S** (read): `func_80283CF0` (chase on the same
  floor), `func_8028854C` / `func_80288654` (line-up tests),
  `func_80289F04`, `func_80289DB8`, `func_802881A0` (aimed moves), and
  `func_80285A9C` places enemy spawns at a distance from S. `func_80282934`
  (16 overlay calls) clears enemies around S.
- **Overlay enemies and bosses aim at a per-frame cache of S's position** (the
  112 call sites in section 2), e.g. the Black Fortress boss
  (ovl_12_subovl_1).
- **Rival bombers** target slot 0 through their AI record (`D_800BCFE8`,
  stride 0x128: +0x10 target slot, +0x1C target actor), which their AI process
  reads every pass.

*Approach:* patch the main-code AI functions above (about 8) to pick the
**nearest living co-op player**, and the clear to work around every player.
Moderate. Rivals: rewrite their target fields every frame (cheap, inferred to
work). Overlay bosses stay on player 1 at first. A later option is a director
that switches S to the other player outside cutscenes (for instance when
player 1 is down), cheap but only safe where no overlay keeps S-derived
pointers across frames.

### 10. Cutscenes and warps

Cutscenes freeze S only (`func_802452E8`, `func_80245280`: control lock and
damage guard), walk S with `func_802506B8` scripts, face S with
`func_80245F74`, and point the single camera state at S's tile
(`func_8024A824`, `func_80250E2C`). A second player would keep walking, keep
taking damage and wander off-script.

*Approach:* patch the freeze, flag and facing wrappers to apply to every
co-op slot, the way `func_8023C170` already loops (cheap, five small
functions, about 400 call sites covered). Scripted walks stay on player 1;
player 2 stays frozen and is placed next to player 1 when the scene ends
(the placement patch of section 4 covers teleports).

### 11. Saving

The EEPROM holds three files of 0x90 bytes, built only from the progress
block at `D_8008E570` (flags and variables): play time, difficulty, credits,
one name and **one set of custom parts** (head, body, arms, legs), the best
time per stage, the 120 gold-card flags and unlock flags. None of it is per
player and none of it touches `g_Players` or S (read). Lives, hearts, bombs,
fire, world and stage are not saved. The game saves after every stage clear
(`func_8023FD08`), from the pause menu, the Custom screen and the file select.

Stage-local flags (`D_8008E640`, 0x400–0x43F; 331 test, 188 set and 105 clear
calls from overlays) are switched during play: doors, switches, collected
things. They are the same block, but a sync concern, not a save concern.

*Approach:* **one campaign, one file**: the **host's** file. The guest gets
the host's whole save at session start and plays on a copy of it, never on
its own file (built in #26: *The session core*, *The save and the build*;
the guest's `func_802947AC` runs as on the host, so both games stay the
same, and writes to the copy). Player 2's look: battle already
applies custom parts per slot (`func_8023EB28` → `g_LoadCustomParts`), so the
guest sends its own parts. Cheap. (Local co-op, dropped for now, would use the
file chosen at the file select and give player 2 another file's parts.)

## The approach in one table

| Assumption | Where | Proposed approach | Cost |
|---|---|---|---|
| One human per scene load | `func_8023D0F8` | hook: the co-op slot stays human | cheap |
| Player 2's slot | the six rival stages use 1–3; slot 4 has no model case or pad | slot 4, controlType 2, input via `g_ComputerPlayerMovement[4]`, model case | cheap–moderate |
| Stats and placement | `g_SpawnPlayer`, `func_802511EC` | stats at spawn; place next to player 1 | cheap |
| Story slot S | 18 wrappers, S restored to 0 everywhere | S stays player 1; make behaviours player-aware in main code | — |
| Cutscene freeze, facing | `func_802452E8`, `func_80245280`, `func_80245F74`, … | loop over co-op slots | cheap |
| Exit and room triggers | `func_802457D8`, `func_802456FC`, `func_802455A8` | any co-op player inside | cheap |
| Enemy AI targets S | ~8 main AI functions | nearest living player | moderate |
| Overlay bosses aim at S | per-frame caches in overlays | player 1 at first; later a director | none / cheap |
| Rival AI targets slot 0 | AI record `D_800BCFE8` | retarget per frame | cheap |
| A death is player 1's, room reloads | `func_8023A870`, `func_8023F26C` | shared lives; downed and respawn while the partner lives | moderate |
| Slot-0 heart and power resets | `func_8023D0F8`, `func_8023E1D8`, `func_802968BC`, `func_80277DF4`, `func_8023DA04` | apply to every co-op slot | cheap |
| Camera follows S | `func_80235D7C` | each machine follows its own player | cheap |
| Split-screen (dropped for now) | one camera state, one render | second camera and render per frame | expensive |
| HUD | `func_80242270` reads no slot; cards read slot 0 | keep; extra per-player panel as a second draw object | cheap–moderate |
| Save | one file, one look | host's/chosen file is the campaign; guest does not save | cheap |
| Stage-local flags | `D_8008E640` | lockstep keeps them equal (session core) | — |

The cheap rows give a playable prototype once the session core carries the
second player's input; the death rework and the enemy targeting are the real
gameplay work.

## The follow-up spike: player 2 in a story stage

Issue #4, run on the **Europe** build (2026-10-18). The plan was:

1. **Slot 1, a room without rivals** (Green Garden stage 1): slot 1 made a
   human player (controlType 1) before the room's spawn loop, with hearts,
   bombs and fire; pad 1 drives it with no input change.
2. **Slot 4**: model case in `g_SpawnPlayer` (asset 0x4B), controlType 2,
   pad 1 copied into `g_ComputerPlayerMovement[4]` before the player update.
3. Placement: placing slot 0 also places the co-op slot.

### How it is built

Hooks (`[[patches.hook]]` in `bm64.us.toml`, inserted into the recompiled
game function at an instruction) call into `patches/coop_spike.c` (since #19
`patches/coop_player2.c` and `patches/coop_harness.c`) on a copy of the
registers, with the stack pointer moved down. No game function is
replaced, so none had to be rewritten in C. All of it is off unless
`BM64_COOP_P2=1|4` is set; the test harness
([building.md](building.md), *The test harness*) gets a run into a room
(`BM64_COOP_WARP`), drives the pads (`BM64_COOP_INPUT`) and logs players and
events.

| Hook | Does |
|---|---|
| `func_80245CDC` entry (room player init) | co-op slot: controlType 1 (slot 1) or 2 (slot 4), 2 hearts, player 1's bombs and fire, bomb type 0x200 |
| `g_SpawnPlayer` at 0x80245BC0 | slot 4: model asset 0x4B in s1 (the switch has no case for it) |
| `func_802511EC` entry (put a slot on a tile) | slot 0 placed: place the co-op slot too, offset by `BM64_COOP_P2_TILE` (gone since #19) |
| `func_8024E088` entry (player update) | slot 4: fill its command record from pad 1 with `func_80239880`, `func_80239FF8`, `func_802399AC` |
| `func_8024E088` at 0x8024E608, `func_80248148`, `func_8023A870`, `func_80245868` | log bomb placed, hit, death, room teardown |

The input path needed one host change: only controller 0 was connected and
read (`src/game/controls.cpp`, `src/game/input.cpp`); with an input script,
pads 0–3 report connected and read the script.

### What the runs showed

Scenarios `p2-slot4`, `p2-slot1`, `p2-entry-walk` (`tools/scenarios/`), room
0x29 (Green Garden stage 1, the room after the tutorial, entered with entry
0x13), plus exploratory runs in room 0x28.

- **Spawn: confirmed.** With controlType set before the spawn loop, the
  room's own init spawns the co-op slot, slot 1 or slot 4, and every reload
  respawns it (the hook sets it again after `func_8023D0F8`'s reset). Rooms
  0x28 and 0x29 call `func_80245CDC` from overlay code, so hooks in main
  functions are reached from overlay stub calls.
- **Model: confirmed.** Slot 4 with asset 0x4B is drawn as the dark bomber
  with a shadow (seen on a local capture in room 0x28); slot 1 gets 0x4B from
  the game's own switch.
- **Input, slot 1 (pad 1, controlType 1): confirmed.** The same stick moves
  slot 0 and slot 1 the same way, camera-relative (right is −z, down +x in
  room 0x29's camera); A places a bomb.
- **Input, slot 4 (command record): partly.** Pad 1 moves slot 4 and pad 0
  does not; pad 0 moves player 1 and pad 1 does not; A places a bomb. But the
  record filled with the pad path's own vector does **not** move slot 4 the
  way a pad moves a controlType 1 slot: stick right moves it along −x (slots
  0 and 1: −z), left and up also −x, down +x. Inside the update the vector
  for slot 0 equals the probe's for the same stick, so the difference is in
  how the update uses a controlType 2 slot's vector, not traced. The rival
  AI, the record's normal writer (`func_802553D4`), writes +0x0 from its own
  state (AI+0x74, inferred a world-space direction) and a speed at +0x8. So
  the record holds something other than the pad vector (open question 3).
- **Bombs and hits: confirmed.** Slot 4's and slot 1's bombs explode and
  their blasts take one of their hearts (2 → 1); player 1's bombs hit slot 4
  too (room 0x28).
- **Death: as predicted.** A second hit kills slot 4: `func_8023A870(4, 1)`,
  flow flags 0x92, slot 4's controlType becomes 3, and the room reloads as
  player 1's retry with one life fewer (2 → 1); the reload frees and
  respawns slot 4. In room 0x28, during the tutorial, the room had not
  reloaded 10 s after the death (not looked into).
- **Cutscenes: as predicted.** During room 0x28's tutorial dialogue player 1
  is locked (flags 0x2010000) and slot 4 is not: it walks.
- **Placement: refuted as planned.** Placed on player 1's tile, or one tile
  ahead, the co-op slot stands in the way of player 1's scripted **entry
  walk**: the walk (flag 0x400) never ends and player 1 stays locked
  (`p2-entry-walk`). Off the walk's path (here 4 tiles along x and 3 along z)
  both play. One tile behind fell off the map. A placement rule needs a free,
  walkable tile off the entry path (or player 2 placed when the walk ends).
- **Pools:** no failure with two players and their bombs in these runs; not
  stressed.

The warp leaves the flow flag 0x80 (set at boot) set in the room, with or
without player 2; play is not affected.

### What followed

The group #19 (next section) took the input, placement, cutscene, exit and
death findings up. The input finding above was a harness bug, not the
record's format.

## Player 2 in a story room

The group #19 (#13, #14, #16, #17, #18), run on the **Europe** build
(2026-10-18) with the scenarios in `tools/scenarios/` (`p2-*.txt`). Player 2
is still local and scripted (pad 1, `BM64_COOP_P2=4`); nothing here is online
yet. The code is `patches/coop_player2.c`; the hooks are in `bm64.us.toml`.

### Input through the command record (#13)

The record holds what the pad path makes of the stick: `func_80239880` gives
an angle in degrees (+0x0, before the camera's yaw, which the update adds), an
elevation (+0x4, 90 for a flat stick) and a length (+0x8), and the update
reads the record for a controlType 2 slot through the same code as a pad
(0x8024E3E4–0x8024E46C). So filling record 4 from pad 1 with the pad
accessors was right. What made slot 4 move along one axis was the hooks'
**register copy**: `recomp_context c = *ctx` keeps `f_odd` pointing at the
original context's odd float registers, so double arithmetic in
`func_8022FF58` (the angle) read half of each double from the wrong context
(stick 124,3 gave 182.3° from the hook and 91.4° inside the update). Each
copy now points `f_odd` at itself, as librecomp's `load_context` does. Slot 4
then moves camera-relative like slots 0 and 1 (`p2-slot4`); moving off its
lane's centre, the game also slides it back onto the lane. The record is
therefore a workable input point for the session core: the stick as the pad
path reads it plus buttons, no camera state needed on the sender's side
(inferred: the receiver adds its own camera's yaw, the same on both sides
while the camera follows player 1).

### Placement: player 2 joins at player 1's first step (#14)

Placements of player 2 around player 1's entry walk that failed (room 0x29,
entry 0x13, `p2-entry-walk`):

- on player 1's tile or ahead: the walk never ends (#4);
- held frozen at the room's spawn point (the origin in room 0x29) and placed
  when the walk is over: after about 6 s its placement no longer holds, and
  released it takes a hit of cause 3 and dies (the update deals cause 3 for
  player flag 0x40000000);
- on the tile player 1 left last during the walk (4,14): blocked once the
  walk is over (the game pushes player 2 to 5,15), and placed just before
  that, player 2 dies there (cause 3);
- spawned mid-room straight onto the tile: a new actor is put back at the
  origin on its first frames, whatever was placed; spawned at the origin and
  placed a few frames later, it died (cause 3) after player 1's retry.

The rule now: the room's init does **not** spawn player 2. Player 1's
placement (`func_802511EC(0, …)`: room entries, story teleports) makes player
2's placement pending. Only player 1's **own steps** count: a step along x or
z (a fall to a lower floor is not one) taken once player 1 has been free of
walks (flag 0x400) and freezes (0x2010000) for 60 frames (room 0x29 still
changes the tiles behind its walk right after it). At such a step player 2 is
put on the tile player 1 just left: player 1 stood there, so it is floor, and
it is next to player 1. The first time in a room this spawns player 2 there
(the spawn point set by `func_802511EC` without an actor, then
`g_SpawnPlayer`, as the battle respawn `func_8023EB28` spawns), holds it
frozen and places it again 3 frames later; afterwards (a teleport, a respawn)
it moves the actor and releases it.

Checked: room 0x29 after its entry walk and after a retry (`p2-entry-walk`,
`p2-p1-death`), room 0x28 after its opening dialogue (`p2-room28`) and when
entered through room 0x29's floor exit, where player 1 falls in (`p2-exit`).
Not checked: a story teleport by `func_802512C0` from a room script (none was
found in these rooms); the same hook covers it. Cost: player 2 appears only
once player 1 moves.

### Cutscenes (#16)

The freeze and release wrappers of S (`func_802452E8`, with the idle
animation, and `func_80245280`) apply to player 2 too when S is player 1,
the way `func_8023C170` freezes every player: hooks after they load S call
`coop_freeze_partner` (log `coop: freeze partner`). A release when player 1
was moved during the scene makes player 2's placement pending again (it
joins player 1 at its next step). Scripted walks (`func_802506B8`) and facing
(`func_80245F74`) stay player 1's. Seen in the runs: room 0x28's opening
dialogue and room 0x29's entry walk freeze player 1 before player 2 has
joined; with player 2 in the room, the exit (`p2-exit`) and player 1's death
freeze it just before the room changes (`p2-p1-death`). No longer scene with
player 2 in the room was found in rooms 0x28 and 0x29, so "pad 1 does not
move a frozen player 2" is not checked. Player 1's death animation itself
does not use the wrappers: player 2 plays on during it.

### Exits and triggers (#17)

The update sends the room event 0x50 for **any** slot that stops centred on
an event tile (`func_8024857C`, called from `func_8024E088` for every slot);
the room then asks the rectangle tests, which looked at S only. The three
tests (`func_802457D8`, `func_802456FC`, `func_802455A8`) are rewritten in C
(`RECOMP_PATCH`): S's tile first, kept in `D_802ACEC8[S]` as before, then
player 2's (log `coop: trigger`). So player 2 can take an exit while player 1
stands still: in `p2-exit` player 2 walks into room 0x29's floor exit (x=3,
z=4..7) and room 0x28 loads with both players. The door back at 1,14 is
closed once the entry walk is over (no event for player 2 there).

### Death: downed and respawn with shared lives (#18)

A hook at the entry of `func_8023A870` takes player 2's death while player 1
plays (controlType 1, hearts > 0): no 0x92, no death process, no retry.
It returns 1 as the function would, and the damage handler puts the slot out
(controlType 3), as in battle. After 120 frames player 2 respawns with 2
hearts for one shared life (`D_802AC624`, 2 → 1) and joins player 1 at its
next step. With no life left it stays out until the next room. A death of
player 1, or of player 2 while player 1 is out, runs the story death as
before: the retry reloads the room and player 2 comes back with it
(`p2-slot4`, `p2-p1-death`). Not done: player 1 downed while player 2 plays
(S would have to change), the slot-0 resets of section 7, step 5, for player
2 (remote bombs, continue), and what a downed player 2 looks like (it stays
where it fell; not looked at on screen).

### Hooks and patches

| Where | Does |
|---|---|
| `func_80245CDC` entry (room player init) | player 2's stats (2 hearts, player 1's bombs and fire, bomb type 0x200); not spawned by the room, placement pending |
| `func_802511EC` entry (put a slot on a tile) | slot 0 placed: player 2's placement pending (held frozen if it is in the room) |
| `func_8024E088` entry (player update) | slot 0: respawn countdown, player 1's trail, player 2's placement; slot 4: its command record from pad 1 |
| `func_802452E8` at 0x80245300, `func_80245280` at 0x80245294 | S = 0: freeze or release player 2 with player 1 |
| `func_8023A870` entry (death) | player 2 while player 1 plays: downed, the function skipped |
| `func_802457D8`, `func_802456FC`, `func_802455A8` (`RECOMP_PATCH`) | rectangle tests for S or player 2 |
| `g_SpawnPlayer` at 0x80245BC0 | slot 4: model asset 0x4B |

## The session core

Issue #21, run on the **Europe** build (2026-10-18) with two games on one
machine, a host and a client (`tools/scenarios/session-*.txt`; how they run:
[building.md](building.md), *Session scenarios*). The code is
`src/coop/session.cpp`, the state hash `patches/coop_session.c`.

### Lockstep on the pads

Both games run every frame on the same pads. Each machine sends its own pad
0 once per frame: the buttons and the stick as the float values the game
will read. In both games the host's pad is pad 0 (player 1) and the client's
is pad 1, so player 2 in slot 4 gets its command record from pad 1 through
the same code on both sides (`coop_feed_command`, #13). Pads 2 and 3 are not
connected in a session.

Pads, not the command record (the issue's proposal): the pads are the
game's only input from a player, they are read once per frame, and they also
drive the title, menus, dialogues and the pause, where there is no record.
Player 2's record is computed from them on each machine, so sending the
record as well would add nothing. Only pad 0, the host's, drives menus.

The step sits in the **per-frame hook** at `func_8023A208` (the end of the
main loop), not at a pad read: upstream's patched loop
(`patches/required_patches.c`) reads the pads twice a frame, once for its
cheats (`Recomp_ProcessCheats`) and once in `func_8023A104`; a lockstep at
the read counted two steps a frame (seen in a run: 1100 reads in 550
frames). Reads return the frame's agreed pads. At the end of frame f a
machine sends its hash of frame f and its pad for frame f + delay, then
waits for the other's pad for frame f + 1. The delay (default 2 frames, the
host's choice) lets each wait be for a message sent a frame or more before;
frames below it hold no buttons.

The transport is TCP with Nagle off: the host listens (IPv6 and IPv4), the
client connects, retrying until `BM64_COOP_CONNECT_TIMEOUT`. A hello carries
the protocol version, the role, the game version (US and Europe are refused
together), the host's seed and delay, and the build identity (#26, below). The connection is made at the game's
first pad read (`func_8023A22C`, before the loop), so both games start
their frames together. The message formats are in `session.cpp`.

### Determinism and the shared seed

The main loop seeds the random generator with the clock:
`g_initRandom(osGetTime())` (`func_80225840`, after the first pad read). A
hook at `g_initRandom` puts the session's seed in (the host's, sent in the
hello) or `BM64_COOP_SEED`'s. Runs of `p2-slot4` (player 2's bombs, its
death and respawn) with a state hash logged every frame:

| Runs | Same hashes |
|---|---|
| two runs, the clock's seed | none, they differ from frame 0 |
| two runs, `BM64_COOP_SEED=12345` | all 1602 frames |
| host and client of `session-lockstep` | all 448 frames |

So on one machine the game is deterministic given the seed and the pads per
frame: nothing else (the audio thread, the scheduler's timings, the 50 Hz
pacing of the Europe build) reached the hashed state in these runs.

The **state hash** (FNV-1a, `coop_state_hash`) covers the scene (overlay,
flow flags, lives, S), the random generator's state, the five player records
and command records, the players' actor positions and the save data (#26:
the EEPROM mirror `D_80063000`, the live progress block `D_8008E570` with the
stage-local flags, the current file `D_802A1DB4`). Enemies, bombs and
the room's tiles are not in it: a divergence there shows once it reaches a
player or the random generator (inferred). Each machine sends its hash with
its pad; a different hash for the same frame logs `coop: session desync`
on both sides (once, then a count every 600 frames). In `session-desync` a
tile step on the client alone moves player 1 there; both games log the
desync for the same frame (366), and the session goes on.

### Ending

A peer that closes, or sends nothing for 10 s, ends the session (`coop:
session lost`); the game then reads its own pads again and plays alone
(`session-lost`). Player 2 stays in the room with no pad. A client that
finds no host, or a host no client, within the timeout plays alone without
player 2 (`session-alone`). Something on the port that never sends a hello
gives up after the same timeout (tried once by hand).

### The save and the build (#26)

Lockstep needs both games to start from the same save data. The game reads
the EEPROM once, at boot, right after the first pad read
(`func_80225840`: `func_8023A22C`, then `func_80294E54`, which reads all 0x37
blocks into `D_80063000`). The session connects at that first pad read, so
the save can be swapped before the game reads it:

- After the hellos the host sends its save file, the whole buffer librecomp
  keeps for the save type (0x20000 bytes for this game's `AllowAll`; zeros
  where there is no file). Read from the file, since the game has read and
  written nothing yet.
- The client writes it to `saves/coop-session/<game id>.bin` and switches
  librecomp to that file (`ultramodern::change_save_file`, what mods use).
  The client's game then runs the same code as the host's, saves included,
  but every write goes to the copy; its own file is not written. Each
  session overwrites the copy. A session lost later leaves the client
  playing on the copy.

Run (`session-save`, Europe, 2026-10-18): the host starts with a save whose
file 0 is used (`save-file`), the client with none; both load file 0 as the
file select does. Before the fix the client's file 0 was empty
(`used=0`), both logged `coop: session desync frame=0` and all 265 hashed
frames differed. After it both load the used file 0, hash the same 265
frames, the client's copy equals the host's file and the client left no
save of its own.

The **build identity** in the hello is FNV-1a over the patches' binary
(`mm_patches_bin`, what librecomp runs) and a SHA-256 prefix of the game's
recompiler config `bm64.<version>.toml`, which holds the hooks, taken when
CMake configures (`src/coop/version.cmake`). Either side refuses another
identity: `coop: session refused: another build (the other's 0x…)`, and
both play alone (`session-build`, with `BM64_COOP_BUILD_ID` faking the
client's). The protocol is now 2; the first 8 bytes of the hello (magic,
protocol, role, game) stay the same in every version, so an older peer is
refused for its protocol rather than misread. Not covered by the identity:
host code (`src/`), librecomp, and mods; a difference there still shows
only as a desync. Two builds of one commit with different compilers may
differ in the patches' binary and be refused (not tried; #29).

### Not done

- (#29) Two machines, the internet (the host's port must be reachable: a forwarded
  port, no NAT traversal), Windows and macOS: not run. The Winsock branch of
  `session.cpp` was not compiled.
- (#27) The camera follows player 1 on both machines, so the client sees player
  1's view (section 5).
- (#28) Starting a session takes environment switches; there is no menu for it.
- A fixed delay: a slow or lost message stalls both games, with no rollback.

## Open questions for a running build

1. Every address on this page, the 0x10 bss shift in particular, on US.
2. Is slot 4 free in every story room, and are all per-slot arrays used with
   it at least 5 entries? (Spike: free in rooms 0x28 and 0x29; the arrays
   not checked.)
3. What does the command record hold, and in which frame? (#13: the pad
   path's angle before the camera's yaw, an elevation and a length, read by
   the update in the same frame; see *Player 2 in a story room*.)
4. Does the camera feed anything back into the simulation? (Not yet asked by
   a run: in every session so far both cameras followed player 1.)
5. Who sets the actor hit bits; do enemies and bosses hit slot 4 like slot 0?
   (Spike: bomb blasts, its own and player 1's, hit slot 4; enemies and
   bosses not tried.)
6. Does retargeting the rival AI record make rivals chase another player?
7. Which screens are overlay 0x9B and the game-over/continue targets (0x20 and
   0x21 with entry 0x11)?
8. Gold cards: the pickup path and what the 3 high bits of a stage record mean.
9. How do the three rooms that never call `func_80245CDC` (ovl_5_subovl_1,
    ovl_5_subovl_6, ovl_7_subovl_1) spawn the player?
