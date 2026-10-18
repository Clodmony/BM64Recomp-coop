// The test harness for unattended runs (tools/run.sh, tools/scenario.py):
// scripted pads, a warp into a stage, probes of the players, and the
// switches for player 2 (#4, #19). All of it is off unless asked for:
//
//   BM64_COOP_INPUT=<file>       pads 0-3 follow the script (format below)
//   BM64_COOP_WARP=<ID>[:<entry>] once the title (overlay 0x20) has run 30
//                                frames, start a new story session and go to
//                                overlay ID (entry default 0x10, as the world
//                                map starts a stage)
//   BM64_COOP_P2=1|4             a second human player in that slot in story
//                                rooms (patches/coop_player2.c): slot 1 reads
//                                pad 1, slot 4 reads its command record, which
//                                pad 1 fills (4 in an online session, whose
//                                pad 1 is the client's: src/coop/session.cpp)
//   BM64_COOP_LOG_PLAYERS=1      one line per room init, spawn (and its model
//                                asset), placement, bomb, hit, death and room
//                                teardown
//
// Input script: one step per line, '#' starts a comment.
//   overlay <ID>        wait for the next load of overlay ID (the n-th
//                       `overlay ID` step waits for the n-th load of ID)
//   wait <frames>       wait that many game frames
//   pad <n> <buttons> [<x> <y>]
//                       from now on pad n holds the buttons (A B Z START L R
//                       DU DD DL DR CU CD CL CR joined with '+', or '-' for
//                       none) and the stick at x, y (-1..1, default 0)
//   probe <label>       log "coop: probe <label>", then one line per player
//   tile <slot> <x> <z> put the player in slot (0-4) on tile x, z of the room,
//                       at its floor level (patches/coop_harness.c)
//   log <text>          log "coop: input: <text>"
//   end                 log "coop: input end"; the pads stay as they are
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "session.h"

namespace {

bool env_flag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

struct Pad {
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Step {
    enum class Kind { Overlay, Wait, Pad, Probe, Log, Tile, End } kind;
    int line;
    uint32_t value = 0; // overlay ID, frames, pad number, slot
    int32_t tile[2] = {0, 0};
    Pad pad;
    std::string text;
};

struct Script {
    std::vector<Step> steps;
    size_t next = 0;
    uint64_t wait_until = 0;
    std::map<uint32_t, int> overlay_steps_done; // `overlay ID` steps passed, per ID
    std::array<Pad, 4> pads{};
    bool loaded = false;
};

std::mutex mutex;
Script script;
std::map<uint32_t, int> overlay_loads; // loads seen, per overlay ID
uint32_t last_overlay = 0xFFFFFFFF;
uint64_t last_overlay_frame = 0;
uint64_t frames = 0;

bool parse_buttons(const std::string& word, uint16_t& out) {
    static const std::map<std::string, uint16_t> names = {
        {"A", 0x8000}, {"B", 0x4000}, {"Z", 0x2000}, {"START", 0x1000},
        {"DU", 0x0800}, {"DD", 0x0400}, {"DL", 0x0200}, {"DR", 0x0100},
        {"L", 0x0020}, {"R", 0x0010},
        {"CU", 0x0008}, {"CD", 0x0004}, {"CL", 0x0002}, {"CR", 0x0001},
    };
    out = 0;
    if (word == "-") {
        return true;
    }
    std::stringstream parts(word);
    std::string name;
    while (std::getline(parts, name, '+')) {
        auto found = names.find(name);
        if (found == names.end()) {
            return false;
        }
        out |= found->second;
    }
    return true;
}

bool parse_script(const char* path, std::vector<Step>& steps) {
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "coop: input error: cannot open %s\n", path);
        return false;
    }
    std::string text;
    int line = 0;
    while (std::getline(file, text)) {
        line++;
        text = text.substr(0, text.find('#'));
        std::stringstream words(text);
        std::string command;
        if (!(words >> command)) {
            continue;
        }
        Step step{Step::Kind::End, line};
        bool ok = true;
        if (command == "overlay") {
            std::string id;
            char* end = nullptr;
            ok = static_cast<bool>(words >> id);
            ok = ok && (std::strtoul(id.c_str(), &end, 0), *end == '\0');
            step.kind = Step::Kind::Overlay;
            step.value = ok ? static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 0)) : 0;
        } else if (command == "wait") {
            step.kind = Step::Kind::Wait;
            ok = static_cast<bool>(words >> step.value);
        } else if (command == "pad") {
            std::string buttons;
            step.kind = Step::Kind::Pad;
            ok = (words >> step.value >> buttons) && step.value < 4 && parse_buttons(buttons, step.pad.buttons);
            std::vector<std::string> stick{std::istream_iterator<std::string>(words), {}};
            if (ok && stick.size() == 2) {
                char* end_x = nullptr;
                char* end_y = nullptr;
                step.pad.x = std::strtof(stick[0].c_str(), &end_x);
                step.pad.y = std::strtof(stick[1].c_str(), &end_y);
                ok = *end_x == '\0' && *end_y == '\0' && step.pad.x >= -1.0f && step.pad.x <= 1.0f &&
                     step.pad.y >= -1.0f && step.pad.y <= 1.0f;
            } else {
                ok = ok && stick.empty();
            }
        } else if (command == "tile") {
            step.kind = Step::Kind::Tile;
            ok = (words >> step.value >> step.tile[0] >> step.tile[1]) && step.value < 5;
        } else if (command == "probe" || command == "log") {
            step.kind = command == "probe" ? Step::Kind::Probe : Step::Kind::Log;
            std::getline(words >> std::ws, step.text);
        } else if (command == "end") {
            step.kind = Step::Kind::End;
        } else {
            ok = false;
        }
        std::string extra;
        if (ok && (step.kind == Step::Kind::Overlay || step.kind == Step::Kind::Wait || step.kind == Step::Kind::Tile ||
                   step.kind == Step::Kind::End)) {
            ok = !(words >> extra);
        }
        if (!ok) {
            std::fprintf(stderr, "coop: input error: %s line %d: %s\n", path, line, text.c_str());
            return false;
        }
        steps.push_back(step);
    }
    return true;
}

