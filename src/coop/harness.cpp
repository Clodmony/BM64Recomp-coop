// The test harness for unattended runs (tools/run.sh, tools/scenario.py):
// scripted pads, a warp into a stage, probes of the players, and the
// switches for player 2 (#4, #19). All of it is off unless asked for:
//
//   BM64_COOP_INPUT=<file>       pads 0-3 follow the script (format below)
//   BM64_COOP_WARP=<ID>[:<entry>[:<file>]]
//                                once the title (overlay 0x20) has run 30
//                                frames, start a story session (a new file, or
//                                file 0-2 loaded as the file select does) and
//                                go to overlay ID (entry default 0x10, as the
//                                world map starts a stage)
//   BM64_COOP_P2=1|4             a second human player in that slot in story
//                                rooms (patches/coop_player2.c): slot 1 reads
//                                pad 1, slot 4 reads its command record, which
//                                pad 1 fills (4 in an online session, whose
//                                pad 1 is the client's: src/coop/session.cpp)
//   BM64_COOP_CAMERA=<slot>      the camera follows the player in that slot
//                                (0-4; -1 the game's choice) while it plays;
//                                default: a session's client follows player 2;
//                                following slot 4, its C-buttons are pad 1's
//   BM64_COOP_LOG_PLAYERS=1      one line per room init, spawn (and its model
//                                asset), placement, bomb, hit, the item a hit
//                                knocks out, a pickup, death and room teardown
//
// One thing here is not a switch and is always on: **Z during the intro skips
// it** (#77, *the intro skip* below), a new file straight to the world map.
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
//                       and, with BM64_COOP_LOG_PLAYERS=1, one for the item
//                       a hit knocked out last while it lies in the room
//   tile <slot> <x> <z> put the player in slot (0-4) on tile x, z of the room,
//                       at its floor level (patches/coop_harness.c)
//   hearts <slot> <n>   set the player's hearts to n (0-9), as a pickup would
//                       (patches/coop_harness.c)
//   save <file>         save the live progress block into file 0-2 and the
//                       EEPROM, as the pause menu does (patches/coop_harness.c)
//   drop <slot>         knock a heart out of the player, as a hit does
//                       (func_8027B39C; patches/coop_harness.c)
//   scene <ID> [<entry>] go to overlay ID (entry default 0x10, as the world map
//                       starts a stage) without a new session
//                       (func_8023AE6C; patches/coop_harness.c)
//   freeze <0|1>        freeze or release the story player as a cutscene
//                       does (func_802452E8; patches/coop_harness.c)
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
    enum class Kind { Overlay, Wait, Pad, Probe, Log, Tile, Hearts, Save, Drop, Scene, Freeze, End } kind;
    int line;
    uint32_t value = 0; // overlay ID, frames, pad number, slot, file
    int32_t tile[2] = {0, 0}; // a tile step's x, z; a hearts step's count, a scene step's entry in [0]
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
        } else if (command == "hearts") {
            step.kind = Step::Kind::Hearts;
            ok = (words >> step.value >> step.tile[0]) && step.value < 5 && step.tile[0] >= 0 && step.tile[0] <= 9;
        } else if (command == "save") {
            step.kind = Step::Kind::Save;
            ok = (words >> step.value) && step.value < 3;
        } else if (command == "freeze") {
            step.kind = Step::Kind::Freeze;
            ok = (words >> step.value) && step.value < 2;
        } else if (command == "drop") {
            step.kind = Step::Kind::Drop;
            ok = (words >> step.value) && step.value < 5;
        } else if (command == "scene") {
            std::vector<std::string> numbers{std::istream_iterator<std::string>(words), {}};
            char* end = nullptr;
            step.kind = Step::Kind::Scene;
            step.tile[0] = 0x10;
            ok = numbers.size() == 1 || numbers.size() == 2;
            for (size_t n = 0; ok && n < numbers.size(); n++) {
                uint32_t number = static_cast<uint32_t>(std::strtoul(numbers[n].c_str(), &end, 0));
                ok = *end == '\0';
                if (n == 0) {
                    step.value = number;
                } else {
                    step.tile[0] = static_cast<int32_t>(number);
                }
            }
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
                   step.kind == Step::Kind::Hearts || step.kind == Step::Kind::Save || step.kind == Step::Kind::Drop || step.kind == Step::Kind::Freeze ||
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
// probe. A tile step fills tile (slot, x, z), a hearts step hearts (slot, n),
// a save step *save (the file), a drop step *drop (the slot), a scene step
// scene (ID, entry), a freeze step *freeze (on); each ends the frame's steps.
bool run_script(int32_t* tile, int32_t* hearts, int32_t* save, int32_t* drop, int32_t* scene, int32_t* freeze) {
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
            case Step::Kind::Hearts:
                hearts[0] = static_cast<int32_t>(step.value);
                hearts[1] = step.tile[0];
                script.next++;
                return probe;
            case Step::Kind::Save:
                *save = static_cast<int32_t>(step.value);
                script.next++;
                return probe;
            case Step::Kind::Drop:
                *drop = static_cast<int32_t>(step.value);
                script.next++;
                return probe;
            case Step::Kind::Freeze:
                *freeze = static_cast<int32_t>(step.value);
                script.next++;
                return probe;
            case Step::Kind::Scene:
                scene[0] = static_cast<int32_t>(step.value);
                scene[1] = step.tile[0];
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
    int32_t file = -1; // a new file
};

// The intro skip (#77): Z during the intro starts a new file and goes to the
// world map at once, instead of the logos, the attract demo and the opening
// cutscene. The step is the warp's, with the world map as its target, so
// nothing new runs in the game. The button is taken from the pads the game
// reads (coop::note_pad), not from this machine's keyboard, so a scenario can
// press it like any other button.
constexpr int32_t world_map_overlay = 0x21;
constexpr int32_t world_map_entry = 0x10; // the code the world map is started with
constexpr uint32_t no_overlay = 0xFFFFFFFF;
constexpr uint16_t pad_z = 0x2000;           // the N64 Z button (parse_buttons)
constexpr uint64_t skip_settle_frames = 30;  // as the warp waits, for the overlay to settle
constexpr uint64_t skip_retry_frames = 60;   // ... before trying again
constexpr int skip_tries = 6;                // ... and how often at most

bool skip_intro_asked = false; // Z was pressed during the intro
bool attract = false;          // the boot's demo loop runs (0x1B and its stages)
int skip_tried = 0;            // steps asked for so far
uint64_t skip_frame = 0;       // the frame of the last one
uint16_t pad0_buttons = 0;     // pad 0 the frame before, for the press edge

// Whether the game is still in the intro and the skip may step out of it: the
// title overlay 0x20, which is the logos until a file is picked, a new file's
// opening cutscene 0x1A, and the attract demo 0x1B with the stages it plays
// (0x38, 0x50, …), which count only while the demo loop runs - the same
// overlays are real rooms once the person plays, and Z must stay the game's
// own there.
bool in_intro() {
    return last_overlay == 0x20 || last_overlay == 0x1A || last_overlay == 0x1B || attract;
}

// The skip's step, under the lock: whether the hook should warp this frame.
// The request stays until the world map is loaded, because one step does not
// always get there: pressed inside the demo (0x1B), the demo's own scene
// change wins and its stage loads instead, so the step is repeated while the
// intro runs. Dropped in an online session, whose other game would play on and
// part ways, once the game left the intro, and after `skip_tries` steps that
// did not arrive.
bool take_skip_intro(int32_t* args) {
    if (!skip_intro_asked) {
        return false;
    }
    if (coop::session_connected()) {
        skip_intro_asked = false;
        std::fprintf(stderr, "coop: skip intro: not in an online session\n");
        return false;
    }
    if (last_overlay == world_map_overlay) {
        skip_intro_asked = false; // arrived
        return false;
    }
    if (last_overlay == no_overlay) {
        return false; // nothing loaded yet: wait for the logos
    }
    if (!in_intro()) {
        skip_intro_asked = false;
        std::fprintf(stderr, "coop: skip intro: only during the intro, not in overlay 0x%02X\n", last_overlay);
        return false;
    }
    if (frames - last_overlay_frame < skip_settle_frames) {
        return false; // let the overlay settle, as the warp does
    }
    if (skip_tried > 0 && frames - skip_frame < skip_retry_frames) {
        return false; // give the last step time to arrive
    }
    if (skip_tried >= skip_tries) {
        skip_intro_asked = false;
        std::fprintf(stderr, "coop: skip intro: gave up after %d steps (overlay 0x%02X)\n", skip_tried,
                     last_overlay);
        return false;
    }
    skip_tried++;
    skip_frame = frames;
    args[0] = world_map_overlay;
    args[1] = world_map_entry;
    args[5] = -1; // a new file
    std::fprintf(stderr, "coop: skip intro: a new file, to the world map (step %d, from overlay 0x%02X)\n",
                 skip_tried, last_overlay);
    return true;
}

Warp read_warp() {
    Warp warp;
    const char* value = std::getenv("BM64_COOP_WARP");
    if (value == nullptr || value[0] == '\0') {
        return warp;
    }
    char* rest = nullptr;
    warp.overlay = static_cast<int32_t>(std::strtol(value, &rest, 0));
    if (rest != nullptr && *rest == ':') {
        warp.entry = static_cast<int32_t>(std::strtol(rest + 1, &rest, 0));
    }
    if (rest != nullptr && *rest == ':') {
        warp.file = static_cast<int32_t>(std::strtol(rest + 1, nullptr, 0));
        if (warp.file < 0 || warp.file > 2) {
            std::fprintf(stderr, "coop: BM64_COOP_WARP's file must be 0-2, not %d; a new file\n", warp.file);
            warp.file = -1;
        }
    }
    warp.wanted = true;
    return warp;
}

// Patches print with recomp_printf (stdout), the host with stderr: line
// buffering keeps the two in order in a run's game.log. Windows has no line
// buffering (_IOLBF is full buffering there, and a size of 0 is an invalid
// parameter that ends the program, #29), so stdout is unbuffered there.
[[maybe_unused]] const bool line_buffered = [] {
    if (env_flag("BM64_COOP_INPUT") || env_flag("BM64_COOP_P2") || env_flag("BM64_COOP_LOG_PLAYERS") ||
        env_flag("BM64_COOP_SESSION")) {
#ifdef _WIN32
        std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
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

void coop::note_pad(int pad, uint16_t buttons) {
    if (pad != 0) {
        return;
    }
    std::lock_guard lock{mutex};
    const uint16_t pressed = static_cast<uint16_t>(buttons & ~pad0_buttons);
    pad0_buttons = buttons;
    // Z asks for the intro skip (#77), and only while the intro runs: in a
    // room Z is the game's own button and must stay it. A fresh press gets the
    // full number of steps again.
    if ((pressed & pad_z) != 0 && in_intro()) {
        skip_intro_asked = true;
        skip_tried = 0;
    }
}

void coop::note_overlay_load(uint32_t id) {
    std::lock_guard lock{mutex};
    id &= 0xFFFF;
    overlay_loads[id]++;
    last_overlay = id;
    last_overlay_frame = frames;
    // The demo loop (#77): it begins with the demo itself and plays stage
    // rooms of its own, which are ordinary overlays; it is over once the
    // title, the file select, the world map or a cutscene loads, all of
    // which only a person's own game reaches.
    if (id == 0x1B) {
        attract = true;
    } else if (id == 0x20 || id == 0x1F || id == 0x21 || id == 0x1A) {
        attract = false;
    }
}

// Called by the per-frame hook (func_8023A208, bm64.us.toml). Returns what the
// hook should call in the patches this frame: bit 0 coop_warp(args[0],
// args[1], args[5]), bit 1 coop_probe(), bit 2 coop_harness_tile(args[2],
// args[3], args[4]), bit 3 coop_harness_save(args[6]), bit 4
// coop_harness_hearts(args[7], args[8]), bit 5 coop_harness_scene(args[9],
// args[10]), bit 6 coop_harness_drop(args[11]), bit 7
// coop_harness_freeze(args[12]).
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
        args[5] = warp.file;
        requests |= 1;
    }
    // The intro skip (#77) uses the same step, so only when the warp did not
    // ask for it this frame; a warp of its own already leaves the intro.
    if ((requests & 1) == 0 && take_skip_intro(args)) {
        requests |= 1;
    }
    args[2] = -1;
    args[6] = -1;
    args[7] = -1;
    args[9] = -1;
    args[11] = -1;
    args[12] = -1;
    if (run_script(&args[2], &args[7], &args[6], &args[11], &args[9], &args[12])) {
        requests |= 2;
    }
    if (args[2] >= 0) {
        requests |= 4;
    }
    if (args[6] >= 0) {
        requests |= 8;
    }
    if (args[7] >= 0) {
        requests |= 16;
    }
    if (args[9] >= 0) {
        requests |= 32;
    }
    if (args[11] >= 0) {
        requests |= 64;
    }
    if (args[12] >= 0) {
        requests |= 128;
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

// The slot the camera follows on this machine while that player is in play
// (#27; patches/coop_camera.c), -1 for the game's own choice (the story slot
// S). BM64_COOP_CAMERA=<slot> picks it (0-4, or -1); without it, the client
// of a live session follows its own player, player 2 in slot 4, and every
// other machine the game's choice.
extern "C" int32_t coop_camera_slot(void) {
    static const char* value = std::getenv("BM64_COOP_CAMERA");
    static const int32_t slot = [] {
        int32_t slot = value != nullptr ? static_cast<int32_t>(std::strtol(value, nullptr, 0)) : -1;
        if (slot < -1 || slot > 4) {
            std::fprintf(stderr, "coop: BM64_COOP_CAMERA must be -1 to 4, not %s; the game's choice\n", value);
            slot = -1;
        }
        return slot;
    }();
    if (value == nullptr) {
        return coop::session_live_client() ? 4 : -1;
    }
    return slot;
}

// The pad the camera's C-buttons are read from on this machine (#33, a hook in
// func_80235934): player 2's, pad 1, when the camera follows player 2 in slot
// 4 (the client of a live session, or BM64_COOP_CAMERA=4), else pad 0.
extern "C" int32_t coop_camera_pad(void) {
    return coop_camera_slot() == 4 ? 1 : 0;
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

// The item a hit knocked out last (#36): its address, 0 for none, and the
// slot that lost it. The drop hook (func_8027B39C) notes it, the pickup and
// room teardown hooks forget it, and a probe logs where it is
// (patches/coop_harness.c). Only with BM64_COOP_LOG_PLAYERS=1.
static std::atomic<uint32_t> noted_item{0};
static std::atomic<int32_t> noted_item_slot{-1};

extern "C" void coop_note_item(int32_t slot, uint32_t item) {
    static const bool enabled = env_flag("BM64_COOP_LOG_PLAYERS");
    if (enabled) {
        noted_item_slot = slot;
        noted_item = item;
    }
}

// item 0: the room's teardown, every item goes.
extern "C" void coop_note_item_gone(uint32_t item) {
    if (item == 0 || item == noted_item) {
        noted_item = 0;
    }
}

// A player passing through an item (#36, #22; the hook in func_80279CA8):
// "coop: pass slot=<slot> item=<type>", again only once the player or the
// item changes (the callback runs every frame they touch).
extern "C" void coop_log_item_pass(int32_t slot, uint32_t item, int32_t type) {
    static int32_t last_slot = -1;
    static uint32_t last_item = 0;
    if (slot != last_slot || item != last_item) {
        last_slot = slot;
        last_item = item;
        coop_log_player_event("pass", slot, "item", type, nullptr, 0);
    }
}

extern "C" uint32_t coop_noted_item(int32_t* slot) {
    *slot = noted_item_slot;
    return noted_item;
}

