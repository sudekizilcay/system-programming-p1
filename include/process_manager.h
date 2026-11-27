#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>
#include <time.h>

#define MAX_CHILDREN 8
#define MAX_RESTARTS 3
#define CHECK_INTERVAL_SEC 5
#define SHUTDOWN_TIMEOUT_SEC 5

typedef enum{
    CS_UNUSED = 0,
    CS_RUNNING,
    CS_EXITED_NORMAL,
    CS_FAILED,
    CS_RESTART_LIMIT,
    CS_STOPPED
} child_state_t;

typedef struct{
    char name[32];
    char path[128];
    char mode[16];
    char secs[16];
    pid_t pid;
    child_state_t state;
    int restart_count;
    time_t start_time;
} child_info_t;

int process_manager_init(int argc,char *argv[]);

int process_manager_start_all(void);

void process_manager_handle_children(void);

void process_manager_periodic_check(void);

void process_manager_shutdown_all(void);

const child_info_t *process_manager_get_children(int *count);
const char *process_manager_state_name(child_state_t s);

int process_manager_running_count(void);

#endif 