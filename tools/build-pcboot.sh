#!/bin/bash
# Build the pcboot kernel and the UEFI loader inside the 9front VM and put
# 9pcboot and bootx64.efi into ../build/.
#   The VM must be running (tools/vm/wsl-run.sh) as a cpu server; rcpu is
#   forwarded to NINE_HOST:17019.
#   NINE_PASS   password of the rcpu user (default: contents of ~/.9front-pass)
#   NINE_HOST   default 127.0.0.1          NINE_USER  default glenda
#   DRAWTERM    drawterm binary (default: drawterm in PATH, else ~/.local/bin)
# drawterm exports the host file system to the VM as /mnt/term, so sources go
# in and results come out through it - no extra ports or tar streams.
# -a and -s point at 127.0.0.1 on purpose: drawterm dials $auth:567 and
# secstore :5356 when no local factotum exists, and a silently dropped SYN
# there makes it hang for minutes.
set -e
HOST=${NINE_HOST:-127.0.0.1}; USER_=${NINE_USER:-glenda}
PASS=${NINE_PASS:-$(tr -d '\r\n' < ~/.9front-pass)}
DT=${DRAWTERM:-$(command -v drawterm || echo "$HOME/.local/bin/drawterm")}
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); out=$root/build
mkdir -p "$out"; rm -f "$out"/9pcboot "$out"/bootx64.efi "$out"/*.log
cmd="hostsrc=/mnt/term$root/9front-x64-boot
hostbuild=/mnt/term$out
$(cat "$here/pcboot-build.rc")"
PASS=$PASS timeout "${BUILD_TIMEOUT:-900}" "$DT" -G -h "$HOST" -a 127.0.0.1 -s 127.0.0.1 -u "$USER_" -c "$cmd" 2>&1 | tr -d '\r' | tee "$out/session.log"
grep -q BUILD-DONE "$out/session.log" || { echo "build did not finish"; exit 1; }
[ -s "$out/9pcboot" ] && [ -s "$out/bootx64.efi" ] || { echo "missing build results"; exit 1; }
echo "results in $out: $(cd "$out" && ls -l 9pcboot bootx64.efi | awk '{print $5, $9}' | tr '\n' ' ')"
