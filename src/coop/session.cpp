// The session core of online co-op (#21, docs/coop/design.md, *The session
// core*): two instances of the same build, a host and a client, run the same
// game frames on the same pads (lockstep). Each machine sends its own pad 0
// once per frame; on both, the game reads the host's as pad 0 (player 1) and
// the client's as pad 1 (player 2 in slot 4, patches/coop_player2.c). Both
// seed the game's random generator with the host's seed, and both compare a
// hash of their state every frame (patches/coop_session.c). With its pad each
// sends its camera's yaw and pitch, which turn that player's stick on both
// machines (#33, coop_session_view).
//
//   BM64_COOP_SESSION=host:<port>         wait for the client on TCP <port>
//   BM64_COOP_SESSION=join:<host>:<port>  connect to the host
//                                         (or the launcher's Co-op menu,
//                                         configure_session, #28)
//   BM64_COOP_DELAY=<frames>              the input delay, 1-60 (host's, default 2)
//   BM64_COOP_SEED=<n>                    seed the random generator with n;
//                                         works without a session too (two
//                                         runs on the same script then match)
//   BM64_COOP_LOG_HASH=<n>                log the state hash every n frames
//   BM64_COOP_LOG_STATE=<n>               log the values behind it (the
//                                         snapshot lines) every n frames
//   BM64_COOP_CONNECT_TIMEOUT=<s>         wait this long for the other (60)
//   BM64_COOP_BUILD_ID=<n>                claim build identity n (a test of
//                                         the hello's build check)
//
// The connection is made at the game's first pad read (before the random
// generator is seeded), so both machines start the game together. A session
// from the launcher waits for the other side without a time limit, until
// cancel_session; session_status() tells the launcher's prompts what goes on
// (src/coop/session_ui.cpp). The frame
// protocol, over TCP with Nagle off, little-endian:
//
//   hello  "COOP", u16 protocol, u8 role (0 host, 1 client), u8 game (0 US,
//          1 Europe), u32 seed, u16 delay, u16 0, u32 build; the client takes
//          the host's seed and delay, and either side refuses another
//          protocol, game or build (the first 8 bytes stay the same in every
//          protocol version, so an older peer is refused, not misread)
//   logs   right after the hellos, the host's first: "LOGS", u32 size, then
//          the last run's log of the sender (coop.prev.log, its last 256
//          KiB; size 0 without one), kept by the other in
//          coop-logs/<time>-<host|client>.prev.log (#29: a crash's last
//          lines reach the other side at the next connect)
//   save   from the host after the hellos: u32 size, then its save file as
//          the game will read it (zeros without one). The client plays on a
//          copy of it (saves/coop-session/<game id>.bin), so both games read
//          and write the same save data and the client's own file is never
//          written (docs/coop/design.md, section 11)
//
// Then every message is u16 kind, u16 size and size bytes (protocol 5, #57;
// a receiver skips a kind it does not know):
//   frame  kind 1, 52 bytes: u32 frame, u16 buttons, u16 flags (1: the
//          camera's angles are set), f32 x, f32 y (the bits the game will
//          read), u32 hash frame, u32 hash (of the state after that frame),
//          f32 yaw, f32 pitch (the sender's camera, in degrees, when it sent
//          the pad), then the hash's five parts, u32 each (scene, random,
//          players, enemies, save; protocol 6, #75)
//   log    kind 2: the next text of the sender's log (coop.log, from the
//          start of its run; whole lines). The other side appends it to
//          coop-logs/<time>-<host|client>.log as it comes, so the text is
//          there when the sender crashes or the session is lost later. Sent
//          after the frame in the same packet, at most 512 bytes a frame (25
//          KiB/s at 50 frames a second), so a frame waits behind no more
//          than that; what was logged before the lockstep goes at its start.
//
// Game frames are numbered from 0, the one of the game's first pad read. The
// per-frame hook at the end of frame f (bm64.us.toml) is the lockstep's step:
// a machine sends its hash of frame f and its pad for frame f + delay, then
// waits for the other's pad for frame f + 1; frames below the delay hold no
// buttons. A hash that differs from
// this machine's for the same frame logs "coop: session desync" with the
// parts that differ and this machine's values of that frame ("coop:
// snapshot" lines, #75; once, then a count every 600 frames). A peer that closes, or sends nothing for 10 s,
// ends the session: "coop: session lost", and the game reads the local pads
// again. The build identity (build_id) is a hash of the patches' binary and
// of the game's recompiler config, which holds the hooks (src/coop/version.cmake).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t no_socket = INVALID_SOCKET;
constexpr int send_flags = 0;
static void close_socket(socket_t s) { closesocket(s); }
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t no_socket = -1;
constexpr int send_flags = MSG_NOSIGNAL; // a closed peer is an error, not SIGPIPE
static void close_socket(socket_t s) { close(s); }
#endif

#include "librecomp/files.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"

#include "coop_debug.h"
#include "session.h"
#include "zelda_config.h"

// The patches' binary, as librecomp loads it (RecompiledPatches/patches_bin.c).
extern "C" const char mm_patches_bin[];
extern "C" const size_t mm_patches_bin_size;

