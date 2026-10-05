/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest-only launcher: upstream RLIMIT_NPROC tests must not run as root. */
#include <grp.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2 || setgroups(0, NULL) || setgid(1000) || setuid(1000) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        getuid() != 1000 || geteuid() != 1000 ||
        getgid() != 1000 || getegid() != 1000 || getgroups(0, NULL) != 0) {
        perror("UPSTREAM_ERROR unprivileged-launcher");
        return 125;
    }
    puts("UPSTREAM_RUN_AS uid=1000 gid=1000 groups=0 no_new_privs=1");
    fflush(stdout);
    execv(argv[1], argv + 1);
    perror("UPSTREAM_ERROR unprivileged-exec");
    return 125;
}
