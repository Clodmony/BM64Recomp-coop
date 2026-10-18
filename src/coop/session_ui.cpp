// The launcher's Co-op menu and the session's state on screen (#28,
// docs/coop/design.md, *Hosting and joining*). The menu hosts a session on a
// port or joins one at an address and port: it sets the session the way
// BM64_COOP_SESSION does (src/coop/session.cpp) and connects while the
// launcher shows the wait (a started game draws nothing before its first
// frame). Once the hellos agree it starts the game, whose first pad read
// takes the host's save and starts the lockstep; refused or cancelled, it
// goes back to the menu. Each change of the session's state is shown as a
// prompt (waiting, with Cancel; connected; refused; lost) and logged as
// "coop: session ui: <header>: <text>", also for a session from the switches,
// which tools/run.sh's autostart connects the same way and then starts, alone
// if it did not connect.
//
//   BM64_COOP_LAUNCHER=host:<port>         fill the menu with it and press Host
//   BM64_COOP_LAUNCHER=join:<host>:<port>  ... Join (tools/run.sh's autostart);
//                                          back in the menu, press Start
//   BM64_COOP_CANCEL_AFTER=<s>             press the waiting prompt's Cancel
//                                          after s seconds (a test switch)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "librecomp/game.hpp"
#include "recomp_ui.h"
#include "../ui/elements/ui_button.h"
#include "../ui/elements/ui_label.h"
#include "../ui/elements/ui_text_input.h"

#include "session.h"
#include "session_ui.h"

extern std::vector<recomp::GameEntry> supported_games;

namespace {

using steady = std::chrono::steady_clock;

struct Menu {
    bool created = false;
    recompui::ContextId context;
    recompui::TextInput* host_port = nullptr;
    recompui::TextInput* join_address = nullptr;
    recompui::TextInput* join_port = nullptr;
    recompui::Label* error = nullptr;
};

Menu menu;

// Who waits for the connection to start the game: the menu (only once
// connected) or the switches (in any case, alone if it did not connect).
enum class Pending { None, Menu, Switches };
Pending pending = Pending::None;
bool test_launcher = false; // BM64_COOP_LAUNCHER: press Start after a failed try

void start_game(const char* how) {
    std::fprintf(stderr, "coop: %s %s\n", how, reinterpret_cast<const char*>(supported_games[0].game_id.c_str()));
    recomp::start_game(supported_games[0].game_id);
    recompui::hide_all_contexts();
}

// Runs fn with the menu's context open (element changes need it), as the
// prompts do.
template <typename Fn>
void with_menu(Fn fn) {
    recompui::ContextId prev = recompui::try_close_current_context();
    menu.context.open();
    fn();
    menu.context.close();
    if (prev != recompui::ContextId::null()) {
        prev.open();
    }
}

std::string one_line(std::string text) {
    for (char& c : text) {
        if (c == '\n') {
            c = ' ';
        }
    }
    return text;
}

// Sets the session from spec and connects; update_session_ui starts the game.
// A spec the session refuses is shown under the menu.
void start(const std::string& spec, const std::string& what) {
    std::string error;
    if (!coop::configure_session(spec, error)) {
        std::fprintf(stderr, "coop: launcher: %s\n", error.c_str());
        with_menu([&] { menu.error->set_text(error); });
        return;
    }
    std::fprintf(stderr, "coop: launcher: %s\n", what.c_str());
    with_menu([] { menu.error->set_text(""); });
    pending = Pending::Menu;
    coop::connect_session();
}

void press_host() {
    const std::string port = menu.host_port->get_input_text();
    start("host:" + port, "host port " + port);
}

void press_join() {
    const std::string address = menu.join_address->get_input_text();
    const std::string port = menu.join_port->get_input_text();
    start("join:" + address + ":" + port, "join " + address + " port " + port);
}

recompui::Element* row(recompui::ContextId context, recompui::Element* parent) {
    using namespace recompui;
    Element* r = context.create_element<Element>(parent);
    r->set_display(Display::Flex);
    r->set_flex_direction(FlexDirection::Row);
    r->set_align_items(AlignItems::Center);
    r->set_gap(24, Unit::Dp);
    r->set_margin(24, Unit::Dp);
    r->set_margin_top(0);
    return r;
}

recompui::TextInput* input(recompui::ContextId context, recompui::Element* parent, const std::string& text,
                           float width) {
    using namespace recompui;
    TextInput* in = context.create_element<TextInput>(parent);
    in->set_width(width, Unit::Dp);
    in->set_font_size(24, Unit::Dp);
    in->set_color(Color{ 242, 242, 242, 255 });
    in->set_text(text);
    return in;
}

void create_menu() {
    using namespace recompui;
    ContextId context = create_context();
    menu.context = context;
    ContextId prev = try_close_current_context();
    context.open();

    Element* window = context.create_element<Element>(context.get_root_element());
    window->set_display(Display::Flex);
    window->set_flex_direction(FlexDirection::Column);
    window->set_width(100, Unit::Percent);
    window->set_align_items(AlignItems::Center);
    window->set_justify_content(JustifyContent::Center);
    window->set_background_color({ 0, 0, 0, 0 });

    Element* content = context.create_element<Element>(window);
    content->set_display(Display::Flex);
    content->set_flex_direction(FlexDirection::Column);
    content->set_width(100, Unit::Percent);
    content->set_max_width(900, Unit::Dp);
    content->set_border_width(1.1, Unit::Dp);
    content->set_border_radius(16, Unit::Dp);
    content->set_border_color(Color{ 255, 255, 255, 51 });
    content->set_background_color(Color{ 8, 7, 13, 229 });

    Label* header = context.create_element<Label>(content, "Co-op session", LabelStyle::Large);
    header->set_margin(24, Unit::Dp);

    Element* host = row(context, content);
    context.create_element<Label>(host, "Host on port", LabelStyle::Small);
    menu.host_port = input(context, host, "7777", 120);
    Button* host_button = context.create_element<Button>(host, "Host", ButtonStyle::Primary);
    host_button->add_pressed_callback(press_host);

    Element* join = row(context, content);
    context.create_element<Label>(join, "Join", LabelStyle::Small);
    menu.join_address = input(context, join, "", 300);
    context.create_element<Label>(join, "port", LabelStyle::Small);
    menu.join_port = input(context, join, "7777", 120);
    Button* join_button = context.create_element<Button>(join, "Join", ButtonStyle::Primary);
    join_button->add_pressed_callback(press_join);

    menu.error = context.create_element<Label>(content, "", LabelStyle::Small);
    menu.error->set_margin(24, Unit::Dp);
    menu.error->set_margin_top(0);
    menu.error->set_color(Color{ 248, 96, 57, 255 });

    Element* controls = row(context, content);
    Button* back = context.create_element<Button>(controls, "Back", ButtonStyle::Secondary);
    back->add_pressed_callback([] {
        recompui::hide_context(menu.context);
        recompui::show_context(recompui::get_launcher_context_id(), "");
    });

    context.set_autofocus_element(host_button);
    context.close();
    if (prev != ContextId::null()) {
        prev.open();
    }
    menu.created = true;
}

// What the UI last showed.
struct Shown {
    uint32_t serial = 0;
    coop::SessionState state = coop::SessionState::Off;
    steady::time_point since{};
};

Shown shown;

void log_shown(const char* header, const std::string& text) {
    std::fprintf(stderr, "coop: session ui: %s: %s\n", header, one_line(text).c_str());
}

} // namespace

