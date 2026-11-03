/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * fork through clone2 with no new stack: the child keeps the parent's
 * register stack.  vfork, and posix_spawn's clone2 onto a stack of its
 * own, run their child; pipes, waitpid and exit statuses round-trip.
 */

#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ia64-test.h"

static __attribute__((noinline)) int child_work(int depth)
{
    volatile int local = depth;

    if (depth == 0) {
        return 0;
    }
    return child_work(depth - 1) + local;
}

int main(void)
{
    int fds[2];
    pid_t pid;
    int status;
    char buf[32] = { 0 };

    CHECK_EQ(pipe(fds), 0);
    pid = fork();
    if (pid == 0) {
        char msg[32];
        int n = snprintf(msg, sizeof(msg), "sum %d", child_work(500));

        close(fds[0]);
        if (write(fds[1], msg, n) != n) {
            _exit(2);
        }
        _exit(42);
    }
    CHECK(pid > 0);
    close(fds[1]);
    CHECK(read(fds[0], buf, sizeof(buf) - 1) > 0);
    CHECK(strcmp(buf, "sum 125250") == 0);
    CHECK_EQ(waitpid(pid, &status, 0), pid);
    CHECK(WIFEXITED(status));
    CHECK_EQ(WEXITSTATUS(status), 42);

    pid = fork();
    if (pid == 0) {
        raise(SIGTERM);
        _exit(0);
    }
    CHECK_EQ(waitpid(pid, &status, 0), pid);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

    pid = vfork();
    if (pid == 0) {
        _exit(5);
    }
    CHECK_EQ(waitpid(pid, &status, 0), pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 5);

    {
        char *argv[] = { (char *)"sh", (char *)"-c", (char *)"exit 7", NULL };
        extern char **environ;

        CHECK_EQ(posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ), 0);
        CHECK_EQ(waitpid(pid, &status, 0), pid);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
    }
    return test_finish("fork-wait");
}
