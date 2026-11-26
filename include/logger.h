#ifndef LOGGER_H
#define LOGGER_H

#include "process_manager.h"

#define DEFAULT_LOG_FILE "watchdog.log"

int logger_init(const char *log_path);
void logger_close(void);

#if defined(__GNUC__) || defined(__clang__)
void logger_log_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void logger_log_event(const char *fmt, ...);
#endif

void logger_log_child_event(const char *event, const child_info_t *child, int status_or_sig);
void logger_report_status(const child_info_t *children, int count);

#endif
