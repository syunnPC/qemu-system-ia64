/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared checks for the IA-64 linux-user tests: each failed check prints
 * the values it compared, and the test exits non-zero if any failed.
 */

#ifndef IA64_TEST_H
#define IA64_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static int ia64_test_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ia64_test_failures++;                                       \
        }                                                               \
    } while (0)

#define CHECK_EQ(got, want)                                             \
    do {                                                                \
        uint64_t got_ = (uint64_t)(got);                                \
        uint64_t want_ = (uint64_t)(want);                              \
        if (got_ != want_) {                                            \
            printf("FAIL %s:%d: %s is 0x%" PRIx64 ", expected 0x%"      \
                   PRIx64 "\n", __FILE__, __LINE__, #got, got_, want_); \
            ia64_test_failures++;                                       \
        }                                                               \
    } while (0)

static inline int test_finish(const char *name)
{
    if (ia64_test_failures) {
        printf("%s: %d check(s) failed\n", name, ia64_test_failures);
        return EXIT_FAILURE;
    }
    printf("%s: ok\n", name);
    return EXIT_SUCCESS;
}

#endif
