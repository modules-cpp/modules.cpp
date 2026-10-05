// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <system_error>

export module platform.linux.storage.device;

import mm.mcu;
import platform.linux.map;
import platform.linux.storage.store;
import platform.linux.storage.sdcard;
import platform.linux.storage.spiflash;
import platform.linux.storage.bus;

// A named, non-exported namespace rather than an unnamed one: Clang emits an
// interface unit's unnamed-namespace objects again in every importer, and a
// provider object must exist exactly once.
namespace platform::linux::storage_device_provider {

using platform::linux::storage::Bus;
using platform::linux::storage::FileStore;
using platform::linux::storage::SdCard;
using platform::linux::storage::SdKind;
using platform::linux::storage::SpiFlash;

void complain(const std::string& what) { std::cerr << "storage-linux: " << what << '\n'; }

// The chips and their files, made at the first mm.mcu call.
class Chips {
public:
    Bus& bus() {
        if (!bus_) open();
        return *bus_;
    }

private:
    void open() {
        platform::linux::Map defaults;
        const platform::linux::Map* map = &defaults;
        const auto& resolution = platform::linux::resolve();
        if (resolution.status == platform::linux::MapStatus::Ok && resolution.map != nullptr) {
            map = resolution.map;
        } else if (resolution.status != platform::linux::MapStatus::NoDefaults) {
            complain("the device map did not resolve (" + resolution.error.file + ": " +
                     resolution.error.key + ": " + resolution.error.reason +
                     "); no SD card, no flash");
            bus_.emplace(nullptr, nullptr);
            return;
        }
        open_card(map->sdcard);
        open_flash(map->spiflash);
        bus_.emplace(card_ ? &*card_ : nullptr, flash_ ? &*flash_ : nullptr);
    }

    void open_card(const platform::linux::SdCardEntry& entry) {
        const std::filesystem::path path = entry.path.empty() ? "sdcard.img" : entry.path;
        const auto kind = entry.kind == platform::linux::SdCardKind::Sdhc ? SdKind::Sdhc
                                                                           : SdKind::SdscVersion2;
        std::error_code error;
        if (!std::filesystem::exists(path, error) && !SdCard::fits(entry.size, kind)) {
            complain("sdcard.size " + std::to_string(entry.size) + " is not a size " +
                     (kind == SdKind::Sdhc ? "an SDHC card can describe (whole 512 KiB)"
                                           : "an SDSC card can describe (whole 256 KiB, "
                                             "at most 1 GiB)") +
                     "; " + path.string() + " not made; no SD card");
            return;
        }
        card_store_.emplace(path, entry.size, std::byte{0x00});
        if (!card_store_->ok()) {
            complain(path.string() + " cannot be opened; no SD card");
            return;
        }
        if (!SdCard::fits(card_store_->size(), kind)) {
            complain(path.string() + " is " + std::to_string(card_store_->size()) +
                     " bytes, which " +
                     (kind == SdKind::Sdhc ? "an SDHC card cannot describe (whole 512 KiB)"
                                           : "an SDSC card cannot describe (whole 256 KiB, "
                                             "at most 1 GiB)") +
                     "; no SD card");
            return;
        }
        card_.emplace(*card_store_, kind);
    }

    void open_flash(const platform::linux::SpiFlashEntry& entry) {
        const std::filesystem::path path = entry.path.empty() ? "spiflash.img" : entry.path;
        flash_store_.emplace(path, entry.size, std::byte{0xff});
        if (!flash_store_->ok()) {
            complain(path.string() + " cannot be opened; no flash");
            return;
        }
        if (!SpiFlash::fits(flash_store_->size())) {
            complain(path.string() + " is " + std::to_string(flash_store_->size()) +
                     " bytes, not a power of two from 64 KiB to 16 MiB; no flash");
            return;
        }
        flash_.emplace(*flash_store_);
    }

    std::optional<FileStore> card_store_;
    std::optional<FileStore> flash_store_;
    std::optional<SdCard> card_;
    std::optional<SpiFlash> flash_;
    std::optional<Bus> bus_;
};

class StoragePlatform final : public mm::mcu::Platform {
public:
    [[nodiscard]] mm::mcu::Capabilities capabilities() const override {
        return {.board = true, .gpio = true, .spi = true, .timer = true};
    }
    [[nodiscard]] mm::mcu::Board board() const override { return idle_.board(); }

    [[nodiscard]] mm::mcu::Status gpio_configure(unsigned int pin, mm::mcu::Direction direction,
                                                 mm::mcu::Pull pull) override {
        return chips_.bus().gpio_configure(pin, direction, pull);
    }
    [[nodiscard]] mm::mcu::Status gpio_write(unsigned int pin, bool high) override {
        return chips_.bus().gpio_write(pin, high);
    }
    [[nodiscard]] mm::mcu::Status gpio_read(unsigned int pin, bool& high) override {
        return chips_.bus().gpio_read(pin, high);
    }
    [[nodiscard]] mm::mcu::Status spi_configure(const mm::mcu::SpiConfiguration& value) override {
        return chips_.bus().spi_configure(value);
    }
    [[nodiscard]] mm::mcu::Status spi_write(unsigned int instance,
                                            std::span<const std::byte> data) override {
        return chips_.bus().spi_write(instance, data);
    }
    [[nodiscard]] mm::mcu::Status spi_transfer(unsigned int instance,
                                               std::span<const std::byte> out,
                                               std::span<std::byte> in) override {
        return chips_.bus().spi_transfer(instance, out, in);
    }
    [[nodiscard]] mm::mcu::Status delay_ms(unsigned long milliseconds) override {
        return idle_.delay_ms(milliseconds);
    }
    [[nodiscard]] mm::mcu::Status delay_us(unsigned long microseconds) override {
        return idle_.delay_us(microseconds);
    }
    [[nodiscard]] mm::mcu::Status ticks_ms(unsigned long& ticks) override {
        return idle_.ticks_ms(ticks);
    }
    [[nodiscard]] mm::mcu::Status ticks_us(unsigned long& ticks) override {
        return idle_.ticks_us(ticks);
    }

private:
    Chips chips_;
    // The board description and the clock, which need no files.
    Bus idle_{nullptr, nullptr};
};

StoragePlatform storage_platform;
struct Register {
    Register() { mm::mcu::set_platform(storage_platform); }
};
const Register registered;

}  // namespace platform::linux::storage_device_provider