namespace {

// 5 (#57): the messages after the save got a kind and a size, for the log's
// text; a protocol 4 peer would read a log message as a frame. 6 (#75): the
// frame message carries the hash's parts
constexpr uint16_t protocol = 6;
constexpr uint32_t no_frame = 0xFFFFFFFF;
constexpr size_t hello_head = 8; // "COOP", protocol, role, game: the same in every protocol
constexpr size_t hello_size = 20;
constexpr size_t frame_size = 52;
constexpr size_t message_head = 4; // u16 kind, u16 size
constexpr uint16_t message_frame = 1;
constexpr uint16_t message_log = 2;
constexpr size_t log_per_frame = 512; // log text after each frame, at most
constexpr size_t log_message_max = 0xFFFF;
constexpr auto peer_timeout = std::chrono::seconds(10);

// The state the per-frame hook hands over (patches/coop_session.c,
// CoopStateWords): the hash, its parts, then the values the snapshot lines
// show. Words, floats as their bits.
constexpr size_t hash_parts = 5;
constexpr const char* part_names[hash_parts] = {"scene", "random", "players", "enemies", "save"};
constexpr size_t state_scene = 1 + hash_parts; // overlay, flow flags, lives, S
constexpr size_t state_random = state_scene + 4; // the generator's index and the word after it
constexpr size_t state_players = state_random + 2; // 5 of: controlType, states, flags, x, y, z
constexpr size_t state_enemies = state_players + 5 * 6; // 16 records of: state, type (-1 not in the room), x, y, z
constexpr size_t state_words = state_enemies + 16 * 5;
constexpr size_t state_fpu = state_words; // then the host's: the game thread's FPU control word
using State = std::array<uint32_t, state_words + 1>;

#ifdef BM64_VERSION_EU
constexpr uint8_t game_version = 1;
#else
constexpr uint8_t game_version = 0;
#endif

#ifndef BM64_COOP_HOOKS_ID
#define BM64_COOP_HOOKS_ID 0 // src/coop/version.cmake defines it
#endif

enum class Role { None, Host, Client };

struct Pad {
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool view = false; // the sender's camera angles are set (none below the delay)
    uint32_t yaw = 0;  // their bits, as the game will read them
    uint32_t pitch = 0;
};

// The other side's hash of a frame, with its parts.
struct PeerHash {
    uint32_t frame = 0;
    uint32_t hash = 0;
    std::array<uint32_t, hash_parts> parts{};
};

struct Config {
    Role role = Role::None;
    std::string host;
    std::string port;
    uint32_t delay = 2;
    bool seed_set = false;
    uint32_t seed = 0;
    bool build_set = false;
    uint32_t build = 0;
    uint32_t log_hash = 0;
    uint32_t log_state = 0;
    int connect_timeout = 60;
    bool launcher = false; // from the launcher's menu: wait until cancelled
};

struct Session {
    Config config;
    bool started = false; // the connection was tried
    bool live = false;    // connected and in lockstep
    bool connected = false; // the hellos were exchanged (live, or lost since)
    coop::LocalInput local = nullptr;
    socket_t sock = no_socket;
    uint32_t seed = 0;
    uint32_t frame = 0;      // the game frame being played
    std::array<Pad, 2> pads; // its pads 0 and 1
    std::map<uint32_t, Pad> own;   // this machine's delayed pads, by frame
    std::map<uint32_t, State> hashes; // this machine's hashes and their values, by frame
    uint32_t hash_frame = no_frame;
    uint32_t hash = 0;
    uint32_t desyncs = 0;
    uint32_t first_desync = no_frame;
    std::string stamp;        // the connect's time, which names the coop-logs/ files
    std::ifstream log;        // this machine's coop.log, streamed to the other (#57)
    uint64_t log_sent = 0;    // its bytes sent so far
    std::vector<uint8_t> out; // the message being sent

