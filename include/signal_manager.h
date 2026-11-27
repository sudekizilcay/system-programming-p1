
#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H


int signal_manager_init(void);

int signal_manager_block_signals(void);

void signal_manager_unblock_signals(void);

void signal_manager_wait(void);

int signal_manager_is_shutdown_requested(void);

int signal_manager_is_report_requested(void);

void signal_manager_reset_report(void);

int signal_manager_is_child_event_pending(void);
void signal_manager_reset_child_event(void);

int signal_manager_is_alarm_pending(void);
void signal_manager_reset_alarm(void);

#endif
