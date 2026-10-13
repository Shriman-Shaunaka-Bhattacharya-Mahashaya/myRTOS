#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstddef>

namespace myrtos::test {
inline std::size_t check_count = 0;

inline void check(bool passed, const char* expression, const char* file, int line) {
    ++check_count;
    if (!passed) {
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expression);
        std::exit(EXIT_FAILURE);
    }
}
} // namespace myrtos::test

// Evaluate exactly once. Unlike assert(), checks remain active with NDEBUG.
#define CHECK(expression) \
    ::myrtos::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
