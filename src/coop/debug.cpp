// Co-op debug output for unattended runs (tools/run.sh), off unless asked for.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "librecomp/game.hpp"
#include "recomp_ui.h"
#include "coop_debug.h"
#include "harness.h"
#include "session_ui.h"
#include "zelda_config.h"

extern std::vector<recomp::GameEntry> supported_games;

namespace {
bool env_flag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}
}

#ifdef _WIN32
namespace {
// "module+0xoffset" for a code address, which the build's .pdb resolves
// (llvm-symbolizer --obj=BM64Recompiled.exe 0x140000000+offset).
void print_where(const char* what, DWORD64 address) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, sizeof(path)) > 0) {
        const char* name = std::strrchr(path, '\\');
        std::fprintf(stderr, "coop: crash: %s %s+0x%llX\n", what, name != nullptr ? name + 1 : path,
                     static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::fprintf(stderr, "coop: crash: %s 0x%llX\n", what, static_cast<unsigned long long>(address));
    }
}

// The stack from a thread's registers, unwound with the functions' unwind
// tables (x64).
void print_stack(CONTEXT context) {
    for (int i = 0; i < 48 && context.Rip != 0; i++) {
        print_where("  at", context.Rip);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (function == nullptr) { // a leaf function: the return address is on top of the stack
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
            continue;
        }
        void* handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
    }
    std::fflush(stderr);
}

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "coop: crash: exception 0x%08lX\n", info->ExceptionRecord->ExceptionCode);
    print_where("in", reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress));
    print_stack(*info->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH; // Windows then ends the program as without this
}

void print_here(const char* why) {
    std::fprintf(stderr, "coop: crash: %s\n", why);
    CONTEXT context{};
    RtlCaptureContext(&context);
    print_stack(context);
}

void on_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    print_here("invalid parameter to the C runtime");
    TerminateProcess(GetCurrentProcess(), 0xC0000417); // what the C runtime does without a handler
}

void on_abort(int) {
    print_here("abort");
}
} // namespace
#endif

// The log of a playtest (#29). A Windows build started from Explorer has no
// console: its stdout and stderr go nowhere, and with them the co-op lines.
// Without them, both go to coop.log in the app folder (the portable folder
// with portable.txt); the log of the run before is kept as coop.prev.log,
// which a session sends to the other side when it connects (session.cpp).
// On Linux the kit's start.sh does the same. A crash on Windows writes its
// exception, place and stack to the log first. Elsewhere, and with a console
// or a redirect, the output is left as it is.
void coop::start_log() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_crash);
    _set_invalid_parameter_handler(on_invalid_parameter);
    std::signal(SIGABRT, on_abort);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != nullptr && err != INVALID_HANDLE_VALUE) {
        return;
    }
    std::error_code ec;
    std::filesystem::path folder = zelda64::get_app_folder_path();
    std::filesystem::create_directories(folder, ec);
    std::filesystem::rename(folder / "coop.log", folder / "coop.prev.log", ec);
    std::string path = (folder / "coop.log").string();
    if (FILE* empty = std::fopen(path.c_str(), "w")) {
        std::fclose(empty);
    }
    // both appending, so their lines interleave instead of overwriting
    if (std::freopen(path.c_str(), "a", stdout) != nullptr) {
        std::setvbuf(stdout, nullptr, _IONBF, 0); // the log survives a crash (Windows has no line buffering)
    }
    if (std::freopen(path.c_str(), "a", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
}

// The last limit bytes of the log of the run before (coop.prev.log in the app
// folder), empty without one.
std::string coop::previous_log(size_t limit) {
    std::error_code ec;
    std::filesystem::path path = zelda64::get_app_folder_path() / "coop.prev.log";
    std::ifstream file(path, std::ios_base::binary);
    if (!file) {
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > limit) {
        text.erase(0, text.size() - limit);
    }
    return text;
}

// Called by a hook at the entry of the game's overlay loader, func_8022691C
// (bm64.us.toml), with the overlay ID it is asked to load. With
// BM64_COOP_LOG_OVERLAYS=1 each load is one line on stderr, e.g.
// "coop: overlay 0x20" for the title screen (docs/coop/design.md, *Overlay map*).
// The test harness counts the loads (src/coop/harness.cpp).
extern "C" void coop_log_overlay_load(uint32_t id) {
    static const bool enabled = env_flag("BM64_COOP_LOG_OVERLAYS");
    static const bool crash = env_flag("BM64_COOP_CRASH");
    coop::note_overlay_load(id);
    if (enabled) {
        std::fprintf(stderr, "coop: overlay 0x%02X\n", id & 0xFFFF);
    }
    if (crash && (id & 0xFFFF) == 0x20) { // a test of the crash log (#29)
        std::fprintf(stderr, "coop: crashing on purpose (BM64_COOP_CRASH)\n");
        *static_cast<volatile int*>(nullptr) = 0;
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
    if (coop::launcher_autostart()) {
        return; // BM64_COOP_LAUNCHER: the Co-op menu's Host or Join instead of Start
    }
    std::fprintf(stderr, "coop: autostart %s\n", reinterpret_cast<const char*>(game_id.c_str()));
    recomp::start_game(game_id);
    recompui::hide_all_contexts();
}
