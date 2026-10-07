// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
module mm.build;
import mm.mdy;
import :config;
import :manifest;

namespace mm::build {
namespace {
std::string content(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string value(const mm::mdy::MDYDocument& doc, std::string_view key) {
    const auto it = doc.metadata.find(key);
    return it == doc.metadata.end() || it->second.empty() ? std::string{} : it->second.front();
}
std::filesystem::path canonical_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::canonical(path, ec);
    return ec ? std::filesystem::path{} : result;
}
bool fail(std::string_view message) {
    std::cerr << "external: " << message << "\n";
    return false;
}
bool lane_name(std::string_view name) {
    return name == "out-host" || (name.starts_with("out-target-") &&
        name.size() > 11 && std::all_of(name.begin() + 11, name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.';
        }));
}
bool safe_tree(const std::filesystem::path& directory) {
    std::error_code ec;
    if (std::filesystem::is_symlink(directory, ec)) return fail("refusing symlink output directory");
    if (ec == std::errc::no_such_file_or_directory) ec.clear();
    if (ec) return false;
    if (!std::filesystem::exists(directory, ec)) return !ec;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) return fail("refusing cleanup through symlink artifacts");
    }
    return !ec;
}
bool ownership(const std::filesystem::path& root, std::vector<std::string>& lanes) {
    const auto out = root / "out";
    if (!safe_tree(out)) return false;
    std::ifstream input(out / ".external-ownership");
    std::string line;
    if (!std::getline(input, line) || line != root.string()) return fail("missing or invalid output ownership record; rerun configure");
    while (std::getline(input, line)) {
        if (!lane_name(line) || std::find(lanes.begin(), lanes.end(), line) != lanes.end())
            return fail("invalid output ownership record");
        lanes.push_back(line);
    }
    return input.eof();
}
class PublicationStage {
    std::filesystem::path directory_;
public:
    explicit PublicationStage(const std::filesystem::path& directory) : directory_(directory) {}
    PublicationStage(const PublicationStage&) = delete;
    PublicationStage& operator=(const PublicationStage&) = delete;
    PublicationStage(PublicationStage&&) = delete;
    PublicationStage& operator=(PublicationStage&&) = delete;
    ~PublicationStage() { std::error_code ec; std::filesystem::remove_all(directory_, ec); }
};

bool publish(const std::filesystem::path& path, std::string_view text) {
    // Unique staging directories avoid races and never truncate the published record.
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    static unsigned counter = 0;
    std::filesystem::path stage;
    for (unsigned attempts = 0; attempts < 100; ++attempts) {
        stage = path.parent_path() / (".external-stage-" + std::to_string(std::rand()) + "-" + std::to_string(++counter));
        if (std::filesystem::create_directory(stage, ec)) break;
        if (attempts == 99 || ec) return false;
    }
    const PublicationStage cleanup(stage);
    const auto temporary = stage / "record";
    { std::ofstream output(temporary); output << text; output.flush(); output.close(); if (!output) return false; }
    std::filesystem::rename(temporary, path, ec);
    const bool ok = !ec;
    return ok;
}
std::string quote(std::string_view value) {
    std::string out = "'";
    for (char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
}
std::string driver_version(std::string_view invocation) {
    const auto command = "command -v " + quote(invocation) + "; " + quote(invocation) + " --version 2>/dev/null";
    std::string result;
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(::popen(command.c_str(), "r"), ::pclose);
    if (pipe) {
        char buffer[1024];
        while (std::fgets(buffer, sizeof(buffer), pipe.get())) result += buffer;
        if (pipe.get_deleter()(pipe.release()) != 0) result.clear();
    }
    return result;
}
}

std::filesystem::path discover_installation(std::string_view executable) {
    std::filesystem::path binary;
    std::error_code ec;
    binary = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) binary.clear();
    // Hosts without /proc use the invoked executable, resolving a PATH name
    // or symlink itself. Never search cwd/ancestors for an installation.
    if (binary.empty() && !executable.empty()) {
        const std::filesystem::path invoked(executable);
        if (invoked.has_parent_path()) binary = invoked;
        else if (const auto* path = std::getenv("PATH")) {
            std::istringstream directories(path);
            std::string directory;
            while (std::getline(directories, directory, ':')) {
                const auto candidate = (directory.empty() ? std::filesystem::path(".") : std::filesystem::path(directory)) / invoked;
                const auto status = std::filesystem::status(candidate, ec);
                const auto executable_bits = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;
                if (!ec && std::filesystem::is_regular_file(status) && (status.permissions() & executable_bits) != std::filesystem::perms::none) {
                    binary = candidate; break;
                }
                ec.clear();
            }
        }
    }
    binary = canonical_path(binary);
    if (binary.parent_path().filename() != "bin" || binary.parent_path().parent_path().filename() != "out") {
        fail("configure must run from an installed out/bin tool");
        return {};
    }
    const auto root = binary.parent_path().parent_path().parent_path();
    const auto doc = mm::mdy::Parser::parse_file(root / "mm.mdy");
    if (value(doc, "kind") != "project") { fail("running configure has no installation manifest"); return {}; }
    return root;
}

