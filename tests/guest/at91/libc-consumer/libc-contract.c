/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Functional libc/kernel contracts exercised on an ARM926 Linux guest. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <regex.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("LIBC_FAILURE %s:%d %s errno=%d\n", \
               __func__, __LINE__, #expr, errno); \
        return 1; \
    } \
} while (0)

static int memory(void)
{
    unsigned char src[1100], dst[1100], ref[1100];
    size_t n, a, b, i;
    long page = sysconf(_SC_PAGESIZE);
    unsigned char *map;

    for (i = 0; i < sizeof(src); i++) {
        src[i] = (unsigned char)(i * 37 + 11);
    }
    for (n = 0; n <= 1024; n += n < 32 ? 1 : 31) {
        for (a = 0; a < 8; a++) {
            for (b = 0; b < 8; b++) {
                memset(dst, 0xa5, sizeof(dst));
                CHECK(memcpy(dst + b, src + a, n) == dst + b);
                CHECK(memcmp(dst + b, src + a, n) == 0);
                CHECK(dst[b + n] == 0xa5);
                for (i = 0; i < sizeof(dst); i++) {
                    dst[i] = ref[i] = (unsigned char)i;
                }
                for (i = 0; i < n; i++) {
                    ref[b + i] = (unsigned char)(a + i);
                }
                CHECK(memmove(dst + b, dst + a, n) == dst + b);
                CHECK(memcmp(dst, ref, sizeof(dst)) == 0);
            }
        }
    }
    CHECK(page > 1024);
    map = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(map != MAP_FAILED);
    CHECK(mprotect(map + page, page, PROT_NONE) == 0);
    for (n = 1; n < 512; n++) {
        CHECK(memcpy(map + page - n, src, n) == map + page - n);
        CHECK(memcmp(map + page - n, src, n) == 0);
    }
    CHECK(munmap(map, (size_t)page * 2) == 0);
    return 0;
}

static int allocation(void)
{
    size_t n, i;
    void *aligned;
    for (n = 1; n < 16384; n = n * 2 + 1) {
        unsigned char *p = calloc(n, 1);
        CHECK(p != NULL);
        for (i = 0; i < n; i++) {
            CHECK(p[i] == 0);
            p[i] = (unsigned char)(i * 13);
        }
        p = realloc(p, n * 3);
        CHECK(p != NULL);
        for (i = 0; i < n; i++) {
            CHECK(p[i] == (unsigned char)(i * 13));
        }
        free(p);
        CHECK(posix_memalign(&aligned, 64, n) == 0);
        CHECK(((uintptr_t)aligned & 63) == 0);
        memset(aligned, 0xff, n);
        free(aligned);
    }
    return 0;
}

static int strings(void)
{
    char buf[80], *end;
    regex_t re;
    regmatch_t match[2];
    wchar_t wc;
    mbstate_t state = { 0 };
    CHECK(setlocale(LC_ALL, "C") != NULL);
    CHECK(snprintf(buf, sizeof(buf), "%lld:%08x:%.3f",
                   4294967297LL, 0x1234, 1.25) == 25);
    CHECK(strcmp(buf, "4294967297:00001234:1.250") == 0);
    CHECK(strtoll("-4294967297!", &end, 10) == -4294967297LL);
    CHECK(*end == '!');
    errno = 0;
    CHECK(strtoull("18446744073709551616", &end, 10) == UINT64_MAX);
    CHECK(errno == ERANGE);
    CHECK(regcomp(&re, "^arm([0-9]+)$", REG_EXTENDED) == 0);
    CHECK(regexec(&re, "arm926", 2, match, 0) == 0);
    CHECK(match[1].rm_so == 3 && match[1].rm_eo == 6);
    CHECK(regexec(&re, "armv5", 2, match, 0) == REG_NOMATCH);
    regfree(&re);
    CHECK(mbrtowc(&wc, "A", 1, &state) == 1 && wc == L'A');
    return 0;
}

