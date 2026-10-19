// Co-op: player 2 in a story room (#4, the group #19). Nothing here runs
// unless a debug switch asks for it: the hooks in bm64.us.toml call these
// functions only when the host says so (src/coop/harness.cpp, BM64_COOP_P2),
// and the patched game functions below act on player 2 only once
// coop_stage_init has made a slot player 2. docs/coop/design.md, *Player 2 in
// a story room*, has the rules and the runs behind them.
#include "coop.h"

#define COOP_P2_PAD 1
#define COOP_P2_HEARTS 2        // at a room's start and at a revive
#define COOP_SETTLE_FRAMES 60   // player 1 free this long before player 2 is placed
#define COOP_PLACE_CLEAR 80.0f  // a placed player's tile centre at least this far from the partner

static s32 coop_slot = -1; // player 2's slot in this room, -1 for none
static s32 coop_joined;    // whether player 2 has been spawned in this room
static s32 coop_p1_free;    // frames player 1 has been free of walks and scenes
static s32 coop_place_pending;
static s32 coop_freeze_tile[3]; // player 1's tile when a cutscene froze it
static s32 coop_scene_frozen;   // player 2 frozen with player 1 by that cutscene
static s32 coop_p1_dying;       // player 1's story death runs, until the next room init (#23)

// Downed players (#22): a player who dies while the partner lives is down, a
// ghost, until a revive. Index 0 is player 1 (slot 0), 1 player 2 (coop_slot).
// Kept across room changes; cleared at a stage start or a retry.
static s32 coop_down[2];
static s32 coop_story_dead; // a story death ran (either player), until the next room init
static s32 coop_scene_loads; // scene loads (func_8023D0F8) so far
static s32 coop_stage_scene; // the scene load of the last stage room's init
static s32 coop_stage_id = -1; // world * 4 + stage of the last stage room
static s32 coop_scene_on;   // S frozen by a scene that began after the room settled
static s32 coop_placing;    // coop_place_down puts slot 0 on a tile itself

// What the ghost look (coop_ghost_look) changed, to put back at a revive.
typedef struct CoopLook {
    s32 saved;
    s32 parts[2]; // the variants of model parts 4 and 5
    s16 alpha;    // model object +0x36
    s32 bits;     // model object +0xC bit 0x1000, actor +0x8 bit 0x80000
} CoopLook;
static CoopLook coop_look[2];

