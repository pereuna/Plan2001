#!/bin/bash
# Plan2001 stage 3 (docs/PoC-experimental-demo.md 3): the native O object
# API.  tools/mkovol formats a fresh O image; the kernel opens it with no
# root= and puts two objects with stable OIDs in the init Space, then
# commits and syncs them.  A second boot fetches both by OID.  No FS, 9P,
# Fid, qid or path participates.
#   TARGET          amd64 (default); see tools/targets
#   MEM, SECONDS_TO_RUN
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/oobjvm
odisk=$b/oobjdisk.img
oimg=$b/oobj.img
[ -x "$root/build/selfroot/amd64/bin/gefs" ] || { echo "test-o-objects: no build/selfroot gefs (tools/selfroot)" >&2; exit 1; }
mkdir -p "$vm"
q=
clean() { tr -d '\r' < "$vm/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g'; }
stop() { [ -n "$q" ] && { kill $q 2>/dev/null || true; wait $q 2>/dev/null || true; }; q=; }
trap stop EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
boot() {
	local disk=$1 until=$2
	stop
	: > "$vm/serial.log"
	cp "$fwvars" "$vm/vars.fd"
	"$qemu" -name plan2001-oobj -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
		-nic none -display none -serial file:"$vm/serial.log" 2>"$vm/qemu.log" &
	q=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-120}))
	until clean | grep -q "$until\\|^panic" || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
		sleep 1
	done
	sleep 1
	clean | grep -a '^gefs\|^oobj\|^otest\|^getest\|^root=\|^init: / on gefs\|^panic' || true
}

echo "### mkovol"
"$here/mkovol" "$oimg" 192

echo "### O objects (no root=)"
NOROOTFS=1 CONSOLE=0 INIT='/boot/mp -B' OOBJ=partuuid ROOTIMG="$oimg" "$here/gefsdisk" "$odisk"
boot "$odisk" 'oobj: put A and B, commit, readback: PASS'
clean | grep -q 'oobj: O volume .* opened without FS' || fail "boot 1: no O volume"
clean | grep -q 'oobj: space init has no objects' || fail "boot 1: init Space was not empty"
clean | grep -q 'oobj: A and B committed at version 1' || fail "boot 1: no version 1 commit"
clean | grep -q 'oobj: put A and B, commit, readback: PASS' || fail "boot 1: no PASS"
clean | grep -q '^init: / on gefs' && fail "boot 1: an FS was mounted"
boot "$odisk" 'oobj: restored A (version 1) and B (version 1): PASS'
clean | grep -q 'oobj: O volume .* opened without FS' || fail "boot 2: no O volume"
clean | grep -q 'oobj: restored A (version 1) and B (version 1): PASS' || fail "boot 2: objects not restored"
clean | grep -q 'oobj: space init has no objects' && fail "boot 2: init Space was empty"
clean | grep -q '^init: / on gefs' && fail "boot 2: an FS was mounted"
stop

[ "$fails" = 0 ] && echo "PASS: native O objects, no FS" || { echo "FAIL: $fails"; exit 1; }
