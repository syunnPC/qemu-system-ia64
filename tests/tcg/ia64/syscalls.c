/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A spread of system calls through break 0x100000: files, directories,
 * stat, time, sockets, polling, process limits and errors.  The IA-64
 * numbers start at 1024 and return errors through r10.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "ia64-test.h"

int main(void)
{
    char dir[] = "/tmp/ia64-syscallsXXXXXX";
    char path[128], buf[64], cwd[4096];
    struct stat st;
    struct timespec ts, req = { 0, 1000000 };
    struct rlimit rl;
    struct pollfd pfd;
    int sv[2], fd, found = 0;
    DIR *d;
    struct dirent *de;

    CHECK_EQ(syscall(SYS_getpid), getpid());
    CHECK(SYS_getpid >= 1024);

    CHECK(mkdtemp(dir) != NULL);
    snprintf(path, sizeof(path), "%s/file", dir);
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0640);
    CHECK(fd >= 0);
    CHECK_EQ(write(fd, "0123456789", 10), 10);
    CHECK_EQ(lseek(fd, 3, SEEK_SET), 3);
    CHECK_EQ(read(fd, buf, 4), 4);
    CHECK(memcmp(buf, "3456", 4) == 0);
    CHECK_EQ(pread(fd, buf, 2, 8), 2);
    CHECK(memcmp(buf, "89", 2) == 0);
    CHECK_EQ(fstat(fd, &st), 0);
    CHECK_EQ(st.st_size, 10);
    CHECK_EQ(st.st_mode & 0777, 0640);
    CHECK(S_ISREG(st.st_mode));
    close(fd);
    CHECK_EQ(stat(path, &st), 0);
    CHECK_EQ(st.st_size, 10);
    CHECK_EQ(lstat(dir, &st), 0);
    CHECK(S_ISDIR(st.st_mode));

    d = opendir(dir);
    CHECK(d != NULL);
    while ((de = readdir(d)) != NULL) {
        found += strcmp(de->d_name, "file") == 0;
    }
    closedir(d);
    CHECK_EQ(found, 1);

    errno = 0;
    CHECK_EQ(open("/nonexistent/ia64", O_RDONLY), -1);
    CHECK_EQ(errno, ENOENT);
    CHECK_EQ(unlink(path), 0);
    CHECK_EQ(rmdir(dir), 0);

    CHECK_EQ(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
    CHECK_EQ(nanosleep(&req, NULL), 0);
    CHECK(time(NULL) > 1500000000);

    CHECK_EQ(getrlimit(RLIMIT_NOFILE, &rl), 0);
    CHECK(rl.rlim_cur > 0);

    CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    CHECK_EQ(send(sv[0], "ping", 4, 0), 4);
    pfd.fd = sv[1];
    pfd.events = POLLIN;
    CHECK_EQ(poll(&pfd, 1, 1000), 1);
    CHECK_EQ(recv(sv[1], buf, sizeof(buf), 0), 4);
    CHECK(memcmp(buf, "ping", 4) == 0);
    close(sv[0]);
    close(sv[1]);

    CHECK(getcwd(cwd, sizeof(cwd)) != NULL && cwd[0] == '/');
    return test_finish("syscalls");
}
