#!/bin/bash
# ./run.sh <name> [watchdog secs] [kill after secs]
#
# Runs the dev build with its output in logs/<name>.log, then prints the
# interesting bits: unresolved addresses, crashes, stubs it hit, and the
# watchdog's report if the game stopped responding. The switches a run needs
# are built into the exe (src/main.c, ar_default_env).
#
# Two gotchas this works around: PowerShell's Start-Process redirection
# drops output mid-line on this exe, and Git Bash's `timeout` leaves the exe
# running, so the run is ended with taskkill instead.
cd "$(dirname "$0")"
mkdir -p logs
LOG=logs/$1.log
RECOMP_WATCHDOG_SECS=${2:-30} ./build/Release/aggro_royale.exe > $LOG 2>&1 &
PID=$!
( sleep ${3:-60}; taskkill //F //IM aggro_royale.exe >/dev/null 2>&1 ) &
KILLER=$!
wait $PID; echo "exit=$?"
kill $KILLER 2>/dev/null
grep -oE 'Failed to resolve VA 0x[0-9A-F]+' $LOG | sort | uniq -c | head
grep -E '\[CRASH\]|\[UNIMPL\]|\[STUB\]|HalReturnToFirmware' $LOG | sort | uniq -c | sort -rn | head -12
grep -A6 '\[WATCHDOG\]' $LOG | head -8
