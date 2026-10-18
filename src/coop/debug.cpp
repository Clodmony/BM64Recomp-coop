// Co-op debug output for unattended runs (tools/run.sh), off unless asked for.
#include <atomic>
#include <chrono>
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

namespace {
std::atomic<uint64_t> audio_frames{0};
std::atomic<uint64_t> audio_queued_us{0};
std::atomic<uint64_t> audio_skips{0};
}

void coop::note_audio(size_t frames, uint64_t queued_us, bool skipped) {
    audio_frames += frames;
    audio_queued_us = queued_us;
    if (skipped) {
        audio_skips++;
    }
}

// Called by a hook at the entry of func_8023A208, which the game's main loop
// calls once per frame (patches/required_patches.c, func_80225840). With
// BM64_COOP_LOG_FRAMES=N, every N frames one line on stderr:
//   coop: frame <frames> <ms> <retraces> <audio frames> <audio queue us> <audio skips>
// ms counts from the first frame; retraces is the game's own count of the VI
// retraces its scheduler passed on (gThreadFrameCount); the audio numbers
// are totals since the start (frames the game queued, host sample skips) and
// the host queue's depth.
extern "C" void coop_log_frame(uint8_t* rdram) {
    static const long every = [] {
        const char* value = std::getenv("BM64_COOP_LOG_FRAMES");
        return value != nullptr ? std::strtol(value, nullptr, 0) : 0L;
    }();
    if (every <= 0) {
        return;
    }
#ifdef BM64_VERSION_EU
    constexpr uint32_t frame_count_addr = 0x802A8944; // gThreadFrameCount, US 0x802A8954
#else
    constexpr uint32_t frame_count_addr = 0x802A8954; // gThreadFrameCount
#endif
    static uint64_t frames = 0;
    static const auto start = std::chrono::steady_clock::now();
    if (frames++ % every == 0) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        int32_t retraces = *reinterpret_cast<int32_t*>(rdram + (frame_count_addr - 0x80000000));
        std::fprintf(stderr, "coop: frame %llu %lld %d %llu %llu %llu\n", (unsigned long long)(frames - 1),
            (long long)ms, retraces, (unsigned long long)audio_frames.load(),
            (unsigned long long)audio_queued_us.load(), (unsigned long long)audio_skips.load());
    }
}

// PAL pacing for the Europe build, from a hook that tools/europe.py adds to
// bm64.eu.toml in the scheduler loop (__scMain), after it receives a message
// and before it handles it. The game is made for a 50 Hz VI, the runtime's
// VI runs at 60 Hz: dropping one retrace message (VIDEO_MSG) in six lets
// the scheduler, and through it the game loop and audio, see 50 per second.
// BM64_COOP_PAL60=1 turns it off (for comparisons).
extern "C" int coop_pal_skip_retrace(uint32_t msg) {
    constexpr uint32_t video_msg = 0x29A;
    static const bool off = env_flag("BM64_COOP_PAL60");
    static uint32_t retraces = 0;
    if (off || msg != video_msg) {
        return 0;
    }
    return ++retraces % 6 == 0;
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