    // filled by the receiver thread
    std::mutex mutex;
    std::condition_variable arrived;
    std::map<uint32_t, Pad> peer;
    std::deque<PeerHash> peer_hashes;
    bool peer_closed = false;
};

Session session;
std::atomic<uint32_t> hashed_frames{0};

std::mutex status_mutex;
coop::SessionStatus status;
std::atomic<bool> cancelled{false};

void set_status(coop::SessionState state, bool host, const std::string& text) {
    std::lock_guard lock{status_mutex};
    status.serial++;
    status.state = state;
    status.host = host;
    status.text = text;
}

uint32_t env_number(const char* name, uint32_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? static_cast<uint32_t>(std::strtoul(value, nullptr, 0)) : fallback;
}

// Reads host:<port> or join:<host>:<port> into config's role, host and port.
bool parse_spec(const std::string& text, Config& config) {
    config.role = Role::None;
    if (text.rfind("host:", 0) == 0) {
        config.role = Role::Host;
        config.port = text.substr(5);
    } else if (text.rfind("join:", 0) == 0 && text.rfind(':') > 5) {
        config.role = Role::Client;
        config.host = text.substr(5, text.rfind(':') - 5);
        config.port = text.substr(text.rfind(':') + 1);
    }
    return config.role != Role::None && !config.port.empty();
}

Config read_config() {
    Config config;
    const char* seed = std::getenv("BM64_COOP_SEED");
    if (seed != nullptr && seed[0] != '\0') {
        config.seed_set = true;
        config.seed = static_cast<uint32_t>(std::strtoul(seed, nullptr, 0));
    }
    const char* build = std::getenv("BM64_COOP_BUILD_ID");
    if (build != nullptr && build[0] != '\0') {
        config.build_set = true;
        config.build = static_cast<uint32_t>(std::strtoul(build, nullptr, 0));
    }
    config.delay = env_number("BM64_COOP_DELAY", 2);
    config.log_hash = env_number("BM64_COOP_LOG_HASH", 0);
    config.log_state = env_number("BM64_COOP_LOG_STATE", 0);
    config.connect_timeout = static_cast<int>(env_number("BM64_COOP_CONNECT_TIMEOUT", 60));
    const char* value = std::getenv("BM64_COOP_SESSION");
    if (value == nullptr || value[0] == '\0') {
        return config;
    }
    if (!parse_spec(value, config) || config.delay < 1 || config.delay > 60) {
        std::fprintf(stderr, "coop: session: BM64_COOP_SESSION must be host:<port> or join:<host>:<port> "
                             "(BM64_COOP_DELAY 1-60), not %s; off\n", value);
        config.role = Role::None;
    }
    return config;
}

// The session's config: BM64_COOP_SESSION and the other switches, or the
// launcher's menu (configure_session) before the game starts.
Config& config() {
    static Config config = read_config();
    return config;
}

void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

void put32(uint8_t* p, uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t get32(const uint8_t* p) {
    return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16);
}

uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

float bits_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool send_all(socket_t sock, const uint8_t* data, size_t size) {
    while (size > 0) {
        auto sent = send(sock, reinterpret_cast<const char*>(data), static_cast<int>(size), send_flags);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

bool recv_all(socket_t sock, uint8_t* data, size_t size) {
    while (size > 0) {
        auto got = recv(sock, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
        if (got <= 0) {
            return false;
        }
        data += got;
        size -= static_cast<size_t>(got);
    }
    return true;
}

// A receive on sock fails after this many seconds without data (0: never).
void set_recv_timeout(socket_t sock, int seconds) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(seconds) * 1000;
#else
    timeval timeout{seconds, 0};
#endif
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
}

// Waits up to timeout_ms for sock to be readable (a connection to accept).
bool wait_readable(socket_t sock, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD fd{sock, POLLIN, 0};
    return WSAPoll(&fd, 1, timeout_ms) > 0;
#else
    pollfd fd{sock, POLLIN, 0};
    return poll(&fd, 1, timeout_ms) > 0;
#endif
}

// Whether a wait that started at `start` should go on: not cancelled and,
// unless the launcher started it, within the connect timeout.
bool keep_waiting(const Config& config, std::chrono::steady_clock::time_point start) {
    return !cancelled && (config.launcher ||
                          std::chrono::steady_clock::now() < start + std::chrono::seconds(config.connect_timeout));
}

socket_t accept_client(const Config& config) {
    addrinfo hints{};
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* found = nullptr;
    socket_t listener = no_socket;
    if (getaddrinfo(nullptr, config.port.c_str(), &hints, &found) == 0) {
        listener = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
        if (listener != no_socket) {
            int on = 1, off = 0; // IPv6 and IPv4 both
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
            setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off));
            if (bind(listener, found->ai_addr, static_cast<int>(found->ai_addrlen)) != 0 || listen(listener, 1) != 0) {
                close_socket(listener);
                listener = no_socket;
            }
        }
        freeaddrinfo(found);
    }
    if (listener == no_socket) {
        std::fprintf(stderr, "coop: session: cannot listen on port %s\n", config.port.c_str());
        set_status(coop::SessionState::Refused, true, "cannot listen on port " + config.port);
        return no_socket;
    }
    std::fprintf(stderr, "coop: session host: waiting on port %s\n", config.port.c_str());
    set_status(coop::SessionState::Waiting, true, "Waiting for player 2 on port " + config.port);
    socket_t sock = no_socket;
    auto start = std::chrono::steady_clock::now();
    while (keep_waiting(config, start)) {
        if (wait_readable(listener, 250)) {
            sock = accept(listener, nullptr, nullptr);
            break;
        }
    }
    close_socket(listener);
    return sock;
}

socket_t connect_host(const Config& config) {
    std::fprintf(stderr, "coop: session client: connecting to %s port %s\n", config.host.c_str(),
                 config.port.c_str());
    set_status(coop::SessionState::Waiting, false, "Connecting to " + config.host + " port " + config.port);
    auto start = std::chrono::steady_clock::now();
    while (keep_waiting(config, start)) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        if (getaddrinfo(config.host.c_str(), config.port.c_str(), &hints, &found) == 0) {
            for (addrinfo* a = found; a != nullptr; a = a->ai_next) {
                socket_t sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
                if (sock == no_socket) {
                    continue;
                }
                if (connect(sock, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) {
                    freeaddrinfo(found);
                    return sock;
                }
                close_socket(sock);
            }
            freeaddrinfo(found);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // the host may not listen yet
    }
    return no_socket;
}

// The other side's log as it streams in (#57), appended to
// coop-logs/<time>-<host|client>.log in the app folder; the file is opened
// with the first text.
void keep_log_text(std::ofstream& file, const uint8_t* text, size_t size) {
    if (!file.is_open()) {
        std::string name = session.stamp + (session.config.role == Role::Host ? "-client.log" : "-host.log");
        std::filesystem::path path = zelda64::get_app_folder_path() / "coop-logs" / name;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        file.open(path, std::ios_base::binary | std::ios_base::app);
        std::fprintf(stderr, "coop: session: the other side's log streams to coop-logs/%s%s\n", name.c_str(),
                     file.is_open() ? "" : " (cannot write it)");
    }
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(text), static_cast<std::streamsize>(size));
        file.flush();
    }
}

