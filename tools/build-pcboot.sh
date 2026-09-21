#!/bin/bash
# Build the pcboot kernel and the UEFI loader on the 9front test machine and
# fetch the results into ../build/.
#   NINE_PASS   factotum password of the rcpu user (required)
#   NINE_HOST   default 10.80.73.196      NINE_USER  default glenda
#   DRAWTERM    path to a drawterm binary (default: drawterm in PATH)
# The machine must already run:  aux/listen1 -t tcp!*!rcpu /rc/bin/service/tcp17019 -R &
# Everything runs in ONE rcpu session: the machine's /tmp is per session,
# so the steps must not be split into separate connections.
set -e
HOST=${NINE_HOST:-10.80.73.196}; USER_=${NINE_USER:-glenda}
PASS=${NINE_PASS:?set NINE_PASS}; DT=${DRAWTERM:-drawterm}
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
out=$root/build; mkdir -p "$out"; rm -f "$out"/{sub.tar,out.tar,session.log}
tar cf "$out/sub.tar" -C "$root/9front-x64-boot" sys amd64
(PASS=$PASS timeout 200 "$DT" -G -h "$HOST" -a "$HOST" -u "$USER_" -c "$(cat "$here/pcboot-build.rc")" >"$out/session.log" 2>&1) &
dt=$!
wait_for() { for _ in $(seq 1 "$2"); do grep -q "$1" "$out/session.log" && return 0; sleep 1; done; return 1; }
wait_for WAITING 60 || { echo "no answer from $HOST (is the rcpu listener running?)"; cat "$out/session.log"; exit 1; }
nc -w 10 "$HOST" 9997 < "$out/sub.tar"
wait_for READY 180 || { echo "build did not finish"; cat "$out/session.log"; exit 1; }
nc -w 15 "$HOST" 9996 > "$out/out.tar"
kill $dt 2>/dev/null || true
tar xf "$out/out.tar" -C "$out"
cat "$out/session.log"
echo "results in $out: $(cd "$out" && ls 9pcboot bootx64.efi 2>&1 | tr '\n' ' ')"