void load_script() {
    if (script.loaded) {
        return;
    }
    script.loaded = true;
    const char* path = std::getenv("BM64_COOP_INPUT");
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    if (!parse_script(path, script.steps)) {
        script.steps.clear();
        return;
    }
    std::fprintf(stderr, "coop: input %s, %zu steps\n", path, script.steps.size());
}

// Runs the script's steps due this frame; returns true when one asks for a
// probe. A tile step fills tile (slot, x, z) and ends the frame's steps.
bool run_script(int32_t* tile) {
    bool probe = false;
    while (script.next < script.steps.size()) {
        const Step& step = script.steps[script.next];
        switch (step.kind) {
            case Step::Kind::Overlay: {
                int& done = script.overlay_steps_done[step.value];
                if (overlay_loads[step.value] <= done) {
                    return probe;
                }
                done++;
                break;
            }
            case Step::Kind::Wait:
                if (script.wait_until == 0) {
                    script.wait_until = frames + step.value;
                }
                if (frames < script.wait_until) {
                    return probe;
                }
                script.wait_until = 0;
                break;
            case Step::Kind::Pad:
                script.pads[step.value] = step.pad;
                break;
            case Step::Kind::Probe:
                std::fprintf(stderr, "coop: probe %s\n", step.text.c_str());
                std::fflush(stderr);
                probe = true;
                script.next++;
                return probe; // one probe per frame, so its lines follow its label
            case Step::Kind::Log:
                std::fprintf(stderr, "coop: input: %s\n", step.text.c_str());
                break;
            case Step::Kind::Tile:
                tile[0] = static_cast<int32_t>(step.value);
                tile[1] = step.tile[0];
                tile[2] = step.tile[1];
                script.next++;
                return probe;
            case Step::Kind::End:
                std::fprintf(stderr, "coop: input end\n");
                break;
        }
        script.next++;
    }
    return probe;
}

struct Warp {
    bool wanted = false;
    bool done = false;
    int32_t overlay = 0;
    int32_t entry = 0x10;
};

