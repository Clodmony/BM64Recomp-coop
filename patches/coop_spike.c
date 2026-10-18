// Co-op spike (#4): a second human player in a story stage, and the probes
// the test harness reads. Nothing here runs unless a debug switch asks for it:
// the functions below are called only from hooks in bm64.us.toml, whose
// conditions come from the host (src/coop/harness.cpp, BM64_COOP_* switches).
// docs/coop/design.md, *The follow-up spike*, has the plan and the results.
#include "patches.h"

// g_Players[5], stride 0xC8 (docs/coop/design.md, sections 1-4). Only the
// fields the spike touches are named.
typedef struct CoopPlayer {
    /* 0x00 */ f32 spawnLocation[3];
    /* 0x0C */ f32 yaw;
    /* 0x10 */ s32 flags;
    /* 0x14 */ s32 heartCount;
    /* 0x18 */ s32 bombType;
    /* 0x1C */ s32 bombCount;
    /* 0x20 */ s32 firepower;
    /* 0x24 */ s32 controlType; // 0 none, 1 pad = slot, 2 command record, 3 out
    /* 0x28 */ u8* levelClass;  // +0x40: the model object, position at +0x10
    /* 0x2C */ u8 pad2C[0xC8 - 0x2C];
} CoopPlayer;

// The command record a controlType 2 slot reads in func_8024E088.
typedef struct CoopCommand {
    /* 0x0 */ f32 move[3];
    /* 0xC */ u16 held;
    /* 0xE */ u16 pressed;
} CoopCommand;

extern CoopPlayer g_Players[5];
extern CoopCommand g_ComputerPlayerMovement[5];
extern s32 g_LevelLoader;   // flow flags; 0x4 battle
extern s32 D_802AC5D4;      // overlay ID being played
extern s32 D_802AC894;      // S, the story player's slot
extern s32 D_802AC624;      // lives (story session block +0x14)

void func_8023DA04(s32 continued);
void func_8023AE6C(s32 overlay, s32 entry, s32 fade);
void func_802511EC(s32 slot, s32 x, s32 y, s32 z);
void func_80239880(s32 pad, s32 unk, f32* out);
s32 func_80239FF8(s32 pad);
s32 func_802399AC(s32 pad);
s32 func_80239714(s32 pad, s32 back);
s32 func_802395A8(s32 pad, s32 back);

#define COOP_P2_PAD 1

static s32 coop_mm(f32 v) {
    return (s32)(v * 10.0f);
}

// Hook at the entry of func_80245CDC, the shared player init of a stage room,
// before its spawn loop: makes the co-op slot a player so that the loop spawns
// it. Slot 1 reads pad 1 (controlType 1); slot 4 has no pad buffer and reads
// its command record (controlType 2), filled by coop_feed_command.
void coop_stage_init(s32 slot) {
    CoopPlayer* p1 = &g_Players[0];
    CoopPlayer* p2 = &g_Players[slot];

    if (g_LevelLoader & 0x4) {
        return; // battle: the battle setup owns slots 0-3
    }
    p2->controlType = (slot == 4) ? 2 : 1;
    p2->heartCount = 2;
    p2->bombCount = p1->bombCount;
    p2->firepower = p1->firepower;
    p2->bombType = 0x200;
    p2->spawnLocation[0] = p1->spawnLocation[0];
    p2->spawnLocation[1] = p1->spawnLocation[1];
    p2->spawnLocation[2] = p1->spawnLocation[2];
    recomp_printf("coop: stage init overlay=0x%02X slot=%d controlType=%d hearts=%d bombs=%d fire=%d\n",
                  D_802AC5D4 & 0xFFFF, slot, p2->controlType, p2->heartCount, p2->bombCount, p2->firepower);
}

// Hook at the entry of func_802511EC(slot, x, y, z), which puts a slot on a
// tile (room entries and story teleports, through func_802512C0 for S):
// placing slot 0 first places the co-op slot on tile x, y, z, which the hook
// offsets from slot 0's tile (BM64_COOP_P2_TILE).
void coop_place_partner(s32 slot, s32 x, s32 y, s32 z) {
    if (g_Players[slot].controlType == 0) {
        return;
    }
    func_802511EC(slot, x, y, z);
    recomp_printf("coop: place slot=%d tile=%d,%d,%d\n", slot, x, y, z);
}

// Hook at the entry of func_8024E088(slot), the player update: fills the
// co-op slot 4's command record from pad 1 with the same accessors the update
// uses for a controlType 1 slot. A finding of the spike: filled this way,
// slot 4 moves along one world axis only (docs/coop/design.md).
void coop_feed_command(s32 slot) {
    CoopCommand* cmd = &g_ComputerPlayerMovement[slot];

    func_80239880(COOP_P2_PAD, 0, cmd->move);
    cmd->held = func_80239FF8(COOP_P2_PAD);
    cmd->pressed = func_802399AC(COOP_P2_PAD);
}

// From the per-frame hook: a warp asked for by BM64_COOP_WARP. Starts a new
// story session the way the file select does for a new file, then asks for
// the scene the way the world map starts a stage (entry 0x10).
void coop_warp(s32 overlay, s32 entry) {
    recomp_printf("coop: warp overlay=0x%02X entry=0x%02X\n", overlay, entry);
    func_8023DA04(0);
    func_8023AE6C(overlay, entry, 0);
}

// From the per-frame hook: a probe asked for by the input script. One line
// per slot that is in play; positions in tenths of a world unit.
void coop_probe(void) {
    s32 slot;

    recomp_printf("coop: state overlay=0x%02X S=%d flags=0x%X lives=%d\n", D_802AC5D4 & 0xFFFF, D_802AC894,
                  g_LevelLoader, D_802AC624);
    for (slot = 0; slot < 2; slot++) {
        f32 move[3] = { 0.0f, 0.0f, 0.0f };

        func_80239880(slot, 0, move);
        recomp_printf("coop: pad %d stick=%d,%d move=%d,%d,%d held=0x%04X\n", slot, func_80239714(slot, 0),
                      func_802395A8(slot, 0), coop_mm(move[0]), coop_mm(move[1]), coop_mm(move[2]),
                      func_80239FF8(slot) & 0xFFFF);
    }
    for (slot = 0; slot < 5; slot++) {
        CoopPlayer* p = &g_Players[slot];
        f32* pos;

        if (p->controlType == 0) {
            continue;
        }
        if (p->levelClass == NULL) {
            recomp_printf("coop: player slot=%d controlType=%d hearts=%d actor=none\n", slot, p->controlType,
                          p->heartCount);
            continue;
        }
        pos = (f32*)(*(u8**)(p->levelClass + 0x40) + 0x10);
        recomp_printf("coop: player slot=%d controlType=%d hearts=%d bombs=%d fire=%d flags=0x%X pos=%d,%d,%d\n", slot,
                      p->controlType, p->heartCount, p->bombCount, p->firepower, p->flags, coop_mm(pos[0]),
                      coop_mm(pos[1]), coop_mm(pos[2]));
        if (p->controlType == 2) {
            CoopCommand* cmd = &g_ComputerPlayerMovement[slot];
            recomp_printf("coop: command slot=%d move=%d,%d,%d held=0x%04X pressed=0x%04X\n", slot, coop_mm(cmd->move[0]),
                          coop_mm(cmd->move[1]), coop_mm(cmd->move[2]), cmd->held, cmd->pressed);
        }
    }
}
