# Watchdog: POSIX Signal-Based Process Monitoring and Management System

Sakarya University, Software Engineering Department
2026-2027 System Programming, Term Project 1

`watchdog` is a supervisor written in C. It starts a set of worker processes,
detects when they terminate, restarts the ones that fail (at most three times),
writes a timestamped log, and shuts everything down cleanly on `SIGINT` or
`SIGTERM`. All control flow is driven by POSIX signals.

## Requirements

- Linux (tested with gcc and GNU make)
- `gcc`, `make`

On Debian/Ubuntu: `sudo apt install build-essential`

## Build

```bash
make
```

This produces two executables:

- `watchdog`: the supervisor
- `workers/worker`: the test worker started by the supervisor

To remove build output:

```bash
make clean
```

## Run

`watchdog` starts its workers from `./workers/worker`, so run it from the
project root directory:

```bash
./watchdog                 # logs to watchdog.log
./watchdog my_run.log      # logs to a file of your choice
```

On start it prints its PID:

```
Watchdog pid=12345 (report: kill -USR1 12345 | stop: Ctrl-C or kill 12345)
```

From a second terminal:

```bash
kill -USR1 <WATCHDOG_PID>      # write a status report to the log
kill -TERM <WATCHDOG_PID>      # graceful shutdown (Ctrl-C in the first terminal does the same)
tail -f watchdog.log           # follow the log
```

To test an unexpected termination, find a worker PID first and check it before
sending the signal:

```bash
ps x | grep "workers/worker"
kill -KILL <WORKER_PID>
```

## What the watchdog does

It starts three workers (defined in `src/process_manager.c`):

| Worker  | Mode    | Behavior                                              |
|---------|---------|-------------------------------------------------------|
| worker1 | `loop`  | Runs until it receives a signal                       |
| worker2 | `exit`  | Runs for 5 seconds, then exits normally (status 0)    |
| worker3 | `crash` | Runs for 3 seconds, then aborts with `SIGABRT`        |

| Event | Watchdog reaction |
|-------|-------------------|
| Child exits with status 0 | Logged as a normal exit; not restarted |
| Child exits with an error code or is killed by a signal | Logged; restarted, up to 3 times |
| Restart limit (3) reached | Logged; the child is not restarted again |
| `SIGUSR1` | PID and state of every child are written to the log |
| `SIGINT` / `SIGTERM` | No new processes are started, `SIGTERM` is sent to all running children, the watchdog waits up to 5 seconds, then sends `SIGKILL` to any child still alive and reaps it |
| No running children left | The watchdog logs this and exits |

Configuration constants are in `include/process_manager.h`:

| Constant | Value | Meaning |
|----------|-------|---------|
| `MAX_CHILDREN` | 8 | Size of the child table |
| `MAX_RESTARTS` | 3 | Restart limit per child |
| `CHECK_INTERVAL_SEC` | 5 | Period of the `SIGALRM` check |
| `SHUTDOWN_TIMEOUT_SEC` | 5 | Wait time before `SIGKILL` |

## Project structure

```
watchdog-project/
├── Makefile
├── README.md
├── include/
│   ├── signal_manager.h
│   ├── process_manager.h
│   └── logger.h
├── src/
│   ├── main.c
│   ├── signal_manager.c
│   ├── process_manager.c
│   └── logger.c
├── workers/
│   └── worker.c
├── tests/
│   └── test_scenarios.sh
└── docs/
    └── REPORT.pdf
```

## Architecture

| Module | Responsibility |
|--------|----------------|
| `signal_manager` | Installs handlers with `sigaction()` for `SIGINT`, `SIGTERM`, `SIGUSR1`, `SIGCHLD`, `SIGALRM`. Handlers only set `volatile sig_atomic_t` flags. Signals are blocked and only delivered inside `sigsuspend()`, so no signal is lost between checking a flag and going to sleep. |
| `process_manager` | `fork()` + `execl()` of workers, `waitpid()` reaping, normal vs. unexpected termination, bounded restart, periodic check with `alarm()`, shutdown sequence (`SIGTERM`, timeout, `SIGKILL`). |
| `logger` | Timestamped log file, event lines, status report. |
| `main` | Start-up order, main control loop, handling of the `SIGUSR1` report, shutdown orchestration. |