void receive_frames(socket_t sock) {
    std::vector<uint8_t> body(log_message_max);
    std::ofstream log;
    uint8_t head[message_head];
    while (recv_all(sock, head, sizeof(head))) {
        uint16_t kind = get16(head);
        uint16_t size = get16(head + 2);
        if (!recv_all(sock, body.data(), size) || (kind == message_frame && size != frame_size)) {
            break;
        }
        if (kind == message_log) {
            keep_log_text(log, body.data(), size);
        }
        if (kind != message_frame) {
            continue;
        }
        const uint8_t* msg = body.data();
        Pad pad{get16(msg + 4), bits_float(get32(msg + 8)), bits_float(get32(msg + 12)), (get16(msg + 6) & 1) != 0,
                get32(msg + 24), get32(msg + 28)};
        PeerHash hash{get32(msg + 16), get32(msg + 20)};
        for (size_t i = 0; i < hash_parts; i++) {
            hash.parts[i] = get32(msg + 32 + i * 4);
        }
        std::lock_guard lock{session.mutex};
        session.peer[get32(msg)] = pad;
        if (hash.frame != no_frame) {
            session.peer_hashes.push_back(hash);
        }
        session.arrived.notify_all();
    }
    std::lock_guard lock{session.mutex};
    session.peer_closed = true;
    session.arrived.notify_all();
}

// Appends to s.out a log message with this machine's next log text (#57):
// the whole lines of coop.log after those sent, at most `budget` bytes (a
// longer line goes in pieces). False when no whole line waits.
bool add_log_text(Session& s, size_t budget) {
    if (!s.log.is_open()) {
        return false;
    }
    size_t at = s.out.size();
    s.out.resize(at + message_head + budget);
    uint8_t* text = s.out.data() + at + message_head;
    s.log.clear();
    s.log.seekg(static_cast<std::streamoff>(s.log_sent));
    s.log.read(reinterpret_cast<char*>(text), static_cast<std::streamsize>(budget));
    size_t got = static_cast<size_t>(s.log.gcount());
    size_t size = got;
    while (size > 0 && text[size - 1] != '\n') {
        size--;
    }
    if (size == 0 && got == budget) {
        size = got;
    }
    s.out.resize(size > 0 ? at + message_head + size : at);
    if (size == 0) {
        return false;
    }
    put16(s.out.data() + at, message_log);
    put16(s.out.data() + at + 2, static_cast<uint16_t>(size));
    s.log_sent += size;
    return true;
}

// At the lockstep's start: open this machine's coop.log and send what it
// holds so far (up to 4 messages of 64 KiB; the rest follows with the frames).
void start_log_stream(Session& s) {
    std::filesystem::path path = coop::log_path();
    if (!path.empty()) {
        s.log.open(path, std::ios_base::binary);
    }
    if (!s.log.is_open()) {
        std::fprintf(stderr, "coop: session: no log file to stream to the other side\n");
        return;
    }
    s.out.clear();
    for (int i = 0; i < 4 && add_log_text(s, log_message_max); i++) {
    }
    if (!s.out.empty() && !send_all(s.sock, s.out.data(), s.out.size())) {
        std::fprintf(stderr, "coop: session: the log could not be sent\n"); // the first step notices too
    }
}

// This build's identity: FNV-1a of the patches' binary, then of the hash of
// the recompiler config (bm64.<version>.toml, with the hooks) taken when
// configuring. Another build of the same commit with another compiler may
// differ (not tried).
uint32_t build_id() {
    uint32_t hash = 2166136261u;
    auto add = [&hash](uint8_t byte) {
        hash ^= byte;
        hash *= 16777619;
    };
    for (size_t i = 0; i < mm_patches_bin_size; i++) {
        add(static_cast<uint8_t>(mm_patches_bin[i]));
    }
    for (int i = 0; i < 4; i++) {
        add(static_cast<uint8_t>(static_cast<uint32_t>(BM64_COOP_HOOKS_ID) >> (i * 8)));
    }
    return hash;
}

// The size of librecomp's save buffer for the game's save type (pi.cpp's
// get_save_size).
size_t save_size() {
    switch (recomp::get_save_type()) {
        case recomp::SaveType::AllowAll:
        case recomp::SaveType::Flashram:
            return 0x20000;
        case recomp::SaveType::Sram:
            return 0x8000;
        case recomp::SaveType::Eep16k:
            return 0x800;
        case recomp::SaveType::Eep4k:
            return 0x200;
        default:
            return 0;
    }
}

// The host's save as the game will read it: the file librecomp loaded when
// the game started (the game reads the EEPROM after the session connects,
// func_80225840, and writes nothing before), zeros where it has none.
std::vector<uint8_t> read_own_save() {
    std::vector<uint8_t> save(save_size(), 0);
    std::ifstream file = recomp::open_input_file_with_backup(ultramodern::get_save_file_path(), std::ios_base::binary);
    if (file.good()) {
        file.read(reinterpret_cast<char*>(save.data()), static_cast<std::streamsize>(save.size()));
    }
    return save;
}

