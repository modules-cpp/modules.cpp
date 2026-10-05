// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <system_error>
#include <vector>

export module platform.linux.storage.store;

export namespace platform::linux::storage {

// Bytes at offsets, as a chip's array: a read or write past the end fails.
class BlockStore {
public:
    virtual ~BlockStore() = default;
    [[nodiscard]] virtual std::uint64_t size() const = 0;
    [[nodiscard]] virtual bool read(std::uint64_t offset, std::span<std::byte> data) = 0;
    [[nodiscard]] virtual bool write(std::uint64_t offset, std::span<const std::byte> data) = 0;
};

class MemoryStore final : public BlockStore {
public:
    MemoryStore(std::uint64_t size, std::byte fill)
        : bytes_(static_cast<std::size_t>(size), fill) {}

    [[nodiscard]] std::uint64_t size() const override { return bytes_.size(); }
    [[nodiscard]] bool read(std::uint64_t offset, std::span<std::byte> data) override {
        if (offset > bytes_.size() || data.size() > bytes_.size() - offset) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), data.size(), data.begin());
        return true;
    }
    [[nodiscard]] bool write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (offset > bytes_.size() || data.size() > bytes_.size() - offset) return false;
        std::copy(data.begin(), data.end(), bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
    }
    [[nodiscard]] std::vector<std::byte>& bytes() { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

class FileStore final : public BlockStore {
public:
    // Opens path read-write. A missing file is made at size bytes of fill;
    // an existing one keeps its contents and its own size. ok() says whether
    // either worked.
    FileStore(const std::filesystem::path& path, std::uint64_t size, std::byte fill) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            std::filebuf maker;
            if (maker.open(path, std::ios_base::out | std::ios_base::binary) == nullptr) return;
            const std::vector<char> chunk(65536, static_cast<char>(fill));
            for (std::uint64_t at = 0; at < size; at += chunk.size()) {
                const auto count = static_cast<std::streamsize>(
                    std::min<std::uint64_t>(chunk.size(), size - at));
                if (maker.sputn(chunk.data(), count) != count) return;
            }
            if (maker.close() == nullptr) return;
        }
        const auto existing = std::filesystem::file_size(path, error);
        if (error) return;
        if (buffer_.open(path, std::ios_base::in | std::ios_base::out | std::ios_base::binary) ==
            nullptr)
            return;
        size_ = existing;
        ok_ = true;
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::uint64_t size() const override { return size_; }

    [[nodiscard]] bool read(std::uint64_t offset, std::span<std::byte> data) override {
        if (!ok_ || offset > size_ || data.size() > size_ - offset) return false;
        if (buffer_.pubseekpos(static_cast<std::streamoff>(offset)) == std::streampos(-1))
            return false;
        return buffer_.sgetn(reinterpret_cast<char*>(data.data()),
                             static_cast<std::streamsize>(data.size())) ==
               static_cast<std::streamsize>(data.size());
    }

    [[nodiscard]] bool write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (!ok_ || offset > size_ || data.size() > size_ - offset) return false;
        if (buffer_.pubseekpos(static_cast<std::streamoff>(offset)) == std::streampos(-1))
            return false;
        if (buffer_.sputn(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size())) !=
            static_cast<std::streamsize>(data.size()))
            return false;
        return buffer_.pubsync() == 0;
    }

private:
    std::filebuf buffer_;
    std::uint64_t size_ = 0;
    bool ok_ = false;
};

}  // namespace platform::linux::storage
