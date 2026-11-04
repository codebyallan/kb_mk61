#!/usr/bin/env bash
# Remove kbdd + kbctl, service and config. Usage: ./uninstall.sh [--purge]
# Profile backups (~/.local/state/kbctl) are kept unless --purge is given.
set -u

PURGE=0
for a in "$@"; do
  case "$a" in
    --purge) PURGE=1 ;;
    *) echo "Usage: $0 [--purge]   (--purge also deletes ~/.local/state/kbctl profile backups)" >&2; exit 2 ;;
  esac
done
MULTIARCH=$(gcc -print-multiarch 2>/dev/null || true)

systemctl --user stop kbdd 2>/dev/null || true
systemctl --user disable kbdd 2>/dev/null || true

rm -f \
  "$HOME/.local/bin/kbdd" \
  "$HOME/.local/bin/kbctl" \
  "$HOME/.config/systemd/user/kbdd.service"
rm -rf \
  "$HOME/.config/kbdd" \
  "$HOME/.config/systemd/user/kbdd.service.d"
if [ "$PURGE" = 1 ]; then
  rm -rf "$HOME/.local/state/kbctl"
else
  echo "Kept profile backups in ~/.local/state/kbctl (use --purge to delete)."
fi

sudo rm -f \
  /usr/bin/kbdd \
  /usr/bin/kbctl \
  /usr/local/bin/kbdd \
  /usr/local/bin/kbctl \
  /usr/lib/libkbcore.a \
  /usr/lib/libkbcore.so* \
  /usr/lib64/libkbcore.a \
  /usr/lib64/libkbcore.so* \
  /usr/local/lib/libkbcore.a \
  /usr/local/lib/libkbcore.so* \
  /usr/local/lib64/libkbcore.a \
  /usr/local/lib64/libkbcore.so* \
  ${MULTIARCH:+"/usr/lib/$MULTIARCH"/libkbcore.a "/usr/lib/$MULTIARCH"/libkbcore.so*} \
  /usr/include/kbcore.h \
  /usr/local/include/kbcore.h \
  /usr/lib/udev/rules.d/60-kbdd.rules \
  /usr/local/lib/udev/rules.d/60-kbdd.rules \
  /usr/lib/systemd/user/kbdd.service \
  /usr/local/lib/systemd/user/kbdd.service \
  /etc/systemd/user/kbdd.service \
  /etc/systemd/user/kbdd.service.d/led.conf

sudo udevadm control --reload-rules 2>/dev/null || true
systemctl --user daemon-reload 2>/dev/null || true
sudo systemctl daemon-reload 2>/dev/null || true

echo "Removed. Open a new terminal (or run: hash -r)."
