#!/bin/bash
# Plan2001 Stage 5 (docs/PoC-experimental-demo.md 5): boot the O-only
# init Space three times from one disk image.  tools/mkovol seeds a fresh
# O image with the demo Environment and its immutable .mpy; plan9.ini has
# only init=/boot/mp -O PARTUUID=... -S init -B (no root=, no -c, no
# Python source).  Boot 1 imports the O-resident module once, initializes
# its Counter to zero and commits a persistent run().  Boots 2 and 3
# restore the graph and call run() without running a module top level.
# The same Counter instance, its alias and its OID survive; the counter
# is 1, 2, 3.  No FS, 9P service, path lookup or .py/.mpy file is used.
#   TARGET          amd64 (default); see tools/targets
#   MEM, SECONDS_TO_RUN
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/obootvm
disk=$b/oboot.img
oimg=$b/oboot-o.img
[ -x "$root/build/selfroot/amd64/bin/gefs" ] || { echo "test-o-boot: no build/selfroot gefs (tools/selfroot)" >&2; exit 1; }
[ -x "$root/build/selfroot/amd64/bin/micropython" ] || { echo "test-o-boot: no build/selfroot micropython (tools/selfroot)" >&2; exit 1; }
mkdir -p "$vm"
q=
clean() { tr -d '\r' < "$vm/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g'; }
stop() { [ -n "$q" ] && { kill $q 2>/dev/null || true; wait $q 2>/dev/null || true; }; q=; }
trap stop EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
# the same disk every time: never formatted or copied between boots
boot() {
	local tag=$1
	stop
	: > "$vm/serial.log"
	cp "$fwvars" "$vm/vars.fd"
	"$qemu" -name plan2001-oboot -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
		-nic none -display none -serial file:"$vm/serial.log" 2>"$vm/qemu.log" &
	q=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-180}))
	until clean | grep -q '^init: done$\|^init: failed, no commit$\|^panic' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
		sleep 1
	done
	sleep 1
	clean > "$vm/$tag.log"
	{ echo "### $tag"; cat "$vm/$tag.log"; } >> "$vm/three-boots.log"
	clean | grep -a '^space:\|^init:\|^boot:\|^panic\|^init: / on gefs' || true
}
oidof() {
	sed -n 's/^init: shared Counter n [0-9]*, alias same, OID \([0-9a-f]*\).*/\1/p' "$1" | tail -1
}
checkboot() {
	local tag=$1 n=$2 version=$3 oidprev=$4
	local log=$vm/$tag.log oid
	grep -q 'o: O volume .* opened without FS' "$log" || fail "$tag: no native O volume"
	grep -q 'space: O: odemo code OID ffffffff00000000, digest [0-9a-f]\{64\}' "$log" || fail "$tag: code not loaded from O"
	grep -q "^init: run() -> $n\$" "$log" || fail "$tag: run() -> $n missing"
	grep -q "^init: shared Counter n $n, alias same, OID [0-9a-f]\+ (alias [0-9a-f]\+)\$" "$log" || fail "$tag: no same Counter/alias"
	grep -q "space: O: committed init version $version" "$log" || fail "$tag: no commit $version"
	grep -q '^init: done$' "$log" || fail "$tag: not done"
	grep -q '^panic' "$log" && fail "$tag: QEMU panic"
	grep -q 'commit failed' "$log" && fail "$tag: commit failure"
	grep -q '^init: / on gefs' "$log" && fail "$tag: an FS was mounted"
	grep -q '\.py\|\.mpy\|/usr/glenda/lib' "$log" && fail "$tag: a runtime source/bytecode file"
	grep -q 'srv/.*9p\|9P.*serve\|9p.*serve' "$log" && fail "$tag: a 9P service"
	oid=$(oidof "$log")
	[ -n "$oid" ] || fail "$tag: no Counter OID"
	[ -n "$oidprev" ] && [ "$oid" != "$oidprev" ] && fail "$tag: Counter OID changed ($oidprev -> $oid)"
	lastoid=$oid
}

rm -f "$vm/three-boots.log"
echo "### mkovol with the demo Environment and code"
"$here/mkovol" "$oimg" 192 "$here/odemo.py"

echo "### ESP + O, no FS root, automatic init run()"
NOROOTFS=1 CONSOLE=0 ROOTIMG="$oimg" \
	INIT='/boot/mp -O PARTUUID=@PARTUUID@ -S init -B' \
	"$here/gefsdisk" "$disk"
puuid=$(cat "$disk.partuuid")

# the boot configuration itself: no root=, no -c, no Python source
grep -q "^init=/boot/mp -O PARTUUID=$puuid -S init -B$" "$disk.ini" || fail "plan9.ini: wrong init line"
grep -q '^root=' "$disk.ini" && fail "plan9.ini: has root="
grep -q -- '-c' "$disk.ini" && fail "plan9.ini: has -c"
grep -q '\.py' "$disk.ini" && fail "plan9.ini: has Python source"

boot boot1
grep -q 'space: O: init has no graph' "$vm/boot1.log" || fail "boot 1: the O Space was not fresh"
grep -q 'space: O: init initialized from O code (odemo)' "$vm/boot1.log" || fail "boot 1: no one-time initialization"
checkboot boot1 1 1 ""; oid=$lastoid

boot boot2
grep -q 'space: O: restored init version 1' "$vm/boot2.log" || fail "boot 2: the O graph was not restored"
grep -q 'space: O: init restored (odemo), no module initialization' "$vm/boot2.log" || fail "boot 2: no restored init"
grep -q 'init initialized from O code' "$vm/boot2.log" && fail "boot 2: module initialized again"
checkboot boot2 2 2 "$oid"; oid2=$lastoid

boot boot3
grep -q 'space: O: restored init version 2' "$vm/boot3.log" || fail "boot 3: the O graph was not restored"
grep -q 'space: O: init restored (odemo), no module initialization' "$vm/boot3.log" || fail "boot 3: no restored init"
grep -q 'init initialized from O code' "$vm/boot3.log" && fail "boot 3: module initialized again"
checkboot boot3 3 3 "$oid2"
stop

echo "three-boot serial output: $vm/three-boots.log"
[ "$fails" = 0 ] && echo "PASS: three O-only boots, persistent Counter and run() from O (no FS)" || { echo "FAIL: $fails"; exit 1; }
