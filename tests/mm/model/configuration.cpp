// Black box tests for mm.model::configuration(), in both cases it must
// describe: a tree with no out/config.mdy, and one configure has written.

#include <filesystem>
#include <string_view>

import mm.configure;
import mm.model;
import mm.test;
import models.configuration;

namespace {

using mm::test::expect;

mm::configure::Settings host_settings() {
    mm::configure::Settings settings;
    settings.name = "gcc-release";
    settings.build = mm::configure::Build::Release;
    settings.host = {
        mm::configure::CompilerFamily::Gcc, "g++-15", "host", "POSIX",
        std::string(mm::configure::build_compile_flags(mm::configure::Build::Release)),
        std::string(mm::configure::build_link_flags(mm::configure::Build::Release)),
    };
    return settings;
}

void unconfigured_reports_the_shared_default() {
    const mm::test::scoped_tree tree{"model_configuration_default"};
    const auto quiet = mm::model::configuration(tree.root(), false);
    const auto loud = mm::model::configuration(tree.root(), true);
    expect(quiet != nullptr && loud != nullptr, "a tree with no configuration still resolves");
    if (quiet == nullptr || loud == nullptr) return;

    expect(!quiet->persisted(), "an absent out/config.mdy reports the default, not a configuration");
    expect(quiet->name() == "default", "the unconfigured lane is named default");
    expect(quiet->build() == models::Build::Debug, "the unconfigured build is debug");
    expect(quiet->compiler_family() == models::CompilerFamily::Gcc, "the unconfigured family is GCC");
    expect(quiet->compiler() == "g++", "the unconfigured C++ driver is g++");
    expect(quiet->selection() == models::CompilerSelection::Host, "the default selects the host");
    expect(quiet->build_directory() == "out" && quiet->host_build_directory() == "out",
           "an unconfigured build writes to out, where bootstrap already writes");
    expect(!quiet->verbose() && loud->verbose(), "verbose is reported as given");
}

void persisted_reports_the_written_lane() {
    const mm::test::scoped_tree tree{"model_configuration_persisted"};
    expect(mm::configure::write_configuration(tree.root(), host_settings()), "configuration written");

    const auto configuration = mm::model::configuration(tree.root(), false);
    expect(configuration != nullptr, "a written configuration resolves");
    if (configuration == nullptr) return;

    expect(configuration->persisted(), "a written out/config.mdy is reported as persisted");
    expect(configuration->name() == "gcc-release", "the persisted name is reported");
    expect(configuration->build() == models::Build::Release, "the persisted build is reported");
    expect(configuration->compiler() == "g++-15", "the persisted driver is reported");
    expect(configuration->selection() == models::CompilerSelection::Host,
           "a native configuration selects the host compiler");
    expect(configuration->build_directory() == mm::configure::host_output_directory() &&
               configuration->host_build_directory() == mm::configure::host_output_directory(),
           "a configured build writes to the host lane, not to out");
    expect(configuration->compiler_flags() ==
               mm::configure::build_compile_flags(mm::configure::Build::Release),
           "flags come from the one shared build policy");
}

// The platform/locale/shell facts are fixed project policy, not derived
// from the environment.
void platform_locale_and_shell_are_fixed() {
    const mm::test::scoped_tree tree{"model_configuration_fixed"};
    const auto first = mm::model::configuration(tree.root(), false);
    const auto second = mm::model::configuration(tree.root(), true);
    expect(first != nullptr && second != nullptr, "both resolve");
    if (first == nullptr || second == nullptr) return;

    expect(first->platform() == "POSIX", "expected platform() to be POSIX");
    expect(first->locale() == "C", "expected locale() to be C");
    expect(first->shell() == "/bin/sh", "expected shell() to be /bin/sh");

    expect(second->platform() == first->platform(),
           "expected platform() not to vary with compiler or verbose");
    expect(second->locale() == first->locale(),
           "expected locale() not to vary with compiler or verbose");
    expect(second->shell() == first->shell(),
           "expected shell() not to vary with compiler or verbose");
}

const mm::test::case_ cases[] = {
    { "unconfigured reports the shared default", &unconfigured_reports_the_shared_default },
    { "persisted reports the written lane",      &persisted_reports_the_written_lane },
    { "platform, locale and shell are fixed",    &platform_locale_and_shell_are_fixed },
};

const mm::test::registrar reg{"mm.model configuration", cases};

}  // namespace