std::filesystem::path connected_external_root(const std::filesystem::path& directory) {
    auto root = canonical_path(directory);
    while (!root.empty() && root.parent_path() != root) {
        const auto parent = root.parent_path();
        const auto doc = mm::mdy::Parser::parse_file(parent / "mm.mdy");
        if (value(doc, "kind") != "dir") break;
        const auto folders = doc.metadata.find("folder");
        bool connected = false;
        if (folders != doc.metadata.end()) for (const auto& folder : folders->second)
            if (canonical_path(parent / folder) == root) connected = true;
        if (!connected) break;
        root = parent;
    }
    return root;
}

bool register_external_outputs(const ResolvedRoots& roots, const std::vector<std::filesystem::path>& directories) {
    if (!roots.managed_external) return true;
    const auto root = *roots.external_root;
    if (root.string().find_first_of("\r\n") != std::string::npos) return fail("unsafe external root identity");
    const auto out = root / "out";
    if (!safe_tree(out)) return false;
    std::vector<std::string> lanes;
    std::error_code ec;
    if (std::filesystem::exists(out / ".external-ownership", ec)) {
        if (!ownership(root, lanes)) return false;
    } else if (std::filesystem::exists(out, ec) && !std::filesystem::is_empty(out, ec)) {
        return fail("out/ has unowned contents; choose an empty output directory before configure");
    }
    for (const auto& directory : directories) {
        const auto name = directory.generic_string();
        if (!lane_name(name) || !safe_tree(root / name)) return fail("unsafe external output directory");
        if (std::find(lanes.begin(), lanes.end(), name) == lanes.end()) {
            if (std::filesystem::exists(root / name, ec) && !std::filesystem::is_empty(root / name, ec))
                return fail("refusing to claim an existing nonempty output directory");
            lanes.push_back(name);
        }
    }
    std::string record = root.string() + "\n";
    for (const auto& lane : lanes) record += lane + "\n";
    return publish(out / ".external-ownership", record);
}