Warp read_warp() {
    Warp warp;
    const char* value = std::getenv("BM64_COOP_WARP");
    if (value == nullptr || value[0] == '\0') {
        return warp;
    }
    char* rest = nullptr;
    warp.overlay = static_cast<int32_t>(std::strtol(value, &rest, 0));
    if (rest != nullptr && *rest == ':') {
        warp.entry = static_cast<int32_t>(std::strtol(rest + 1, nullptr, 0));
    }
    warp.wanted = true;
    return warp;
}

// Patches print with recomp_printf (stdout), the host with stderr: line
// buffering keeps the two in order in a run's game.log.
[[maybe_unused]] const bool line_buffered = [] {
    if (env_flag("BM64_COOP_INPUT") || env_flag("BM64_COOP_P2") || env_flag("BM64_COOP_LOG_PLAYERS") ||
        env_flag("BM64_COOP_SESSION")) {
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    return true;
}();

} // namespace

bool coop::scripted_input_active() {
    std::lock_guard lock{mutex};
    load_script();
    return !script.steps.empty();
}

bool coop::scripted_input(int pad, uint16_t* buttons, float* x, float* y) {
    std::lock_guard lock{mutex};
    load_script();
    if (script.steps.empty() || pad < 0 || pad >= 4) {
        return false;
    }
    *buttons = script.pads[pad].buttons;
    *x = script.pads[pad].x;
    *y = script.pads[pad].y;
    return true;
}

void coop::note_overlay_load(uint32_t id) {
    std::lock_guard lock{mutex};
    id &= 0xFFFF;
    overlay_loads[id]++;
    last_overlay = id;
    last_overlay_frame = frames;
}

// Called by the per-frame hook (func_8023A208, bm64.us.toml). Returns what the
// hook should call in the patches this frame: bit 0 coop_warp(args[0],
// args[1]), bit 1 coop_probe(), bit 2 coop_harness_tile(args[2], args[3],
// args[4]).
extern "C" uint32_t coop_harness_frame(int32_t* args) {
    static Warp warp = read_warp();
    std::lock_guard lock{mutex};
    uint32_t requests = 0;
    load_script();
    frames++;
    if (warp.wanted && !warp.done && last_overlay == 0x20 && frames - last_overlay_frame >= 30) {
        warp.done = true;
        args[0] = warp.overlay;
        args[1] = warp.entry;
        requests |= 1;
    }
    args[2] = -1;
    if (run_script(&args[2])) {
        requests |= 2;
    }
    if (args[2] >= 0) {
        requests |= 4;
    }
    return requests;
}

// BM64_COOP_P2: the co-op spike's slot for player 2, 1 or 4; 0 when off.
// Without it, an online session that connected (src/coop/session.cpp) has
// player 2 in slot 4, read from the client's pad; one that found no partner
// has none.
extern "C" int32_t coop_p2_slot(void) {
    static const char* value = std::getenv("BM64_COOP_P2");
    static const int32_t slot = [] {
        int32_t slot = value != nullptr ? static_cast<int32_t>(std::strtol(value, nullptr, 0)) : 0;
        if (slot != 0 && slot != 1 && slot != 4) {
            std::fprintf(stderr, "coop: BM64_COOP_P2 must be 1 or 4, not %s; off\n", value);
            slot = 0;
        }
        return slot;
    }();
    if (value == nullptr) {
        return coop::session_connected() ? 4 : 0;
    }
    return slot;
}

// Hooks in bm64.us.toml report player events here (BM64_COOP_LOG_PLAYERS=1):
// "coop: <event> slot=<slot> [<a_name>=<a>] [<b_name>=<b>]".
extern "C" void coop_log_player_event(const char* event, int32_t slot, const char* a_name, int32_t a,
    const char* b_name, int32_t b) {
    static const bool enabled = env_flag("BM64_COOP_LOG_PLAYERS");
    if (!enabled) {
        return;
    }
    char extra[64] = "";
    if (a_name != nullptr && b_name != nullptr) {
        std::snprintf(extra, sizeof(extra), " %s=%d %s=%d", a_name, a, b_name, b);
    } else if (a_name != nullptr) {
        std::snprintf(extra, sizeof(extra), " %s=%d", a_name, a);
    }
    std::fprintf(stderr, "coop: %s slot=%d%s\n", event, slot, extra);
}

