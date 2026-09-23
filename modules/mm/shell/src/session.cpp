// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

module mm.shell;

import :command;
import :execute;
import :parse;
import :session;
import :source;
import :status;
import :syntax;

namespace mm::shell {
namespace {

constexpr char backspace = '\b';
constexpr char delete_byte = '\x7f';

[[nodiscard]] bool printable(char byte) {
    const auto value = static_cast<unsigned char>(byte);
    return (value >= 0x20 && value < 0x7f) || byte == '\t';
}

[[nodiscard]] bool line_ending(char byte) {
    return byte == '\n' || byte == '\r';
}

}  // namespace

Status Session::begin(Registry& registry, CommandContext& context,
                      SessionStorage storage) {
    registry_ = &registry;
    context_ = &context;
    storage_ = storage;
    script_ = {};
    used_ = 0;
    state_ = SessionState::Ready;
    last_status_ = 0;
    if (storage.source.empty()) return Status::Overflow;
    return Status::Ok;
}

void Session::reset() {
    script_ = {};
    used_ = 0;
    state_ = SessionState::Ready;
}

bool Session::append(char byte) {
    if (used_ == storage_.source.size()) return false;
    storage_.source[used_++] = byte;
    return true;
}

// A complete command is measured, parsed into caller storage, and handed to
// the evaluator. Nothing is executed unless every preflight succeeds.
FeedResult Session::launch() {
    const auto source = SourceView{
        std::string_view{storage_.source.data(), used_}};
    const auto measured = measure_embedded(source);
    if (measured.status == ParseStatus::Incomplete) {
        state_ = SessionState::Continuing;
        return FeedResult{.state = state_,
                          .parse_status = measured.status};
    }
    if (measured.status != ParseStatus::Complete) {
        used_ = 0;
        state_ = SessionState::Ready;
        return FeedResult{.status = Status::BadArgument,
                          .state = state_,
                          .parse_status = measured.status,
                          .issue = measured.issue};
    }
    const auto parsed = parse_embedded(source, storage_.script, script_);
    if (parsed.status != ParseStatus::Complete) {
        used_ = 0;
        state_ = SessionState::Ready;
        return FeedResult{
            .status = parsed.status == ParseStatus::Overflow
                          ? Status::Overflow
                          : Status::BadArgument,
            .state = state_,
            .parse_status = parsed.status,
            .issue = parsed.issue,
            .overflow = parsed.overflow,
        };
    }
    const auto started = evaluator_.begin(script_, *registry_, *context_,
                                          storage_.evaluator);
    if (started != Status::Ok) {
        used_ = 0;
        state_ = SessionState::Ready;
        return FeedResult{.status = started, .state = state_};
    }
    state_ = SessionState::Executing;
    return FeedResult{.state = state_};
}

FeedResult Session::feed(std::span<const char> bytes) {
    if (registry_ == nullptr || context_ == nullptr) {
        return FeedResult{.status = Status::Unavailable, .state = state_};
    }
    if (state_ == SessionState::Executing) {
        return FeedResult{.status = Status::Unavailable, .state = state_};
    }
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto byte = bytes[index];
        if (line_ending(byte)) {
            if (state_ == SessionState::Discarding) {
                used_ = 0;
                state_ = SessionState::Ready;
                continue;
            }
            // An empty command line is a no-op rather than a parse error.
            if (used_ == 0) continue;
            if (!append('\n')) {
                used_ = 0;
                state_ = SessionState::Ready;
                auto result = FeedResult{
                    .status = Status::Overflow,
                    .consumed = index + 1,
                    .state = state_,
                    .overflow = {StorageClass::SourceBytes,
                                 storage_.source.size() + 1},
                };
                return result;
            }
            auto result = launch();
            result.consumed = index + 1;
            if (result.state == SessionState::Continuing && result.ok()) {
                continue;
            }
            return result;
        }
        if (byte == backspace || byte == delete_byte) {
            if (state_ != SessionState::Discarding && used_ != 0) --used_;
            continue;
        }
        if (!printable(byte)) continue;
        if (state_ == SessionState::Discarding) continue;
        if (!append(byte)) {
            // Report the overflow once, then swallow the rest of the line.
            state_ = SessionState::Discarding;
            return FeedResult{
                .status = Status::Overflow,
                .consumed = index + 1,
                .state = state_,
                .overflow = {StorageClass::SourceBytes,
                             storage_.source.size() + 1},
            };
        }
    }
    return FeedResult{.consumed = bytes.size(), .state = state_};
}

StepResult Session::step(std::size_t operation_budget) {
    if (state_ != SessionState::Executing) {
        return StepResult{.step = Step::Complete,
                          .command = {.status = last_status_}};
    }
    const auto result = evaluator_.step(operation_budget);
    if (result.step == Step::Running || result.step == Step::Yielded) {
        return result;
    }
    last_status_ = result.command.status;
    // The source buffer is only reclaimed once nothing views it any more.
    used_ = 0;
    state_ = SessionState::Ready;
    return result;
}

}  // namespace mm::shell
