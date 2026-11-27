#include "signal_manager.h"
#include "process_manager.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
const char *log_path;
int exit_code;
int reports_served;
} watchdog_ctx_t;

static watchdog_ctx_t *ctx_create(int argc, char *argv[])
{
watchdog_ctx_t *ctx = (watchdog_ctx_t *) malloc(sizeof(watchdog_ctx_t));

    if (ctx == NULL) {
        fprintf(stderr, "main: malloc failed\n");
        return NULL;
    }

    memset(ctx, 0, sizeof(watchdog_ctx_t));
    if (argc > 1)
        ctx->log_path = argv[1];
    else
        ctx->log_path = DEFAULT_LOG_FILE;
    ctx->exit_code = 0;
    ctx->reports_served = 0;
    return ctx;
}

static void ctx_destroy(watchdog_ctx_t **ctx)
{
if (ctx == NULL || *ctx == NULL)
return;

    free(*ctx);
    *ctx = NULL;
}

static int finish(watchdog_ctx_t **ctx)
{
int code = (*ctx)->exit_code;

    ctx_destroy(ctx);
    return code;
}

static int snapshot_children(child_info_t **snap, int *count)
{
const child_info_t *src;
int n = 0;
int i;

    *snap = NULL;
    *count = 0;

    src = process_manager_get_children(&n);
    if (src == NULL || n <= 0)
        return 0;

    *snap = (child_info_t *) malloc(n * sizeof(child_info_t));
    if (*snap == NULL)
        return -1;

    for (i = 0; i < n; i++)
        (*snap)[i] = src[i];

    *count = n;
    return 0;
}

static void log_status(void)
{
child_info_t *snap = NULL;
int count = 0;
const child_info_t *live;

    if (snapshot_children(&snap, &count) == 0) {
        logger_report_status(snap, count);
        free(snap);
        snap = NULL;
    } else {
        live = process_manager_get_children(&count);
        logger_log_event("WARNING: snapshot malloc failed, reporting from live table");
        logger_report_status(live, count);
    }
}

static void serve_report(watchdog_ctx_t *ctx)
{
signal_manager_reset_report();
log_status();
ctx->reports_served++;
}

static int start_watchdog(watchdog_ctx_t *ctx, int argc, char *argv[])
{
if (signal_manager_init() == -1) {
perror("signal_manager_init");
return -1;
}

    if (signal_manager_block_signals() == -1) {
        perror("signal_manager_block_signals");
        return -1;
    }

    if (logger_init(ctx->log_path) == -1)
        return -1;

    if (process_manager_init(argc, argv) == -1) {
        logger_log_event("ERROR: process_manager_init failed");
        logger_close();
        return -1;
    }

    if (process_manager_start_all() == -1) {
        logger_log_event("ERROR: no child process could be started");
        logger_close();
        return -1;
    }

    logger_log_event("WATCHDOG_STARTED | watchdog_pid=%d running_children=%d",
                     (int) getpid(), process_manager_running_count());

    printf("Watchdog pid=%d (report: kill -USR1 %d | stop: Ctrl-C or kill %d)\n",
           (int) getpid(), (int) getpid(), (int) getpid());
    fflush(stdout);
    return 0;
}

static void run_loop(watchdog_ctx_t *ctx)
{
while (!signal_manager_is_shutdown_requested()) {
signal_manager_wait();

        if (signal_manager_is_shutdown_requested())
            break;

        process_manager_handle_children();
        process_manager_periodic_check();

        if (signal_manager_is_report_requested())
            serve_report(ctx);

        if (process_manager_running_count() == 0) {
            logger_log_event("NO_RUNNING_CHILDREN | exiting (reports_served=%d)",
                             ctx->reports_served);
            break;
        }
    }
}

static void stop_watchdog(watchdog_ctx_t *ctx)
{
if (signal_manager_is_shutdown_requested())
logger_log_event("SHUTDOWN_REQUESTED | SIGINT/SIGTERM received, running_children=%d",
process_manager_running_count());

    process_manager_shutdown_all();

    logger_log_event("SHUTDOWN_COMPLETE | all children stopped, reports_served=%d",
                     ctx->reports_served);
    logger_close();
}

int main(int argc, char *argv[])
{
watchdog_ctx_t *ctx = ctx_create(argc, argv);

    if (ctx == NULL)
        return 1;

    if (start_watchdog(ctx, argc, argv) == -1) {
        ctx->exit_code = 1;
        return finish(&ctx);
    }

    run_loop(ctx);
    stop_watchdog(ctx);

    return finish(&ctx);
}