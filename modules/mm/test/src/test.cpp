// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <exception>
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

module mm.test;

namespace mm::test {

namespace {

struct entry {
    std::string_view name;
    const case_* cases;
    std::size_t count;
};

std::vector<entry>& registry() {
    static std::vector<entry> suites;
    return suites;
}

}

void expect(bool condition, std::string_view message, std::source_location where) {
    if (!condition) {
        throw failure(
            std::string(where.file_name()) + ":" +
            std::to_string(where.line()) + ": " +
            std::string(message)
        );
    }
}

void add_suite(std::string_view name, const case_* cases, std::size_t count) {
    registry().push_back({name, cases, count});
}

int run_all() {
    int total = 0;
    int failed = 0;
    int expected = 0;

    for (const auto& suite : registry()) {
        std::cout << "[suite] " << suite.name << "\n";

        for (std::size_t i = 0; i < suite.count; ++i) {
            const case_& test = suite.cases[i];
            ++total;

            std::string reason;
            bool threw = false;

            try {
                test.run();
            } catch (const std::exception& e) {
                threw = true;
                reason = e.what();
            }

            if (test.expected_failure) {
                if (threw) {
                    ++expected;
                    std::cout << "  [xfail] " << test.name << ": " << reason << "\n";
                } else {
                    // The defect is gone. Leaving the marker in place would hide
                    // the next regression, so this fails the run until cleared.
                    ++failed;
                    std::cerr
                        << "  [xpass] "
                        << test.name
                        << ": expected to fail but passed; remove the expected_failure marker\n";
                }
                continue;
            }

            if (threw) {
                ++failed;
                std::cerr << "  [fail] " << test.name << ": " << reason << "\n";
            } else {
                std::cout << "  [pass] " << test.name << "\n";
            }
        }
    }

    if (total == 0) {
        std::cerr << "ERROR: no tests registered\n";
        return 2;
    }

    std::cout << (failed == 0 ? "OK" : "FAILED") << " (" << failed << "/" << total << " failed";
    if (expected > 0) std::cout << ", " << expected << " expected";
    std::cout << ")\n";

    return failed == 0 ? 0 : 1;
}

}
