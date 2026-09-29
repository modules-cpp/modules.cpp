// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module mm.ino;

namespace mm::ino {

namespace {

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1));
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool is_plain_file(const std::filesystem::path& file) {
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec) && !ec &&
           !std::filesystem::is_symlink(file, ec);
}

bool is_plain_directory(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::is_directory(dir, ec) && !ec &&
           !std::filesystem::is_symlink(dir, ec);
}

std::filesystem::path canonical_or_empty(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::weakly_canonical(path, ec);
    if (ec) return {};
    return result;
}

// The names in a depends= value: comma separated, each without the version
// constraint the library manager allows after it, as in "Name (>=1.2)".
std::vector<std::string> depends_names(std::string_view value) {
    std::vector<std::string> names;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        auto item = value.substr(start, comma == std::string_view::npos
                                            ? std::string_view::npos
                                            : comma - start);
        if (const auto paren = item.find('('); paren != std::string_view::npos)
            item = item.substr(0, paren);
        auto name = trimmed(item);
        if (!name.empty()) names.push_back(std::move(name));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return names;
}

// The header an include directive names, or empty for any other line.
std::string included_header(std::string_view line) {
    auto text = std::string_view(line);
    const auto hash = text.find_first_not_of(" \t");
    if (hash == std::string_view::npos || text[hash] != '#') return {};
    text.remove_prefix(hash + 1);
    const auto word = text.find_first_not_of(" \t");
    if (word == std::string_view::npos || !text.substr(word).starts_with("include"))
        return {};
    text.remove_prefix(word + 7);
    const auto open = text.find_first_not_of(" \t");
    if (open == std::string_view::npos) return {};
    const char close = text[open] == '<' ? '>' : text[open] == '"' ? '"' : '\0';
    if (close == '\0') return {};
    const auto end = text.find(close, open + 1);
    if (end == std::string_view::npos) return {};
    return std::string(text.substr(open + 1, end - open - 1));
}

bool provides(const std::filesystem::path& include_dir, const std::string& header) {
    return !header.empty() && is_plain_file(include_dir / header);
}

bool names_library(const SketchLibraryEntry& entry, std::string_view wanted) {
    const auto want = lowered(wanted);
    if (lowered(entry.name) == want) return true;
    auto spaced = want;
    std::replace(spaced.begin(), spaced.end(), ' ', '_');
    const auto dir = lowered(entry.root.filename().string());
    if (dir == spaced) return true;
    std::replace(spaced.begin(), spaced.end(), '_', '-');
    return dir == spaced;
}

} // namespace

