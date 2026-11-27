#define _POSIX_C_SOURCE 200809L
#include "signal_manager.h"

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t report_requested = 0;
static volatile sig_atomic_t child_event_pending = 0;
static volatile sig_atomic_t alarm_pending = 0;

static sigset_t orig_mask;
static int signals_blocked = 0;

static void handle_shutdown(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static void handle_report(int signal_number)
{
    (void)signal_number;
    report_requested = 1;
}

static void handle_child(int signal_number)
{
    (void)signal_number;
    child_event_pending = 1;
}

static void handle_alarm(int signal_number)
{
    (void)signal_number;
    alarm_pending = 1;
}

static int install_handler(int signal_number, void (*handler)(int))
{
    struct sigaction action = {0};

    action.sa_handler = handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    return sigaction(signal_number, &action, NULL);
}

int signal_manager_init(void)
{
    shutdown_requested = 0;
    report_requested = 0;
    child_event_pending = 0;
    alarm_pending = 0;

    if (install_handler(SIGINT, handle_shutdown) == -1)
        return -1;

    if (install_handler(SIGTERM, handle_shutdown) == -1)
        return -1;

    if (install_handler(SIGUSR1, handle_report) == -1)
        return -1;

    if (install_handler(SIGCHLD, handle_child) == -1)
        return -1;

    if (install_handler(SIGALRM, handle_alarm) == -1)
        return -1;

    return 0;
}

int signal_manager_block_signals(void)
{
    sigset_t block_mask;

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);
    sigaddset(&block_mask, SIGUSR1);
    sigaddset(&block_mask, SIGCHLD);
    sigaddset(&block_mask, SIGALRM);

    if (sigprocmask(SIG_BLOCK, &block_mask, &orig_mask) == -1)
        return -1;

    signals_blocked = 1;
    return 0;
}

void signal_manager_unblock_signals(void)
{
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigprocmask(SIG_SETMASK, &empty_mask, NULL);
}

void signal_manager_wait(void)
{
    if (shutdown_requested || report_requested || child_event_pending || alarm_pending)
        return;

    if (signals_blocked) {
        sigsuspend(&orig_mask);
    } else {
        sigset_t empty_mask;
        sigemptyset(&empty_mask);
        sigsuspend(&empty_mask);
    }
}

int signal_manager_is_shutdown_requested(void)
{
    return shutdown_requested != 0;
}

int signal_manager_is_report_requested(void)
{
    return report_requested != 0;
}

void signal_manager_reset_report(void)
{
    report_requested = 0;
}

int signal_manager_is_child_event_pending(void)
{
    return child_event_pending != 0;
}

void signal_manager_reset_child_event(void)
{
    child_event_pending = 0;
}

int signal_manager_is_alarm_pending(void)
{
    return alarm_pending != 0;
}

void signal_manager_reset_alarm(void)
{
    alarm_pending = 0;
}