// The client plays on a copy of the host's save: written to
// saves/coop-session/<game id>.bin and loaded into librecomp's save buffer in
// place of the client's own file, before the game reads it.
bool use_host_save(const std::vector<uint8_t>& save) {
    const std::u8string game_id = recomp::current_game_id();
    const std::u8string folder = u8"coop-session";
    std::filesystem::path path = ultramodern::get_save_file_path().parent_path() / folder / (game_id + u8".bin");
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    {
        std::ofstream file(path, std::ios_base::binary | std::ios_base::trunc);
        file.write(reinterpret_cast<const char*>(save.data()), static_cast<std::streamsize>(save.size()));
        if (!file.good()) {
            return false;
        }
    }
    ultramodern::change_save_file(folder, game_id);
    return ultramodern::get_save_file_path() == path;
}

// The logs, first after the hellos (#29): each side sends its last run's log
// and keeps the other's in coop-logs/<time>-<host|client>.prev.log in the
// app folder. The host sends first, the client answers, so neither waits on
// a full socket buffer. This run's log follows during the session (#57,
// start_log_stream).
bool exchange_logs(Session& s) {
    constexpr size_t limit = 256 * 1024;
    bool host = s.config.role == Role::Host;
    const std::string mine = coop::previous_log(limit);
    auto send_mine = [&] {
        uint8_t head[8] = {'L', 'O', 'G', 'S'};
        put32(head + 4, static_cast<uint32_t>(mine.size()));
        return send_all(s.sock, head, sizeof(head)) &&
               (mine.empty() || send_all(s.sock, reinterpret_cast<const uint8_t*>(mine.data()), mine.size()));
    };
    std::string theirs;
    auto receive_theirs = [&] {
        uint8_t head[8];
        if (!recv_all(s.sock, head, sizeof(head)) || std::memcmp(head, "LOGS", 4) != 0 || get32(head + 4) > limit) {
            return false;
        }
        theirs.resize(get32(head + 4));
        return theirs.empty() || recv_all(s.sock, reinterpret_cast<uint8_t*>(theirs.data()), theirs.size());
    };
    set_recv_timeout(s.sock, s.config.connect_timeout);
    bool ok = host ? send_mine() && receive_theirs() : receive_theirs() && send_mine();
    set_recv_timeout(s.sock, 0);
    if (!ok) {
        return false;
    }
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    s.stamp = stamp; // also names the stream's file (#57)
    if (theirs.empty()) {
        std::fprintf(stderr, "coop: session: the other side has no last log\n");
        return true;
    }
    std::string name = s.stamp + (host ? "-client.prev.log" : "-host.prev.log");
    std::filesystem::path path = zelda64::get_app_folder_path() / "coop-logs" / name;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios_base::binary);
    file.write(theirs.data(), static_cast<std::streamsize>(theirs.size()));
    std::fprintf(stderr, "coop: session: the other side's last log, %zu bytes, %s coop-logs/%s\n", theirs.size(),
                 file.good() ? "kept in" : "could not be written to", name.c_str());
    return true;
}

// Connects and exchanges the hellos: from the launcher (connect_session,
// before the game starts) or at the game's first pad read. True when both
// sides agree; the status is then Connected.
bool connect_hello(Session& s) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const Config& c = s.config;
    bool host = c.role == Role::Host;
    const char* alone = c.launcher ? "" : "; playing alone"; // the launcher goes back to its menu
    s.sock = host ? accept_client(c) : connect_host(c);
    if (s.sock == no_socket) {
        if (cancelled) {
            std::fprintf(stderr, "coop: session: cancelled%s\n", alone);
            set_status(coop::SessionState::Cancelled, host, "");
            return false;
        }
        std::fprintf(stderr, "coop: session: no %s within %d s%s\n", host ? "client" : "host", c.connect_timeout,
                     alone);
        if (coop::session_status().state == coop::SessionState::Waiting) { // not a port it cannot listen on
            set_status(coop::SessionState::Refused, host,
                       std::string("no ") + (host ? "client" : "host") + " within " +
                           std::to_string(c.connect_timeout) + " s");
        }
        return false;
    }
    int on = 1;
    setsockopt(s.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    s.seed = c.seed_set ? c.seed : std::random_device{}();
    uint32_t build = c.build_set ? c.build : build_id();
    std::fprintf(stderr, "coop: session build 0x%08X%s\n", build, c.build_set ? " (BM64_COOP_BUILD_ID)" : "");
    uint8_t hello[hello_size] = {'C', 'O', 'O', 'P'};
    put16(hello + 4, protocol);
    hello[6] = host ? 0 : 1;
    hello[7] = game_version;
    put32(hello + 8, host ? s.seed : 0);
    put16(hello + 12, static_cast<uint16_t>(c.delay));
    put32(hello + 16, build);
    uint8_t theirs[hello_size];
    set_recv_timeout(s.sock, c.connect_timeout); // something else on the port may never say hello
    if (!send_all(s.sock, hello, sizeof(hello)) || !recv_all(s.sock, theirs, hello_head)) {
        std::fprintf(stderr, "coop: session: no hello from the other side%s\n", alone);
        set_status(coop::SessionState::Refused, host, "no hello from the other side");
        close_socket(s.sock);
        return false;
    }
    char refused[96] = "";
    if (std::memcmp(theirs, "COOP", 4) != 0) {
        std::snprintf(refused, sizeof(refused), "not a co-op session");
    } else if (get16(theirs + 4) != protocol) {
        std::snprintf(refused, sizeof(refused), "another protocol version (%u, this %u)", get16(theirs + 4), protocol);
    } else if (!recv_all(s.sock, theirs + hello_head, hello_size - hello_head)) {
        std::snprintf(refused, sizeof(refused), "the hello was cut off");
    } else if (theirs[6] != (host ? 1 : 0)) {
        std::snprintf(refused, sizeof(refused), "both sides have the same role");
    } else if (theirs[7] != game_version) {
        std::snprintf(refused, sizeof(refused), "another game version (US and Europe do not mix)");
    } else if (get32(theirs + 16) != build) {
        std::snprintf(refused, sizeof(refused), "another build (the other's 0x%08X)", get32(theirs + 16));
    }
    set_recv_timeout(s.sock, 0);
    if (refused[0] != '\0') {
        std::fprintf(stderr, "coop: session refused: %s%s\n", refused, alone);
        set_status(coop::SessionState::Refused, host, refused);
        close_socket(s.sock);
        return false;
    }
    if (!exchange_logs(s)) {
        std::fprintf(stderr, "coop: session: the connection broke while sending the logs%s\n", alone);
        set_status(coop::SessionState::Refused, host, "the connection broke");
        close_socket(s.sock);
        return false;
    }
    if (!host) {
        s.seed = get32(theirs + 8);
        s.config.delay = get16(theirs + 12);
    }
    set_status(coop::SessionState::Connected, host, host ? "Player 2 joined" : "Connected to the host");
    return true;
}

