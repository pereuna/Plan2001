#!/bin/bash
# Plan2001 Stage 4 (docs/PoC-experimental-demo.md 4): boot MicroPython
# from an ESP + native O disk, with no FS root.  tools/mkovol seeds a
# fresh O image with one Environment manifest and the demo module's
# immutable .mpy.  Boot 1 loads them from O, creates the module's graph
# and commits it.  Boot 2 restores that graph from O: the counter and
# the shared reference survive, the top level is not run again, and the
# code's OID and digest are the ones the manifest names.  Nothing reads
# a runtime filesystem for code or state.
#   TARGET          amd64 (default); see tools/targets
#   MEM, SECONDS_TO_RUN
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/ospvm
disk=$b/osp.img
oimg=$b/osp-o.img
[ -x "$root/build/selfroot/amd64/bin/gefs" ] || { echo "test-o-space: no build/selfroot gefs (tools/selfroot)" >&2; exit 1; }
[ -x "$root/build/selfroot/amd64/bin/micropython" ] || { echo "test-o-space: no build/selfroot micropython (tools/selfroot)" >&2; exit 1; }
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
	"$qemu" -name plan2001-osp -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
		-nic none -display none -serial file:"$vm/serial.log" 2>"$vm/qemu.log" &
	q=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-120}))
	until clean | grep -q 'space: O: committed \|^panic' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
		sleep 1
	done
	sleep 2
	clean | grep -a '^space:\|^O1\|^O2\|^panic\|^init: / on gefs' || true
}
setini() {
	local f=$1; shift
	{ printf 'bootfile=/%s\nconsole=0\nuser=glenda\n' "$kernel"; printf 'init=%s\n' "$*"; } > "$f"
	mcopy -o -i "$disk@@1048576" "$f" ::/plan9.ini
}

echo "### mkovol with the Stage 4 demo Environment"
"$here/mkovol" "$oimg" 192 "$here/odemo.py"

echo "### ESP + O, no FS root"
NOROOTFS=1 CONSOLE=0 ROOTIMG="$oimg" \
	INIT='/boot/mp -O PARTUUID=@PARTUUID@ -S init -c "print(1)"' \
	"$here/gefsdisk" "$disk"
puuid=$(cat "$disk.partuuid")

py1='import space, odemo;print("O1", odemo.shared.n, odemo.alias is odemo.shared);print("O1run", odemo.run());print("O1oid", space.oid(odemo.alias) == space.oid(odemo.shared))'
setini "$vm/boot1.ini" "/boot/mp -O PARTUUID=$puuid -S init -c '$py1'"
boot

clean | grep -q 'o: O volume .* opened without FS' || fail "boot 1: no native O volume"
clean | grep -q 'space: O volume selected' || fail "boot 1: -O did not select the volume"
clean | grep -q 'space: O: init has no graph' || fail "boot 1: the O Space was not empty"
clean | grep -q 'space: O: odemo code OID ffffffff00000000, digest [0-9a-f]\{64\}' || fail "boot 1: code not loaded from O"
clean | grep -q '^O1 41 True$' || fail "boot 1: no demo object/alias"
clean | grep -q '^O1run 42$' || fail "boot 1: run() did not run"
clean | grep -q '^O1oid True$' || fail "boot 1: shared OID differs"
clean | grep -q 'space: O: committed init version 1' || fail "boot 1: no O commit"
clean | grep -q '^init: / on gefs' && fail "boot 1: an FS was mounted"

py2='import space, odemo;print("O2", odemo.shared.n, odemo.alias is odemo.shared);print("O2run", odemo.run());print("O2oid", space.oid(odemo.alias) == space.oid(odemo.shared))'
setini "$vm/boot2.ini" "/boot/mp -O PARTUUID=$puuid -S init -c '$py2'"
boot

clean | grep -q 'space: O: restored init version 1' || fail "boot 2: the O graph was not restored"
clean | grep -q 'space: O: odemo code OID ffffffff00000000, digest [0-9a-f]\{64\}' || fail "boot 2: code not loaded from O"
clean | grep -q '^O2 42 True$' || fail "boot 2: counter/shared reference did not survive"
clean | grep -q '^O2run 43$' || fail "boot 2: restored run() did not run"
clean | grep -q '^O2oid True$' || fail "boot 2: restored shared OID differs"
clean | grep -q 'space: O: committed init version 2' || fail "boot 2: no second O commit"
clean | grep -q '^init: / on gefs' && fail "boot 2: an FS was mounted"
stop

[ "$fails" = 0 ] && echo "PASS: Stage 4 Environment, code and Space graph from O (no FS)" || { echo "FAIL: $fails"; exit 1; }