std::string external_build_identity(const ResolvedRoots& roots, const BuildConfiguration& configuration,
                                    bool target, const Project& project) {
    if (!roots.managed_external) return {};
    std::uint64_t digest = 14695981039346656037ULL;
    auto add = [&](std::string_view text) { for (unsigned char c : text) { digest ^= c; digest *= 1099511628211ULL; } digest ^= 255; digest *= 1099511628211ULL; };
    add("external-app-1"); add(roots.project_root.string()); add(roots.external_root->string());
    const auto* toolchain = configuration.toolchain_for(target);
    if (!toolchain) return {};
    add(toolchain->compiler.invocation); add(toolchain->compiler.arguments);
    add(toolchain->linker.invocation); add(toolchain->linker.arguments);
    add(toolchain->c_compiler.invocation); add(toolchain->c_compiler.arguments);
    add(driver_version(toolchain->compiler.invocation));
    if (!toolchain->c_compiler.invocation.empty()) add(driver_version(toolchain->c_compiler.invocation));
    const auto doc = mm::mdy::Parser::parse_file(roots.configuration_root / "out/config.mdy");
    for (const auto& [key, values] : doc.metadata) {
        if (key.starts_with(target ? "cross-" : "host-") || (target && key.starts_with("target-"))) {
            add(key); for (const auto& v : values) add(v);
        }
    }
    for (const auto& node : project.nodes) { add(node.dir.string()); add(content(node.dir / "mm.mdy")); }
    // Include private adapters, headers, PIO and linker inputs beside declared
    // sources; these may be compiled by an SDK bridge rather than file: units.
    std::set<std::filesystem::path> directories, inputs;
    for (const auto& node : project.nodes) directories.insert(node.source_dir);
    for (const auto& node : project.targets) for (const auto& source : node.sources) directories.insert(source.source.parent_path());
    for (const auto& directory : directories) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const auto extension = it->path().extension().string();
            if (extension == ".h" || extension == ".hpp" || extension == ".inc" || extension == ".c" ||
                extension == ".cpp" || extension == ".cppm" || extension == ".pio" || extension == ".ld" ||
                extension == ".S" || extension == ".s" || extension == ".cmake" || it->path().filename() == "CMakeLists.txt") inputs.insert(it->path());
        }
    }
    for (const auto& library : project.libraries) {
        if (library.external_build.empty()) continue;
        const auto bridge = roots.project_root / library.manifest.parent_path() / "cmake";
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(bridge, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec)) inputs.insert(it->path());
        add(content(roots.project_root / library.source / ".git/HEAD"));
        add(content(roots.project_root / library.source / "pico_sdk_version.cmake"));
    }
    for (const auto& input : inputs) { add(input.string()); add(content(input)); }
    for (const auto& node : project.targets) {
        for (const auto& source : node.sources) { add(source.source.string()); add(content(source.source)); }
        for (const auto& sketch : node.sketches) { add(sketch); add(content(node.source_dir / sketch)); }
        if (!node.sketches.empty()) add(content(node.source_dir / "Sketch.h"));
        for (const auto& library : node.sketch_libraries) {
            std::error_code ec;
            std::vector<std::filesystem::path> inputs;
            for (std::filesystem::recursive_directory_iterator it(library, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const auto extension = it->path().extension().string();
                if (extension == ".h" || extension == ".hpp" || extension == ".c" || extension == ".cpp" || extension == ".ino") inputs.push_back(it->path());
            }
            std::sort(inputs.begin(), inputs.end());
            for (const auto& input : inputs) { add(input.string()); add(content(input)); }
        }
    }
    for (const char* variable : {"PICO_SDK_PATH", "PICO_TOOLCHAIN_PATH", "PICO_RISCV_TOOLCHAIN_PATH"}) {
        if (const auto* v = std::getenv(variable)) add(v);
    }
    std::ostringstream output; output << std::hex << digest;
    return output.str();
}

bool prepare_external_build(const ResolvedRoots& roots, const std::filesystem::path& output, std::string_view identity) {
    if (!roots.managed_external) return true;
    std::vector<std::string> lanes;
    if (!ownership(*roots.external_root, lanes)) return false;
    const auto name = output.filename().string();
    if (output.parent_path() != *roots.external_root || std::find(lanes.begin(), lanes.end(), name) == lanes.end() || !safe_tree(output))
        return fail("build output is not owned by this external project");
    std::error_code ec;
    if (content(output / ".build-identity") != identity) {
        std::filesystem::remove_all(output, ec);
        if (ec) return false;
    }
    return publish(output / ".build-identity", identity);
}
bool finish_external_build(const ResolvedRoots& roots, const std::filesystem::path& output, std::string_view identity) {
    if (!roots.managed_external) return true;
    std::vector<std::string> lanes;
    if (!ownership(*roots.external_root, lanes) || output.parent_path() != *roots.external_root ||
        std::find(lanes.begin(), lanes.end(), output.filename().string()) == lanes.end() || !safe_tree(output)) return false;
    return publish(output / ".build-identity", identity);
}
namespace {
std::string artifact_digest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "missing";
    std::uint64_t hash = 14695981039346656037ULL;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof(buffer));
        for (std::streamsize i = 0; i < input.gcount(); ++i) { hash ^= static_cast<unsigned char>(buffer[i]); hash *= 1099511628211ULL; }
    }
    std::ostringstream out; out << std::hex << hash;
    return out.str();
}
std::string artifact_record(const std::filesystem::path& executable, std::string_view identity) {
    auto uf2 = executable; uf2 += ".uf2";
    return std::string(identity) + "\n" + artifact_digest(executable) + "\n" + artifact_digest(uf2) + "\n";
}
}
bool record_external_artifact(const ResolvedRoots& roots, const std::filesystem::path& executable, std::string_view identity) {
    return !roots.managed_external || publish(executable.string() + ".build-state", artifact_record(executable, identity));
}
bool check_external_artifact(const ResolvedRoots& roots, const BuildConfiguration& configuration, bool target,
                              const Project& project, const std::filesystem::path& executable) {
    if (!roots.managed_external) return true;
    if (content(executable.string() + ".build-state") != artifact_record(executable, external_build_identity(roots, configuration, target, project)))
        return fail("application is missing or stale; rebuild it before run, flash, or debug");
    return true;
}

