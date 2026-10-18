// The session core of online co-op (#21, docs/coop/design.md, *The session
// core*): two instances of the same build, a host and a client, run the same
// game frames on the same pads (lockstep). Each machine sends its own pad 0
// once per frame; on both, the game reads the host's as pad 0 (player 1) and
// the client's as pad 1 (player 2 in slot 4, patches/coop_player2.c). Both
// seed the game's random generator with the host's seed, and both compare a
// hash of their state every frame (patches/coop_session.c).
//
//   BM64_COOP_SESSION=host:<port>         wait for the client on TCP <port>
//   BM64_COOP_SESSION=join:<host>:<port>  connect to the host
//   BM64_COOP_DELAY=<frames>              the input delay, 1-60 (host's, default 2)
//   BM64_COOP_SEED=<n>                    seed the random generator with n;
//                                         works without a session too (two
//                                         runs on the same script then match)
//   BM64_COOP_LOG_HASH=<n>                log the state hash every n frames
//   BM64_COOP_CONNECT_TIMEOUT=<s>         wait this long for the other (60)
//
// The connection is made at the game's first pad read (before the random
// generator is seeded), so both machines start the game together. The frame
// protocol, over TCP with Nagle off, little-endian:
//
//   hello  "COOP", u16 protocol, u8 role (0 host, 1 client), u8 game (0 US,
//          1 Europe), u32 seed, u16 delay, u16 0; the client takes the host's
//          seed and delay, and either side refuses another protocol or game
//   frame  u32 frame, u16 buttons, u16 0, f32 x, f32 y (the bits the game
//          will read), u32 hash frame, u32 hash (of the state after that
//          frame)
//
// Game frames are numbered from 0, the one of the game's first pad read. The
// per-frame hook at the end of frame f (bm64.us.toml) is the lockstep's step:
// a machine sends its hash of frame f and its pad for frame f + delay, then
// waits for the other's pad for frame f + 1; frames below the delay hold no
// buttons. A hash that differs from
// this machine's for the same frame logs "coop: session desync" (once, then a
// count every 600 frames). A peer that closes, or sends nothing for 10 s,
// ends the session: "coop: session lost", and the game reads the local pads
// again.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

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

#include "session.h"

