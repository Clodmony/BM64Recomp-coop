// Co-op: the "PRESS Z TO SKIP" hint during a new file's story intro (#77).
// The skip itself is in src/coop/harness.cpp; without a hint on screen nobody
// knows the button is there (the person running the session, 2026-10-21).
//
// Drawn the way the co-op HUD draws its own icons (#53, coop_hud.c): the text
// is painted once into an RGBA5551 texture from a 5x7 pixel font, and a draw
// object of the game's own pool (func_8022A328) puts it on screen twice its
// size, low and centred. The host decides *when* it shows - the story intro,
// and never in an online session - and the per-frame hook passes each change
// of that in (bm64.us.toml, request bit 8 of coop_harness_frame). Like the
// HUD, this only draws: no game state is written, and in a session it never
// shows at all, so the lockstep's hash is untouched either way.
#include "coop.h"

#define COOP_HINT_GLYPH_W 5
#define COOP_HINT_GLYPH_H 7
#define COOP_HINT_CELL 6                                     // a glyph and one column of air
#define COOP_HINT_CHARS 15                                   // "PRESS Z TO SKIP"
#define COOP_HINT_W (COOP_HINT_CHARS * COOP_HINT_CELL + 2)   // 92, with the outline's margin
#define COOP_HINT_H (COOP_HINT_GLYPH_H + 2)                  // 9
#define COOP_HINT_SCALE 2
#define COOP_HINT_X ((320 - COOP_HINT_W * COOP_HINT_SCALE) / 2) // centred: 68
#define COOP_HINT_Y 196                                      // low, inside the 8,6 304x228 screen

static const char* const coop_hint_text = "PRESS Z TO SKIP";

typedef struct CoopHintGlyph {
    char c;
    const char* rows[COOP_HINT_GLYPH_H];
} CoopHintGlyph;

// Only the letters the text uses; '#' is a lit pixel.
static const CoopHintGlyph coop_hint_font[] = {
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
};

static u16 coop_hint_tex[COOP_HINT_W * COOP_HINT_H] __attribute__((aligned(8)));
static s32 coop_hint_painted;
static u8* coop_hint_obj; // our draw object while the hint shows

void coop_hint_draw(u8* obj, Gfx** gfxP);
// A draw object of the game's pool, as the co-op HUD takes one (coop_hud.c):
// kind 1 calls draw(obj, &gfx) each frame.
u8* func_8022A328(void (*draw)(u8*, Gfx**), s32 priority, s32 kind, s32 arg, s32 unk);
void func_8022A1E8(u8* obj); // free a draw object

static u16 coop_hint_rgba(s32 r, s32 g, s32 b) {
    return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
}

static const char* const* coop_hint_glyph(char c) {
    s32 i;

    for (i = 0; i < (s32)(sizeof(coop_hint_font) / sizeof(coop_hint_font[0])); i++) {
        if (coop_hint_font[i].c == c) {
            return coop_hint_font[i].rows;
        }
    }
    return NULL; // a space, or a letter the font does not have
}

static u16 coop_hint_at(s32 x, s32 y) {
    if (x < 0 || y < 0 || x >= COOP_HINT_W || y >= COOP_HINT_H) {
        return 0;
    }
    return coop_hint_tex[y * COOP_HINT_W + x];
}

// The text in white, then a black outline around it, so it reads over any
// scene without a panel behind it. Transparent elsewhere (alpha 0).
static void coop_hint_paint(void) {
    const u16 white = coop_hint_rgba(248, 248, 248);
    const u16 black = coop_hint_rgba(16, 16, 16);
    s32 i;
    s32 x;
    s32 y;

    for (i = 0; i < COOP_HINT_W * COOP_HINT_H; i++) {
        coop_hint_tex[i] = 0;
    }
    for (i = 0; i < COOP_HINT_CHARS; i++) {
        const char* const* rows = coop_hint_glyph(coop_hint_text[i]);

        if (rows == NULL) {
            continue;
        }
        for (y = 0; y < COOP_HINT_GLYPH_H; y++) {
            for (x = 0; x < COOP_HINT_GLYPH_W; x++) {
                if (rows[y][x] == '#') {
                    coop_hint_tex[(y + 1) * COOP_HINT_W + (i * COOP_HINT_CELL + x + 1)] = white;
                }
            }
        }
    }
    // The outline: every clear pixel that touches a white one, diagonals
    // included, so a glyph keeps its shape at twice the size. Written in
    // place, which is safe because only white neighbours count - the black
    // pixels this adds never spread the outline further.
    for (y = 0; y < COOP_HINT_H; y++) {
        for (x = 0; x < COOP_HINT_W; x++) {
            s32 dx;
            s32 dy;

            if (coop_hint_tex[y * COOP_HINT_W + x] != 0) {
                continue;
            }
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    if (coop_hint_at(x + dx, y + dy) == white) {
                        coop_hint_tex[y * COOP_HINT_W + x] = black;
                    }
                }
            }
        }
    }
}

// The draw object's callback, once a frame while the hint shows.
void coop_hint_draw(u8* obj, Gfx** gfxP) {
    Gfx* gfx = *gfxP;

    gDPSetRenderMode(gfx++, G_RM_AA_TEX_EDGE, G_RM_AA_TEX_EDGE2);
    gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gSPTexture(gfx++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTexturePersp(gfx++, G_TP_NONE);
    gDPSetTextureLUT(gfx++, G_TT_NONE);
    gDPLoadTextureBlock(gfx++, coop_hint_tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, COOP_HINT_W, COOP_HINT_H, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    // 0x0400 is one texel per pixel, so 0x0400 / scale draws it `scale` times
    // as large
    gSPTextureRectangle(gfx++, COOP_HINT_X << 2, COOP_HINT_Y << 2,
                        (COOP_HINT_X + COOP_HINT_W * COOP_HINT_SCALE) << 2,
                        (COOP_HINT_Y + COOP_HINT_H * COOP_HINT_SCALE) << 2, 0, 0, 0, 0x0400 / COOP_HINT_SCALE,
                        0x0400 / COOP_HINT_SCALE);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    *gfxP = gfx;
}

// From the per-frame hook (bm64.us.toml), with the host side's answer in `on`
// (coop_harness_frame, request bit 8, only when it changed): register our draw
// object while the hint shows and free it again after.
void coop_intro_hint(s32 on) {
    if (on && coop_hint_obj == NULL) {
        if (!coop_hint_painted) {
            coop_hint_paint();
            coop_hint_painted = 1;
        }
        coop_hint_obj = func_8022A328(coop_hint_draw, 0x3F, 1, 0, 0);
        if (coop_hint_obj == NULL) {
            recomp_printf("coop: hint none: no free draw object\n");
            return;
        }
        *(u32*)(coop_hint_obj + 0xC) |= 0x210; // as the story HUD's flags
        recomp_printf("coop: hint on x=%d y=%d\n", COOP_HINT_X, COOP_HINT_Y);
    } else if (!on && coop_hint_obj != NULL) {
        if (*(u32*)(coop_hint_obj + 0x3C) == (u32)coop_hint_draw) { // still ours (#86)
            func_8022A1E8(coop_hint_obj);
        }
        coop_hint_obj = NULL;
        recomp_printf("coop: hint off\n");
    }
}
