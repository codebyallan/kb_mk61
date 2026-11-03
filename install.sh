#!/usr/bin/env bash
# Install or update kbdd and kbctl and leave everything ready to use.
# Run from the project root: ./install.sh
# Opt-in flags (independent; both default to 0):
#   ENABLE_LED=1            colour/frame control, RAM only (KBDD_ENABLE_LED)
#   ENABLE_PROFILE_WRITE=1  flash saves, effect/profile changes, restore-profile
#                           (KBDD_ENABLE_PROFILE_WRITE)
set -euo pipefail
cd "$(dirname "$0")"

command -v gcc >/dev/null && command -v make >/dev/null || sudo dnf install -y gcc make

echo "==> removing old installations (all prefixes)"
systemctl --user stop kbdd 2>/dev/null || true
systemctl --user disable kbdd 2>/dev/null || true
sudo rm -f /usr/local/bin/kbdd /usr/local/bin/kbctl /usr/local/include/kbcore.h \
  /usr/local/lib/libkbcore.* /usr/local/lib64/libkbcore.* \
  /usr/local/lib/systemd/user/kbdd.service /usr/local/lib/udev/rules.d/60-kbdd.rules \
  /usr/bin/kbdd /usr/bin/kbctl /usr/lib64/libkbcore.* /usr/include/kbcore.h \
  /usr/lib/systemd/user/kbdd.service /usr/lib/udev/rules.d/60-kbdd.rules
rm -f ~/.local/bin/kbctl ~/.local/bin/kbdd ~/.config/systemd/user/kbdd.service
rm -rf ~/.config/systemd/user/kbdd.service.d

echo "==> building and testing"
make clean >/dev/null
make
make test

echo "==> installing to /usr"
sudo make install PREFIX=/usr LIBDIR=/usr/lib64
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw

mkdir -p ~/.config/kbdd
DROPIN_ENV=""
[ "${ENABLE_LED:-0}" = "1" ] && DROPIN_ENV="${DROPIN_ENV}Environment=KBDD_ENABLE_LED=1\n"
[ "${ENABLE_PROFILE_WRITE:-0}" = "1" ] && DROPIN_ENV="${DROPIN_ENV}Environment=KBDD_ENABLE_PROFILE_WRITE=1\n"
if [ -n "$DROPIN_ENV" ]; then
  mkdir -p ~/.config/systemd/user/kbdd.service.d
  printf "[Service]\n${DROPIN_ENV}" > ~/.config/systemd/user/kbdd.service.d/led.conf
else
  rm -f ~/.config/systemd/user/kbdd.service.d/led.conf
  rmdir ~/.config/systemd/user/kbdd.service.d 2>/dev/null || true
fi
if [ "${ENABLE_LED:-0}" = "1" ] && [ "${ENABLE_PROFILE_WRITE:-0}" != "1" ]; then
  echo "NOTE: ENABLE_LED=1 no longer enables flash writes. For 'led save', restore-profile and"
  echo "      effect/profile changes, also set ENABLE_PROFILE_WRITE=1."
fi

systemctl --user daemon-reload
systemctl --user enable kbdd >/dev/null 2>&1 || true
systemctl --user restart kbdd

echo "==> verificando"
fail() { echo "FALHOU: $1"; journalctl --user -u kbdd -n 15 --no-pager; exit 1; }
EXEC=$(systemctl --user show kbdd -p ExecStart --value | sed -n 's/.*path=\([^ ;]*\).*/\1/p')
[ "$EXEC" = "/usr/bin/kbdd" ] || fail "unit points to '$EXEC' (expected /usr/bin/kbdd)"
for _ in $(seq 1 10); do [ -S "${XDG_RUNTIME_DIR:-/run/user/$UID}/kbdd.sock" ] && break; sleep 0.5; done
systemctl --user is-active --quiet kbdd || fail "kbdd is not active"
/usr/bin/kbctl status || fail "kbctl cannot communicate with kbdd"
OUT=$(/usr/bin/kbctl list)
echo "$OUT"
case "$OUT" in
  *"no devices"*) echo "WARNING: no keyboard found. Reconnect the USB device and run: kbctl list" ;;
esac
echo "DONE. Open a new terminal (or run: hash -r) and test: kbctl status"