std::vector<std::filesystem::path> sketch_library_search_path(
    std::string_view value, const std::filesystem::path& library_root) {
    std::vector<std::filesystem::path> folders;
    std::set<std::filesystem::path> seen;
    const auto add = [&](const std::filesystem::path& folder) {
        const auto canonical = canonical_or_empty(folder);
        if (canonical.empty() || !is_plain_directory(canonical)) return;
        if (seen.insert(canonical).second) folders.push_back(canonical);
    };
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto colon = value.find(':', start);
        const auto item = trimmed(value.substr(
            start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
        if (!item.empty()) add(item);
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    const auto root = canonical_or_empty(library_root);
    if (!root.empty()) add(root.parent_path());
    return folders;
}

std::vector<std::filesystem::path> sketch_library_search_path(
    const std::filesystem::path& library_root) {
    const std::string variable(sketch_libraries_path_variable);
    const char* value = std::getenv(variable.c_str());
    return sketch_library_search_path(value == nullptr ? "" : value, library_root);
}

bool read_sketch_library(const std::filesystem::path& dir, SketchLibraryEntry& entry) {
    const auto properties = dir / "library.properties";
    const bool has_properties = is_plain_file(properties);
    if (!has_properties && !is_plain_file(dir / "library.json")) return false;
    entry = {};
    entry.root = canonical_or_empty(dir);
    if (entry.root.empty()) return false;
    entry.name = entry.root.filename().string();
    entry.include_dir =
        is_plain_directory(entry.root / "src") ? entry.root / "src" : entry.root;
    if (has_properties) {
        std::ifstream in(properties);
        std::string line;
        while (std::getline(in, line)) {
            const auto text = trimmed(line);
            if (text.starts_with("name=")) {
                auto name = trimmed(std::string_view(text).substr(5));
                if (!name.empty()) entry.name = std::move(name);
            } else if (text.starts_with("depends=")) {
                entry.depends = depends_names(std::string_view(text).substr(8));
            }
        }
    }
    return true;
}

std::vector<SketchLibraryEntry> index_sketch_libraries(
    std::span<const std::filesystem::path> search_path) {
    std::vector<SketchLibraryEntry> index;
    std::set<std::filesystem::path> seen;
    const auto add = [&](const std::filesystem::path& dir) {
        SketchLibraryEntry entry;
        if (read_sketch_library(dir, entry) && seen.insert(entry.root).second)
            index.push_back(std::move(entry));
    };
    for (const auto& folder : search_path) {
        SketchLibraryEntry itself;
        if (read_sketch_library(folder, itself)) {
            add(folder);
            continue;
        }
        std::vector<std::filesystem::path> children;
        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(folder, ec)) {
            const auto name = item.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            if (is_plain_directory(item.path())) children.push_back(item.path());
        }
        // Directory order is not defined; the search order is.
        std::sort(children.begin(), children.end());
        for (const auto& child : children) add(child);
    }
    return index;
}

SiblingResolution resolve_sibling_libraries(
    const std::filesystem::path& app_dir, const SketchLibraryEntry& library,
    std::span<const SketchLibraryEntry> index) {
    SiblingResolution result;
    std::vector<const SketchLibraryEntry*> chosen;
    std::set<std::filesystem::path> chosen_roots{library.root};
    std::vector<std::filesystem::path> include_dirs{library.include_dir};
    std::set<std::string> reported;

    const auto choose = [&](const SketchLibraryEntry& entry) {
        if (!chosen_roots.insert(entry.root).second) return;
        chosen.push_back(&entry);
        include_dirs.push_back(entry.include_dir);
        result.libraries.push_back(entry.root);
    };

    // The example's own files, generated ones excepted: their includes are
    // the sketch's, already scanned in its .ino, or the compatibility
    // headers'.
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator(app_dir, ec)) {
        const auto ext = item.path().extension().string();
        if (ext != ".ino" && ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".c")
            continue;
        if (!is_plain_file(item.path())) continue;
        std::ifstream in(item.path());
        std::string first;
        std::getline(in, first);
        if (first.rfind("// Generated by sketch", 0) == 0) continue;
        files.push_back(item.path());
    }
    std::sort(files.begin(), files.end());

    const auto forwarders = sketch_alias_headers(true);
    for (const auto& file : files) {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line)) {
            const auto header = included_header(line);
            if (header.empty() || header == sketch_header_name()) continue;
            if (std::find(forwarders.begin(), forwarders.end(), header) != forwarders.end())
                continue;
            if (provides(app_dir, header)) continue;
            if (std::any_of(include_dirs.begin(), include_dirs.end(),
                            [&](const auto& dir) { return provides(dir, header); }))
                continue;
            const SketchLibraryEntry* found = nullptr;
            const auto stem = lowered(std::filesystem::path(header).stem().string());
            for (const auto& entry : index) {
                if (chosen_roots.contains(entry.root) || !provides(entry.include_dir, header))
                    continue;
                if (lowered(entry.root.filename().string()) == stem ||
                    lowered(entry.name) == stem) {
                    found = &entry;
                    break;
                }
                if (found == nullptr) found = &entry;
            }
            if (found != nullptr) choose(*found);
        }
    }

    // depends=, the exercised library's first, then each chosen library's,
    // until nothing new is found.
    std::vector<const SketchLibraryEntry*> pending{&library};
    pending.insert(pending.end(), chosen.begin(), chosen.end());
    for (std::size_t next = 0; next < pending.size(); ++next) {
        for (const auto& name : pending[next]->depends) {
            const auto it = std::find_if(index.begin(), index.end(),
                                         [&](const auto& entry) { return names_library(entry, name); });
            if (it == index.end()) {
                if (reported.insert(name).second) result.unresolved.push_back(name);
                continue;
            }
            if (chosen_roots.contains(it->root)) continue;
            choose(*it);
            pending.push_back(&*it);
        }
    }
    return result;
}

std::string with_sketch_libraries(std::string_view manifest,
                                  std::span<const std::string> entries) {
    if (!manifest.starts_with("---\n")) return {};
    const auto close = manifest.find("\n---", 3);
    if (close == std::string_view::npos) return {};
    const auto front = manifest.substr(0, close + 1);
    const std::string key = "sketch-library: ";
    const auto last = front.rfind("\n" + key);
    if (last == std::string_view::npos) return {};
    const auto line_end = front.find('\n', last + 1);
    std::string added;
    for (const auto& entry : entries) {
        const auto line = key + entry + "\n";
        if (front.find("\n" + line) != std::string_view::npos) continue;
        if (added.find(line) != std::string::npos) continue;
        added += line;
    }
    std::string out(manifest.substr(0, line_end + 1));
    out += added;
    out += manifest.substr(line_end + 1);
    return out;
}

} // namespace mm::ino