static s32 coop_same_tile(const s32* a, const s32* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

// A free floor tile on floor level y, read from the room's tile map
// (func_8026D7D8): the low nibble of a cell is its kind, 1 a plain floor at
// that level (0 air, F a wall or block, 2, 9, A and B the event and damage
// tiles func_8024857C reacts to, others ramps and doors), and bits 0x20 and
// 0x40 mean something stands on it (the battle respawn's test,
// func_8023E8FC, refuses them). Room 0x29 closes the tile behind its entry
// walk by turning it into a wall (0x1F).
static s32 coop_free_floor(s32 x, s32 y, s32 z) {
    s32 cell = func_8026D7D8(x, y, z);

    return (cell & 0xF) == 1 && (cell & 0x60) == 0;
}

// A tile for a player next to the player in slot `near`: one of the four
// tiles beside it on its floor level that is free floor (coop_free_floor) and
// whose centre is at least COOP_PLACE_CLEAR from that player's actor, the
// farthest of them (on a tie the first along -z, +z, -x, +x); else one two
// tiles away along a free one. Two players' bodies (radius 30, g_SpawnPlayer)
// must not overlap: the game puts an actor whose move ends inside another
// back where it was the frame before, which for a new actor is the origin
// (#55). Returns 0 when there is none.
static s32 coop_find_tile(s32 near, s32* out) {
    static const s32 dirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    f32* pos = (f32*)(*(u8**)(g_Players[near].levelClass + 0x40) + 0x10);
    f32 best = -1.0f;
    s32 tile[3];
    s32 k;
    s32 d;

    func_802454F8(near, tile);
    for (k = 1; k <= 2 && best < 0.0f; k++) {
        for (d = 0; d < 4; d++) {
            s32 x = tile[0] + dirs[d][0] * k;
            s32 z = tile[2] + dirs[d][1] * k;
            f32 dx = x * 100.0f - pos[0];
            f32 dz = z * 100.0f - pos[2];
            f32 dist = dx * dx + dz * dz;

            if (!coop_free_floor(x, tile[1], z) ||
                (k == 2 && !coop_free_floor(x - dirs[d][0], tile[1], z - dirs[d][1]))) {
                continue;
            }
            if (dist >= COOP_PLACE_CLEAR * COOP_PLACE_CLEAR && dist > best) {
                best = dist;
                out[0] = x;
                out[1] = tile[1];
                out[2] = z;
            }
        }
    }
    return best >= 0.0f;
}

// Puts the slot on a tile (func_802511EC: the actor, or without one the spawn
// point) and makes it the actor's last position too, which the collision pass
// (func_80232FF8) keeps at +0x58 and puts a blocked actor back to: for a new
// actor it is the origin, from where player 2 fell (cause 3) in room 0x28.
static void coop_put(s32 slot, const s32* tile) {
    u8* actor = g_Players[slot].levelClass;
    f32* pos;

    func_802511EC(slot, tile[0], tile[1], tile[2]);
    if (actor == NULL) {
        return;
    }
    pos = (f32*)(*(u8**)(actor + 0x40) + 0x10);
    *(f32*)(actor + 0x58) = pos[0];
    *(f32*)(actor + 0x5C) = pos[1];
    *(f32*)(actor + 0x60) = pos[2];
}

// Player 2 has been spawned in this room and plays (not out after a death).
static s32 coop_partner_active(void) {
    CoopPlayer* p2;

    if (coop_slot < 0 || !coop_joined) {
        return 0;
    }
    p2 = &g_Players[coop_slot];
    return (p2->controlType == 1 || p2->controlType == 2) && p2->levelClass != NULL;
}

// The co-op index of a slot: 0 player 1, 1 player 2, -1 neither.
static s32 coop_index(s32 slot) {
    if (slot == 0) {
        return 0;
    }
    return (coop_slot >= 0 && slot == coop_slot) ? 1 : -1;
}

static s32 coop_slot_of(s32 i) {
    return i == 0 ? 0 : coop_slot;
}

// Whether the slot is a co-op player who is down (a ghost).
static s32 coop_is_down(s32 slot) {
    s32 i = coop_index(slot);

    return coop_slot >= 0 && i >= 0 && coop_down[i];
}

// Whether the co-op player i plays and is not down: player 1 with an actor
// and a pad, player 2 once it has joined the room.
static s32 coop_living(s32 i) {
    CoopPlayer* p1 = &g_Players[0];

    if (coop_down[i]) {
        return 0;
    }
    if (i == 1) {
        return coop_partner_active();
    }
    return p1->controlType == 1 && p1->levelClass != NULL && p1->heartCount > 0;
}

// The look g_SpawnPlayer gives a slot spawned with the ghost flag
// (0x80245AF0-0x80245B64), put on the slot's actor (on) or taken off again.
static void coop_ghost_look(s32 i, s32 on) {
    u8* actor = g_Players[coop_slot_of(i)].levelClass;
    CoopLook* look = &coop_look[i];
    u8* obj;
    u8* parts;

    if (actor == NULL) {
        return;
    }
    obj = *(u8**)(actor + 0x40);
    parts = *(u8**)(*(u8**)(obj + 0x4C) + 0x4); // func_8022979C's table: part * 8 + 4
    if (on) {
        if (!look->saved) {
            look->saved = 1;
            look->parts[0] = *(s32*)(parts + 4 * 8 + 4);
            look->parts[1] = *(s32*)(parts + 5 * 8 + 4);
            look->alpha = *(s16*)(obj + 0x36);
            look->bits = (*(s32*)(obj + 0xC) & 0x1000) | (*(s32*)(actor + 0x8) & 0x80000);
        }
        func_8022979C(obj, 4, 4);
        func_8022979C(obj, 5, 5);
        func_80231408(actor, 0x80000);
        *(s16*)(obj + 0x36) = 0x80;
        *(s32*)(obj + 0xC) |= 0x1000;
        return;
    }
    if (!look->saved) {
        return; // spawned as a ghost (a room change): a spawn's own values are not known
    }
    func_8022979C(obj, 4, look->parts[0]);
    func_8022979C(obj, 5, look->parts[1]);
    if (!(look->bits & 0x80000)) {
        func_802313DC(actor, 0x80000);
    }
    *(s16*)(obj + 0x36) = look->alpha;
    *(s32*)(obj + 0xC) = (*(s32*)(obj + 0xC) & ~0x1000) | (look->bits & 0x1000);
}

// Puts a downed or revived player next to the living one: player 2 by the
// pending placement (held frozen until coop_partner_tick places it, as at a
// room entry), player 1 on a free tile next to player 2 (coop_find_tile; with
// none, player 2's own tile).
static void coop_place_down(s32 i) {
    s32 tile[3];

    if (i == 1) {
        coop_place_pending = 1;
        func_80245208(coop_slot, 1);
        return;
    }
    if (!coop_partner_active()) {
        return;
    }
    if (!coop_find_tile(coop_slot, tile)) {
        func_802454F8(coop_slot, tile);
    }
    recomp_printf("coop: place slot=0 tile=%d,%d,%d next to slot=%d\n", tile[0], tile[1], tile[2], coop_slot);
    coop_placing = 1;
    coop_put(0, tile);
    coop_placing = 0;
}

// A downed player plays again, for one shared life (none left: it stays
// down), with full hearts and its own look. `fresh` at a room's init, before
// the spawn (a new actor, placed by the room); `place` puts it next to the
// living player.
static void coop_revive(s32 i, const char* why, s32 fresh, s32 place) {
    CoopPlayer* p = &g_Players[coop_slot_of(i)];

    if (!coop_down[i]) {
        return;
    }
    if (D_802AC624 <= 0) {
        recomp_printf("coop: stays down slot=%d why=%s lives=%d\n", coop_slot_of(i), why, D_802AC624);
        return;
    }
    D_802AC624--;
    coop_down[i] = 0;
    p->states &= ~COOP_STATE_GHOST;
    p->heartCount = COOP_P2_HEARTS;
    if (!fresh) {
        coop_ghost_look(i, 0);
    }
    coop_look[i].saved = 0;
    recomp_printf("coop: revive slot=%d why=%s hearts=%d lives=%d\n", coop_slot_of(i), why, p->heartCount,
                  D_802AC624);
    if (place) {
        coop_place_down(i);
    }
}

// Hook at the entry of func_8023D0F8, the setup of every scene load (stage
// rooms, menus, cutscenes): counted, so that a stage room's init can tell a
// room change from a stage start (#22).
void coop_scene_load(void) {
    coop_scene_loads++;
}

// The downed players at a stage room's init (#22). A retry after a story
// death brings both back (the game's own retry, no extra life). A stage start
// (another world or stage than the last stage room, or another scene in
// between: the world map, a cutscene) revives a downed player for a shared
// life. A room change keeps it down, a ghost with one heart: player 2's spawn
// at its join gives it the look; player 1's flag and look are put back after
// the room's spawn loop (coop_stage_spawned).
static void coop_stage_downs(void) {
    s32 stage = D_802AC5E8 * 4 + D_802AC5EC;
    s32 start = stage != coop_stage_id || coop_scene_loads != coop_stage_scene + 1;
    s32 i;

    coop_stage_id = stage;
    coop_stage_scene = coop_scene_loads;
    for (i = 0; i < 2; i++) {
        CoopPlayer* p = &g_Players[coop_slot_of(i)];

        if (coop_story_dead) {
            coop_down[i] = 0;
            coop_look[i].saved = 0;
        } else if (start) {
            coop_revive(i, "stage", 1, 0);
        }
        if (coop_down[i]) {
            p->states |= COOP_STATE_GHOST;
            p->heartCount = 1;
            recomp_printf("coop: still down slot=%d lives=%d\n", coop_slot_of(i), D_802AC624);
        } else {
            p->states &= ~COOP_STATE_GHOST;
        }
    }
    coop_story_dead = 0;
}

// Hook in func_80245CDC at 0x80245D68, after its spawn loop, which clears
// +0x68 of every slot it spawns (in the branch's delay slot): a player 1 still
// down is made a ghost again, flag and look on the new actor (#22). Player 2
// is not spawned by the loop; its own spawn keeps the flag and gives the look.
void coop_stage_spawned(void) {
    if (coop_slot < 0 || !coop_down[0]) {
        return;
    }
    g_Players[0].states |= COOP_STATE_GHOST;
    coop_ghost_look(0, 1);
}

// Hook at the entry of func_80245CDC, the shared player init of a stage room,
// before its spawn loop. Player 2 is not spawned by the loop: it joins once
// the room's opening is over (coop_partner_tick), so that it never stands in
// the way of player 1's entry walk (#14). This sets its stats and its pending
// placement. Slot 1 reads pad 1 (controlType 1); slot 4 has no pad buffer and
// reads its command record (controlType 2), filled by coop_feed_command.
void coop_stage_init(s32 slot) {
    CoopPlayer* p1 = &g_Players[0];
    CoopPlayer* p2 = &g_Players[slot];

    coop_place_pending = 1;
    coop_joined = 0;
    coop_p1_free = 0;
    coop_scene_on = 0;
    coop_scene_frozen = 0;
    coop_p1_dying = 0;
    if (g_LevelLoader & 0x4) {
        coop_slot = -1;
        coop_down[0] = coop_down[1] = 0;
        coop_stage_id = -1;
        return; // battle: the battle setup owns slots 0-3
    }
    coop_slot = slot;
    p2->controlType = 0;
    p2->levelClass = NULL; // the last room's actor is gone with its room
    p2->heartCount = COOP_P2_HEARTS;
    coop_stage_downs();
    p2->bombCount = p1->bombCount;
    p2->firepower = p1->firepower;
    p2->bombType = 0x200;
    p2->spawnLocation[0] = p1->spawnLocation[0];
    p2->spawnLocation[1] = p1->spawnLocation[1];
    p2->spawnLocation[2] = p1->spawnLocation[2];
    recomp_printf("coop: stage init overlay=0x%02X slot=%d controlType=%d hearts=%d bombs=%d fire=%d\n",
                  D_802AC5D4 & 0xFFFF, slot, (slot == 4) ? 2 : 1, p2->heartCount, p2->bombCount, p2->firepower);
}

// Hook at the entry of func_802511EC(slot, x, y, z), which puts a slot on a
// tile (room entries and story teleports, through func_802512C0 for S), when
// slot 0 is placed. Player 2 is not put next to player 1 at once: on or ahead
// of player 1's tile it stands in the way of player 1's scripted entry walk,
// which then never ends (#14), and the room may still change its tiles. It
// waits instead (held frozen where it stands, once it has joined), and
// coop_partner_tick places it once player 1 has settled.
void coop_place_partner(s32 slot, s32 x, s32 y, s32 z) {
    if (slot != coop_slot || coop_placing) {
        return; // coop_placing: player 1 put next to player 2, who stays where it is
    }
    coop_place_pending = 1;
    if (coop_partner_active()) {
        func_80245208(slot, 1);
    }
    recomp_printf("coop: place pending slot=%d p1 tile=%d,%d,%d\n", slot, x, y, z);
}

// Hook at the entry of func_8023A870(slot, cause), a death (#18, #22). While
// the partner lives, a dying player (either one) does not run the story death
// (flags 0x92, the death process, the room's retry): it is down instead, a
// ghost as in battle. It stays a player (its update runs: it moves, the scene
// waits on it finish) with the ghost flag, which the game's own code reads
// (no bombs, no lifting, no pickups, speed x0.8, rivals ignore it), and the
// ghost look. Returns 1 when it took the death; the hook then skips
// func_8023A870 and returns 0, so the damage handler keeps the slot in play
// with one heart. No timed respawn: coop_revive's events bring it back. A
// death of the last living player runs the story death as before.
s32 coop_partner_death(s32 slot, s32 cause) {
    s32 i = coop_index(slot);

    if (i < 0 || coop_slot < 0 || coop_down[i] || coop_p1_dying || coop_story_dead) {
        return 0;
    }
    if (!coop_living(i ^ 1)) {
        return 0; // the last player, or player 2 not in the room yet
    }
    coop_down[i] = 1;
    g_Players[slot].states |= COOP_STATE_GHOST;
    coop_ghost_look(i, 1);
    recomp_printf("coop: down slot=%d cause=%d lives=%d ghost\n", slot, cause, D_802AC624);
    if (cause == 3) {
        coop_place_down(i); // a fall: out of the pit, next to the partner
    }
    return 1;
}

// Hook at the entry of func_80248148(slot, cause), damage: a ghost takes none
// (#22). Returns 1 to skip the function. A fall (cause 3, the update's pit)
// puts the ghost back next to the living player.
s32 coop_ghost_hit(s32 slot, s32 cause) {
    s32 i = coop_index(slot);

    if (!coop_is_down(slot)) {
        return 0;
    }
    if (cause == 3) {
        recomp_printf("coop: ghost fell slot=%d\n", slot);
        coop_place_down(i);
    }
    return 1;
}

// Hook at the entry of func_8023E8AC, a stage won (the goal, a boss, the last
// rival): a downed player is revived where it is, for a shared life (#22).
void coop_stage_clear(void) {
    if (coop_slot < 0) {
        return;
    }
    coop_revive(0, "clear", 0, 0);
    coop_revive(1, "clear", 0, 0);
}

// Hook in func_8023A870 at 0x8023A9AC, on the story death's path (flags 0x92
// and the death process follow), for the dying slot (#23). Player 1's death
// animation does not go through the freeze wrappers until just before the
// room's teardown, so player 2 would play on during it: walk, take exits, be
// hit. It is frozen here (control lock and damage guard), its trigger tests
// stop, and a release of player 1 does not release it, until the next room's
// init (the retry's reload) spawns it anew. Player 2's story death (player 1
// down, #22) plays on player 1's actor: player 1's ghost is frozen for it.
void coop_story_death(s32 slot) {
    if (coop_slot < 0) {
        return;
    }
    coop_story_dead = 1;
    if (slot == coop_slot && g_Players[0].levelClass != NULL) {
        recomp_printf("coop: freeze slot=0 on=1 death of slot=%d\n", slot);
        func_80245208(0, 1);
    }
    if (slot != 0) {
        return;
    }
    coop_p1_dying = 1;
    if (!coop_partner_active()) {
        return; // not joined yet: nothing to freeze
    }
    recomp_printf("coop: freeze partner slot=%d on=1 death\n", coop_slot);
    func_80245208(coop_slot, 1);
}

// Hook at the entry of func_8024E088(0), player 1's update, every frame:
// places player 2 while its placement is pending (#55). The room's opening
// must be over first: player 1 free of walks (flag 0x400) and freezes
// (0x2010000) for COOP_SETTLE_FRAMES (room 0x29 still changes the tiles behind
// its entry walk right after it). Then player 2 goes on a free floor tile
// next to player 1 (coop_find_tile), whether player 1 moves or not; while
// there is none it waits. The first time in a room this spawns player 2 there
// (the spawn point set by func_802511EC without an actor, then g_SpawnPlayer,
// as the battle respawn func_8023EB28 spawns); afterwards (a room entry, a
// teleport, a revive) it moves the actor and releases it.
void coop_partner_tick(s32 slot) {
    CoopPlayer* p1 = &g_Players[0];
    s32 tile[3];
    s32 at[3];

    if (slot != coop_slot || p1->levelClass == NULL) {
        return;
    }
    if (p1->flags & (COOP_FLAG_WALK | COOP_FLAG_FROZEN)) {
        coop_p1_free = 0;
    } else if (coop_p1_free < COOP_SETTLE_FRAMES) {
        coop_p1_free++;
    }
    if (!coop_place_pending || coop_p1_free < COOP_SETTLE_FRAMES || coop_p1_dying ||
        (coop_joined && !coop_partner_active())) {
        return; // a frozen player 2 waits for the retry
    }
    if (!coop_find_tile(0, tile)) {
        return; // no free tile next to player 1 yet
    }
    coop_place_pending = 0;
    func_802454F8(0, at);
    recomp_printf("coop: place slot=%d tile=%d,%d,%d p1 tile=%d,%d,%d\n", slot, tile[0], tile[1], tile[2], at[0],
                  at[1], at[2]);
    if (!coop_joined) {
        func_802511EC(slot, tile[0], tile[1], tile[2]);
        g_Players[slot].controlType = (slot == 4) ? 2 : 1;
        g_SpawnPlayer(slot);
        coop_joined = 1;
    }
    coop_put(slot, tile);
    func_80245208(slot, 0);
}

// Hooks at the entries of func_802452E8(on) (with the idle animation) and
// func_80245280(on), the cutscene freeze and release of S, when S is player 1:
// player 2 is frozen and released with player 1 (#16), the way func_8023C170
// freezes every player. While its placement is pending player 2 is held
// frozen already and left alone, and a release leaves a player 2 that joined
// during the scene alone (room 0x28 releases player 1 twice after its
// opening dialogue); when player 1 was moved during the scene (a scripted
// walk), the release keeps player 2 frozen until it is placed next to player
// 1 again.
// A scene revives a downed player (#22) when it ends: a freeze of S that
// began once player 1 had been free for COOP_SETTLE_FRAMES (not a room's
// entry walk or opening) and is released in the same room (not an exit or a
// death, which the room's teardown ends).
void coop_freeze_partner(s32 on, s32 anim) {
    s32 tile[3];

    if (on) {
        coop_scene_on = coop_scene_on || (coop_p1_free >= COOP_SETTLE_FRAMES && !coop_p1_dying && !coop_story_dead);
    } else if (coop_scene_on) {
        coop_scene_on = 0;
        coop_revive(0, "scene", 0, 1);
        coop_revive(1, "scene", 0, 1);
    }
    if (!coop_partner_active() || coop_place_pending) {
        return; // not joined yet, or held frozen for its placement already
    }
    if (!on && coop_p1_dying) {
        return; // held frozen through player 1's death until the retry (#23)
    }
    if (!on && !coop_scene_frozen) {
        return; // joined during the scene, not frozen with player 1
    }
    coop_scene_frozen = on;
    if (!on && g_Players[0].levelClass != NULL) {
        func_802454F8(0, tile);
        if (!coop_same_tile(tile, coop_freeze_tile)) {
            coop_place_pending = 1;
            recomp_printf("coop: place pending slot=%d after a scene\n", coop_slot);
            return;
        }
    }
    if (on && !(g_Players[coop_slot].flags & COOP_FLAG_FROZEN) && g_Players[0].levelClass != NULL) {
        func_802454F8(0, coop_freeze_tile);
    }
    recomp_printf("coop: freeze partner slot=%d on=%d\n", coop_slot, on);
    if (anim) {
        D_802AC894 = coop_slot;
        func_802452E8(on);
        D_802AC894 = 0;
    } else {
        func_80245208(coop_slot, on);
    }
}

// Whether player 2 stands in a rectangle of tiles (x0..x1, z0..z1) and, if
// y1 >= y0, on a floor level y0..y1; refreshes its entry of D_802ACEC8.
static s32 coop_partner_inside(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1) {
    s32* tile;

    if (!coop_partner_active() || coop_down[1] || coop_place_pending || coop_p1_dying) {
        return 0;
    }
    tile = D_802ACEC8[coop_slot];
    func_802454F8(coop_slot, tile);
    if (tile[0] < x0 || tile[0] > x1 || tile[2] < z0 || tile[2] > z1) {
        return 0;
    }
    if (y1 >= y0 && (tile[1] < y0 || tile[1] > y1)) {
        return 0;
    }
    recomp_printf("coop: trigger slot=%d tile=%d,%d,%d\n", coop_slot, tile[0], tile[1], tile[2]);
    return 1;
}

// The three rectangle tests rooms use for exits and event triggers (#17),
// rewritten from the game's code: true when S is inside, as before, or when
// player 2 is; a downed player (a ghost) fires none (#22). S's tile is read first and kept in D_802ACEC8[S], as the
// originals do, for callers that read it afterwards.

// func_802457D8(x0, z0, x1, z1): S inside the rectangle (any floor level).
RECOMP_PATCH s32 func_802457D8(s32 x0, s32 z0, s32 x1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (!coop_is_down(s) && tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, 0, z0, x1, -1, z1);
}

// func_802456FC(x0, y, z0, x1, z1): S inside the rectangle on floor level y.
RECOMP_PATCH s32 func_802456FC(s32 x0, s32 y, s32 z0, s32 x1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (!coop_is_down(s) && tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1 && tile[1] == y) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, y, z0, x1, y, z1);
}

