// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>

export module mm.shell.full:execute;

import :service;

export namespace mm::shell::full {

struct PipelineOutcome {
    ServiceStatus service = ServiceStatus::Ok;
    int exit_status = 0;
    std::size_t stages_started = 0;

    [[nodiscard]] bool ok() const {
        return service == ServiceStatus::Ok;
    }
};

// Executes one pipeline using abstract services. A failed launch closes all
// parent pipe ends, signals started children, and reaps every child. The last
// stage supplies the pipeline status; earlier statuses are still reaped.
[[nodiscard]] PipelineOutcome run_pipeline(
    std::span<const ProcessRequest> stages, Services services);

}  // namespace mm::shell::full
