#define _POSIX_C_SOURCE 200809L
#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *log_fp = NULL;

static int format_timestamp(char *buf, size_t size)
{
    struct timespec ts;
    struct tm tm_info;
    size_t len;

    if (!buf || size < 32) {
        return -1;
    }

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }

    if (localtime_r(&ts.tv_sec, &tm_info) == NULL) {
        return -1;
    }

    len = strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);
    if (len == 0 || len + 8 >= size) {
        return -1;
    }

    snprintf(buf + len, size - len, ".%03ld", ts.tv_nsec / 1000000L);
    return 0;
}

static void write_log_line(const char *level, const char *fmt, va_list args)
{
    char ts_buf[64];
    char msg_buf[512];
    va_list copy;

    if (!log_fp || !fmt) {
        return;
    }

    if (format_timestamp(ts_buf, sizeof(ts_buf)) != 0) {
        snprintf(ts_buf, sizeof(ts_buf), "0000-00-00 00:00:00.000");
    }

    va_copy(copy, args);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, copy);
    va_end(copy);

    fprintf(log_fp, "[%s] [%s] %s\n", ts_buf, level ? level : "INFO", msg_buf);
    fflush(log_fp);
}

static void write_log_line_fmt(const char *level, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    write_log_line(level, fmt, args);
    va_end(args);
}

int logger_init(const char *log_path)
{
    const char *path = (log_path && log_path[0] != '\0') ? log_path : DEFAULT_LOG_FILE;

    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }

    log_fp = fopen(path, "a");
    if (!log_fp) {
        perror("logger_init: fopen failed");
        return -1;
    }

    setvbuf(log_fp, NULL, _IOLBF, 0);
    write_log_line_fmt("EVENT", "WATCHDOG_INIT log_file=%s pid=%d", path, (int)getpid());
    return 0;
}

void logger_close(void)
{
    if (!log_fp) {
        return;
    }

    write_log_line_fmt("EVENT", "WATCHDOG_CLOSE pid=%d", (int)getpid());
    fflush(log_fp);
    fclose(log_fp);
    log_fp = NULL;
}

void logger_log_event(const char *fmt, ...)
{
    va_list args;

    if (!log_fp || !fmt) {
        return;
    }

    va_start(args, fmt);
    write_log_line("EVENT", fmt, args);
    va_end(args);
}

void logger_report_status(const child_info_t *children, int count)
{
    int i;
    int reported = 0;

    if (!log_fp) {
        return;
    }

    write_log_line_fmt("STATUS", "SIGUSR1_REPORT begin total_entries=%d", count);

    if (!children || count <= 0) {
        write_log_line_fmt("STATUS", "SIGUSR1_REPORT no child records");
        write_log_line_fmt("STATUS", "SIGUSR1_REPORT end reported=0");
        return;
    }

    for (i = 0; i < count; i++) {
        if (children[i].state == CS_UNUSED) {
            continue;
        }

        if (children[i].state == CS_RUNNING) {
            write_log_line_fmt(
                "STATUS",
                "child[%d] name=%s pid=%d state=%s restarts=%d/%d",
                i,
                children[i].name[0] ? children[i].name : "worker",
                (int)children[i].pid,
                process_manager_state_name(children[i].state),
                children[i].restart_count,
                MAX_RESTARTS
            );
            reported++;
        } else {
            write_log_line_fmt(
                "STATUS_OTHER",
                "child[%d] name=%s pid=%d state=%s restarts=%d/%d",
                i,
                children[i].name[0] ? children[i].name : "worker",
                (int)children[i].pid,
                process_manager_state_name(children[i].state),
                children[i].restart_count,
                MAX_RESTARTS
            );
        }
    }

    write_log_line_fmt("STATUS", "SIGUSR1_REPORT end running_reported=%d", reported);
}
