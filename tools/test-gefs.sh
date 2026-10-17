#!/bin/bash
# gefs as the root in the kernel (devgefs, docs/gefs.md) in QEMU, over
# the serial console (tools/9run's relay): tools/gefsroot makes the root
# image in the build VM, tools/gefsdisk the disk (ESP and gefs, root=-w
# PARTUUID=), then the root is booted: files written, a program built,
# the console's check, the adm tree's ctl and status (/srv/gefs.root), a
# snapshot, fshalt.  At the end tools/gefsck checks the partition in the
# build VM with the userspace gefs and reads the boot's files back.
#   tools/test-gefs.sh
#   NOROOT=1    build/selfroot as it is (else tools/selfroot makes it)
#   BUILD=1     also build the tree (mk install), the kernel and the loader
#               on the gefs root (MEM=4096 SMP=4 is quicker)
#   CRASH=N     then N boots killed (QEMU, no sync) while they write; each
#               next one must load, check clean and keep what it synced
#   KTEST="all"  the kernel tests (sys/src/cmd/ktest, its arguments) in the
#               booted system, over the console after the other checks:
#               every KTEST line OK (on a machine: ktest all at the prompt)
#   INI='k=v;k=v'  more plan9.ini lines (gefsdebug=1, gefstrace=16)
#   KEEP=1      leave QEMU running after a failure (tools/9run -d build/$TARGET/gefsvm)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
tree=$root/build/selfroot
vm=$b/gefsvm
disk=$b/gefsdisk.img
[ "${NOROOT:-0}" = 1 ] && [ -d "$tree/.git" ] || "$here/selfroot" "$tree"
rm -f "$b/root.gefs"
"$here/gefsroot" "$tree" "$b/root.gefs" $(( $(du -sm "$tree" | cut -f1) + 4096 ))
CONSOLE=0 INIT=/init INI="$INI" "$here/gefsdisk" "$disk"

