#!/bin/bash
# Build the full pc64 kernel in the 9front VM with the repo's changes laid
# over the VM's sources; result: ../build/9pc64-full.  See pcboot-build.rc
# for the connection details (NINE_PASS, NINE_HOST, DRAWTERM).
set -e
HOST=${NINE_HOST:-127.0.0.1}; USER_=${NINE_USER:-glenda}
PASS=${NINE_PASS:-$(tr -d '\r\n' < ~/.9front-pass)}
DT=${DRAWTERM:-$(command -v drawterm || echo "$HOME/.local/bin/drawterm")}
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); out=$root/build
mkdir -p "$out"; rm -f "$out"/9pc64-full "$out"/full-kernel.log
cmd="hostsrc=/mnt/term$root/9front-x64-boot
hostbuild=/mnt/term$out
$(cat "$here/full-build.rc")"
PASS=$PASS timeout "${BUILD_TIMEOUT:-900}" "$DT" -G -h "$HOST" -a 127.0.0.1 -s 127.0.0.1 -u "$USER_" -c "$cmd" 2>&1 | tr -d '\r' | cut -c1-200 | tee "$out/full-session.log"
grep -q BUILD-DONE "$out/full-session.log" && [ -s "$out/9pc64-full" ] || { echo "full build failed"; exit 1; }
echo "result: $(ls -l "$out/9pc64-full" | awk '{print $5, $9}')"
