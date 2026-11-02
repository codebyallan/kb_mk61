#!/usr/bin/env bash
# Install or update kbdd and kbctl and leave everything ready to use.
# Run from the project root: ./install.sh   (without LED: ENABLE_LED=0 ./install.sh)
set -euo pipefail
cd "$(dirname "$0")"

command -v gcc >/dev/null && command -v make >/dev/null || sudo dnf install -y gcc make

echo "==> limpando instalacoes antigas (todos os prefixos)"
systemctl --user stop kbdd 2>/dev/null || true
systemctl --user disable kbdd 2>/dev/null || true
sudo rm -f /usr/local/bin/kbdd /usr/local/bin/kbctl /usr/local/include/kbcore.h \
  /usr/local/lib/libkbcore.* /usr/local/lib64/libkbcore.* \
  /usr/local/lib/systemd/user/kbdd.service /usr/local/lib/udev/rules.d/60-kbdd.rules \
  /usr/bin/kbdd /usr/bin/kbctl /usr/lib64/libkbcore.* /usr/include/kbcore.h \
  /usr/lib/systemd/user/kbdd.service /usr/lib/udev/rules.d/60-kbdd.rules
rm -f ~/.local/bin/kbctl ~/.local/bin/kbdd ~/.config/systemd/user/kbdd.service
rm -rf ~/.config/systemd/user/kbdd.service.d

echo "==> compilando e testando"
make clean >/dev/null
make
make test

echo "==> instalando em /usr"
sudo make install PREFIX=/usr LIBDIR=/usr/lib64
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw

if [ "${ENABLE_LED:-1}" = "1" ]; then
  mkdir -p ~/.config/kbdd ~/.config/systemd/user/kbdd.service.d
  printf '[Service]\nEnvironment=KBDD_ENABLE_LED=1\nEnvironment=KBDD_ENABLE_PROFILE_WRITE=1\n' > ~/.config/systemd/user/kbdd.service.d/led.conf
else
  mkdir -p ~/.config/kbdd
  rm -f ~/.config/systemd/user/kbdd.service.d/led.conf
  rmdir ~/.config/systemd/user/kbdd.service.d 2>/dev/null || true
fi

systemctl --user daemon-reload
systemctl --user enable kbdd >/dev/null 2>&1 || true
systemctl --user restart kbdd

echo "==> verificando"
fail() { echo "FALHOU: $1"; journalctl --user -u kbdd -n 15 --no-pager; exit 1; }
EXEC=$(systemctl --user show kbdd -p ExecStart --value | sed -n 's/.*path=\([^ ;]*\).*/\1/p')
[ "$EXEC" = "/usr/bin/kbdd" ] || fail "unit aponta para '$EXEC' (esperado /usr/bin/kbdd)"
for _ in $(seq 1 10); do [ -S "${XDG_RUNTIME_DIR:-/run/user/$UID}/kbdd.sock" ] && break; sleep 0.5; done
systemctl --user is-active --quiet kbdd || fail "kbdd nao esta ativo"
/usr/bin/kbctl status || fail "kbctl nao conversa com o kbdd"
OUT=$(/usr/bin/kbctl list)
echo "$OUT"
case "$OUT" in
  *"no devices"*) echo "AVISO: nenhum teclado visto. Reconecte o USB e rode: kbctl list" ;;
esac
echo "PRONTO. Abra um terminal novo (ou rode: hash -r) e teste: kbctl led solid FF0000"
