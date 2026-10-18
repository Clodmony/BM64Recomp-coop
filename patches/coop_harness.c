// The test harness's side in the game (src/coop/harness.cpp,
// docs/coop/building.md, *The test harness*): the warp into a room and the
// probes. Both run only when the per-frame hook in bm64.us.toml asks for them.
#include "coop.h"

// File 0-2 in the EEPROM mirror (docs/coop/design.md, section 11): +0 the play
// time in seconds, bit 7 of +4 "slot used".
static u8* coop_save_slot(s32 file) {
    return D_80063000 + 8 + file * 0x90;
}

static s32 coop_mm(f32 v) {
    return (s32)(v * 10.0f);
}

// From the per-frame hook: a warp asked for by BM64_COOP_WARP. Starts a story
// session the way the file select does, for a new file (file < 0) or by
// loading that file (0-2), then asks for the scene the way the world map
// starts a stage (entry 0x10).
void coop_warp(s32 overlay, s32 entry, s32 file) {
    if (file < 0) {
        recomp_printf("coop: warp overlay=0x%02X entry=0x%02X\n", overlay, entry);
        func_8023DA04(0);
    } else {
        func_80293EA0(file);
        func_80293E1C();
        recomp_printf("coop: warp overlay=0x%02X entry=0x%02X file=%d used=%d time=%d\n", overlay, entry, file,
                      (coop_save_slot(file)[4] & 0x80) != 0, *(u32*)coop_save_slot(file));
        func_8023DA04(1);
    }
    func_8023AE6C(overlay, entry, 0);
}

// From the per-frame hook: a save step of the input script. Writes the live
// progress block into file 0-2 and the EEPROM, as the pause menu's save does.
void coop_harness_save(s32 file) {
    func_80293EA0(file);
    func_802947AC();
    recomp_printf("coop: save file=%d used=%d time=%d\n", file, (coop_save_slot(file)[4] & 0x80) != 0,
                  *(u32*)coop_save_slot(file));
}

// From the per-frame hook: a probe asked for by the input script. The camera,
// then one line per slot that is in play, then the item a hit knocked out of
// item_slot (#36; src/coop/harness.cpp notes it, NULL once picked up or the
// room is gone); positions in tenths of a world unit, the camera's yaw and
// pitch in tenths of a degree.
void coop_probe(u8* item, s32 item_slot) {
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
    recomp_printf("coop: camera eye=%d,%d,%d angles=%d,%d,%d\n", coop_mm(D_8008F3E8[0]), coop_mm(D_8008F3E8[1]),
                  coop_mm(D_8008F3E8[2]), coop_mm(D_8008F3F8[0]), coop_mm(D_8008F3F8[1]), coop_mm(D_8008F3F8[2]));
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
    // +0x2 is 1 once func_802790E0 has removed the item; its actor at +0x14
    // is placed like a player's (func_8027AA74)
    if (item != NULL && *(s16*)(item + 0x2) == 0) {
        f32* pos = (f32*)(*(u8**)(*(u8**)(item + 0x14) + 0x40) + 0x10);
        recomp_printf("coop: item from=%d type=%d pos=%d,%d,%d\n", item_slot, *(s16*)(item + 0x20), coop_mm(pos[0]),
                      coop_mm(pos[1]), coop_mm(pos[2]));
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

// From the per-frame hook: a hearts step of the input script. Sets the
// player's hearts, as a heart pickup does (func_8024512C).
void coop_harness_hearts(s32 slot, s32 hearts) {
    recomp_printf("coop: hearts slot=%d from=%d to=%d\n", slot, g_Players[slot].heartCount, hearts);
    g_Players[slot].heartCount = hearts;
}
