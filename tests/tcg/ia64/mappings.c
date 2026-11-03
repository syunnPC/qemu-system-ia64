/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Memory mappings at IA-64 addresses: mmap hands out region 1 addresses
 * as Linux does, a fixed mapping lands at the region 3 address asked for,
 * a fault reports the address the program used, and requests the kernel
 * refuses (beyond TASK_SIZE, across a region's limit) fail.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ia64-test.h"

#define REGION(addr) ((uintptr_t)(addr) >> 61)
#define REGION3_FIXED ((void *)0x6000000123400000UL)
#define GATE_REGION ((void *)0xa000000000100000UL)

static long page;
static sigjmp_buf fault_jump;
static volatile uintptr_t fault_addr;
static volatile int fault_code;

static void fault_handler(int sig, siginfo_t *info, void *uc)
{
    fault_addr = (uintptr_t)info->si_addr;
    fault_code = info->si_code;
    siglongjmp(fault_jump, 1);
}

static int probe_write(volatile char *p)
{
    fault_addr = 0;
    if (sigsetjmp(fault_jump, 1) == 0) {
        *p = 1;
        return 0;
    }
    return 1;
}

/* The /proc/self/maps line that holds addr, as the program's address. */
static int maps_find(uintptr_t addr, char *perms)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    unsigned long start, end;
    char p[5];
    int found = 0;

    if (!maps) {
        return 0;
    }
    while (fscanf(maps, "%lx-%lx %4s%*[^\n]", &start, &end, p) == 3) {
        if (addr >= start && addr < end) {
            memcpy(perms, p, 5);
            found = 1;
            break;
        }
    }
    fclose(maps);
    return found;
}

int main(void)
{
    struct sigaction sa = { .sa_sigaction = fault_handler,
                            .sa_flags = SA_SIGINFO };
    char *anon, *fixed, *grown, *file;
    char path[] = "/tmp/ia64-mappingsXXXXXX";
    int fd;

    page = sysconf(_SC_PAGESIZE);
    sigaction(SIGSEGV, &sa, NULL);

    anon = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(anon != MAP_FAILED);
    CHECK_EQ(REGION(anon), 1);
    memset(anon, 0x5a, 4 * page);
    CHECK_EQ(anon[4 * page - 1], 0x5a);

    fixed = mmap(REGION3_FIXED, 2 * page, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK_EQ((uintptr_t)fixed, (uintptr_t)REGION3_FIXED);
    fixed[0] = 3;
    fixed[2 * page - 1] = 4;
    CHECK_EQ(fixed[0] + fixed[2 * page - 1], 7);

    /* A fault names the region 3 address, and unmapped is MAPERR. */
    CHECK_EQ(probe_write(fixed + 2 * page + 8), 1);
    CHECK_EQ(fault_addr, (uintptr_t)fixed + 2 * page + 8);
    CHECK_EQ(fault_code, SEGV_MAPERR);
    CHECK_EQ(mprotect(fixed, page, PROT_READ), 0);
    CHECK_EQ(probe_write(fixed + 16), 1);
    CHECK_EQ(fault_addr, (uintptr_t)fixed + 16);
    CHECK_EQ(fault_code, SEGV_ACCERR);

    grown = mremap(anon, 4 * page, 64 * page, MREMAP_MAYMOVE);
    CHECK(grown != MAP_FAILED);
    CHECK_EQ(REGION(grown), 1);
    CHECK_EQ(grown[4 * page - 1], 0x5a);
    grown[64 * page - 1] = 9;
    CHECK_EQ(munmap(grown, 64 * page), 0);
    CHECK_EQ(probe_write(grown), 1);
    CHECK_EQ(fault_addr, (uintptr_t)grown);

    fd = mkstemp(path);
    CHECK(fd >= 0);
    unlink(path);
    CHECK_EQ(write(fd, "mapped file", 11), 11);
    file = mmap(NULL, page, PROT_READ, MAP_SHARED, fd, 0);
    CHECK(file != MAP_FAILED);
    CHECK(memcmp(file, "mapped file", 11) == 0);
    CHECK_EQ(REGION(file), 1);
    munmap(file, page);
    close(fd);

    /* Region 5 and up belong to the kernel. */
    errno = 0;
    CHECK(mmap(GATE_REGION, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS |
               MAP_FIXED, -1, 0) == MAP_FAILED);
    CHECK_EQ(errno, ENOMEM);

    /* /proc/self/maps and the main thread's stack use IA-64 addresses. */
    {
        char perms[5] = "";
        int local = 0;
        pthread_attr_t attr;
        void *stack;
        size_t size;

        CHECK(maps_find((uintptr_t)fixed, perms) && perms[0] == 'r');
        CHECK(maps_find((uintptr_t)main, perms) && perms[2] == 'x');
        CHECK(maps_find((uintptr_t)&local, perms) && perms[1] == 'w');
        CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
        CHECK_EQ(pthread_attr_getstack(&attr, &stack, &size), 0);
        CHECK((char *)&local >= (char *)stack &&
              (char *)&local < (char *)stack + size);
        pthread_attr_destroy(&attr);
    }

    CHECK_EQ(munmap(fixed, 2 * page), 0);
    return test_finish("mappings");
}
