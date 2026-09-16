// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <array>
#include <csignal>

import mm.test;
import mm.stdio;
import platform.linux.stdio;

namespace {

using mm::test::expect;

void stdio_initialize_sets_sigpipe_to_ignore() {
    static constexpr std::array monitored_signals{
        SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2};

    std::array<struct sigaction, monitored_signals.size()> before{};
    for (std::size_t i = 0; i < monitored_signals.size(); ++i) {
        sigaction(monitored_signals[i], nullptr, &before[i]);
    }

    struct sigaction original_pipe{};
    sigaction(SIGPIPE, nullptr, &original_pipe);

    // Reset SIGPIPE to default to assert that initialize alters it.
    struct sigaction reset_action{};
    reset_action.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &reset_action, nullptr);

    // This binary links one console provider and no stand-in, so the seam is
    // the Linux one. Ok rules out the unserved fallback, which answers
    // Unsupported to everything; a second registrant would show up here rather
    // than as a puzzling SIGPIPE result below.
    auto& console = mm::stdio::selected_console();
    expect(console.initialize() == mm::stdio::Status::Ok,
           "Linux console initialization succeeds, so the seam is a real provider");

    struct sigaction current_pipe{};
    sigaction(SIGPIPE, nullptr, &current_pipe);
    expect(current_pipe.sa_handler == SIG_IGN,
           "initialize sets SIGPIPE to SIG_IGN");

    // Assert that no other signal disposition was altered
    for (std::size_t i = 0; i < monitored_signals.size(); ++i) {
        struct sigaction current{};
        sigaction(monitored_signals[i], nullptr, &current);
        expect(current.sa_handler == before[i].sa_handler &&
                   current.sa_flags == before[i].sa_flags,
               "initialize alters no other signal disposition");
    }

    // Restore original SIGPIPE disposition
    sigaction(SIGPIPE, &original_pipe, nullptr);
}

const mm::test::case_ cases[]{
    {"SIGPIPE is SIG_IGN and others unaltered",
     &stdio_initialize_sets_sigpipe_to_ignore},
};
const mm::test::registrar reg{"platform.linux.stdio", cases};

}
