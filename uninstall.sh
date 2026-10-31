#!/usr/bin/env bash
# Remove kbdd + kbctl de todos os locais.
set -u
systemctl --user stop kbdd 2>/dev/null
systemctl --user disable kbdd 2>/dev/null
sudo rm -f /usr/bin/kbdd /usr/bin/kbctl /usr/local/bin/kbdd /usr/local/bin/kbctl \
  /usr/lib64/libkbcore.* /usr/local/lib/libkbcore.* /usr/local/lib64/libkbcore.* \
  /usr/include/kbcore.h /usr/local/include/kbcore.h \
  /usr/lib/udev/rules.d/60-kbdd.rules /usr/local/lib/udev/rules.d/60-kbdd.rules \
  /usr/lib/systemd/user/kbdd.service /usr/local/lib/systemd/user/kbdd.service
rm -f ~/.local/bin/kbctl ~/.local/bin/kbdd ~/.config/systemd/user/kbdd.service
rm -rf ~/.config/systemd/user/kbdd.service.d
sudo udevadm control --reload-rules
systemctl --user daemon-reload
echo "Removido. Abra um terminal novo (ou: hash -r)."
