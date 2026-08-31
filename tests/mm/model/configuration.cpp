// Black box tests for mm.model::default_configuration().

#include <cstdlib>
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

void reflects_cxx_when_set() {
    const char* previous = std::getenv("CXX");

    setenv("CXX", "test-compiler", 1);
    const auto configuration = mm::model::default_configuration(false);
    mm::test::expect(configuration->compiler() == "test-compiler -fmodules-ts",
                     "expected compiler() to honor $CXX the same way mm::build::default_toolchain does");

    if (previous == nullptr)
        unsetenv("CXX");
    else
        setenv("CXX", previous, 1);
}

// The platform/locale/shell facts are fixed project policy, not derived
// from the environment: two calls under different conditions must still
// agree, unlike compiler().
void platform_locale_and_shell_are_fixed() {
    const auto first = mm::model::default_configuration(false);

    setenv("CXX", "irrelevant-to-these-three", 1);
    const auto second = mm::model::default_configuration(true);
    unsetenv("CXX");

    mm::test::expect(first->platform() == "POSIX", "expected platform() to be POSIX");
    mm::test::expect(first->locale() == "C", "expected locale() to be C");
    mm::test::expect(first->shell() == "/bin/sh", "expected shell() to be /bin/sh");

    mm::test::expect(second->platform() == first->platform(),
                     "expected platform() not to vary with $CXX or verbose");
    mm::test::expect(second->locale() == first->locale(),
                     "expected locale() not to vary with $CXX or verbose");
    mm::test::expect(second->shell() == first->shell(),
                     "expected shell() not to vary with $CXX or verbose");
}

const mm::test::case_ cases[] = {
    { "reflects the given verbose flag",         &reflects_the_given_verbose_flag },
    { "reflects $CXX when set",                  &reflects_cxx_when_set },
    { "platform, locale and shell are fixed",    &platform_locale_and_shell_are_fixed },
};

const mm::test::registrar reg{"mm.model configuration", cases};

}  // namespace
