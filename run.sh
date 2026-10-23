#!/bin/bash
# usage: ./run.sh <name> [watchdog s] [kill after s]  -- run, then summarise <name>.log
# The switches a run needs are built-in defaults (src/main.c ar_default_env).
# Redirect through the shell: PowerShell's Start-Process redirection lost
# output mid-line on this exe. Git Bash's timeout leaves the exe running, so a
# timed taskkill ends it instead.
cd "$(dirname "$0")"
RECOMP_WATCHDOG_SECS=${2:-30} ./build/Release/aggro_royale.exe > $1.log 2>&1 &
PID=$!
( sleep ${3:-60}; taskkill //F //IM aggro_royale.exe >/dev/null 2>&1 ) &
KILLER=$!
wait $PID; echo "exit=$?"
kill $KILLER 2>/dev/null
grep -oE 'Failed to resolve VA 0x[0-9A-F]+' $1.log | sort | uniq -c | head
grep -E '\[CRASH\]|\[UNIMPL\]|\[STUB\]|HalReturnToFirmware' $1.log | sort | uniq -c | sort -rn | head -12
grep -A6 '\[WATCHDOG\]' $1.log | head -8
