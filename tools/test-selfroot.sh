#!/bin/bash
# The self-hosting root (tools/selfroot, docs/selfhost.md) in QEMU: its
# ext4 disk (written: root=-w), QEMU's user network (DHCP, DNS), and over
# the serial console (tools/9run's relay): the network (ip/ipconfig, dns,
# webfs: hget over https), git (git/clone from GitHub), the tree built by
# its own tools (mk install in /sys/src) and the kernel and loader with
# them, then fshalt and Linux's e2fsck.
#   tools/test-selfroot.sh
#   NOROOT=1    build/selfroot as it is (else tools/selfroot makes it)
#   NET=0       no network checks          BUILD=0   no self build
#   KEEP=1      leave QEMU running at the end (tools/9run -d build/$TARGET/selfvm)
#   BOOT=1      only boot it and leave it running (its console as above)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
tree=$root/build/selfroot
vm=$b/selfvm
[ "${NOROOT:-0}" = 1 ] && [ -d "$tree/.git" ] || "$here/selfroot" "$tree"

# the root: the tree and 2 GiB to build in
mkfs=$(command -v mkfs.ext4 || echo /sbin/mkfs.ext4)
uuid=$(python3 -I -c 'import uuid; print(uuid.uuid4())')
size=$(( $(du -sm "$tree" | cut -f1) + 2048 ))
rm -f "$b/root.ext4"
"$mkfs" -q -F -t ext4 -b 4096 -L plan2001 -U "$uuid" -E root_owner="$(id -u):$(id -g)" -d "$tree" "$b/root.ext4" "${size}M"
echo "$uuid" > "$b/root.uuid"; : > "$b/root.dir"
disk=$b/selfdisk.img
CONSOLE=0 NOROOT=1 RW=1 INIT=/init "$here/ext4disk" "$disk"

mkdir -p "$vm"; rm -f "$vm"/*.sock "$vm/serial.in"
: > "$vm/serial.log"
cp "$fwvars" "$vm/vars.fd"
"$qemu" -name plan2001-selfroot -nodefaults "${qemuargs[@]}" -m "${MEM:-4096}" -smp "${SMP:-4}" \
	-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
	-drive if=pflash,format=raw,file="$vm/vars.fd" \
	-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
	-nic user,model=virtio-net-pci-non-transitional -display none \
	-chardev socket,id=ser,path="$vm/serial.sock",server=on,wait=off -serial chardev:ser \
	2>"$vm/qemu.log" &
q=$!
mkfifo "$vm/serial.in"
python3 "$here/9run" --relay "$vm" </dev/null >/dev/null 2>&1 &
relay=$!
cleanup() {
	[ "${KEEP:-0}" = 1 ] && { echo "QEMU left running (pid $q), console: tools/9run -d $vm"; return; }
	kill $q $relay 2>/dev/null || true
}
trap cleanup EXIT

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

end=$((SECONDS + 120))
until clean | grep -q 'plan2001% ' || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do sleep 1; done
clean | grep -q '^panic' && { clean | tail -20; fail "panic"; exit 1; }
clean | grep -q 'plan2001% ' || { clean | tail -20; fail "no prompt"; exit 1; }
clean | grep '^init:'
[ "${BOOT:-0}" = 1 ] && { trap - EXIT; echo "QEMU running (pid $q, relay $relay), console: tools/9run -d $vm"; exit 0; }

run "cat '#E/ctl'; ls /amd64/bin | wc -l" || fail root
if [ "${NET:-1}" != 0 ]; then
	run 'cat /net/ndb' || fail ndb
	run 'echo github.com ip | ndb/dnsquery' || fail dns
	grep -q '[0-9]\+\.[0-9]\+\.[0-9]\+\.[0-9]\+' "$vm/out" || fail "dns: no address"
	run 'hget https://github.com/ | wc -c' 60 || fail hget
	run 'cd /tmp && rm -rf p && git/clone https://github.com/pereuna/Plan2001 p && cd p && git/log >/tmp/gl && sed 3q /tmp/gl' 900 || fail git/clone
	run "cd / && git/log >/tmp/gl && sed 3q /tmp/gl && git/fs && git/walk >/tmp/gw; wc -l </tmp/gw" 900 || fail "git in /"
fi
if [ "${BUILD:-1}" != 0 ]; then
	run "cd /sys/src && mk install >/tmp/mk.log >[2=1]; st=\$status; tail -3 /tmp/mk.log; ~ \$st ''" 1800 || fail "mk install"
	run "grep -s '^mk: .*(exit status|know how)|undefined' /tmp/mk.log; ! ~ \$status ''" 60 || fail "mk install: errors"
	run "cd /sys/src/9/pc64 && mk >/tmp/k.log >[2=1]; st=\$status; tail -2 /tmp/k.log; ~ \$st ''" 900 || fail kernel
	run "cd /sys/src/boot/efi && mk bootx64.efi >/tmp/l.log >[2=1]; st=\$status; md5sum bootx64.efi; ~ \$st ''" 300 || fail loader
	run 'cd /sys/src/9/pc64 && md5sum 9pc64 && size 9pc64' || true
fi
run 'fshalt' 120 || true
end=$((SECONDS + 60))
until clean | grep -q 'done halting' || [ $SECONDS -ge $end ]; do sleep 1; done
clean | grep -q 'done halting' || fail "fshalt"
kill $q 2>/dev/null || true; wait $q 2>/dev/null || true
part=$b/selfpart.img
dd if="$disk" of="$part" bs=1M skip=65 status=none
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
"$e2fsck" -fn "$part" > "$vm/fsck.log" 2>&1 && echo "e2fsck -fn clean" || { fail e2fsck; head -20 "$vm/fsck.log"; }
[ "$fails" = 0 ] && echo "PASS: self-hosting root" || { echo "FAIL: $fails"; exit 1; }
