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

# 4b. Offline macro listing: reads local state only, never creates the dir.
macro_dir="$XDG_CONFIG_HOME/kbdd"
mkdir -p "$macro_dir"
check_rc 0 "(no macros saved for any profile)" -- macro list
check_rc 0 "profile 2: no macro saved"        -- macro list --profile 2

# A profile with no saved macro is a successful query (exit 0), and listing
# must not create $XDG_CONFIG_HOME/kbdd when it does not exist yet.
tmpcfg="$XDG_RUNTIME_DIR/macro-empty-config"
mkdir -p "$tmpcfg"
out=$(XDG_CONFIG_HOME="$tmpcfg" "$KBCTL" macro list 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "macro list (no config dir): rc=$rc, expected 0"
[ ! -d "$tmpcfg/kbdd" ] || fail "macro list created kbdd/ in the config directory"
rm -rf "$tmpcfg"

# Valid macro: Fn+1 (position 75), one press of 'c'.
{
    printf '\104\122\103\115\001\000\000\000\113\000\000\000\000\001\000\000'
    printf '\000\001\224\000\006\024\000\006'
    dd if=/dev/zero bs=248 count=1 2>/dev/null
} > "$macro_dir/macro-profile1.bin"
check_rc 0 "fn+1"                   -- macro list --profile 1
check_rc 0 "+0x06 20ms -0x06 20ms"  -- macro list --profile 1

# Wrong magic: present but invalid state is reported, not silently ignored.
printf '\001\000\000\000\001\000\000\000\000\000\000\000\000\001\000\000' \
    > "$macro_dir/macro-profile2.bin"
check_rc 0 "<corrupt>"              -- macro list --profile 2
check_rc 0 "<corrupt>"              -- macro list

# Maximum step count (80) must render fully, not as <render error>.
{
    printf '\104\122\103\115\001\000\000\000\000\000\000\000\000\001\000\000'
    printf '\000\001'
    i=0; while [ "$i" -lt 80 ]; do printf '\224\000\004'; i=$((i + 1)); done
    dd if=/dev/zero bs=14 count=1 2>/dev/null
} > "$macro_dir/macro-profile3.bin"
out=$("$KBCTL" macro list --profile 3 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "macro list (80 steps): rc=$rc, expected 0"
case $out in *"<render error>"*) fail "macro list (80 steps): render error" ;; esac
tokens=$(printf '%s\n' "$out" | tr ' ' '\n' | grep -c '^+0x04$')
[ "$tokens" -eq 80 ] || fail "macro list (80 steps): rendered $tokens steps, expected 80"

# Multiple macros per profile with auto-incremented slots.
# Slot 1 (legacy macro-profile4.bin): Fn+1 = position 75, macro +c (0x06).
# Slot 2 (macro-profile4-002.bin): Fn+2 = position 80, macro +d (0x07).
python3 -c "
import struct, os
macro_dir = '$macro_dir'
slot1 = struct.pack('<IIII', 0x4D435244, 1, 75, 256)
slot1 += b'\x00\x01' + b'\x94\x00\x06' + b'\x14\x00\x06' + b'\x00' * (256 - 8)
with open(os.path.join(macro_dir, 'macro-profile4.bin'), 'wb') as f:
    f.write(slot1)
slot2 = struct.pack('<IIII', 0x4D435244, 1, 80, 256)
slot2 += b'\x00\x01' + b'\x94\x00\x07' + b'\x14\x00\x07' + b'\x00' * (256 - 8)
with open(os.path.join(macro_dir, 'macro-profile4-002.bin'), 'wb') as f:
    f.write(slot2)
"
out=$("$KBCTL" macro list --profile 4 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "macro list multi (profile 4): rc=$rc"
echo "$out" | grep -q "fn+1" || fail "macro list multi: missing fn+1"
echo "$out" | grep -q "fn+2" || fail "macro list multi: missing fn+2"
echo "$out" | grep -q "+0x06 20ms -0x06 20ms" || fail "macro list multi: missing +0x06 steps"
echo "$out" | grep -q "+0x07 20ms -0x07 20ms" || fail "macro list multi: missing +0x07 steps"

# Remove slot 2 file directly (macro clear needs profile write, disabled in e2e).
rm -f "$macro_dir/macro-profile4-002.bin"
out=$("$KBCTL" macro list --profile 4 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "macro list after slot removal (profile 4): rc=$rc"
echo "$out" | grep -q "fn+1" || fail "macro list after slot removal: missing fn+1"
echo "$out" | grep -q "fn+2" && fail "macro list after slot removal: fn+2 should be gone"

# Verify slot 1 file still exists.
[ -f "$macro_dir/macro-profile4.bin" ] || fail "macro slot removal: slot 1 file missing"

# Corrupt indexed file (slot 2) should show <corrupt> but slot 1 still listed.
python3 -c "
import struct, os
with open('$macro_dir/macro-profile4-002.bin', 'wb') as f:
    f.write(struct.pack('<IIII', 0x01, 1, 1, 256))
    f.write(b'\x00' * 256)
"
out=$("$KBCTL" macro list --profile 4 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "macro list corrupt slot 2: rc=$rc"
echo "$out" | grep -q "<corrupt>" || fail "macro list corrupt slot 2: missing <corrupt>"
echo "$out" | grep -q "fn+1" || fail "macro list corrupt slot 2: missing fn+1"

rm -f "$macro_dir/macro-profile4.bin" "$macro_dir/macro-profile4-002.bin"
rm -f "$macro_dir/macro-profile4.bin"

# A FIFO at the state path must not block the offline listing.
if command -v timeout >/dev/null 2>&1; then
    mkfifo "$macro_dir/macro-profile4.bin"
    out=$(timeout 5 "$KBCTL" macro list --profile 4 2>&1); rc=$?
    [ "$rc" -eq 0 ] || fail "macro list (FIFO): rc=$rc (hung or failed)"
    case $out in *"<corrupt>"*) ;; *) fail "macro list (FIFO): expected <corrupt>" ;; esac
    rm -f "$macro_dir/macro-profile4.bin"
fi
rm -f "$macro_dir"/macro-profile1.bin "$macro_dir"/macro-profile2.bin \
      "$macro_dir"/macro-profile3.bin

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