static int soft_float(void)
{
    volatile double x = 2.0, y = 0.5;
    volatile uint64_t numerator = UINT64_C(0xfedcba9876543210);
    volatile uint64_t denominator = 1234567;
    CHECK(fabs(sqrt(x) * sqrt(x) - x) < 1e-12);
    CHECK(fabs(sin(y) * sin(y) + cos(y) * cos(y) - 1.0) < 1e-12);
    CHECK(pow(x, 10.0) == 1024.0);
    CHECK(isnan(sqrt(-x)));
    CHECK(nextafter(1.0, 2.0) > 1.0);
    CHECK((numerator / denominator) * denominator +
          numerator % denominator == numerator);
    return 0;
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static pthread_key_t key;
static int ready, start, count, destroyed;
static _Thread_local int tls;

static void destructor(void *value)
{
    if ((uintptr_t)value >= 1 && (uintptr_t)value <= 4) {
        pthread_mutex_lock(&lock);
        destroyed++;
        pthread_mutex_unlock(&lock);
    }
}

static void *worker(void *arg)
{
    int i, id = (int)(uintptr_t)arg;
    tls = id;
    if (pthread_setspecific(key, arg)) {
        return (void *)1;
    }
    pthread_mutex_lock(&lock);
    ready++;
    pthread_cond_broadcast(&condition);
    while (!start) {
        pthread_cond_wait(&condition, &lock);
    }
    pthread_mutex_unlock(&lock);
    for (i = 0; i < 2000; i++) {
        pthread_mutex_lock(&lock);
        count++;
        pthread_mutex_unlock(&lock);
        if (tls != id || pthread_getspecific(key) != arg) {
            return (void *)1;
        }
    }
    return NULL;
}

static int threads(void)
{
    pthread_t thread[4];
    void *result;
    int i;
    CHECK(pthread_key_create(&key, destructor) == 0);
    for (i = 0; i < 4; i++) {
        CHECK(pthread_create(&thread[i], NULL, worker,
                             (void *)(uintptr_t)(i + 1)) == 0);
    }
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (ready != 4) {
        CHECK(pthread_cond_wait(&condition, &lock) == 0);
    }
    start = 1;
    CHECK(pthread_cond_broadcast(&condition) == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    for (i = 0; i < 4; i++) {
        CHECK(pthread_join(thread[i], &result) == 0 && result == NULL);
    }
    CHECK(count == 8000 && destroyed == 4 && tls == 0);
    CHECK(pthread_key_delete(key) == 0);
    return 0;
}

static volatile sig_atomic_t signal_seen, signal_on_stack;
static uintptr_t stack_begin, stack_end;

static void handler(int sig)
{
    volatile char local;
    uintptr_t address = (uintptr_t)&local;
    signal_seen = sig;
    signal_on_stack = address >= stack_begin && address < stack_end;
}

static int signals(void)
{
    struct sigaction action = { 0 }, old;
    stack_t alternate, previous;
    sigset_t blocked, saved;
    int sig;
    alternate.ss_sp = malloc(65536);
    CHECK(alternate.ss_sp != NULL);
    alternate.ss_size = 65536;
    alternate.ss_flags = 0;
    stack_begin = (uintptr_t)alternate.ss_sp;
    stack_end = stack_begin + alternate.ss_size;
    CHECK(sigaltstack(&alternate, &previous) == 0);
    action.sa_handler = handler;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &old) == 0);
    CHECK(raise(SIGUSR1) == 0);
    CHECK(signal_seen == SIGUSR1 && signal_on_stack);
    CHECK(sigaction(SIGUSR1, &old, NULL) == 0);
    CHECK(sigaltstack(&previous, NULL) == 0);
    free(alternate.ss_sp);
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR2);
    CHECK(pthread_sigmask(SIG_BLOCK, &blocked, &saved) == 0);
    CHECK(raise(SIGUSR2) == 0);
    CHECK(sigwait(&blocked, &sig) == 0 && sig == SIGUSR2);
    CHECK(pthread_sigmask(SIG_SETMASK, &saved, NULL) == 0);
    return 0;
}

static int processes(void)
{
    int fd[2], status;
    char buf[8] = { 0 };
    pid_t pid;
    struct pollfd pfd;
    CHECK(pipe(fd) == 0);
    pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        close(fd[0]);
        _exit(write(fd[1], "arm926", 6) == 6 ? 0 : 1);
    }
    CHECK(close(fd[1]) == 0);
    pfd = (struct pollfd){ .fd = fd[0], .events = POLLIN };
    CHECK(poll(&pfd, 1, 5000) == 1);
    CHECK(read(fd[0], buf, 6) == 6 && strcmp(buf, "arm926") == 0);
    CHECK(close(fd[0]) == 0);
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}

