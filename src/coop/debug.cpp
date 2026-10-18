// Co-op debug output for unattended runs (tools/run.sh), off unless asked for.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "librecomp/game.hpp"
#include "recomp_ui.h"
#include "coop_debug.h"

extern std::vector<recomp::GameEntry> supported_games;

namespace {
bool env_flag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}
}

// Called by a hook at the entry of the game's overlay loader, func_8022691C
// (bm64.us.toml), with the overlay ID it is asked to load. With
// BM64_COOP_LOG_OVERLAYS=1 each load is one line on stderr, e.g.
// "coop: overlay 0x20" for the title screen (docs/coop/design.md, *Overlay map*).
extern "C" void coop_log_overlay_load(uint32_t id) {
    static const bool enabled = env_flag("BM64_COOP_LOG_OVERLAYS");
    if (enabled) {
        std::fprintf(stderr, "coop: overlay 0x%02X\n", id & 0xFFFF);
    }
}

void coop::autostart() {
    static bool done = false;
    if (done || !env_flag("BM64_COOP_AUTOSTART")) {
        return;
    }
    done = true;
    const std::u8string& game_id = supported_games[0].game_id;
    if (!recomp::is_rom_valid(const_cast<std::u8string&>(game_id))) {
        std::fprintf(stderr, "coop: autostart: no valid stored ROM %s.z64 in the app folder\n",
            reinterpret_cast<const char*>(game_id.c_str()));
        return;
    }
    std::fprintf(stderr, "coop: autostart %s\n", reinterpret_cast<const char*>(game_id.c_str()));
    recomp::start_game(game_id);
    recompui::hide_all_contexts();
}