Signal handlers are async-signal-safe: they do nothing except set a flag. All
logging, `waitpid()`, `fork()` and memory handling happen in the main control
flow.

Main loop (`src/main.c`):

1. Wait for a signal (`signal_manager_wait()`).
2. If a shutdown was requested, leave the loop.
3. Reap terminated children and apply the restart policy.
4. Run the periodic check.
5. If `SIGUSR1` arrived, write the status report.
6. If no child is running any more, leave the loop.

## Log format

Every line starts with a timestamp:

```
[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message
```

Levels: `EVENT` for events, `STATUS` for running children in a report,
`STATUS_OTHER` for children in any other state.

Sample output (timestamps omitted here for brevity):

```
[EVENT] WATCHDOG_INIT log_file=watchdog.log pid=139
[EVENT] Started child worker1 (pid 141, mode=loop)
[EVENT] Started child worker2 (pid 142, mode=exit)
[EVENT] Started child worker3 (pid 143, mode=crash)
[EVENT] WATCHDOG_STARTED | watchdog_pid=139 running_children=3
[STATUS] SIGUSR1_REPORT begin total_entries=3
[STATUS] child[0] name=worker1 pid=141 state=RUNNING restarts=0/3
[STATUS] child[1] name=worker2 pid=142 state=RUNNING restarts=0/3
[STATUS] child[2] name=worker3 pid=143 state=RUNNING restarts=0/3
[STATUS] SIGUSR1_REPORT end running_reported=3
[EVENT] Child worker1 (pid 141) terminated by signal 9 (Killed)
[EVENT] Restarting worker1 (attempt 1/3)
[EVENT] Restarted worker1 with new pid 150
[EVENT] Child worker3 (pid 143) terminated by signal 6 (Aborted)
[EVENT] Restarting worker3 (attempt 1/3)
[EVENT] Restart limit (3) reached for worker3; it will NOT be restarted again
[EVENT] SHUTDOWN_REQUESTED | SIGINT/SIGTERM received, running_children=3
[EVENT] Sent SIGTERM to worker1 (pid 150)
[EVENT] SHUTDOWN_COMPLETE | all children stopped, reports_served=1
[EVENT] WATCHDOG_CLOSE pid=139
```

## Tests

The automated test script covers the scenarios of the assignment:

```bash
./tests/test_scenarios.sh
```

It runs for about 20 seconds, starts the watchdog with a temporary log file and
checks the log and the process list.

| Scenario | What is checked |
|----------|-----------------|
| S1 Normal operation | Watchdog and all three workers start |
| S2 Unexpected termination | A worker killed with `SIGKILL` is logged and restarted |
| S3 Normal termination | worker2 exits with status 0 and is not restarted |
| S4 Restart limit | worker3 is restarted exactly 3 times, then no more |
| S5 Status reporting | `SIGUSR1` writes the report to the log |
| S6 Graceful shutdown | `SIGTERM` stops the watchdog, children are terminated, no leftover processes |
| S7 Ctrl-C shutdown | `SIGINT` stops the watchdog the same way |

The script prints `[PASS]` / `[FAIL]` for each check and exits with status 0
only if all checks pass.

## Team and task allocation

| Student | GitHub account | Module |
|---------|----------------|--------|
| Student 1: _Sudenaz Kızılçay_ | _@sudekizilcay_ | Signal management and main program: `signal_manager.c/.h`, `main.c` |
| Student 2: _Ebru Çağlar_ | _@ebruclr_ | Child process management: `process_manager.c/.h`, `workers/worker.c` |
| Student 3: _Semih Bulut_ | _@Semih741_ | Logging and test infrastructure: `logger.c/.h`, `Makefile`, `tests/`, `README.md` |

Repository: https://github.com/sudekizilcay/system-programming-p1
