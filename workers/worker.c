#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void handle_stop(int signum)
{
    (void)signum;
    stop_requested = 1;
}

static int install_handler(int signum, void (*handler)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(signum, &sa, NULL);
}

static int run_ticks(const char *name, int secs)
{
    int elapsed = 0;

    while (!stop_requested) {
        if (secs > 0 && elapsed >= secs)
            return 1;
        printf("[%s pid=%d] running... %d sn\n", name, (int)getpid(), elapsed);
        fflush(stdout);
        sleep(1);
        elapsed++;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *name, *mode;
    char *end;
    long secs;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <name> <mode> <seconds>\n", argv[0]);
        return 2;
    }
    name = argv[1];
    mode = argv[2];

    errno = 0;
    secs = strtol(argv[3], &end, 10);
    if (errno != 0 || *end != '\0' || secs < 0 || secs > 86400) {
        fprintf(stderr, "%s: invalid seconds value: %s\n", name, argv[3]);
        return 2;
    }

    if (install_handler(SIGTERM, handle_stop) == -1 ||
        install_handler(SIGINT,  handle_stop) == -1) {
        perror("sigaction");
        return 1;
    }

    printf("[%s pid=%d] started (mod=%s, duration=%ld)\n", name, (int)getpid(), mode, secs);
    fflush(stdout);

    if (strcmp(mode, "loop") == 0) {
        run_ticks(name, 0);
        printf("[%s pid=%d] signal received, clean exit\n", name, (int)getpid());
        fflush(stdout);
        return 0;
    }

    if (strcmp(mode, "exit") == 0) {
        if (run_ticks(name, (int)secs))
            printf("[%s pid=%d] task finished, normal exit\n", name, (int)getpid());
        else
            printf("[%s pid=%d] signal received, clean exit\n", name, (int)getpid());
        fflush(stdout);
        return 0;
    }

    if (strcmp(mode, "crash") == 0) {
        if (!run_ticks(name, (int)secs)) {
            printf("[%s pid=%d] signal received, clean exit\n", name, (int)getpid());
            fflush(stdout);
            return 0;
        }
        printf("[%s pid=%d] crashing intentionally (SIGABRT)\n", name, (int)getpid());
        fflush(stdout);

        struct rlimit rl = { 0, 0 };
        setrlimit(RLIMIT_CORE, &rl);

        abort();
    }

    fprintf(stderr, "%s: unknown mode: %s\n", name, mode);
    return 2;
}