// At the game's first pad read, after the hellos: the host sends its save,
// the client plays on it. Then the session is live.
bool exchange_save(Session& s) {
    bool host = s.config.role == Role::Host;
    char refused[96] = "";
    set_recv_timeout(s.sock, s.config.connect_timeout); // the host's game may start later
    std::vector<uint8_t> save;
    if (host) {
        save = read_own_save();
        uint8_t size[4];
        put32(size, static_cast<uint32_t>(save.size()));
        if (!send_all(s.sock, size, sizeof(size)) || !send_all(s.sock, save.data(), save.size())) {
            std::snprintf(refused, sizeof(refused), "the save could not be sent");
        }
    } else {
        uint8_t size[4];
        if (!recv_all(s.sock, size, sizeof(size))) {
            std::snprintf(refused, sizeof(refused), "no save from the host");
        } else if (get32(size) != save_size()) {
            std::snprintf(refused, sizeof(refused), "the host's save is 0x%X bytes, this game's 0x%zX", get32(size),
                          save_size());
        } else {
            save.resize(save_size());
            if (!recv_all(s.sock, save.data(), save.size())) {
                std::snprintf(refused, sizeof(refused), "the host's save was cut off");
            } else if (!use_host_save(save)) {
                std::snprintf(refused, sizeof(refused), "the host's save could not be stored");
            }
        }
    }
    set_recv_timeout(s.sock, 0);
    if (refused[0] != '\0') {
        std::fprintf(stderr, "coop: session refused: %s; playing alone\n", refused);
        set_status(coop::SessionState::Refused, host, refused);
        close_socket(s.sock);
        return false;
    }
    std::fprintf(stderr, host ? "coop: session host: sent the save, %zu bytes\n"
                              : "coop: session client: playing on the host's save, %zu bytes\n", save.size());
    std::fprintf(stderr, "coop: session %s: connected, seed=0x%08X delay=%u\n", host ? "host" : "client", s.seed,
                 s.config.delay);
    std::thread(receive_frames, s.sock).detach();
    return true;
}

// The launcher's connection (connect_session): 0 none, 1 connecting, 2 the
// hellos agreed, 3 not.
std::atomic<int> preconnect{0};

void lose(Session& s, const char* why) {
    std::fprintf(stderr, "coop: session lost frame=%u: %s; playing alone\n", s.frame, why);
    set_status(coop::SessionState::Lost, s.config.role == Role::Host, why);
    s.live = false;
    shutdown(s.sock, 2);
}

float word_float(uint32_t word) {
    return bits_float(word);
}

// The control bits of the calling thread's SSE control word, MXCSR (#75): the
// rounding mode the game sets (its ctc1 becomes fesetround, N64Recomp's
// set_cop1_cs), flush-to-zero (0x8000) and denormals-are-zero (0x40), which
// change float results and which a platform may start a thread with. 0 where
// there is none (not x86-64). The status bits are left out: host code sets
// them in its own way on each machine.
uint32_t fpu_control() {
#if defined(__x86_64__) || defined(_M_X64)
    return _mm_getcsr() & 0xFFC0;
#else
    return 0;
#endif
}

