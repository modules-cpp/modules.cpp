// Tests for output-lane-owned compiled-module caches.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

import mm.build;
import mm.test;

namespace {

class current_directory_guard {
public:
    current_directory_guard() : original_(std::filesystem::current_path()) {}
    ~current_directory_guard() {
        std::error_code ec;
        std::filesystem::current_path(original_, ec);
    }

    current_directory_guard(const current_directory_guard&) = delete;
    current_directory_guard& operator=(const current_directory_guard&) = delete;

private:
    std::filesystem::path original_;
};

void gcc_cache_is_private_and_maps_every_named_unit() {
    const mm::test::scoped_tree scratch{"build_module_cache"};
    current_directory_guard restore;
    std::filesystem::current_path(scratch.root());

    mm::build::Tree tree;
    mm::build::BuildableNode alpha;
    alpha.kind = "module";
    alpha.module_name = "mm.alpha";
    alpha.sources.push_back({"alpha.cppm", {}});
    tree.targets.push_back(alpha);

    mm::build::BuildableNode beta;
    beta.kind = "module";
    beta.module_name = "mm.beta";
    beta.sources.push_back({"part.cppm", "mm.beta:part"});
    beta.sources.push_back({"beta.cppm", {}});
    tree.targets.push_back(beta);

    mm::build::Toolchain toolchain;
    toolchain.family = mm::build::CompilerFamily::Gcc;

    mm::test::expect(mm::build::prepare_module_cache(toolchain, tree, "lane"),
                     "GCC cache preparation succeeds");

    const auto cache = scratch.root() / "lane/bmi";
    mm::test::expect(std::filesystem::is_directory(cache),
                     "cache is created beneath the output lane");
    mm::test::expect(!std::filesystem::exists(scratch.root() / "gcm.cache"),
                     "preparation does not create a shared root cache");

    std::ifstream input(cache / "gcc.mapper");
    const std::string mapper((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    mm::test::expect(
        mapper == "$root lane/bmi\n"
                  "mm.alpha mm.alpha.gcm\n"
                  "mm.beta mm.beta.gcm\n"
                  "mm.beta:part mm.beta-part.gcm\n",
        "mapper names primary modules and partitions deterministically");

    std::ofstream(cache / "stale.gcm") << "stale";
    mm::test::expect(mm::build::prepare_module_cache(toolchain, tree, "lane"),
                     "a second preparation succeeds");
    mm::test::expect(!std::filesystem::exists(cache / "stale.gcm"),
                     "a second preparation removes stale lane state");
}

const mm::test::case_ cases[] = {
    {"GCC cache is private and maps named units",
     &gcc_cache_is_private_and_maps_every_named_unit},
};

const mm::test::registrar reg{"mm.build module cache", cases};

}
