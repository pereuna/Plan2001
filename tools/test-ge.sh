#!/bin/bash
# Plan2001 stage 1 (docs/PoC-experimental-demo.md 1): GE volume opening
# without FS.  A disposable GE image is booted with no root=; getest=
# opens it with geopen/gestart, reads and writes a raw GE key/value
# record in the snapshot tree, syncs, and halts it.  No FS mount, user
# metadata or 9P server is started.  A second boot reads the same key
# and writes it back, so the write crossed a reboot.
#   TARGET          amd64 (default); see tools/targets
#   ROOTIMG=FILE    an existing GE image (default build/$TARGET/getest.gefs,
#                   made by tools/gefsroot from build/selfroot if missing)
#   MEM, SECONDS_TO_RUN
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/getestvm
disk=$b/getestdisk.img
rootimg=${ROOTIMG:-$b/getest.gefs}
[ -s "$rootimg" ] || "$here/gefsroot" "$root/build/selfroot" "$rootimg"
NOROOTFS=1 CONSOLE=0 INIT='/boot/mp -B' GETEST=partuuid ROOTIMG="$rootimg" "$here/gefsdisk" "$disk"
mkdir -p "$vm"
q=
clean() { tr -d '\r' < "$vm/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g'; }
stop() { [ -n "$q" ] && { kill $q 2>/dev/null || true; wait $q 2>/dev/null || true; }; q=; }
trap stop EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
boot() {
	stop
	: > "$vm/serial.log"
	cp "$fwvars" "$vm/vars.fd"
	"$qemu" -name plan2001-getest -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
		-nic none -display none -serial file:"$vm/serial.log" 2>"$vm/qemu.log" &
	q=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-120}))
	until clean | grep -q 'getest: raw GE key getest.seq written .* PASS\|^panic' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
		sleep 1
	done
	sleep 1
	clean | grep -a '^gefs\|^getest\|^root=\|^panic' || true
	clean | grep -q '^panic' && fail "panic"
	clean | grep -q 'getest: raw GE key getest.seq written .* PASS' || fail "no getest PASS"
}
n=0
boot
clean | grep -q 'getest: GE volume .* opened without FS' || fail "boot 1: no GE volume"
n=$(clean | sed -n 's/^getest: raw GE key getest.seq = //p' | tail -1)
[ -n "$n" ] || { fail "boot 1: no sequence"; n=0; }
clean | grep -q "getest: raw GE key getest.seq written $((n+1)), synced: PASS" || fail "boot 1: did not write $((n+1))"
clean | grep -q '^init: / on gefs' && fail "boot 1: an FS was mounted"
boot
clean | grep -q 'getest: GE volume .* opened without FS' || fail "boot 2: no GE volume"
clean | grep -q "getest: raw GE key getest.seq = $((n+1))" || fail "boot 2: did not read $((n+1))"
clean | grep -q "getest: raw GE key getest.seq written $((n+2)), synced: PASS" || fail "boot 2: did not write $((n+2))"
clean | grep -q '^init: / on gefs' && fail "boot 2: an FS was mounted"
stop
[ "$fails" = 0 ] && echo "PASS: GE volume, raw key/value, no FS" || { echo "FAIL: $fails"; exit 1; }
