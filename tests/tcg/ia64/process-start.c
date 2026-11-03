/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * What a process sees at start: its arguments, environment and auxiliary
 * vector, the region layout Linux gives an executable, and the uname of
 * an IA-64 machine.
 */

#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "ia64-test.h"

extern char _start[];
extern char __executable_start[];
static int data_in_region3 = 1;

#define REGION(addr) ((uintptr_t)(addr) >> 61)

int main(int argc, char **argv, char **envp)
{
    struct utsname name;
    const ElfW(Phdr) *phdr;
    const unsigned char *random;
    int local;

    CHECK_EQ(argc, 3);
    CHECK(strcmp(argv[1], "first") == 0);
    CHECK(strcmp(argv[2], "second argument") == 0);
    CHECK(argv[3] == NULL);
    CHECK(getenv("IA64_TEST_VAR") != NULL &&
          strcmp(getenv("IA64_TEST_VAR"), "value") == 0);
    CHECK(envp == argv + argc + 1);

    /* binfmt_elf loads an executable's text in region 2, data in 3. */
    CHECK_EQ(REGION(main), 2);
    CHECK_EQ(REGION(&data_in_region3), 3);
    CHECK_EQ(getauxval(AT_PAGESZ), sysconf(_SC_PAGESIZE));
    CHECK_EQ(getauxval(AT_ENTRY), (uintptr_t)_start);
    phdr = (const ElfW(Phdr) *)getauxval(AT_PHDR);
    CHECK_EQ((uintptr_t)phdr, (uintptr_t)__executable_start + 64);
    CHECK_EQ(getauxval(AT_PHENT), sizeof(ElfW(Phdr)));
    CHECK(getauxval(AT_PHNUM) > 0 && phdr[0].p_type != PT_NULL);
    random = (const unsigned char *)getauxval(AT_RANDOM);
    CHECK(random != NULL && REGION(random) == REGION(&local));
    /* AT_EXECFN names the file that was executed, as argv[0] does here. */
    {
        const char *execfn = (const char *)getauxval(AT_EXECFN);
        const char *base = strrchr(argv[0], '/');

        base = base ? base + 1 : argv[0];
        CHECK(execfn != NULL && strlen(execfn) >= strlen(base) &&
              strcmp(execfn + strlen(execfn) - strlen(base), base) == 0);
    }

    CHECK_EQ(uname(&name), 0);
    CHECK(strcmp(name.machine, "ia64") == 0);
    return test_finish("process-start");
}
