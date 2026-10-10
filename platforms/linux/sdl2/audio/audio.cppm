module;

#include <SDL2/SDL_audio.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

export module platform.linux.audio;

import mm.audio;

namespace platform::linux::audio_provider {

using mm::audio::Status;
constexpr std::size_t chunk_samples = 512;
constexpr std::size_t queue_samples = 2048;

// SDL's audio subsystem is shared by playback and capture, while each device
// retains its own stream and sample queue.
class Runtime {
public:
    [[nodiscard]] bool acquire() {
        if (users_ == 0 && SDL_AudioInit(nullptr) != 0) return false;
        ++users_;
        return true;
    }
    void release() {
        if (users_ != 0 && --users_ == 0) SDL_AudioQuit();
    }
private:
    unsigned int users_ = 0;
};

Runtime runtime;

[[nodiscard]] SDL_AudioDeviceID open_device(bool capture, unsigned int requested,
                                             mm::audio::Format& actual) {
    if (requested == 0 || requested > static_cast<unsigned int>(
            std::numeric_limits<int>::max())) return 0;
    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = static_cast<int>(requested);
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = static_cast<Uint16>(chunk_samples);
    const auto device = SDL_OpenAudioDevice(nullptr, capture ? 1 : 0,
        &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (device == 0) return 0;
    if (obtained.freq <= 0 || obtained.format != AUDIO_S16SYS ||
        obtained.channels != 1) {
        SDL_CloseAudioDevice(device);
        return 0;
    }
    actual.rate_hz = static_cast<unsigned int>(obtained.freq);
    SDL_PauseAudioDevice(device, 1);
    return device;
}

class Speaker final : public mm::audio::Out {
public:
    ~Speaker() override { close(); }

    [[nodiscard]] mm::audio::Description description() const override {
        return {"Linux SDL2 playback", 8000, 192000, queue_samples};
    }

    [[nodiscard]] Status initialize() override {
        if (initialized_) return Status::Ok;
        if (!runtime.acquire()) return Status::TransportError;
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status configure(const mm::audio::Format& requested,
                                   mm::audio::Format& actual) override {
        if (!initialized_) return Status::NotInitialized;
        if (started_) return Status::Busy;
        if (requested.rate_hz < 8000 || requested.rate_hz > 192000)
            return Status::BadArgument;
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        device_ = 0;
        format_ = {};
        device_ = open_device(false, requested.rate_hz, format_);
        if (device_ == 0) return Status::TransportError;
        actual = format_;
        return Status::Ok;
    }

    [[nodiscard]] Status rate(mm::audio::Rate& actual) const override {
        if (device_ == 0) return Status::NotInitialized;
        actual = {format_.rate_hz, 1};
        return Status::Ok;
    }

    [[nodiscard]] Status start(mm::audio::Stream& stream) override {
        if (started_) return Status::Busy;
        if (device_ == 0) return Status::NotInitialized;
        if (stream.format() != format_) return Status::BadArgument;
        const auto status = stream.claim(mm::audio::End::Consumer);
        if (status != Status::Ok) return status;
        SDL_ClearQueuedAudio(device_);
        submitted_ = 0;
        pending_count_ = 0;
        stream_ = &stream;
        started_ = true;
        SDL_PauseAudioDevice(device_, 0);
        return Status::Ok;
    }

    [[nodiscard]] Status service() override {
        if (!started_) return Status::NotInitialized;
        // One bounded offer per call. A failed queue operation leaves samples
        // in pending_ for the next call; the Stream is never read twice.
        const auto queued = SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
        if (queued >= queue_samples) return Status::Ok;
        if (pending_count_ == 0) {
            std::size_t count = 0;
            const auto limit = std::min(chunk_samples, queue_samples - queued);
            const auto status = stream_->read(
                std::span<std::int16_t>{pending_}.first(limit), count);
            if (status != Status::Ok) return status;
            if (count > limit) return Status::TransportError;
            pending_count_ = count;
        }
        if (pending_count_ == 0) return Status::Ok;
        if (SDL_QueueAudio(device_, pending_.data(),
                           static_cast<Uint32>(pending_count_ * sizeof(std::int16_t))) != 0)
            return Status::TransportError;
        submitted_ += pending_count_;
        pending_count_ = 0;
        return Status::Ok;
    }

    [[nodiscard]] Status position(std::uint64_t& samples) const override {
        if (!started_) return Status::NotInitialized;
        const auto queued = SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
        samples = submitted_ > queued ? submitted_ - queued : 0;
        return Status::Ok;
    }

    [[nodiscard]] Status pending(std::size_t& samples) const override {
        if (!started_) return Status::NotInitialized;
        samples = pending_count_ + SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
        return Status::Ok;
    }

    [[nodiscard]] Status stop() override {
        if (!started_) return Status::NotInitialized;
        SDL_PauseAudioDevice(device_, 1);
        SDL_ClearQueuedAudio(device_);
        stream_->release(mm::audio::End::Consumer);
        stream_ = nullptr;
        pending_count_ = 0;
        started_ = false;
        return Status::Ok;
    }

    [[nodiscard]] Status sleep() override {
        if (!initialized_) return Status::NotInitialized;
        close();
        return Status::Ok;
    }

private:
    void close() {
        if (started_) (void)stop();
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        device_ = 0;
        format_ = {};
        if (initialized_) runtime.release();
        initialized_ = false;
    }

    SDL_AudioDeviceID device_ = 0;
    mm::audio::Format format_{};
    mm::audio::Stream* stream_ = nullptr;
    std::array<std::int16_t, chunk_samples> pending_{};
    std::size_t pending_count_ = 0;
    std::uint64_t submitted_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

class Microphone final : public mm::audio::In {
public:
    ~Microphone() override { close(); }

    [[nodiscard]] mm::audio::Description description() const override {
        return {"Linux SDL2 capture", 8000, 192000, queue_samples};
    }

    [[nodiscard]] Status initialize() override {
        if (initialized_) return Status::Ok;
        if (!runtime.acquire()) return Status::TransportError;
        initialized_ = true;
        return Status::Ok;
    }

    [[nodiscard]] Status configure(const mm::audio::Format& requested,
                                   mm::audio::Format& actual) override {
        if (!initialized_) return Status::NotInitialized;
        if (started_) return Status::Busy;
        if (requested.rate_hz < 8000 || requested.rate_hz > 192000)
            return Status::BadArgument;
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        device_ = 0;
        format_ = {};
        device_ = open_device(true, requested.rate_hz, format_);
        if (device_ == 0) return Status::TransportError;
        actual = format_;
        return Status::Ok;
    }

    [[nodiscard]] Status rate(mm::audio::Rate& actual) const override {
        if (device_ == 0) return Status::NotInitialized;
        actual = {format_.rate_hz, 1};
        return Status::Ok;
    }

    [[nodiscard]] Status start(mm::audio::Stream& stream) override {
        if (started_) return Status::Busy;
        if (device_ == 0) return Status::NotInitialized;
        if (stream.format() != format_) return Status::BadArgument;
        const auto status = stream.claim(mm::audio::End::Producer);
        if (status != Status::Ok) return status;
        SDL_ClearQueuedAudio(device_);
        captured_ = 0;
        stream_ = &stream;
        started_ = true;
        SDL_PauseAudioDevice(device_, 0);
        return Status::Ok;
    }

    [[nodiscard]] Status service() override {
        if (!started_) return Status::NotInitialized;
        const auto queued = SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
        if (queued == 0) return Status::Ok;
        const auto room = stream_->writable();
        // When the application falls behind, discard a bounded batch rather
        // than let SDL's capture queue grow without limit.
        if (room == 0 && queued < queue_samples) return Status::Ok;
        const auto count = std::min(chunk_samples, queued);
        const auto bytes = SDL_DequeueAudio(device_, samples_.data(),
                                            static_cast<Uint32>(count * sizeof(std::int16_t)));
        if (bytes % sizeof(std::int16_t) != 0) return Status::TransportError;
        const auto received = bytes / sizeof(std::int16_t);
        captured_ += received;
        const auto offered = std::min(received, room);
        std::size_t written = 0;
        if (offered != 0) {
            const auto status = stream_->write(
                std::span<const std::int16_t>{samples_}.first(offered), written);
            if (status != Status::Ok) return status;
            if (written > offered) return Status::TransportError;
        }
        if (written < received) stream_->overran(received - written);
        return Status::Ok;
    }

    [[nodiscard]] Status position(std::uint64_t& samples) const override {
        if (!started_) return Status::NotInitialized;
        samples = captured_ + SDL_GetQueuedAudioSize(device_) / sizeof(std::int16_t);
        return Status::Ok;
    }

    [[nodiscard]] Status stop() override {
        if (!started_) return Status::NotInitialized;
        SDL_PauseAudioDevice(device_, 1);
        SDL_ClearQueuedAudio(device_);
        stream_->release(mm::audio::End::Producer);
        stream_ = nullptr;
        started_ = false;
        return Status::Ok;
    }

    [[nodiscard]] Status sleep() override {
        if (!initialized_) return Status::NotInitialized;
        close();
        return Status::Ok;
    }

private:
    void close() {
        if (started_) (void)stop();
        if (device_ != 0) SDL_CloseAudioDevice(device_);
        device_ = 0;
        format_ = {};
        if (initialized_) runtime.release();
        initialized_ = false;
    }

    SDL_AudioDeviceID device_ = 0;
    mm::audio::Format format_{};
    mm::audio::Stream* stream_ = nullptr;
    std::array<std::int16_t, chunk_samples> samples_{};
    std::uint64_t captured_ = 0;
    bool initialized_ = false;
    bool started_ = false;
};

Speaker speaker;
Microphone microphone;

struct Register {
    Register() {
        mm::audio::set_out(speaker);
        mm::audio::set_in(microphone);
    }
};

const Register registered;

}  // namespace platform::linux::audio_provider
