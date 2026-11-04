#!/bin/sh
# End-to-end test: real kbdd + kbctl over the Unix socket.
# Hardware-independent: it passes with or without a keyboard attached and
# never sends a command that would reach a device.
# Usage: tests/integration/test_e2e.sh [bin-dir]   (default: build/bin)
set -u

BIN=${1:-build/bin}
KBDD="$BIN/kbdd"
KBCTL="$BIN/kbctl"
failures=0
pid=

fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

# check_rc <expected-rc> <expected-substring> -- <kbctl args...>
check_rc() {
    want_rc=$1; want_txt=$2; shift 3
    out=$("$KBCTL" "$@" 2>&1); rc=$?
    [ "$rc" -eq "$want_rc" ] || fail "kbctl $*: rc=$rc, expected $want_rc"
    case $out in
        *"$want_txt"*) ;;
        *) fail "kbctl $*: output lacks '$want_txt'" ;;
    esac
}

cleanup() {
    [ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null
    rm -rf "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME"
}

XDG_RUNTIME_DIR=$(mktemp -d) || exit 1
XDG_CONFIG_HOME=$(mktemp -d) || exit 1
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR XDG_CONFIG_HOME
trap cleanup EXIT INT TERM
sock="$XDG_RUNTIME_DIR/kbdd.sock"

# 1. Without a daemon the client must fail cleanly (rc 1), not hang or crash.
check_rc 1 "cannot connect to kbdd" -- status

# 2. Start the daemon with all write gates closed.
env -u KBDD_ENABLE_LED -u KBDD_ENABLE_PROFILE_WRITE "$KBDD" >"$XDG_RUNTIME_DIR/kbdd.log" 2>&1 &
pid=$!
i=0
while [ ! -S "$sock" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -S "$sock" ] || { fail "kbdd did not create $sock"; exit 1; }

# 3. Socket must be owner-only.
mode=$(stat -c %a "$sock" 2>/dev/null || stat -f %Lp "$sock")
[ "$mode" = "600" ] || fail "socket mode is $mode, expected 600"

# 4. Read-only commands.
check_rc 0 "hid interface"               -- status
check_rc 0 "libkbcore"                -- version
check_rc 0 ""                           -- list
check_rc 0 "config:"                  -- read-config
check_rc 0 "esc"                      -- led keys
check_rc 0 "effect"                   -- led effects
check_rc 0 "Usage:"                   -- help

# 5. Argument validation happens client-side (rc 2).
check_rc 2 "unknown command"          -- bogus
check_rc 2 "profile <1-4>"            -- profile 9
check_rc 2 "invalid profile"          -- read-profile 7
check_rc 2 "needs a colour"           -- led solid zz
check_rc 2 "led brightness"           -- led brightness 11
check_rc 2 "Usage:"                   -- status extra

# 6. 'led custom' argument validation is client-side too (rc 2).
check_rc 2 "led custom [--base"      -- led custom
check_rc 2 "led custom [--base"      -- led custom esc
check_rc 2 "unknown key"             -- led custom nope=ff0000
check_rc 2 "led custom [--base"      -- led custom ,=ff0000
check_rc 2 "led custom [--base"      -- led custom --base ff0000 --base 00ff00 esc=ff0000
check_rc 2 "led custom [--base"      -- led custom esc=ff0000 --base ff0000
check_rc 2 "led custom [--base"      -- led custom clear extra

# 7. Gated writes are refused by the daemon (rc 1) and never reach a device.
check_rc 1 "LED control is disabled"        -- led solid ff0000
check_rc 1 "profile switching is disabled"  -- profile 1
check_rc 1 "cannot read"                    -- restore-profile 1 /nonexistent/file
# These go through profile_write(), which first reads the active profile; on a
# machine without a keyboard the daemon rejects that read, so only the rc=1
# (and an error message) is asserted to keep the suite hardware-independent.
check_rc 1 "error:"                       -- led custom esc=ff0000
check_rc 1 "error:"                       -- led custom clear

# 'led effect 13/14' are reserved and rejected client-side; 15 re-selects the
# stored custom layout.
check_rc 2 "led effect <name|index>"       -- led effect 13
check_rc 2 "led effect <name|index>"       -- led effect 14
check_rc 1 "error:"                       -- led effect 15

# 8. Daemon survived all of the above and shuts down cleanly on SIGTERM.
kill -0 "$pid" 2>/dev/null || fail "kbdd died during the test"
kill -TERM "$pid" 2>/dev/null
wait "$pid"; rc=$?
pid=
[ "$rc" -eq 0 ] || fail "kbdd exit status $rc after SIGTERM, expected 0"
[ ! -S "$sock" ] || fail "socket not removed on shutdown"

if [ "$failures" -ne 0 ]; then
    echo "$failures e2e check(s) failed" >&2
    exit 1
fi
echo "ok: all e2e tests"
