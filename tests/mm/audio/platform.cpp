// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Selection, and the fallbacks a lane with no audio provider links. A host
// test binds no platform provider, so selected_in and selected_out start at
// mm.audio's own devices, which answer Unsupported, until a stand-in is set.
#include <cstddef>
#include <cstdint>
#include <string_view>

import mm.audio;
import mm.test;

namespace {

using mm::audio::Format;
using mm::audio::Rate;
using mm::audio::Status;
using mm::test::expect;

// A stand-in speaker: enough of an Out to be told apart from the fallback.
class Stand final : public mm::audio::Out {
public:
    [[nodiscard]] mm::audio::Description description() const override {
        return {"stand", 8'000, 48'000, 64};
    }
    [[nodiscard]] Status initialize() override { return Status::Ok; }
};

class Listener final : public mm::audio::In {
public:
    [[nodiscard]] Status initialize() override { return Status::Ok; }
};

// Namespace scope, so the selection never outlives what it selects.
Stand speaker;
Listener microphone;

void the_fallbacks_answer_unsupported() {
    mm::audio::Out out;
    mm::audio::In in;
    mm::audio::Ring ring;
    Format actual{.rate_hz = 11};
    Rate rate{7, 3};
    std::uint64_t position = 13;
    std::size_t pending = 17;
    expect(out.description().name.empty() && in.description().name.empty(),
           "an unserved device has no description");
    expect(out.initialize() == Status::Unsupported && in.initialize() == Status::Unsupported,
           "and cannot be initialised");
    expect(out.configure({.rate_hz = 8'000}, actual) == Status::Unsupported &&
               in.configure({.rate_hz = 8'000}, actual) == Status::Unsupported &&
               actual.rate_hz == 11,
           "configure answers without writing the actual format");
    expect(out.rate(rate) == Status::Unsupported && in.rate(rate) == Status::Unsupported &&
               rate.numerator == 7,
           "so does rate");
    expect(out.start(ring) == Status::Unsupported && in.start(ring) == Status::Unsupported &&
               out.service() == Status::Unsupported && in.service() == Status::Unsupported,
           "start and service answer rather than dereferencing nothing");
    expect(out.position(position) == Status::Unsupported &&
               in.position(position) == Status::Unsupported &&
               out.pending(pending) == Status::Unsupported && position == 13 &&
               pending == 17,
           "position and pending answer without writing");
    expect(out.stop() == Status::Unsupported && in.stop() == Status::Unsupported &&
               out.sleep() == Status::Unsupported && in.sleep() == Status::Unsupported,
           "and so do stop and sleep");
}

void selection_returns_what_was_registered() {
    expect(mm::audio::selected_out().initialize() == Status::Unsupported &&
               mm::audio::selected_in().initialize() == Status::Unsupported,
           "with no provider the selection is the fallback");
    mm::audio::set_out(speaker);
    mm::audio::set_in(microphone);
    expect(&mm::audio::selected_out() == &speaker && &mm::audio::selected_in() == &microphone,
           "a registered device is the selected one");
    expect(mm::audio::selected_out().description().name == "stand" &&
               mm::audio::selected_in().initialize() == Status::Ok,
           "and calls reach it through the interface");
}

const mm::test::case_ cases[] = {
    {"the fallbacks answer Unsupported", &the_fallbacks_answer_unsupported},
    {"selection returns what was registered", &selection_returns_what_was_registered},
};

const mm::test::registrar reg{"mm.audio platform", cases};

}  // namespace