int clean_external(const std::filesystem::path& manifest, bool host, bool target, bool reset) {
    const auto file = std::filesystem::absolute(resolve_manifest(manifest));
    auto root = connected_external_root(file.parent_path());
    if (root.empty() || !std::filesystem::is_regular_file(file)) {
        fail("requested mm.mdy is missing; clean does not guess an application"); return exit_manifest;
    }
    const auto doc = mm::mdy::Parser::parse_file(file);
    if (value(doc, "kind") != "app" && value(doc, "kind") != "dir") return exit_manifest;
    const auto root_doc = mm::mdy::Parser::parse_file(root / "mm.mdy");
    if (!value(doc, "project").empty() || !value(root_doc, "project").empty()) {
        fail("ambiguous project: locator and external cleanup configuration"); return exit_manifest;
    }
    const auto cfg = mm::mdy::Parser::parse_file(root / "out/config.mdy");
    if (cfg.status != mm::mdy::ParseStatus::Ok || value(cfg, "mm") != "3.0" || value(cfg, "kind") != "configuration") return exit_manifest;
    if (value(cfg, "schema") != "external-configuration-1" || value(cfg, "tool-contract") != "external-app-1" ||
        value(cfg, "external-root") != root.string() || value(cfg, "modules-root").empty()) {
        fail("clean requires a local external configuration"); return exit_manifest;
    }
    if (!validate_external_cleanup_configuration(root / "out/config.mdy")) return exit_manifest;
    if (reset && (host || target)) { fail("--distclean cannot take lane selectors"); return exit_usage; }
    std::vector<std::string> lanes;
    if (!ownership(root, lanes)) return exit_manifest;
    std::vector<std::filesystem::path> remove;
    for (const auto& lane : lanes) {
        if ((!host && !target) || (host && lane == "out-host") || (target && lane.starts_with("out-target-"))) {
            if (!safe_tree(root / lane)) return exit_manifest;
            remove.push_back(root / lane);
        }
    }
    if (reset) remove.push_back(root / "out");
    std::cout << (reset ? "Reset" : "Clean") << " external tree " << root.string() << " (all applications in selected lanes)\n";
    std::error_code ec;
    for (const auto& path : remove) {
        std::filesystem::remove_all(path, ec);
        if (ec) { fail("cannot remove owned build artifacts"); return exit_manifest; }
        std::cout << "Clean " << path.string() << "\n";
    }
    return exit_ok;
}

bool external_source_is_ordinary(const std::filesystem::path& source) {
    const auto input = content(source);
    std::string text;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            if (input[i + 1] == '\n') { ++i; continue; }
            if (input[i + 1] == '\r' && i + 2 < input.size() && input[i + 2] == '\n') { i += 2; continue; }
        }
        text += input[i];
    }
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "//") == 0) { i = text.find('\n', i); if (i == std::string::npos) break; continue; }
        if (text.compare(i, 2, "/*") == 0) { const auto end = text.find("*/", i + 2); if (end == std::string::npos) break; i = end + 2; continue; }
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '\'' || text[i] == '.')) ++i;
            continue;
        }
        std::size_t raw = std::string::npos;
        for (std::string_view prefix : {"R\"", "u8R\"", "uR\"", "UR\"", "LR\""})
            if (text.compare(i, prefix.size(), prefix) == 0) { raw = i + prefix.size() - 2; break; }
        if (raw != std::string::npos) {
            const auto opening = text.find('(', raw + 2);
            if (opening != std::string::npos && opening - raw <= 18) {
                const auto end = text.find(")" + text.substr(raw + 2, opening - raw - 2) + "\"", opening + 1);
                if (end == std::string::npos) break;
                i = end + (opening - raw - 2) + 2;
                continue;
            }
        }
        if (text[i] == '"' || text[i] == '\'') {
            const auto delimiter = text[i++];
            while (i < text.size()) { const auto c = text[i++]; if (c == '\\' && i < text.size()) ++i; else if (c == delimiter) break; }
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            const auto start = i++;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
            tokens.push_back(text.substr(start, i - start));
        } else if (!std::isspace(static_cast<unsigned char>(text[i]))) tokens.push_back(text.substr(i++, 1));
        else ++i;
    }
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) if (tokens[i] == "module") {
        const auto& next = tokens[i + 1];
        if (!next.empty() && (std::isalpha(static_cast<unsigned char>(next[0])) || next[0] == '_' || next == ":")) return false;
    }
    return true;
}
}
