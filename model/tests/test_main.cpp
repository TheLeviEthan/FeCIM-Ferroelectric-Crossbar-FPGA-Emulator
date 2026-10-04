#include "test_harness.h"

int main() {
    int failed_tests = 0;
    for (const auto& t : th::registry()) {
        const int before = th::failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            th::fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const bool ok = th::failures() == before;
        if (!ok) ++failed_tests;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << th::registry().size() - static_cast<std::size_t>(failed_tests)
              << "/" << th::registry().size() << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}
