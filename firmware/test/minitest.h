/* Minimal unit-test helpers for host-side firmware tests. SPDX-License-Identifier: MIT */
#pragma once
#include <stdio.h>
#include <stdlib.h>

static int mt_failures;
static int mt_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        mt_checks++;                                                                \
        if (!(cond)) {                                                              \
            mt_failures++;                                                          \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        long long _a = (long long)(a), _b = (long long)(b);                         \
        mt_checks++;                                                                \
        if (_a != _b) {                                                             \
            mt_failures++;                                                          \
            fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__,    \
                    __LINE__, #a, #b, _a, _b);                                      \
        }                                                                           \
    } while (0)

#define RUN(test)                                                                   \
    do {                                                                            \
        int _before = mt_failures;                                                  \
        test();                                                                     \
        printf("%s %s\n", mt_failures == _before ? "PASS" : "FAIL", #test);         \
    } while (0)

#define MT_REPORT()                                                                 \
    (printf("%d checks, %d failures\n", mt_checks, mt_failures), mt_failures ? 1 : 0)
