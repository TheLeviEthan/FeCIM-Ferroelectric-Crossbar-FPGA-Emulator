// Minimal, dependency-free test harness.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_THROWS(expr, ExcType); }
//
// Every TEST registers itself; test_main.cpp runs them all and exits non-zero
// if any check failed, which is what CI keys on.

#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace th {

struct Test {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cerr << "    FAIL " << file << ":" << line << ": " << msg << "\n";
}

// Print integers as numbers even when they are (u)int8_t.
template <typename T>
auto printable(const T& v) -> decltype(+v) { return +v; }

} // namespace th

#define TH_CAT2(a, b) a##b
#define TH_CAT(a, b)  TH_CAT2(a, b)

#define TEST(name)                                                        \
    static void name();                                                   \
    static th::Registrar TH_CAT(th_reg_, name)(#name, name);              \
    static void name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) th::fail(__FILE__, __LINE__, "CHECK(" #cond ")");    \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        const auto th_a = (a);                                            \
        const auto th_b = (b);                                            \
        if (!(th_a == th_b))                                              \
            th::fail(__FILE__, __LINE__,                                  \
                     "CHECK_EQ(" #a ", " #b "): " +                       \
                     std::to_string(th::printable(th_a)) + " != " +       \
                     std::to_string(th::printable(th_b)));                \
    } while (0)

#define CHECK_THROWS(expr, exc)                                           \
    do {                                                                  \
        bool th_thrown = false;                                           \
        try { (void)(expr); } catch (const exc&) { th_thrown = true; }    \
        if (!th_thrown)                                                   \
            th::fail(__FILE__, __LINE__,                                  \
                     "CHECK_THROWS(" #expr ", " #exc ")");                \
    } while (0)
