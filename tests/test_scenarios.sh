cd "$(dirname "$0")/.." || exit 1

WATCHDOG=./watchdog
LOG=$(mktemp /tmp/watchdog_test.XXXXXX.log)
PASS=0
FAIL=0
WD_PID=""

cleanup() {
    if [ -n "$WD_PID" ] && kill -0 "$WD_PID" 2>/dev/null; then
        kill -KILL "$WD_PID" 2>/dev/null
    fi
    pkill -f "^\./workers/worker " 2>/dev/null
    rm -f "$LOG"
}
trap cleanup EXIT

ok()   { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }

check() {
    if grep -q -- "$2" "$LOG"; then ok "$1"; else fail "$1   (pattern: $2)"; fi
}

check_not() {
    if grep -q -- "$2" "$LOG"; then fail "$1   (unexpected: $2)"; else ok "$1"; fi
}

check_count() {
    local n
    n=$(grep -c -- "$2" "$LOG")
    if [ "$n" -eq "$3" ]; then ok "$1"; else fail "$1   (expected $3, got $n)"; fi
}

worker_pid() {
    grep -m1 "Started child $1 " "$LOG" | sed -E 's/.*\(pid ([0-9]+).*/\1/'
}

wait_exit() {
    local i=0
    while kill -0 "$1" 2>/dev/null && [ "$i" -lt "$2" ]; do
        sleep 1
        i=$((i + 1))
    done
    ! kill -0 "$1" 2>/dev/null
}

start_watchdog() {
    rm -f "$LOG"
    "$WATCHDOG" "$LOG" > /dev/null 2>&1 &
    WD_PID=$!
    sleep 1
}

if [ ! -x "$WATCHDOG" ] || [ ! -x workers/worker ]; then
    echo "Missing binary, running make..."
    make > /dev/null || { echo "make failed"; exit 1; }
fi

echo "=== Scenarios 1-5: startup, external kill, crash, report, shutdown ==="
start_watchdog

check "S1: watchdog started"                "WATCHDOG_STARTED"
check "S1: worker1 started"                 "Started child worker1"
check "S1: worker2 started"                 "Started child worker2"
check "S1: worker3 started"                 "Started child worker3"

W1_PID=$(worker_pid worker1)
if [ -n "$W1_PID" ]; then
    kill -KILL "$W1_PID" 2>/dev/null
    sleep 2
    check "S2: worker1 killed by SIGKILL (signal 9)" "worker1 (pid $W1_PID) terminated by signal 9"
    check "S2: worker1 restarted"                    "Restarting worker1"
else
    fail "S2: could not read worker1 PID from log"
fi

echo "  ... waiting for worker2 and worker3 (~13s)"
sleep 11
check     "S3: worker2 exited normally"          "Child worker2 (pid [0-9]*) exited normally"
check_not "S3: worker2 not restarted"            "Restarting worker2"

check_count "S4: worker3 restarted 3 times"      "Restarting worker3" 3
check       "S4: worker3 crashed with SIGABRT"     "worker3 (pid [0-9]*) terminated by signal 6"
check       "S4: worker3 hit restart limit"        "Restart limit (3) reached for worker3"

kill -USR1 "$WD_PID"
sleep 1
check "S5: SIGUSR1 report start"    "SIGUSR1_REPORT begin"
check "S5: SIGUSR1 report end"      "SIGUSR1_REPORT end"

kill -TERM "$WD_PID"
if wait_exit "$WD_PID" 10; then
    ok "S6: watchdog exited after SIGTERM"
else
    fail "S6: watchdog did not exit within 10s"
fi
check "S6: shutdown signal logged"    "SHUTDOWN_REQUESTED"
check "S6: SIGTERM sent to workers"   "Sent SIGTERM to"
check "S6: shutdown complete logged"  "SHUTDOWN_COMPLETE"

if pgrep -f "^\./workers/worker " > /dev/null; then
    fail "S6: leftover worker processes found"
else
    ok "S6: no leftover worker processes"
fi
WD_PID=""

echo
echo "=== Scenario 7: SIGINT (Ctrl-C) shutdown ==="
start_watchdog
kill -INT "$WD_PID"
if wait_exit "$WD_PID" 10; then
    ok "S7: watchdog exited after SIGINT"
else
    fail "S7: watchdog did not exit within 10s"
fi
check "S7: shutdown signal logged"   "SHUTDOWN_REQUESTED"
check "S7: shutdown complete logged" "SHUTDOWN_COMPLETE"
if pgrep -f "^\./workers/worker " > /dev/null; then
    fail "S7: leftover worker processes found"
else
    ok "S7: no leftover worker processes"
fi
WD_PID=""
echo
echo "=== Result: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
