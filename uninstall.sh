#!/usr/bin/env bash
# Remove kbdd + kbctl, including installation, service, and local state.
set -u

systemctl --user stop kbdd 2>/dev/null || true
systemctl --user disable kbdd 2>/dev/null || true

rm -f \
  "$HOME/.local/bin/kbdd" \
  "$HOME/.local/bin/kbctl" \
  "$HOME/.config/systemd/user/kbdd.service"
rm -rf \
  "$HOME/.config/kbdd" \
  "$HOME/.config/systemd/user/kbdd.service.d" \
  "$HOME/.local/state/kbctl"

sudo rm -f \
  /usr/bin/kbdd \
  /usr/bin/kbctl \
  /usr/local/bin/kbdd \
  /usr/local/bin/kbctl \
  /usr/lib/libkbcore.a \
  /usr/lib/libkbcore.so \
  /usr/lib64/libkbcore.a \
  /usr/lib64/libkbcore.so \
  /usr/local/lib/libkbcore.a \
  /usr/local/lib/libkbcore.so \
  /usr/local/lib64/libkbcore.a \
  /usr/local/lib64/libkbcore.so \
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

echo "Removido. Abra um terminal novo (ou: hash -r)."
