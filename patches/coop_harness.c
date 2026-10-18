// The test harness's side in the game (src/coop/harness.cpp,
// docs/coop/building.md, *The test harness*): the warp into a room and the
// probes. Both run only when the per-frame hook in bm64.us.toml asks for them.
#include "coop.h"

static s32 coop_mm(f32 v) {
    return (s32)(v * 10.0f);
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

// From the per-frame hook: a tile step of the input script. Puts the player in
// slot on tile x, z at the floor level it stands on, as a room entry would.
void coop_harness_tile(s32 slot, s32 x, s32 z) {
    s32 tile[3];

    if (g_Players[slot].levelClass == NULL) {
        recomp_printf("coop: tile slot=%d: no actor\n", slot);
        return;
    }
    func_802454F8(slot, tile);
    recomp_printf("coop: tile slot=%d from=%d,%d,%d to=%d,%d,%d\n", slot, tile[0], tile[1], tile[2], x, tile[1], z);
    func_802511EC(slot, x, tile[1], z);
}
