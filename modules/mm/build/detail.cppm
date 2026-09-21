// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module mm.build:detail;

import mm.configure;
import mm.mdy;
import :manifest;

// Helpers shared by more than one implementation unit. Exported from this partition alone, never re-exported by mm.build, so importers of mm.build do not see them.

export namespace mm::build {

std::optional<mm::configure::Responsibility> parse_responsibility(std::string_view value);

const std::vector<std::string>* lookup(const mm::mdy::MDYDocument& doc, std::string_view key);
std::string first(const mm::mdy::MDYDocument& doc, std::string_view key);
std::vector<std::string> all(const mm::mdy::MDYDocument& doc, std::string_view key);

bool structural_property_name(std::string_view name);
std::vector<std::string> structural_property_declarations(
    const std::vector<std::string>& declarations);

bool safe_exists(const std::filesystem::path& path);
bool is_safe_name(std::string_view name);
bool is_safe_relative_path(const std::filesystem::path& raw, const std::filesystem::path& joined);
bool path_within(const std::filesystem::path& base, const std::filesystem::path& path);
bool path_contained_in(const std::filesystem::path& container, const std::filesystem::path& path);
bool within_root(const std::filesystem::path& path);
std::filesystem::path absolute_from_root(const std::filesystem::path& root,
                                          const std::filesystem::path& path);
std::filesystem::path relative_to_root(const std::filesystem::path& root,
                                       const std::filesystem::path& path);

bool valid_mm_version(const mm::mdy::MDYDocument& doc, const std::filesystem::path& manifest,
                      const LoadPolicy& policy);
bool valid_manifest(const mm::mdy::MDYDocument& doc, std::string_view kind, std::string_view name,
                    const std::filesystem::path& manifest, const LoadPolicy& policy);

bool observe_checkout(const std::filesystem::path& path, bool& present,
                      const std::filesystem::path& manifest, std::string_view tool);

std::map<std::string, std::size_t, std::less<>> modules_by_module_name(const Project& project);
std::size_t index_of_module(const Tree& tree, const std::string& module_name);

}