namespace {

constexpr uint16_t protocol = 1;
constexpr uint32_t no_frame = 0xFFFFFFFF;
constexpr size_t hello_size = 16;
constexpr size_t frame_size = 24;
constexpr auto peer_timeout = std::chrono::seconds(10);

#ifdef BM64_VERSION_EU
constexpr uint8_t game_version = 1;
#else
constexpr uint8_t game_version = 0;
#endif

enum class Role { None, Host, Client };

struct Pad {
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Config {
    Role role = Role::None;
    std::string host;
    std::string port;
    uint32_t delay = 2;
    bool seed_set = false;
    uint32_t seed = 0;
    uint32_t log_hash = 0;
    int connect_timeout = 60;
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
    std::map<uint32_t, uint32_t> hashes; // this machine's hashes, by frame
    uint32_t hash_frame = no_frame;
    uint32_t hash = 0;
    uint32_t desyncs = 0;
    uint32_t first_desync = no_frame;

    // filled by the receiver thread
    std::mutex mutex;
    std::condition_variable arrived;
    std::map<uint32_t, Pad> peer;
    std::deque<std::pair<uint32_t, uint32_t>> peer_hashes;
    bool peer_closed = false;
};

Session session;
std::atomic<uint32_t> hashed_frames{0};

uint32_t env_number(const char* name, uint32_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? static_cast<uint32_t>(std::strtoul(value, nullptr, 0)) : fallback;
}

Config read_config() {
    Config config;
    const char* seed = std::getenv("BM64_COOP_SEED");
    if (seed != nullptr && seed[0] != '\0') {
        config.seed_set = true;
        config.seed = static_cast<uint32_t>(std::strtoul(seed, nullptr, 0));
    }
    config.delay = env_number("BM64_COOP_DELAY", 2);
    config.log_hash = env_number("BM64_COOP_LOG_HASH", 0);
    config.connect_timeout = static_cast<int>(env_number("BM64_COOP_CONNECT_TIMEOUT", 60));
    const char* value = std::getenv("BM64_COOP_SESSION");
    if (value == nullptr || value[0] == '\0') {
        return config;
    }
    std::string text = value;
    if (text.rfind("host:", 0) == 0) {
        config.role = Role::Host;
        config.port = text.substr(5);
    } else if (text.rfind("join:", 0) == 0 && text.rfind(':') > 5) {
        config.role = Role::Client;
        config.host = text.substr(5, text.rfind(':') - 5);
        config.port = text.substr(text.rfind(':') + 1);
    }
    if (config.role == Role::None || config.port.empty() || config.delay < 1 || config.delay > 60) {
        std::fprintf(stderr, "coop: session: BM64_COOP_SESSION must be host:<port> or join:<host>:<port> "
                             "(BM64_COOP_DELAY 1-60), not %s; off\n", value);
        config.role = Role::None;
    }
    return config;
}

const Config& config() {
    static const Config config = read_config();
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
        return no_socket;
    }
    std::fprintf(stderr, "coop: session host: waiting on port %s\n", config.port.c_str());
    socket_t sock = no_socket;
    if (wait_readable(listener, config.connect_timeout * 1000)) {
        sock = accept(listener, nullptr, nullptr);
    }
    close_socket(listener);
    return sock;
}

socket_t connect_host(const Config& config) {
    std::fprintf(stderr, "coop: session client: connecting to %s port %s\n", config.host.c_str(),
                 config.port.c_str());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.connect_timeout);
    while (std::chrono::steady_clock::now() < deadline) {
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

void receive_frames(socket_t sock) {
    uint8_t msg[frame_size];
    while (recv_all(sock, msg, sizeof(msg))) {
        Pad pad{get16(msg + 4), bits_float(get32(msg + 8)), bits_float(get32(msg + 12))};
        std::lock_guard lock{session.mutex};
        session.peer[get32(msg)] = pad;
        if (get32(msg + 16) != no_frame) {
            session.peer_hashes.emplace_back(get32(msg + 16), get32(msg + 20));
        }
        session.arrived.notify_all();
    }
    std::lock_guard lock{session.mutex};
    session.peer_closed = true;
    session.arrived.notify_all();
}

// Connects and exchanges the hellos; true when the session is live.
bool start(Session& s) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const Config& c = s.config;
    bool host = c.role == Role::Host;
    s.sock = host ? accept_client(c) : connect_host(c);
    if (s.sock == no_socket) {
        std::fprintf(stderr, "coop: session: no %s within %d s; playing alone\n", host ? "client" : "host",
                     c.connect_timeout);
        return false;
    }
    int on = 1;
    setsockopt(s.sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    s.seed = c.seed_set ? c.seed : std::random_device{}();
    uint8_t hello[hello_size] = {'C', 'O', 'O', 'P'};
    put16(hello + 4, protocol);
    hello[6] = host ? 0 : 1;
    hello[7] = game_version;
    put32(hello + 8, host ? s.seed : 0);
    put16(hello + 12, static_cast<uint16_t>(c.delay));
    uint8_t theirs[hello_size];
    set_recv_timeout(s.sock, c.connect_timeout); // something else on the port may never say hello
    if (!send_all(s.sock, hello, sizeof(hello)) || !recv_all(s.sock, theirs, sizeof(theirs))) {
        std::fprintf(stderr, "coop: session: no hello from the other side; playing alone\n");
        close_socket(s.sock);
        return false;
    }
    set_recv_timeout(s.sock, 0);
    const char* refused = nullptr;
    if (std::memcmp(theirs, "COOP", 4) != 0) {
        refused = "not a co-op session";
    } else if (get16(theirs + 4) != protocol) {
        refused = "another protocol version";
    } else if (theirs[6] != (host ? 1 : 0)) {
        refused = "both sides have the same role";
    } else if (theirs[7] != game_version) {
        refused = "another game version (US and Europe do not mix)";
    }
    if (refused != nullptr) {
        std::fprintf(stderr, "coop: session refused: %s; playing alone\n", refused);
        close_socket(s.sock);
        return false;
    }
    if (!host) {
        s.seed = get32(theirs + 8);
        s.config.delay = get16(theirs + 12);
    }
    std::fprintf(stderr, "coop: session %s: connected, seed=0x%08X delay=%u\n", host ? "host" : "client", s.seed,
                 s.config.delay);
    std::thread(receive_frames, s.sock).detach();
    return true;
}

void lose(Session& s, const char* why) {
    std::fprintf(stderr, "coop: session lost frame=%u: %s; playing alone\n", s.frame, why);
    s.live = false;
    shutdown(s.sock, 2);
}

// Compares the other side's hashes with this machine's; called with the lock.
void compare_hashes(Session& s) {
    while (!s.peer_hashes.empty()) {
        auto [frame, hash] = s.peer_hashes.front();
        auto mine = s.hashes.find(frame);
        if (mine == s.hashes.end()) {
            if (frame > s.hash_frame || s.hash_frame == no_frame) {
                break; // this machine has not got there yet
            }
            s.peer_hashes.pop_front(); // too old to compare
            continue;
        }
        if (mine->second != hash) {
            if (s.desyncs++ == 0) {
                s.first_desync = frame;
                std::fprintf(stderr, "coop: session desync frame=%u local=%08X peer=%08X\n", frame, mine->second,
                             hash);
            }
        }
        s.peer_hashes.pop_front();
    }
    while (!s.hashes.empty() && s.hash_frame != no_frame && s.hashes.begin()->first + 600 < s.hash_frame) {
        s.hashes.erase(s.hashes.begin());
    }
}

// One frame of the lockstep, from the per-frame hook at the end of frame
// s.frame: this machine's hash of it, its pad for frame s.frame + delay, and
// both pads of the next frame (the other's waited for).
void step(Session& s, uint32_t hash) {
    s.hash_frame = s.frame;
    s.hash = hash;
    s.hashes[s.frame] = hash;
    Pad mine{};
    if (!s.local(0, &mine.buttons, &mine.x, &mine.y)) {
        mine = Pad{};
    }
    uint32_t target = s.frame + s.config.delay;
    s.own[target] = mine;
    uint8_t msg[frame_size] = {};
    put32(msg, target);
    put16(msg + 4, mine.buttons);
    put32(msg + 8, float_bits(mine.x));
    put32(msg + 12, float_bits(mine.y));
    put32(msg + 16, s.hash_frame);
    put32(msg + 20, s.hash);
    if (!send_all(s.sock, msg, sizeof(msg))) {
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

bool coop::session_configured() {
    return config().role != Role::None;
}

bool coop::session_connected() {
    return session.connected;
}

std::optional<bool> coop::session_input(int pad, uint16_t* buttons, float* x, float* y, LocalInput local) {
    Session& s = session;
    if (!session_configured() || (s.started && !s.live)) {
        return std::nullopt;
    }
    if (!s.started) {
        // the game's first pad read, before its random generator is seeded
        s.started = true;
        s.config = config();
        s.local = local;
        s.live = start(s);
        s.connected = s.live;
        if (!s.live) {
            return std::nullopt;
        }
        std::fprintf(stderr, "coop: session lockstep from frame 0\n");
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
// BM64_COOP_LOG_HASH).
extern "C" uint32_t coop_session_hashing(void) {
    return coop::session_configured() || config().log_hash > 0;
}

// From the per-frame hook, at the end of each game frame: the hash of the
// state after it (patches/coop_session.c) and, in a session, the lockstep's
// step to the next frame. Frames count from the one of the game's first pad
// read. (Upstream's main loop reads the pads twice a frame, once for its
// cheats, so the step is here rather than at a pad read.)
extern "C" void coop_session_frame(uint32_t hash) {
    Session& s = session;
    uint32_t frame = hashed_frames++;
    if (config().log_hash > 0 && frame % config().log_hash == 0) {
        std::fprintf(stderr, "coop: hash frame=%u hash=%08X\n", frame, hash);
    }
    if (s.live) {
        step(s, hash);
    }
}