// func_802455A8(x0, y0, z0, x1, y1, z1): S inside the box of tiles.
RECOMP_PATCH s32 func_802455A8(s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1) {
    s32 s = D_802AC894;
    s32* tile = D_802ACEC8[s];

    func_802454F8(s, tile);
    if (!coop_is_down(s) && tile[0] >= x0 && tile[2] >= z0 && tile[0] <= x1 && tile[2] <= z1 && tile[1] >= y0 && tile[1] <= y1) {
        return 1;
    }
    return s == 0 && coop_partner_inside(x0, y0, z0, x1, y1, z1);
}

// Hook at the entry of func_8024E088(slot), the player update: fills the
// co-op slot 4's command record from pad 1 with the same accessors the update
// uses for a controlType 1 slot: the stick as an angle (degrees, before the
// camera's yaw), an elevation and a length, and the held and newly pressed
// buttons. Slot 4 then moves like a pad player (#13).
void coop_feed_command(s32 slot) {
    CoopCommand* cmd = &g_ComputerPlayerMovement[slot];

    func_80239880(COOP_P2_PAD, 0, cmd->move);
    cmd->held = func_80239FF8(COOP_P2_PAD);
    cmd->pressed = func_802399AC(COOP_P2_PAD);
}

// From the hook in the item collision callback (func_80279CA8), which has the
// actor that touches an item: the slot of a player who leaves the item where
// it is, the slot + 0x10 when the item was used up here, else -1 (the pickup
// runs). While player 2 plays:
// - a ghost passes through every item (#22): the pickup (func_802797BC)
//   gives a ghost nothing, but the callback would remove the item;
// - a heart (type 4) the living player finds revives the downed partner
//   (#22) and is used up for it, the finder does not get it;
// - a heart passes through a player it would give nothing (#36), so a full
//   player does not use up the other's knocked-out heart: the pickup sets
//   hearts to 2, or adds one with stage flag 0x8 for slots 1-4.
s32 coop_item_passes(u8* actor, u8* item) {
    s32 slot;
    s32 i;

    if (!coop_partner_active()) {
        return -1;
    }
    for (slot = 0; slot < 5 && g_Players[slot].levelClass != actor; slot++) {
    }
    i = coop_index(slot);
    if (slot == 5) {
        return -1;
    }
    if (i >= 0 && coop_down[i]) {
        return slot;
    }
    if (*(s16*)(item + 0x20) != 4) {
        return -1;
    }
    if (i >= 0 && coop_down[i ^ 1] && D_802AC624 > 0) {
        recomp_printf("coop: heart found slot=%d\n", slot);
        coop_revive(i ^ 1, "heart", 0, 1);
        func_802790E0(item);
        return slot + 0x10;
    }
    if ((D_802AC5C0 & 0x8) && slot != 0) {
        return -1;
    }
    return g_Players[slot].heartCount >= 2 ? slot : -1;
}
