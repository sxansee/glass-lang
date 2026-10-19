#pragma once

#include <iostream>
#include <string>

namespace testkit {

inline int g_testsRun = 0;
inline int g_testsFailed = 0;

inline void reportSummary() {
    std::cout << "\n" << g_testsRun << " проверок, " << g_testsFailed << " провалено.\n";
}

} // namespace testkit

#define CHECK(cond)                                                              \
    do {                                                                         \
        testkit::g_testsRun++;                                                   \
        if (!(cond)) {                                                           \
            testkit::g_testsFailed++;                                            \
            std::cerr << "FAILED: " << #cond << "\n  at " << __FILE__ << ":"     \
                      << __LINE__ << "\n";                                       \
        }                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        testkit::g_testsRun++;                                                   \
        auto _a = (actual);                                                      \
        auto _e = (expected);                                                    \
        if (!(_a == _e)) {                                                       \
            testkit::g_testsFailed++;                                            \
            std::cerr << "FAILED: " << #actual << " == " << #expected << "\n"    \
                      << "  получено: " << _a << "\n"                            \
                      << "  ожидалось: " << _e << "\n"                           \
                      << "  at " << __FILE__ << ":" << __LINE__ << "\n";         \
        }                                                                        \
    } while (0)
