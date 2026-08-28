// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <source_location>
#include <stdexcept>
#include <string_view>

export module mm.test;

export namespace mm::test {

struct failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(
    bool condition,
    std::string_view message,
    std::source_location where = std::source_location::current()
);

using fn = void (*)();

struct case_ {
    std::string_view name;
    fn run;

    // A case pinning behaviour the code does not have yet. Failing is expected
    // and does not fail the run; passing does, because the marker is then stale
    // and the case belongs with the ordinary tests.
    bool expected_failure = false;
};

void add_suite(std::string_view name, const case_* cases, std::size_t count);
int run_all();

struct registrar {
    registrar(std::string_view name, const case_* cases, std::size_t count) {
        add_suite(name, cases, count);
    }

    template <std::size_t N>
    registrar(std::string_view name, const case_ (&cases)[N]) {
        add_suite(name, cases, N);
    }
};

}

