#define _POSIX_C_SOURCE 200809L
#include "process_manager.h"
#include "signal_manager.h"
#include "logger.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WORKER_PATH "./workers/worker"

static child_info_t children[MAX_CHILDREN];
static int          child_count = 0;
static int          shutting_down = 0;


static int add_child(const char *name, const char *mode, int secs)
{
    if (child_count >= MAX_CHILDREN)
        return -1;
    child_info_t *c = &children[child_count];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    snprintf(c->path, sizeof c->path, "%s", WORKER_PATH);
    snprintf(c->mode, sizeof c->mode, "%s", mode);
    snprintf(c->secs, sizeof c->secs, "%d", secs);
    c->state = CS_UNUSED;
    return child_count++;
}

int process_manager_init(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    add_child("worker1", "loop",  0);
    add_child("worker2", "exit",  5);
    add_child("worker3", "crash", 3);
    return 0;
}

static int spawn(child_info_t *c)
{
    pid_t pid = fork();
    if (pid < 0) {
        logger_log_event("ERROR: fork failed for %s: %s", c->name, strerror(errno));
        c->state = CS_FAILED;
        return -1;
    }
    if (pid == 0) {
        signal_manager_unblock_signals();

        execl(c->path, c->path, c->name, c->mode, c->secs, (char *)NULL);
        perror(c->path);
        _exit(127);
    }
    c->pid = pid;
    c->state = CS_RUNNING;
    c->start_time = time(NULL);
    return 0;
}

static void start_timer(int seconds)
{
    alarm(seconds);
}

static void stop_timer(void)
{
    alarm(0);
}

int process_manager_start_all(void)
{
    int ok = 0;
    for (int i = 0; i < child_count; i++) {
        if (spawn(&children[i]) == 0) {
            logger_log_event("Started child %s (pid %d, mode=%s)",
                             children[i].name, (int)children[i].pid, children[i].mode);
            ok++;
        }
    }
    if (ok == 0)
        return -1;
    start_timer(CHECK_INTERVAL_SEC);
    return ok;
}

static void handle_exit(child_info_t *c, int status)
{
    pid_t old_pid = c->pid;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        logger_log_event("Child %s (pid %d) exited normally (status 0)", c->name, (int)old_pid);
        c->state = CS_EXITED_NORMAL;
        return;
    }

    if (WIFEXITED(status))
        logger_log_event("Child %s (pid %d) exited with error code %d",
                         c->name, (int)old_pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        logger_log_event("Child %s (pid %d) terminated by signal %d (%s)",
                         c->name, (int)old_pid, WTERMSIG(status), strsignal(WTERMSIG(status)));

    if (shutting_down) {
        c->state = CS_STOPPED;
        return;
    }

    if (c->restart_count < MAX_RESTARTS) {
        c->restart_count++;
        logger_log_event("Restarting %s (attempt %d/%d)", c->name, c->restart_count, MAX_RESTARTS);
        if (spawn(c) == 0)
            logger_log_event("Restarted %s with new pid %d", c->name, (int)c->pid);
    } else {
        c->state = CS_RESTART_LIMIT;
        logger_log_event("Restart limit (%d) reached for %s; it will NOT be restarted again",
                         MAX_RESTARTS, c->name);
    }
}

static void reap_children(void)
{
    int   status;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            int found = 0;
            for (int i = 0; i < child_count; i++) {
                if (children[i].state == CS_RUNNING && children[i].pid == pid) {
                    handle_exit(&children[i], status);
                    found = 1;
                    break;
                }
            }
            if (!found)
                logger_log_event("Reaped unknown child pid %d", (int)pid);
        } else if (pid < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

void process_manager_handle_children(void)
{
    if (!signal_manager_is_child_event_pending())
        return;
    signal_manager_reset_child_event();
    reap_children();
}

void process_manager_periodic_check(void)
{
    if (!signal_manager_is_alarm_pending())
        return;
    signal_manager_reset_alarm();

    if (!shutting_down)
        start_timer(CHECK_INTERVAL_SEC);

    reap_children();
    for (int i = 0; i < child_count; i++) {
        child_info_t *c = &children[i];
        if (c->state == CS_RUNNING && kill(c->pid, 0) < 0 && errno == ESRCH) {
            logger_log_event("WARNING: %s (pid %d) is marked running but does not exist",
                             c->name, (int)c->pid);
            c->state = CS_FAILED;
        }
    }
}

int process_manager_running_count(void)
{
    int n = 0;
    for (int i = 0; i < child_count; i++)
        if (children[i].state == CS_RUNNING)
            n++;
    return n;
}

void process_manager_shutdown_all(void)
{
    shutting_down = 1;
    stop_timer();

    for (int i = 0; i < child_count; i++) {
        child_info_t *c = &children[i];
        if (c->state == CS_RUNNING && kill(c->pid, SIGTERM) == 0)
            logger_log_event("Sent SIGTERM to %s (pid %d)", c->name, (int)c->pid);
    }

    for (int t = 0; t < SHUTDOWN_TIMEOUT_SEC; t++) {
        reap_children();
        if (process_manager_running_count() == 0)
            break;
        sleep(1);
    }
    reap_children();

    for (int i = 0; i < child_count; i++) {
        child_info_t *c = &children[i];
        if (c->state != CS_RUNNING)
            continue;
        logger_log_event("%s (pid %d) did not stop within %ds, sending SIGKILL",
                         c->name, (int)c->pid, SHUTDOWN_TIMEOUT_SEC);
        kill(c->pid, SIGKILL);
        int status;
        while (waitpid(c->pid, &status, 0) < 0 && errno == EINTR) { }
        logger_log_event("Child %s (pid %d) killed and reaped", c->name, (int)c->pid);
        c->state = CS_STOPPED;
    }
}

const child_info_t *process_manager_get_children(int *count)
{
    if (count)
        *count = child_count;
    return children;
}

const char *process_manager_state_name(child_state_t s)
{
    switch (s) {
    case CS_RUNNING:       return "RUNNING";
    case CS_EXITED_NORMAL: return "EXITED_NORMALLY";
    case CS_FAILED:        return "FAILED";
    case CS_RESTART_LIMIT: return "RESTART_LIMIT_REACHED";
    case CS_STOPPED:       return "STOPPED";
    default:               return "NOT_STARTED";
    }
}
