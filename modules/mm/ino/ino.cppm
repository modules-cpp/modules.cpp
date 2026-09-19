// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module mm.ino;

import mm.mdy;

export namespace mm::ino {

struct SourceFile {
    std::string path;
    std::string content;
};

struct Diagnostic {
    std::string file;
    std::size_t line = 0;
    std::string message;
    bool is_warning = false;
};

struct TransformResult {
    bool ok = true;
    std::string output;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] TransformResult transform(std::span<const SourceFile> sources);

// The compatibility header a sketch application receives when it declares
// sketch-library. A vendored library includes it unconditionally under the
// name its own toolchain provides; this is that header, written beside
// main.cpp and built on mm.sketch. Generated, committed, and verified the
// same way main.cpp is.
[[nodiscard]] std::string sketch_header();

[[nodiscard]] bool check_application(const std::filesystem::path& app_dir,
                                     const mm::mdy::MDYDocument& doc,
                                     std::string& error);

[[nodiscard]] bool write_guarded(const std::filesystem::path& app_dir,
                                 std::string_view target_filename,
                                 std::string_view content,
                                 std::string& error,
                                 std::string_view temp_filename = "");

}

