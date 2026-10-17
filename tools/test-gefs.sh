#!/bin/bash
# gefs as the root in the kernel (devgefs, docs/gefs.md) in QEMU, three
# boots over the serial console (tools/9run's relay):
#   1. the ext4 root (tools/selfroot) reams a gefs partition on a second
#      disk with its own gefs (sys/src/cmd/gefs) and copies itself there;
#   2. root=-w PARTUUID= of that partition: the kernel's gefs is the
#      root; files written, a program built, the console's check, the
#      adm tree's ctl and status (/srv/gefs.root), then fshalt;
#   3. the ext4 root again: gefs -c of the partition, and the files of
#      boot 2 read back with the userspace gefs.
#   tools/test-gefs.sh
#   NOROOT=1    build/selfroot as it is (else tools/selfroot makes it)
#   GEFSMB      the gefs disk in MiB (default 4096)
#   KEEP=1      leave QEMU running after a failure (tools/9run -d build/$TARGET/gefsvm)
#   ONLY=2      boot 2 (and 3) on the disks of an earlier run (3: boot 3), with the
#               kernel of build/$TARGET; INI="line ..." more plan9.ini lines
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
tree=$root/build/selfroot
vm=$b/gefsvm
disk=$b/gefsboot.img
gdisk=$b/gefsdisk.img
if [ "${ONLY:-}" = 2 ] || [ "${ONLY:-}" = 3 ]; then
	. "$b/gefs-run.sh"
	mcopy -o -i "$disk@@1M" "$b/$kernel" ::/
else
[ "${NOROOT:-0}" = 1 ] && [ -d "$tree/.git" ] || "$here/selfroot" "$tree"

# the boot disk: the ESP and the ext4 root (as test-selfroot.sh)
mkfs=$(command -v mkfs.ext4 || echo /sbin/mkfs.ext4)
uuid=$(python3 -I -c 'import uuid; print(uuid.uuid4())')
size=$(( $(du -sm "$tree" | cut -f1) + 1024 ))
rm -f "$b/root.ext4"
"$mkfs" -q -F -t ext4 -b 4096 -L plan2001 -U "$uuid" -E root_owner="$(id -u):$(id -g)" -d "$tree" "$b/root.ext4" "${size}M"
echo "$uuid" > "$b/root.uuid"; : > "$b/root.dir"
CONSOLE=0 NOROOT=1 RW=1 INIT=/init "$here/ext4disk" "$disk"
cp "$b/ext4plan9.ini" "$b/gefs-ext4.ini"

# the gefs disk: GPT, one Plan 9 partition from sector 2048, its PARTUUID known
mb=${GEFSMB:-4096}
sectors=$((mb*2048))
psize=$((sectors - 4096))
puuid=$(python3 -I -c 'import uuid; print(uuid.uuid4())')
rm -f "$gdisk"; truncate -s ${mb}M "$gdisk"
sfdisk=$(command -v sfdisk || echo /sbin/sfdisk)
printf 'label: gpt\nstart=2048, size=%d, type=C91818F9-8025-47AF-89D2-F030D7000C2C, uuid=%s, name=gefs\n' \
	$psize "$puuid" | "$sfdisk" -q "$gdisk"