// The values behind a frame's hash, a line each (#75): the scene, every player
// with a control type, the enemy records' states and every enemy in the room,
// positions in world units and as their bits. Two machines' lines for the same
// frame show what parted them (tools/compare_logs.py).
void log_snapshot(uint32_t frame, const State& w) {
    std::fprintf(stderr,
                 "coop: snapshot frame=%u scene overlay=0x%X flags=0x%X lives=%d s=%d random=%u,%08X fpu=0x%04X\n",
                 frame, w[state_scene], w[state_scene + 1], static_cast<int32_t>(w[state_scene + 2]),
                 static_cast<int32_t>(w[state_scene + 3]), w[state_random], w[state_random + 1], w[state_fpu]);
    for (size_t slot = 0; slot < 5; slot++) {
        const uint32_t* p = &w[state_players + slot * 6];
        if (p[0] == 0) {
            continue;
        }
        std::fprintf(stderr,
                     "coop: snapshot frame=%u slot=%zu control=%d states=0x%X flags=0x%X x=%.3f y=%.3f z=%.3f "
                     "bits=%08X,%08X,%08X\n",
                     frame, slot, static_cast<int32_t>(p[0]), p[1], p[2], word_float(p[3]), word_float(p[4]),
                     word_float(p[5]), p[3], p[4], p[5]);
    }
    std::string records;
    for (size_t rec = 0; rec < 16; rec++) {
        records += (rec == 0 ? "" : ",") + std::to_string(static_cast<int32_t>(w[state_enemies + rec * 5]));
    }
    std::fprintf(stderr, "coop: snapshot frame=%u records=%s\n", frame, records.c_str());
    for (size_t rec = 0; rec < 16; rec++) {
        const uint32_t* e = &w[state_enemies + rec * 5];
        if (static_cast<int32_t>(e[1]) == -1) {
            continue;
        }
        std::fprintf(stderr,
                     "coop: snapshot frame=%u enemy rec=%zu state=%d type=0x%X x=%.3f y=%.3f z=%.3f "
                     "bits=%08X,%08X,%08X\n",
                     frame, rec, static_cast<int32_t>(e[0]), e[1], word_float(e[2]), word_float(e[3]),
                     word_float(e[4]), e[2], e[3], e[4]);
    }
}

// Compares the other side's hashes with this machine's; called with the lock.
// The first frame that differs is logged with the parts that differ and this
// machine's snapshot of it: the other side logs the same frame with its own.
void compare_hashes(Session& s) {
    while (!s.peer_hashes.empty()) {
        const PeerHash& peer = s.peer_hashes.front();
        uint32_t frame = peer.frame;
        auto mine = s.hashes.find(frame);
        if (mine == s.hashes.end()) {
            if (frame > s.hash_frame || s.hash_frame == no_frame) {
                break; // this machine has not got there yet
            }
            s.peer_hashes.pop_front(); // too old to compare
            continue;
        }
        const State& state = mine->second;
        if (state[0] != peer.hash) {
            if (s.desyncs++ == 0) {
                s.first_desync = frame;
                std::string parts;
                for (size_t i = 0; i < hash_parts; i++) {
                    if (state[1 + i] != peer.parts[i]) {
                        parts += std::string(parts.empty() ? "" : ",") + part_names[i];
                    }
                }
                std::fprintf(stderr, "coop: session desync frame=%u local=%08X peer=%08X parts=%s\n", frame,
                             state[0], peer.hash, parts.empty() ? "none" : parts.c_str());
                log_snapshot(frame, state);
            }
        }
        s.peer_hashes.pop_front();
    }
    while (!s.hashes.empty() && s.hash_frame != no_frame && s.hashes.begin()->first + 600 < s.hash_frame) {
        s.hashes.erase(s.hashes.begin());
    }
}

// One frame of the lockstep, from the per-frame hook at the end of frame
// s.frame: this machine's hash of it (with its parts), its pad and its
// camera's yaw and pitch for frame s.frame + delay, and both pads of the next
// frame (the other's waited for).
void step(Session& s, const State& state, uint32_t yaw, uint32_t pitch) {
    s.hash_frame = s.frame;
    s.hash = state[0];
    s.hashes[s.frame] = state;
    Pad mine{};
    if (!s.local(0, &mine.buttons, &mine.x, &mine.y)) {
        mine = Pad{};
    }
    mine.view = true;
    mine.yaw = yaw;
    mine.pitch = pitch;
    uint32_t target = s.frame + s.config.delay;
    s.own[target] = mine;
    s.out.assign(message_head + frame_size, 0);
    put16(s.out.data(), message_frame);
    put16(s.out.data() + 2, frame_size);
    uint8_t* msg = s.out.data() + message_head;
    put32(msg, target);
    put16(msg + 4, mine.buttons);
    put16(msg + 6, 1);
    put32(msg + 8, float_bits(mine.x));
    put32(msg + 12, float_bits(mine.y));
    put32(msg + 16, s.hash_frame);
    put32(msg + 20, s.hash);
    put32(msg + 24, mine.yaw);
    put32(msg + 28, mine.pitch);
    for (size_t i = 0; i < hash_parts; i++) {
        put32(msg + 32 + i * 4, state[1 + i]);
    }
    add_log_text(s, log_per_frame); // after the frame, in the same packet (#57)
    if (!send_all(s.sock, s.out.data(), s.out.size())) {
        lose(s, "send failed");
        return;
    }
    uint32_t next = s.frame + 1;
    Pad own{}, peer{};
    {
        std::unique_lock lock{s.mutex};
        if (next >= s.config.delay) {
            if (!s.arrived.wait_for(lock, peer_timeout, [&] { return s.peer.count(next) || s.peer_closed; }) ||
                !s.peer.count(next)) {
                lock.unlock();
                lose(s, s.peer_closed ? "the other side closed" : "nothing for 10 s");
                return;
            }
            own = s.own[next];
            peer = s.peer[next];
            s.peer.erase(next);
        }
        compare_hashes(s);
    }
    s.own.erase(next);
    bool host = s.config.role == Role::Host;
    s.pads[0] = host ? own : peer;
    s.pads[1] = host ? peer : own;
    if (s.desyncs > 0 && next % 600 == 0) {
        std::fprintf(stderr, "coop: session desyncs=%u since frame %u\n", s.desyncs, s.first_desync);
    }
    s.frame = next;
}

} // namespace

coop::SessionStatus coop::session_status() {
    std::lock_guard lock{status_mutex};
    return status;
}

