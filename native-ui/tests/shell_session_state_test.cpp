#include "../shell/src/main/cpp/shell_session_state.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(msg);
}
} // namespace

int main() {
    using shell_session::State;
    try {
        State state;
        state.unlocked = true;
        state.focus_page = 2;
        state.focus_slot = 7;
        state.active = 1;
        state.tabs = {"PCSE00291", "PCSE00089"};
        const auto parsed = shell_session::parse(shell_session::serialize(state));
        require(parsed && *parsed == state, "Round trip keeps every field");

        const auto home = shell_session::parse("v1\nunlocked 1\nfocus 0 3\nactive -1\n");
        require(home && home->unlocked && home->active == -1 && home->tabs.empty() && home->focus_slot == 3,
            "Home-only session");
        const auto locked = shell_session::parse(shell_session::serialize(State{}));
        require(locked && !locked->unlocked, "Locked session round-trips as locked");

        require(!shell_session::parse(""), "Empty text is no session");
        require(!shell_session::parse("v2\nunlocked 1\n"), "Unknown version is rejected");
        require(!shell_session::parse("unlocked 1\n"), "Missing header is rejected");
        require(!shell_session::parse("v1\nfocus 1\n"), "Truncated focus is rejected");
        require(!shell_session::parse("v1\nfocus -1 0\n"), "Negative focus is rejected");
        require(!shell_session::parse("v1\nfocus 1x 0\n"), "Garbage number is rejected");
        require(!shell_session::parse("v1\nactive 0\n"), "Active tab without tabs is rejected");
        require(!shell_session::parse("v1\ntab A B\n"), "Title with a space is rejected");
        require(!shell_session::parse("v1\ntab " + std::string(65, 'X') + "\n"), "Oversized title is rejected");
        std::string many = "v1\n";
        for (int i = 0; i < 7; ++i) many += "tab T" + std::to_string(i) + "\n";
        require(!shell_session::parse(many), "More tabs than the deck allows is rejected");

        const auto crlf = shell_session::parse("v1\r\nunlocked 1\r\ntab PCSE00089\r\nfuture key\r\nactive 0\r\n");
        require(crlf && crlf->unlocked && crlf->tabs.size() == 1 && crlf->active == 0,
            "CRLF and unknown keys are tolerated");

        std::cout << "PASS shell_session_state_test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