printf 'sectors=%d\npsize=%d\npuuid=%s\n' $sectors $psize "$puuid" > "$b/gefs-run.sh"
fi
{ sed "s/^root=.*/root=-w PARTUUID=$puuid/" "$b/gefs-ext4.ini"; for l in $INI; do echo "$l"; done; } > "$b/gefs-gefs.ini"

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
# boot with plan9.ini $1; 0 at the prompt
boot() {
	stop
	echo "=== boot: $(grep '^root=' "$1")"
	mcopy -o -i "$disk@@1M" "$1" ::/plan9.ini
	rm -f "$vm"/*.sock "$vm/serial.in"
	: > "$vm/serial.log"
	cp "$fwvars" "$vm/vars.fd"
	"$qemu" -name plan2001-gefs -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp "${SMP:-2}" \
		-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
		-drive if=pflash,format=raw,file="$vm/vars.fd" \
		-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0,bootindex=0 \
		-drive file="$gdisk",format=raw,if=none,id=d1 -device virtio-blk-pci,drive=d1 \
		-nic none -display none \
		-chardev socket,id=ser,path="$vm/serial.sock",server=on,wait=off -serial chardev:ser \
		2>"$vm/qemu.log" &
	q=$!
	mkfifo "$vm/serial.in"
	python3 "$here/9run" --relay "$vm" </dev/null >/dev/null 2>&1 &
	relay=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-150}))
	until clean | grep -q 'plan2001% \|^panic' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do sleep 1; done
	clean | grep -a '^gefs\|^init:\|^root='
	clean | grep -q '^panic' && { clean | tail -30; fail "panic"; return 1; }
	clean | grep -q 'plan2001% ' || { clean | tail -30; fail "no prompt"; return 1; }
}
halt() {
	run 'fshalt' 180 || true
	local end=$((SECONDS + 120))
	until clean | grep -q 'done halting' || [ $SECONDS -ge $end ]; do sleep 1; done
	clean | grep -q 'done halting' || fail "fshalt"
}
# the gefs disk's partition as /dev/sdXX/gefs: the disk of $sectors sectors
part="d=(); for(c in /dev/sd*/ctl) if(grep -s '^geometry '$sectors' ' \$c) d=\`{basename -d \$c}; \
	echo part gefs 2048 $((2048 + psize)) >\$d(1)^/ctl; g=\$d(1)^/gefs; echo \$g"

# 1: ream and copy the ext4 root to gefs
if [ -z "${ONLY:-}" ]; then
boot "$b/gefs-ext4.ini" || exit 1
run "$part; gefs -r glenda -f \$g && gefs -A -n gt -f \$g && mount -c /srv/gt /n/g && ls /n/g" 120 || fail ream
run "@{cd '#E/root' && tar c .} | @{cd /n/g && tar x}; du -s /n/g; ls /n/g" 1800 || fail copy
run "test -x /n/g/amd64/bin/rc && test -r /n/g/init && echo copied" 60 || fail "copy: no rc or init"
# synced before the unmount: gefs drops an unmounted tree's writes since its last sync
run "mount -c /srv/gt /n/a adm && echo sync >/n/a/ctl && unmount /n/a && unmount /n/g; echo halt >>/srv/gt.cmd; for(i in 1 2 3 4 5 6 7 8 9 10) if(test -e /srv/gt.cmd) sleep 1; ! test -e /srv/gt.cmd" 60 || fail "userspace gefs halt"
halt
[ "$fails" = 0 ] || { echo "FAIL: $fails (boot 1)"; exit 1; }
fi

# 2: the kernel's gefs as the root
if [ "${ONLY:-}" != 3 ]; then
boot "$b/gefs-gefs.ini" || exit 1
clean | grep -q '^gefs: .*read-write' || fail "no gefs line"
run "ls /amd64/bin | wc -l; cat /dev/sysname" || fail root
run "echo gefs root >/tmp/gefstest && cat /tmp/gefstest && rm /tmp/gefstest && echo hello >/usr/glenda/gefshello && cat /usr/glenda/gefshello" || fail write
run "dd -if /dev/zero -of /tmp/big -bs 65536 -count 512 >[2]/dev/null; ls -l /tmp/big; md5sum </tmp/big >/usr/glenda/bigsum; cat /usr/glenda/bigsum" 120 || fail "big write"
run "cd /sys/src/cmd && 6c -FTVw cat.c && 6l -o /tmp/6.cat cat.6 && /tmp/6.cat /usr/glenda/gefshello && rm cat.6" 120 || fail "build"
run "mount -c /srv/gefs.root /n/adm adm && cat /n/adm/status && echo check >/n/adm/ctl && echo checked" 120 30 || fail "adm ctl"
run "echo snap main gefstest >/n/adm/ctl && sleep 2 && cat /n/adm/status | grep gefstest" 60 || fail snap
halt
clean | grep -q 'gefs: halted' || fail "gefs not halted"
[ "$fails" = 0 ] || { echo "FAIL: $fails (boot 2)"; exit 1; }
fi

# 3: checked and read back by the userspace gefs
boot "$b/gefs-ext4.ini" || exit 1
run "$part; gefs -c -f \$g && echo gefs-check-ok" 600 30 || fail "gefs -c"
grep -q gefs-check-ok "$vm/out" || fail "gefs -c: not ok"
run "$part; gefs -R -A -n gt -f \$g && mount -c /srv/gt /n/g && cat /n/g/usr/glenda/gefshello /n/g/usr/glenda/bigsum" 120 || fail "read back"
grep -q '^hello' "$vm/out" || fail "read back: no hello"
halt
stop
[ "$fails" = 0 ] && echo "PASS: gefs root" || { echo "FAIL: $fails"; exit 1; }