q= relay=
mkdir -p "$vm"
stop() {
	[ -n "$q" ] && { kill $q 2>/dev/null || true; wait $q 2>/dev/null || true; }
	[ -n "$relay" ] && { kill $relay 2>/dev/null || true; wait $relay 2>/dev/null || true; }
	q= relay=
}
trap 'if [ "${KEEP:-0}" = 1 ] && [ -n "$q" ]; then echo "QEMU left running (pid $q), console: tools/9run -d $vm"; else stop; fi' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
clean() { tr -d '\r' < "$vm/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g'; }
# a command over the console; its output in $vm/out
run() {
	echo "### $1"
	if python3 "$here/9run" -d "$vm" -t "${2:-120}" "$1" > "$vm/out" 2>&1; then
		sed 's/^/	/' "$vm/out" | tail -${3:-10}; return 0
	fi
	sed 's/^/	/' "$vm/out" | tail -20; return 1
}
boot() {
	stop
	echo "=== boot: $(grep '^root=' "$disk.ini")"
	rm -f "$vm"/*.sock "$vm/serial.in"
	: > "$vm/serial.log"
	cp "$fwvars" "$vm/vars.fd"
	"$qemu" -name plan2001-gefs -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp "${SMP:-2}" \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
		-nic none -display none \
		-chardev socket,id=ser,path="$vm/serial.sock",server=on,wait=off -serial chardev:ser \
		2>"$vm/qemu.log" &
	q=$!
	mkfifo "$vm/serial.in"
	python3 "$here/9run" --relay "$vm" </dev/null >/dev/null 2>&1 &
	relay=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-150}))
	until clean | grep -q 'plan2001% \|^panic' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do sleep 1; done
	clean | grep -a '^gefs\|^init:'
	clean | grep -q '^panic' && { clean | tail -30; fail "panic"; return 1; }
	clean | grep -q 'plan2001% ' || { clean | tail -30; fail "no prompt"; return 1; }
}
halt() {
	run 'fshalt' 180 || true
	local end=$((SECONDS + 120))
	until clean | grep -q 'done halting' || [ $SECONDS -ge $end ]; do sleep 1; done
	clean | grep -q 'done halting' || fail "fshalt"
	clean | grep -q 'gefs: halted' || fail "gefs not halted"
}

boot || exit 1
clean | grep -q '^gefs: .*read-write' || fail "no gefs line"
clean | grep -q '^init: / on gefs' || fail "init: not on gefs"
run "ls /amd64/bin | wc -l; cat /dev/sysname" || fail root
run "echo gefs root >/tmp/gefstest && cat /tmp/gefstest && rm /tmp/gefstest && echo hello >/usr/glenda/gefshello && cat /usr/glenda/gefshello" || fail write
run "dd -if /dev/zero -of /tmp/big -bs 65536 -count 512 >[2]/dev/null; ls -l /tmp/big; md5sum </tmp/big >/usr/glenda/bigsum; cat /usr/glenda/bigsum" 120 || fail "big write"
run "cd /sys/src/cmd && 6c -FTVw cat.c && 6l -o /tmp/6.cat cat.6 && /tmp/6.cat /usr/glenda/gefshello && rm cat.6" 120 || fail "build"
run "mount -c /srv/gefs.root /n/adm adm && cat /n/adm/status && echo check >/n/adm/ctl && echo checked" 120 30 || fail "adm ctl"
run "echo snap main gefstest >/n/adm/ctl && sleep 2 && cat /n/adm/status | grep gefstest" 60 || fail snap
if [ "${BUILD:-0}" = 1 ]; then
	run "cd /sys/src && mk install >/tmp/mk.log >[2=1]; st=\$status; tail -3 /tmp/mk.log; ~ \$st ''" 3600 || fail "mk install"
	run "grep -s '^mk: .*(exit status|know how)|undefined' /tmp/mk.log; ! ~ \$status ''" 60 || fail "mk install: errors"
	run "cd /sys/src/9/pc64 && mk >/tmp/k.log >[2=1]; st=\$status; tail -2 /tmp/k.log; ~ \$st ''" 1800 || fail kernel
	run "cd /sys/src/boot/efi && mk bootx64.efi >/tmp/l.log >[2=1]; st=\$status; md5sum bootx64.efi; ~ \$st ''" 300 || fail loader
	run "cd /sys/src/9/pc64 && md5sum 9pc64 && size 9pc64; cat /n/adm/status | sed 2q; echo check >/n/adm/ctl && echo checked" 600 || fail "check after build"
fi
if [ -n "$KTEST" ]; then
	run "ktest $KTEST" 1800 40 || true
	grep -q '^KTEST-DONE' "$vm/out" || fail "ktest: not done"
	grep -q '^KTEST .* FAIL' "$vm/out" && fail "ktest"
fi
halt
[ "$fails" = 0 ] || { echo "FAIL: $fails (boot)"; exit 1; }

# crashes: QEMU killed (no sync, no halt) while files are written; each
# next boot must load, check clean and have what was synced before
n=${CRASH:-0}
for r in $(seq 1 $((n > 0 ? n+1 : 0))); do
	boot || exit 1
	run "mount -c /srv/gefs.root /n/adm adm && echo check >/n/adm/ctl && echo checked; cat /usr/glenda/synced$((r-1)) /usr/glenda/crashcount >[2]/dev/null; echo done" 600 30 || fail "crash $r: check"
	grep -q '^checked' "$vm/out" || fail "crash $r: not checked"
	[ $r -gt 1 ] && { grep -q "^synced $((r-1))" "$vm/out" || fail "crash $r: synced$((r-1)) lost"; }
	[ "$fails" = 0 ] || { echo "FAIL: $fails (crash $r)"; exit 1; }
	[ $r -gt $n ] && { halt; break; }
	run "rm -f /tmp/crash*; echo synced $r >/usr/glenda/synced$r && echo sync >/n/adm/ctl && echo synced" 300 || fail "crash $r: sync"
	run "{ for(i in \`{seq 1 100000}){ dd -if /dev/zero -of /tmp/crash\$i -bs 16384 -count 8; echo \$i >/usr/glenda/crashcount; if(~ \$i *0) rm -f /tmp/crash*[1-5] } } >/dev/null >[2=1] &" 30 || fail "crash $r: writer"
	t=$((3 + RANDOM % 15))
	sleep $t
	echo "=== crash $r: QEMU killed after ${t}s of writing"
	kill -9 $q; wait $q 2>/dev/null || true; q=
done
[ "$fails" = 0 ] || { echo "FAIL: $fails (crashes)"; exit 1; }
stop

# the partition checked and read back in the build VM (userspace gefs)
part=$b/gefspart.img
rm -f "$part"
mib=$(( $(stat -c %s "$disk") / 1048576 - 66 ))
dd if="$disk" of="$part" bs=1M skip=65 count=$mib conv=sparse status=none
echo "### gefsck"
"$here/gefsck" "$part" /usr/glenda/gefshello /usr/glenda/bigsum > "$vm/out" 2>&1 || fail gefsck
sed 's/^/	/' "$vm/out" | tail -5
grep -q '^ok' "$vm/out" || fail "gefsck: not ok"
grep -q '^hello' "$vm/out" || fail "gefsck: no hello"
rm -f "$part"
[ "$fails" = 0 ] && echo "PASS: gefs root" || { echo "FAIL: $fails"; exit 1; }