void coop::open_session_menu() {
    if (!menu.created) {
        create_menu();
    }
    with_menu([] { menu.error->set_text(""); });
    recompui::hide_all_contexts();
    recompui::show_context(menu.context, "");
}

void coop::update_session_ui() {
    static const long cancel_after = [] {
        const char* value = std::getenv("BM64_COOP_CANCEL_AFTER");
        return value != nullptr ? std::strtol(value, nullptr, 0) : 0L;
    }();
    SessionStatus status = session_status();
    if (status.serial == shown.serial) {
        auto waited = steady::now() - shown.since;
        if (shown.state == SessionState::Waiting && cancel_after > 0 && waited >= std::chrono::seconds(cancel_after)) {
            std::fprintf(stderr, "coop: session ui: cancel\n");
            shown.state = SessionState::Off;
            recompui::close_prompt(); // as the Cancel button: its action, then the prompt hides
        } else if (shown.state == SessionState::Connected && waited >= std::chrono::seconds(2)) {
            shown.state = SessionState::Off;
            recompui::close_prompt();
        }
        return;
    }
    shown = Shown{ status.serial, status.state, steady::now() };
    if (pending != Pending::None && status.state != SessionState::Waiting) {
        // the connection is done: start the game, or go back to the menu
        bool connected = status.state == SessionState::Connected;
        if (connected || pending == Pending::Switches) {
            start_game(pending == Pending::Menu ? "launcher: start" : connected ? "autostart" : "autostart alone");
        } else if (test_launcher) {
            start_game("launcher: start alone");
        } else {
            recompui::show_context(menu.context, "");
        }
        pending = Pending::None;
    }
    switch (status.state) {
        case SessionState::Waiting: {
            // Replacing or closing a prompt runs its cancel action: only a
            // press while this same state is shown cancels the session.
            uint32_t serial = status.serial;
            log_shown("Co-op session", status.text);
            recompui::open_info_prompt("Co-op session", status.text, "Cancel", [serial] {
                SessionStatus now = session_status();
                if (now.serial == serial && now.state == SessionState::Waiting) {
                    cancel_session();
                }
            });
            break;
        }
        case SessionState::Connected:
            log_shown("Co-op session", status.text);
            recompui::open_notification("Co-op session", status.text);
            break;
        case SessionState::Refused:
            log_shown("No co-op session", status.text);
            recompui::open_info_prompt("No co-op session", status.text + "\nPlaying alone.", "OK", {});
            break;
        case SessionState::Lost:
            log_shown("Co-op session lost", status.text);
            recompui::open_info_prompt("Co-op session lost", status.text + "\nPlaying alone.", "OK", {});
            break;
        case SessionState::Cancelled:
            std::fprintf(stderr, "coop: session ui: cancelled\n");
            recompui::close_prompt();
            break;
        case SessionState::Off:
            break;
    }
}

bool coop::launcher_autostart() {
    const char* value = std::getenv("BM64_COOP_LAUNCHER");
    if (value == nullptr || value[0] == '\0') {
        if (!session_configured()) {
            return false;
        }
        // BM64_COOP_SESSION: connect with the launcher's prompts, then start
        pending = Pending::Switches;
        connect_session();
        return true;
    }
    test_launcher = true;
    std::string spec = value;
    open_session_menu();
    if (spec.rfind("join:", 0) == 0 && spec.rfind(':') > 5) {
        with_menu([&] {
            menu.join_address->set_text(spec.substr(5, spec.rfind(':') - 5));
            menu.join_port->set_text(spec.substr(spec.rfind(':') + 1));
        });
        press_join();
    } else {
        // host:<port>, or anything else: Host shows what the session refuses
        with_menu([&] { menu.host_port->set_text(spec.rfind("host:", 0) == 0 ? spec.substr(5) : spec); });
        press_host();
    }
    return true;
}
