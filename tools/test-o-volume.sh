#!/bin/bash
# Plan2001 stage 2 (docs/PoC-experimental-demo.md 2): a native O volume.
# tools/mkovol formats a fresh image; a disk with no root= boots it with
# otest=, checks its profile, version and init catalog entry, writes a
# plain O record, syncs and halts.  A second boot reads it back.  Then
# the FS opener is given an O volume and the O opener an FS volume; both
# must refuse without modifying them.
#   TARGET          amd64 (default); see tools/targets
#   MEM, SECONDS_TO_RUN
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/ovolvm
odisk=$b/ovoldisk.img
oimg=$b/ovol.img
fsreject=$b/ovolfsreject.img
legacy=$b/ovollegacy.img
legacydisk=$b/ovollegacydisk.img
[ -x "$root/build/selfroot/amd64/bin/gefs" ] || { echo "test-o-volume: no build/selfroot gefs (tools/selfroot)" >&2; exit 1; }
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
	"$qemu" -name plan2001-ovol -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
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
	clean | grep -a '^gefs\|^otest\|^getest\|^root=\|^init: / on gefs\|^panic' || true
}

echo "### mkovol"
"$here/mkovol" "$oimg" 192

echo "### O disk (no root=)"
NOROOTFS=1 CONSOLE=0 INIT='/boot/mp -B' OTEST=partuuid ROOTIMG="$oimg" "$here/gefsdisk" "$odisk"
n=0
boot "$odisk" 'otest: value written .* PASS'
clean | grep -q 'otest: O volume .* opened without FS' || fail "boot 1: no O volume"
clean | grep -q 'otest: O profile version 1, space init = 1' || fail "boot 1: wrong profile/init"
n=$(clean | sed -n 's/^otest: value = //p' | tail -1)
[ -n "$n" ] || { fail "boot 1: no value"; n=0; }
clean | grep -q "otest: value written $((n+1)), synced: PASS" || fail "boot 1: did not write $((n+1))"
clean | grep -q '^init: / on gefs' && fail "boot 1: an FS was mounted"
boot "$odisk" 'otest: value written .* PASS'
clean | grep -q "otest: value = $((n+1))" || fail "boot 2: did not read $((n+1))"
clean | grep -q "otest: value written $((n+2)), synced: PASS" || fail "boot 2: did not write $((n+2))"
clean | grep -q '^init: / on gefs' && fail "boot 2: an FS was mounted"
stop

echo "### FS opener rejects the O volume"
CONSOLE=0 ROOTIMG="$oimg" INIT='/boot/mp -B' "$here/gefsdisk" "$fsreject"
boot "$fsreject" 'root=.*: gefs: O volume, not FS'
clean | grep -q 'root=.*: gefs: O volume, not FS' || fail "FS did not reject O"
clean | grep -q '^init: / on gefs' && fail "FS mounted an O volume"
stop

echo "### O opener rejects a legacy FS volume"
rm -rf "$b/ovollegacytree"
mkdir -p "$b/ovollegacytree/amd64/bin"
cp "$root/build/selfroot/amd64/bin/gefs" "$b/ovollegacytree/amd64/bin/gefs"
"$here/gefsroot" "$b/ovollegacytree" "$legacy" 192
NOROOTFS=1 CONSOLE=0 INIT='/boot/mp -B' OTEST=partuuid ROOTIMG="$legacy" "$here/gefsdisk" "$legacydisk"
boot "$legacydisk" 'otest=.*: O: not an O volume'
clean | grep -q 'otest=.*: O: not an O volume' || fail "O did not reject FS"
clean | grep -q 'otest: value written .* PASS' && fail "O opened an FS volume"
stop

[ "$fails" = 0 ] && echo "PASS: O volume, no FS" || { echo "FAIL: $fails"; exit 1; }
