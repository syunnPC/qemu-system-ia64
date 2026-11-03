/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Signal delivery and return on IA-64: the siginfo of a fault, a handler
 * that fixes the fault and returns to retry it, a handler that moves the
 * interrupted IP past a break, siglongjmp out of a handler, the alternate
 * signal stack, and asynchronous signals that must leave the interrupted
 * stacked and floating-point registers intact.
 */

#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include "ia64-test.h"

static char *fault_page;
static long page_size;
static volatile void *seen_addr;
static volatile int seen_code;
static volatile int segv_count;

static void segv_handler(int sig, siginfo_t *info, void *uc)
{
    seen_addr = info->si_addr;
    seen_code = info->si_code;
    segv_count++;
    /* Let the faulting store run again. */
    mprotect(fault_page, page_size, PROT_READ | PROT_WRITE);
}

static void test_fault_retry(void)
{
    struct sigaction sa = { .sa_sigaction = segv_handler,
                            .sa_flags = SA_SIGINFO };

    fault_page = mmap(NULL, page_size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(fault_page != MAP_FAILED);
    CHECK_EQ(sigaction(SIGSEGV, &sa, NULL), 0);
    ((volatile char *)fault_page)[123] = 77;
    CHECK_EQ(segv_count, 1);
    CHECK_EQ((uintptr_t)seen_addr, (uintptr_t)fault_page + 123);
    CHECK_EQ(seen_code, SEGV_ACCERR);
    CHECK_EQ(fault_page[123], 77);
    munmap(fault_page, page_size);
    signal(SIGSEGV, SIG_DFL);
}

static volatile int trap_count;
static volatile unsigned long trap_ip;

static void trap_handler(int sig, siginfo_t *info, void *ucv)
{
    ucontext_t *uc = ucv;
    unsigned long ip = uc->uc_mcontext.sc_ip;

    trap_count++;
    trap_ip = (unsigned long)info->si_addr;
    /* sc_ip holds the bundle address and the slot: step over the break. */
    if ((ip & 3) == 2) {
        uc->uc_mcontext.sc_ip = (ip & ~0xfUL) + 16;
    } else {
        uc->uc_mcontext.sc_ip = ip + 1;
    }
}

static void test_break_step(void)
{
    struct sigaction sa = { .sa_sigaction = trap_handler,
                            .sa_flags = SA_SIGINFO };
    long before = 5, after;

    CHECK_EQ(sigaction(SIGTRAP, &sa, NULL), 0);
    /* break 0x80000 is a debugger breakpoint: SIGTRAP, TRAP_BRKPT. */
    asm volatile("mov r14 = %1 ;;\n\t"
                 "break.m 0x80000 ;;\n\t"
                 "adds %0 = 1, r14 ;;"
                 : "=r"(after) : "r"(before) : "r14");
    CHECK_EQ(trap_count, 1);
    CHECK_EQ(after, 6);
    CHECK(trap_ip != 0);
    signal(SIGTRAP, SIG_DFL);
}

static sigjmp_buf jump_out;

static void jump_handler(int sig)
{
    siglongjmp(jump_out, sig);
}

static __attribute__((noinline)) int spin_deep(int n)
{
    volatile int x = n;

    if (n == 0) {
        raise(SIGUSR1);
        return -1;
    }
    return spin_deep(n - 1) + x;
}

static void test_siglongjmp(void)
{
    int ret;

    signal(SIGUSR1, jump_handler);
    ret = sigsetjmp(jump_out, 1);
    if (ret == 0) {
        spin_deep(2000);
        CHECK(!"the handler returned");
    }
    CHECK_EQ(ret, SIGUSR1);
    signal(SIGUSR1, SIG_DFL);
}

static char *altstack;
static volatile int on_altstack;

static void altstack_handler(int sig)
{
    char here;

    on_altstack = &here >= altstack && &here < altstack + SIGSTKSZ;
}

static void test_altstack(void)
{
    stack_t ss = { .ss_size = SIGSTKSZ };
    struct sigaction sa = { .sa_handler = altstack_handler,
                            .sa_flags = SA_ONSTACK };

    altstack = malloc(SIGSTKSZ);
    ss.ss_sp = altstack;
    CHECK_EQ(sigaltstack(&ss, NULL), 0);
    CHECK_EQ(sigaction(SIGUSR2, &sa, NULL), 0);
    raise(SIGUSR2);
    CHECK_EQ(on_altstack, 1);
    ss.ss_flags = SS_DISABLE;
    sigaltstack(&ss, NULL);
    signal(SIGUSR2, SIG_DFL);
}

static volatile int alarm_count;

static void alarm_handler(int sig)
{
    /* Clobber scratch state the interrupted code must not see. */
    volatile double d = 1.0;

    for (int i = 0; i < 100; i++) {
        d = d * 1.5 + i;
    }
    alarm_count++;
}

/* Integer and FP values held in registers across many asynchronous signals. */
static __attribute__((noinline)) double busy(int depth, uint64_t *isum)
{
    uint64_t a = depth, b = depth * 7, c = depth ^ 0x55, d = depth + 3;
    double x = depth * 0.5, y = depth + 0.25;

    for (long i = 0; i < 200000; i++) {
        a += b;
        b ^= c + i;
        c += d;
        d = d * 3 + a;
        x = x * 0.999 + y;
        y = y * 1.001 - 0.5;
    }
    if (depth > 0) {
        x += busy(depth - 1, isum);
    }
    *isum += a ^ b ^ c ^ d;
    return x + y;
}

static void test_async_signals(void)
{
    struct itimerval timer = { .it_interval = { 0, 2000 },
                               .it_value = { 0, 2000 } };
    struct itimerval off = { };
    uint64_t quiet_sum = 0, noisy_sum = 0;
    double quiet, noisy;

    quiet = busy(8, &quiet_sum);
    signal(SIGALRM, alarm_handler);
    CHECK_EQ(setitimer(ITIMER_REAL, &timer, NULL), 0);
    noisy = busy(8, &noisy_sum);
    setitimer(ITIMER_REAL, &off, NULL);
    signal(SIGALRM, SIG_DFL);
    printf("async signals delivered: %d\n", alarm_count);
    CHECK(alarm_count > 0);
    CHECK_EQ(noisy_sum, quiet_sum);
    CHECK(noisy == quiet);
}

static volatile int outer_seen, inner_seen;

static void inner_handler(int sig)
{
    inner_seen = outer_seen;
}

static void outer_handler(int sig)
{
    outer_seen = 1;
    raise(SIGUSR2);
    outer_seen = 2;
}

static void test_nested(void)
{
    signal(SIGUSR1, outer_handler);
    signal(SIGUSR2, inner_handler);
    raise(SIGUSR1);
    CHECK_EQ(outer_seen, 2);
    CHECK_EQ(inner_seen, 1);
    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
}

static void test_mask(void)
{
    sigset_t block, pending;

    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    CHECK_EQ(sigprocmask(SIG_BLOCK, &block, NULL), 0);
    signal(SIGUSR1, jump_handler);
    raise(SIGUSR1);
    CHECK_EQ(sigpending(&pending), 0);
    CHECK(sigismember(&pending, SIGUSR1));
    signal(SIGUSR1, SIG_IGN);
    CHECK_EQ(sigprocmask(SIG_UNBLOCK, &block, NULL), 0);
    signal(SIGUSR1, SIG_DFL);
}

int main(void)
{
    page_size = sysconf(_SC_PAGESIZE);
    test_fault_retry();
    test_break_step();
    test_siglongjmp();
    test_altstack();
    test_async_signals();
    test_nested();
    test_mask();
    return test_finish("signal-frame");
}
