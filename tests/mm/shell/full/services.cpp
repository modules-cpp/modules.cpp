// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import mm.shell.full;
import mm.shell;
import mm.test;

namespace {

using mm::test::expect;
namespace full = mm::shell::full;

struct RecordingServices {
    std::size_t spawns = 0;
    std::size_t fail_spawn_at = 0;
    bool fail_pipe = false;
    std::vector<full::Handle> closed;
    std::vector<full::Handle> signaled;
    std::vector<full::Handle> waited;
    std::vector<full::Handle> inputs;
    std::vector<full::Handle> outputs;

    static full::ServiceStatus pipe(
        void* data, full::Handle& read, full::Handle& write) {
        auto& self = *static_cast<RecordingServices*>(data);
        read = 10;
        if (self.fail_pipe) return full::ServiceStatus::Failed;
        write = 11;
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus close(void* data,
                                     full::Handle handle) {
        static_cast<RecordingServices*>(data)->closed.push_back(handle);
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus spawn(
        void* data, const full::ProcessRequest& request,
        full::Handle& child) {
        auto& self = *static_cast<RecordingServices*>(data);
        ++self.spawns;
        self.inputs.push_back(request.input);
        self.outputs.push_back(request.output);
        if (self.spawns == self.fail_spawn_at) {
            return full::ServiceStatus::NotFound;
        }
        child = 99 + self.spawns;
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus wait(void* data,
                                    full::Handle child, int& status) {
        auto& self = *static_cast<RecordingServices*>(data);
        self.waited.push_back(child);
        status = child == 100 ? 3 : 7;
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus signal(void* data,
                                      full::Handle child, int) {
        static_cast<RecordingServices*>(data)->signaled.push_back(child);
        return full::ServiceStatus::Ok;
    }

    [[nodiscard]] full::Services services() {
        return {
            .io = {.context = this, .pipe = &pipe, .close = &close},
            .process = {.context = this, .spawn = &spawn,
                        .wait = &wait, .signal = &signal},
        };
    }
};

void pipeline_status_and_handles() {
    RecordingServices recorder;
    const std::string_view first[]{"producer"};
    const std::string_view second[]{"consumer"};
    const full::ProcessRequest stages[]{
        {.arguments = first}, {.arguments = second},
    };
    const auto result = full::run_pipeline(stages,
                                           recorder.services());
    expect(result.ok() && result.exit_status == 7 &&
               result.stages_started == 2 &&
               recorder.inputs[1] == 10 &&
               recorder.outputs[0] == 11 &&
               recorder.closed.size() == 2 &&
               recorder.waited.size() == 2,
           "pipeline wires and reaps every stage; last status wins");
}

void pipeline_failure_reaps_started_children() {
    RecordingServices recorder;
    recorder.fail_spawn_at = 2;
    const std::string_view first[]{"producer"};
    const std::string_view second[]{"missing"};
    const full::ProcessRequest stages[]{
        {.arguments = first}, {.arguments = second},
    };
    const auto result = full::run_pipeline(stages,
                                           recorder.services());
    expect(result.service == full::ServiceStatus::NotFound &&
               result.stages_started == 1 &&
               recorder.closed.size() == 2 &&
               recorder.signaled.size() == 1 &&
               recorder.waited.size() == 1,
           "failed launch closes pipes and reaps the started stage");

    RecordingServices failed_pipe;
    failed_pipe.fail_pipe = true;
    const auto refused = full::run_pipeline(stages,
                                            failed_pipe.services());
    expect(refused.service == full::ServiceStatus::Failed &&
               refused.stages_started == 0 &&
               failed_pipe.closed.size() == 1,
           "partial pipe creation is closed before returning");
}

struct RecordingSignals {
    std::vector<int> installed;
    std::vector<int> restored;
    int pending = 0;

    static full::ServiceStatus install(void* data, int number) {
        static_cast<RecordingSignals*>(data)->installed.push_back(number);
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus restore(void* data, int number) {
        static_cast<RecordingSignals*>(data)->restored.push_back(number);
        return full::ServiceStatus::Ok;
    }

    static full::ServiceStatus poll(void* data, int& number) {
        auto& self = *static_cast<RecordingSignals*>(data);
        if (self.pending == 0) return full::ServiceStatus::WouldBlock;
        number = self.pending;
        self.pending = 0;
        return full::ServiceStatus::Ok;
    }

    [[nodiscard]] full::SignalService service() {
        return {.context = this, .install = &install,
                .restore = &restore, .poll = &poll};
    }
};

void trap_spellings_and_child_state() {
    full::FullState parent;
    RecordingSignals signals;
    const std::string_view numeric[]{"trap", "restore_host", "0"};
    const std::string_view mixed[]{
        "trap", "mm_cleanup", "EXIT", "INT", "TERM"};
    expect(full::configure_trap(parent, numeric,
                                signals.service()).ok() &&
               parent.trap(0) == "restore_host" &&
               signals.installed.empty(),
           "numeric exit trap is a shell event, not a signal");
    expect(full::configure_trap(parent, mixed,
                                signals.service()).ok() &&
               parent.trap(0) == "mm_cleanup" &&
               parent.trap(2) == "mm_cleanup" &&
               parent.trap(15) == "mm_cleanup" &&
               signals.installed.size() == 2,
           "mixed symbolic conditions install signal hooks");
    full::FullState child;
    expect(parent.fork_into(child) == mm::shell::Status::Ok,
           "trap-bearing state can fork");
    const std::string_view reset[]{
        "trap", "-", "EXIT", "INT", "TERM"};
    expect(full::configure_trap(child, reset,
                                signals.service()).ok() &&
               child.trap(0).empty() && child.trap(2).empty() &&
               parent.trap(0) == "mm_cleanup" &&
               signals.restored.size() == 2,
           "reset changes only the child trap state");
    // The fourth pinned form: numeric reset of the exit condition alone.
    const std::string_view numeric_reset[]{"trap", "-", "0"};
    const auto before = signals.restored.size();
    expect(full::configure_trap(parent, numeric_reset,
                                signals.service()).ok() &&
               parent.trap(0).empty() &&
               parent.trap(2) == "mm_cleanup" &&
               signals.restored.size() == before,
           "numeric reset clears only the exit action and no signal hook");
    const std::string_view restore_exit[]{"trap", "mm_cleanup", "0"};
    expect(full::configure_trap(parent, restore_exit,
                                signals.service()).ok() &&
               parent.trap(0) == "mm_cleanup",
           "the exit action reinstalls for the remaining assertions");

    signals.pending = 15;
    const auto fired = full::poll_trap(parent, signals.service());
    expect(fired.service == full::ServiceStatus::Ok &&
               fired.condition == 15 &&
               fired.action == "mm_cleanup",
           "pending signal resolves the owning shell action");
    const std::string_view bad[]{
        "trap", "new", "INT", "BOGUS"};
    const auto refused = full::configure_trap(
        parent, bad, signals.service());
    expect(refused.service == full::ServiceStatus::Invalid &&
               refused.bad_condition == "BOGUS" &&
               parent.trap(2) == "mm_cleanup",
           "invalid condition refuses the entire trap update");
}

const mm::test::case_ cases[]{
    {"pipeline status and handles", &pipeline_status_and_handles},
    {"pipeline failure cleanup", &pipeline_failure_reaps_started_children},
    {"trap spellings and child state", &trap_spellings_and_child_state},
};

const mm::test::registrar reg{"mm.shell.full services", cases};

}  // namespace