void coop::cancel_session() {
    std::lock_guard lock{status_mutex};
    if (status.state == SessionState::Waiting) {
        cancelled = true;
    }
}

bool coop::configure_session(const std::string& spec, std::string& error) {
    Config parsed = config();
    if (!parse_spec(spec, parsed)) {
        error = "Not host:<port> or join:<address>:<port>: " + spec;
        return false;
    }
    char* end = nullptr;
    unsigned long port = std::strtoul(parsed.port.c_str(), &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) {
        error = "The port must be a number from 1 to 65535.";
        return false;
    }
    if (parsed.role == Role::Client && parsed.host.empty()) {
        error = "Enter the host's address.";
        return false;
    }
    parsed.launcher = true;
    config() = parsed;
    return true;
}

void coop::connect_session() {
    if (!session_configured() || session.started || preconnect == 1) {
        return;
    }
    cancelled = false;
    session.config = config();
    preconnect = 1;
    std::thread([] {
        bool ok = connect_hello(session);
        if (!ok && session.config.launcher) {
            config().role = Role::None; // back in the launcher; its Start plays alone
        }
        preconnect = ok ? 2 : (session.config.launcher ? 0 : 3);
    }).detach();
}

bool coop::session_configured() {
    return config().role != Role::None;
}

bool coop::session_connected() {
    return session.connected;
}

bool coop::session_live_client() {
    return session.live && session.config.role == Role::Client;
}

std::optional<bool> coop::session_input(int pad, uint16_t* buttons, float* x, float* y, LocalInput local) {
    Session& s = session;
    if (!session_configured() || (s.started && !s.live)) {
        return std::nullopt;
    }
    if (!s.started) {
        // the game's first pad read, before its random generator is seeded
        s.started = true;
        s.local = local;
        while (preconnect == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        bool hellos = preconnect == 2;
        if (preconnect == 0) {
            s.config = config(); // cleared if the launcher's connection just failed
            hellos = s.config.role != Role::None && connect_hello(s);
        }
        s.live = hellos && exchange_save(s);
        s.connected = s.live;
        if (!s.live) {
            return std::nullopt;
        }
        std::fprintf(stderr, "coop: session lockstep from frame 0\n");
        std::fprintf(stderr, "coop: session fpu=0x%04X\n", fpu_control()); // the game thread's (#75)
        start_log_stream(s);
    }
    if (pad > 1) {
        return false;
    }
    *buttons = s.pads[pad].buttons;
    *x = s.pads[pad].x;
    *y = s.pads[pad].y;
    return true;
}

std::optional<bool> coop::session_pad_connected(int pad) {
    if (!session_configured() || (session.started && !session.live)) {
        return std::nullopt;
    }
    return pad == 0 || pad == 1;
}

// Hook at the entry of g_initRandom(seed) (bm64.us.toml): the seed the game
// uses, the session's or BM64_COOP_SEED, otherwise its own (the clock).
extern "C" uint32_t coop_session_seed(uint32_t seed) {
    const Session& s = session;
    if (s.live) {
        seed = s.seed;
    } else if (config().seed_set) {
        seed = config().seed;
    } else {
        return seed;
    }
    std::fprintf(stderr, "coop: seed 0x%08X\n", seed);
    return seed;
}

// Whether the per-frame hook should hash the state (a session, or
// BM64_COOP_LOG_HASH or BM64_COOP_LOG_STATE).
extern "C" uint32_t coop_session_hashing(void) {
    return coop::session_configured() || config().log_hash > 0 || config().log_state > 0;
}

// Hook in the player update func_8024E088 (bm64.us.toml), where it has read
// the camera's yaw and pitch to turn the stick by (#33): in a live session,
// the angles of the camera of the machine whose pad drives the player in
// `slot` (the host's for slot 0, the client's for player 2 in slot 4), as
// sent with the pad for this frame. False otherwise: the game's own camera.
extern "C" int32_t coop_session_view(int32_t slot, uint32_t* yaw, uint32_t* pitch) {
    const Session& s = session;
    if (!s.live || (slot != 0 && slot != 4)) {
        return 0;
    }
    const Pad& pad = s.pads[slot == 0 ? 0 : 1];
    if (!pad.view) {
        return 0;
    }
    *yaw = pad.yaw;
    *pitch = pad.pitch;
    return 1;
}

// From the per-frame hook, at the end of each game frame: the hash of the
// state after it with its parts and values (`words` words, patches/
// coop_session.c), the camera's yaw and pitch (their bits) and, in a session,
// the lockstep's step to the next frame. Frames count from the one of the
// game's first pad read. (Upstream's main loop reads the pads twice a frame,
// once for its cheats, so the step is here rather than at a pad read.)
extern "C" void coop_session_frame(const uint32_t* words, uint32_t count, uint32_t yaw, uint32_t pitch) {
    Session& s = session;
    uint32_t frame = hashed_frames++;
    State state{};
    std::memcpy(state.data(), words, std::min<size_t>(count, state_words) * sizeof(uint32_t));
    state[state_fpu] = fpu_control();
    if (config().log_hash > 0 && frame % config().log_hash == 0) {
        std::fprintf(stderr, "coop: hash frame=%u hash=%08X\n", frame, state[0]);
    }
    if (config().log_state > 0 && frame % config().log_state == 0) {
        log_snapshot(frame, state);
    }
    if (s.live) {
        step(s, state, yaw, pitch);
    }
}