static int clocks(void)
{
    struct timespec before, after, delay = { .tv_nsec = 20000000 }, res;
    int64_t elapsed;
    CHECK(clock_getres(CLOCK_MONOTONIC, &res) == 0);
    CHECK(res.tv_sec >= 0 && res.tv_nsec >= 0 && res.tv_nsec < 1000000000);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    CHECK(nanosleep(&delay, NULL) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    elapsed = (int64_t)(after.tv_sec - before.tv_sec) * 1000000000 +
        after.tv_nsec - before.tv_nsec;
    CHECK(elapsed >= delay.tv_nsec);
    printf("LIBC_CLOCK resolution_ns=%lld elapsed_ns=%lld\n",
           (long long)res.tv_sec * 1000000000 + res.tv_nsec,
           (long long)elapsed);
    return 0;
}

static int events(void)
{
    struct epoll_event event = { .events = EPOLLIN }, received;
    struct itimerspec timer = { .it_value = { .tv_nsec = 10000000 } };
    uint64_t value = 7;
    int efd = eventfd(0, EFD_CLOEXEC);
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    CHECK(efd >= 0 && ep >= 0 && tfd >= 0);
    event.data.fd = efd;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, efd, &event) == 0);
    CHECK(write(efd, &value, sizeof(value)) == sizeof(value));
    CHECK(epoll_wait(ep, &received, 1, 5000) == 1);
    CHECK(received.data.fd == efd && (received.events & EPOLLIN));
    CHECK(read(efd, &value, sizeof(value)) == sizeof(value) && value == 7);
    CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, efd, NULL) == 0);
    event.data.fd = tfd;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &event) == 0);
    CHECK(timerfd_settime(tfd, 0, &timer, NULL) == 0);
    CHECK(epoll_wait(ep, &received, 1, 5000) == 1);
    CHECK(received.data.fd == tfd && (received.events & EPOLLIN));
    CHECK(read(tfd, &value, sizeof(value)) == sizeof(value) && value >= 1);
    CHECK(close(tfd) == 0 && close(efd) == 0 && close(ep) == 0);
    return 0;
}

static int files_and_time(void)
{
    char name[] = "/tmp/libc-contract-XXXXXX", value;
    const off_t offset = (off_t)5 * 1024 * 1024 * 1024;
    struct stat st;
    struct tm tm;
    struct timespec times[2];
    time_t future = sizeof(time_t) == 8 ? (time_t)2208988800LL : 2114380800;
    int fd = mkstemp(name);
    CHECK(fd >= 0 && unlink(name) == 0);
    CHECK(sizeof(off_t) == 8);
    CHECK(pwrite(fd, "X", 1, offset) == 1);
    CHECK(pread(fd, &value, 1, offset) == 1 && value == 'X');
    CHECK(fstat(fd, &st) == 0 && st.st_size == offset + 1);
    CHECK(gmtime_r(&future, &tm) == &tm);
    CHECK(tm.tm_year == (sizeof(time_t) == 8 ? 140 : 137));
    times[0] = times[1] = (struct timespec){ future, 123456789 };
    CHECK(futimens(fd, times) == 0);
    CHECK(fstat(fd, &st) == 0);
    CHECK(st.st_mtim.tv_sec == future && st.st_mtim.tv_nsec == 123456789);
    CHECK(close(fd) == 0);
    printf("LIBC_ABI time_bits=%u off_bits=%u future=%lld\n",
           (unsigned)(sizeof(time_t) * 8), (unsigned)(sizeof(off_t) * 8),
           (long long)future);
    return 0;
}

static int random_bytes(void)
{
    unsigned char bytes[32];
    ssize_t n;
    do {
        n = getrandom(bytes, sizeof(bytes), 0);
    } while (n == -1 && errno == EINTR);
    CHECK(n == sizeof(bytes));
    return 0;
}

int main(void)
{
    static const struct {
        const char *name;
        int (*run)(void);
    } tests[] = {
        { "memory", memory }, { "allocation", allocation },
        { "strings", strings }, { "soft-float", soft_float },
        { "threads-tls", threads }, { "signals", signals },
        { "processes-poll", processes }, { "clocks", clocks },
        { "events-timers", events }, { "files-time", files_and_time },
        { "getrandom", random_bytes },
    };
    size_t i;
    int failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int result = tests[i].run();
        printf("LIBC_CASE %s %s\n", tests[i].name, result ? "FAIL" : "PASS");
        failures += result != 0;
    }
    printf("LIBC_RESULT cases=%u failures=%d\n", (unsigned)i, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
