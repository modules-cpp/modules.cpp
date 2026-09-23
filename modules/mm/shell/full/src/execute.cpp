// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <vector>

module mm.shell.full;

import :execute;
import :service;

namespace mm::shell::full {

PipelineOutcome run_pipeline(
    std::span<const ProcessRequest> stages, Services services) {
    if (stages.empty() || services.process.spawn == nullptr ||
        services.process.wait == nullptr || services.io.close == nullptr ||
        (stages.size() > 1 && services.io.pipe == nullptr)) {
        return {.service = ServiceStatus::Invalid};
    }
    std::vector<Handle> children;
    children.reserve(stages.size());
    Handle previous_read = invalid_handle;
    ServiceStatus failure = ServiceStatus::Ok;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        Handle next_read = invalid_handle;
        Handle next_write = invalid_handle;
        if (i + 1 < stages.size()) {
            failure = services.io.pipe(services.io.context,
                                       next_read, next_write);
            if (failure != ServiceStatus::Ok) {
                if (next_read != invalid_handle) {
                    (void)services.io.close(services.io.context,
                                            next_read);
                }
                if (next_write != invalid_handle) {
                    (void)services.io.close(services.io.context,
                                            next_write);
                }
                break;
            }
        }
        auto request = stages[i];
        if (request.input == invalid_handle) {
            request.input = previous_read;
        }
        if (request.output == invalid_handle) {
            request.output = next_write;
        }
        Handle child = invalid_handle;
        failure = services.process.spawn(
            services.process.context, request, child);
        if (previous_read != invalid_handle) {
            (void)services.io.close(services.io.context,
                                    previous_read);
        }
        if (next_write != invalid_handle) {
            (void)services.io.close(services.io.context, next_write);
        }
        previous_read = next_read;
        if (failure != ServiceStatus::Ok) break;
        children.push_back(child);
    }
    if (previous_read != invalid_handle) {
        (void)services.io.close(services.io.context, previous_read);
    }
    if (failure != ServiceStatus::Ok &&
        services.process.signal != nullptr) {
        for (const auto child : children) {
            (void)services.process.signal(services.process.context,
                                          child, 15);
        }
    }
    int last_status = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        int status = 0;
        const auto waited = services.process.wait(
            services.process.context, children[i], status);
        if (waited != ServiceStatus::Ok &&
            failure == ServiceStatus::Ok) failure = waited;
        if (i + 1 == children.size()) last_status = status;
    }
    return {.service = failure, .exit_status = last_status,
            .stages_started = children.size()};
}

}  // namespace mm::shell::full
