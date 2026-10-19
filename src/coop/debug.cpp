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
#include <fcntl.h>
#include <io.h>
#include <thread>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"
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

std::filesystem::path log_file; // coop.log, once start_log made it
}

#ifdef _WIN32
namespace {
// The tee of a game whose stdout and stderr go somewhere (a redirect, the
// harness in Wine): a thread copies the pipe they now write into to coop.log
// and to where they went before.
HANDLE tee_pipe = nullptr; // the pipe's read end
std::atomic<bool> tee_busy{false};

bool tee_to(const std::filesystem::path& path) {
    int file = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    int fds[2];
    if (file < 0 || _pipe(fds, 64 * 1024, _O_BINARY) != 0) {
        if (file >= 0) {
            _close(file);
        }
        return false;
    }
    int out = _dup(_fileno(stdout)); // where both went (run.sh sends them to one file)
    _dup2(fds[1], _fileno(stdout));
    _dup2(fds[1], _fileno(stderr));
    _close(fds[1]);
    SetStdHandle(STD_OUTPUT_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
    tee_pipe = reinterpret_cast<HANDLE>(_get_osfhandle(fds[0]));
    std::thread([pipe = fds[0], file, out] {
        char buffer[4096];
        int got;
        while ((got = _read(pipe, buffer, sizeof(buffer))) > 0) {
            tee_busy = true;
            _write(file, buffer, static_cast<unsigned>(got));
            if (out >= 0) {
                _write(out, buffer, static_cast<unsigned>(got));
            }
            tee_busy = false;
        }
    }).detach();
    return true;
}

// Waits (up to a second) until the tee has written what the pipe holds: on a
// crash and at the exit, which would otherwise end the thread first.
void drain_tee() {
    if (tee_pipe == nullptr) {
        return;
    }
    for (int i = 0, idle = 0; i < 200 && idle < 2; i++) {
        DWORD queued = 0;
        if (!PeekNamedPipe(tee_pipe, nullptr, 0, nullptr, &queued, nullptr)) {
            return;
        }
        idle = queued == 0 && !tee_busy ? idle + 1 : 0;
        Sleep(5);
    }
}

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
    drain_tee();
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

#ifndef _WIN32
namespace {
void write_all(int fd, const char* data, ssize_t size) {
    while (size > 0) {
        ssize_t done = write(fd, data, static_cast<size_t>(size));
        if (done < 0 && errno == EINTR) {
            continue;
        }
        if (done <= 0) {
            return; // e.g. the terminal is gone: the file still gets it
        }
        data += done;
        size -= done;
    }
}

// The tee (#57): a child process copies the pipe stdout and stderr now write
// into to coop.log and to where stdout went before (the terminal, run.sh's
// game.log). A process of its own, so a crash or a kill of the game loses no
// line: the child reads the pipe to its end, then exits. It ignores the
// terminal's and run.sh's stop signals for that reason.
bool tee_to(const std::filesystem::path& path) {
    int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    int fds[2];
    if (file < 0 || pipe(fds) != 0) {
        if (file >= 0) {
            close(file);
        }
        return false;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);
        close(file);
        return false;
    }
    if (child == 0) { // async-signal-safe calls only: other threads may hold locks
        close(fds[1]);
#ifdef __linux__
        prctl(PR_SET_NAME, "coop-log");
#endif
        for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGQUIT}) {
            signal(sig, SIG_IGN);
        }
        char buffer[4096];
        for (;;) {
            ssize_t got = read(fds[0], buffer, sizeof(buffer));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            write_all(file, buffer, got);
            write_all(STDOUT_FILENO, buffer, got);
        }
        _exit(0);
    }
    close(fds[0]);
    close(file);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    // should the child be gone (killed by hand), a line written to the pipe
    // fails instead of ending the game
    signal(SIGPIPE, SIG_IGN);
    return true;
}
} // namespace
#endif

// The game's own log (#29, #57): stdout and stderr, together, in coop.log in
// the app folder (the portable folder with portable.txt), on every platform;
// the log of the run before is kept as coop.prev.log, which a session sends
// to the other side when it connects, and a session streams coop.log to the
// other side while it is live (session.cpp).
// - Linux and macOS: a child process copies both to coop.log and to where
//   stdout went (tee_to above); the kit's start.sh only starts the game.
// - Windows started from Explorer has no console: stdout and stderr went
//   nowhere and now go straight to coop.log. With somewhere to go (a
//   redirect, the harness in Wine) a thread copies them to both.
// A crash on Windows writes its exception, place and stack to the log first.
void coop::start_log() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_crash);
    _set_invalid_parameter_handler(on_invalid_parameter);
    std::signal(SIGABRT, on_abort);
#endif
    std::error_code ec;
    std::filesystem::path folder = zelda64::get_app_folder_path();
    std::filesystem::create_directories(folder, ec);
    std::filesystem::rename(folder / "coop.log", folder / "coop.prev.log", ec);
    std::filesystem::path path = folder / "coop.log";
    bool ok = false;
#ifdef _WIN32
    // the log survives a crash and keeps both in order (Windows has no line
    // buffering); never on a stream without a file, which is an invalid
    // parameter that ends the program
    auto unbuffered = [](FILE* stream) { std::setvbuf(stream, nullptr, _IONBF, 0); };
    auto goes_somewhere = [](DWORD which, FILE* stream) {
        HANDLE handle = GetStdHandle(which);
        return handle != nullptr && handle != INVALID_HANDLE_VALUE && _fileno(stream) >= 0;
    };
    if (goes_somewhere(STD_OUTPUT_HANDLE, stdout) && goes_somewhere(STD_ERROR_HANDLE, stderr)) {
        ok = tee_to(path);
        unbuffered(stdout);
        unbuffered(stderr);
        std::atexit(drain_tee);
        std::at_quick_exit(drain_tee);
    } else {
        std::string name = path.string();
        if (FILE* empty = std::fopen(name.c_str(), "w")) {
            std::fclose(empty);
        }
        // both appending, so their lines interleave instead of overwriting
        ok = std::freopen(name.c_str(), "a", stdout) != nullptr;
        if (ok) {
            unbuffered(stdout);
        }
        if (std::freopen(name.c_str(), "a", stderr) != nullptr) {
            unbuffered(stderr);
        } else {
            ok = false;
        }
    }
#else
    ok = tee_to(path);
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // whole lines, in order with stderr's
#endif
    if (ok) {
        log_file = path;
    } else {
        std::fprintf(stderr, "coop: no log file: cannot write %s\n", path.string().c_str());
    }
}

std::filesystem::path coop::log_path() {
    return log_file;
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

// The game's frame rate, which patches/required_patches.c (Gfx_InitGfx) hands
// RT64 with gEXSetRefreshRate every frame: 50 for a Europe build with its PAL
// pacing, else 60. RT64 takes it as the rate the game makes frames at and,
// when its target rate is higher, generates the frames in between
// (docs/coop/building.md, *Europe speed*). The first call logs
// "coop: refresh rate <N>".
extern "C" void coop_game_frame_rate(uint8_t* rdram, recomp_context* ctx) {
    static const int32_t rate = [] {
#ifdef BM64_VERSION_EU
        const int32_t value = env_flag("BM64_COOP_PAL60") ? 60 : 50;
#else
        const int32_t value = 60;
#endif
        std::fprintf(stderr, "coop: refresh rate %d\n", value);
        return value;
    }();
    _return(ctx, rate);
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
