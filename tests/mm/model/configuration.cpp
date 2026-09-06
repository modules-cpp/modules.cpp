// Black box tests for mm.model::default_configuration().

#include <string_view>

import mm.model;
import mm.test;
import models.configuration;

namespace {

void reflects_the_given_verbose_flag() {
    const auto quiet = mm::model::default_configuration(false);
    const auto loud = mm::model::default_configuration(true);

    mm::test::expect(!quiet->verbose(), "expected verbose(false) to report false");
    mm::test::expect(loud->verbose(), "expected verbose(true) to report true");
}

void reports_the_shared_debug_gcc_default() {
    const auto configuration = mm::model::default_configuration(false);
    mm::test::expect(configuration->build() == models::Build::Debug,
                     "expected the unconfigured build to be debug");
    mm::test::expect(configuration->compiler_family() == models::CompilerFamily::Gcc,
                     "expected the unconfigured compiler family to be GCC");
    mm::test::expect(configuration->compiler() == "g++",
                     "expected the unconfigured C++ driver to be g++");
}

// The platform/locale/shell facts are fixed project policy, not derived
// from the environment.
void platform_locale_and_shell_are_fixed() {
    const auto first = mm::model::default_configuration(false);
    const auto second = mm::model::default_configuration(true);

    mm::test::expect(first->platform() == "POSIX", "expected platform() to be POSIX");
    mm::test::expect(first->locale() == "C", "expected locale() to be C");
    mm::test::expect(first->shell() == "/bin/sh", "expected shell() to be /bin/sh");

    mm::test::expect(second->platform() == first->platform(),
                     "expected platform() not to vary with compiler or verbose");
    mm::test::expect(second->locale() == first->locale(),
                     "expected locale() not to vary with compiler or verbose");
    mm::test::expect(second->shell() == first->shell(),
                     "expected shell() not to vary with compiler or verbose");
}

const mm::test::case_ cases[] = {
    { "reflects the given verbose flag",         &reflects_the_given_verbose_flag },
    { "reports the shared debug GCC default",    &reports_the_shared_debug_gcc_default },
    { "platform, locale and shell are fixed",    &platform_locale_and_shell_are_fixed },
};

const mm::test::registrar reg{"mm.model configuration", cases};

}  // namespace
