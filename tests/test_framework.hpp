#pragma once

#include <cstdio>

namespace testfw {

inline int gChecks = 0;
inline int gFailures = 0;

inline void check(bool cond, const char* expr, int line) {
    gChecks++;
    if (!cond) {
        gFailures++;
        printf("FAIL line %d: %s\n", line, expr);
    }
}

}  // namespace testfw

#define CHECK(cond) testfw::check((cond), #cond, __LINE__)
