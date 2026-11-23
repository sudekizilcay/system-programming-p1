#define _POSIX_C_SOURCE 200809L
#include "signal_manager.h"

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t shutdown_requested = 0;
static volatile sig_atomic_t report_requested = 0;

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

    if (install_handler(SIGINT, handle_shutdown) == -1)
        return -1;

    if (install_handler(SIGTERM, handle_shutdown) == -1)
        return -1;

    if (install_handler(SIGUSR1, handle_report) == -1)
        return -1;

    return 0;
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