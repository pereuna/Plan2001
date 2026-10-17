#!/bin/bash
# Boot Plan2001 with its root file system in the kernel's ext4 (#E,
# devext4) in QEMU with UEFI: tools/ext4disk's disk (ESP and ext4 root,
# CONSOLE=0) on DISK, the kernel finds the root by its uuid, init and
# termrc run from it and tools/ext4root's profile checks it (EXT4OK /
# EXT4FAIL lines).  PASS when every boot ends in EXT4ROOT-DONE with no
# EXT4FAIL and no panic, and every check of the disk on the host passes.
#   TARGET          amd64 (default)
#   DISK            ahci (default), virtio, nvme (sdnvme, like silli's disk)
#                   or usb (xHCI, nusb/disk: like a USB stick)
#   SECONDS_TO_RUN  give up on a boot after this many seconds, default 150
#   DISPLAY_QEMU=1  also show the screen in a GTK window (one boot)
#   MEM, SMP        guest RAM in MiB (2048) and processors (2)
#   NOROOT=1        use build/$TARGET/root.ext4 as it is
#   RW=1            the root written (mount -w), booted twice: the profile's
#                   write checks (the second boot also finds the first's),
#                   after each e2fsck -fn of the root partition and its
#                   files read back with debugfs
#                   (the second boot ends in 9front's fshalt: the root
#                   must be left clean, its journal stopped)
#   ESPDIR=EFI/BOOT plan9.ini and the kernel in the loader's directory
#   KTEST="all"     the kernel tests (tools/ktest, ktest's arguments) run
#                   by the root's /init, written root: every KTEST line
#                   OK, then e2fsck -fn clean
#   KTESTAT=profile ktest run by the profile, in init's name space (as
#                   typed at term%), not by /init before init
#   CRASH=1         RW, but the first boot is killed (QEMU, no sync) while
#                   it writes: e2fsck -fy of a copy may only replay the
#                   journal (and fix the superblock's free counts); the
#                   second boot, lwext4's replay, must pass and sync clean
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
disk=$b/ext4disk.img
[ -n "$CRASH" ] && RW=1
if [ -n "$KTEST" ]; then
	RW=1 INIT=/init SECONDS_TO_RUN=${SECONDS_TO_RUN:-1800}
	export INIT
fi
export RW
CONSOLE=0 "$here/ext4disk" "$disk"
e2fsck=$(command -v e2fsck || echo /sbin/e2fsck)
debugfs=$(command -v debugfs || echo /sbin/debugfs)
dir=$(cat "$b/root.dir" 2>/dev/null || true)
part=$b/ext4part.img
log=$b/ext4-serial.log
q=
trap '[ -n "$q" ] && kill $q 2>/dev/null || true' EXIT
fails=0
fail() { echo "FAIL: $*"; fails=$((fails+1)); }
clean() { tr -d '\r' < "$log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g'; }

# plan9.ini on the disk's ESP, with extra lines
ini() {
	cp "$b/ext4plan9.ini" "$b/ext4plan9-run.ini"
	for l in "$@"; do echo "$l" >> "$b/ext4plan9-run.ini"; done
	mcopy -o -i "$disk@@1M" "$b/ext4plan9-run.ini" ::/${ESPDIR:+$ESPDIR/}plan9.ini
}

# boot NAME [UNTIL]: QEMU until EXT4ROOT-DONE (or UNTIL), a panic or the
# time; killed then (kill -9: no sync)
boot() {
	local name=$1 until=${2:-EXT4ROOT-DONE}
	rm -f "$log" "$b/test-vars.fd"
	cp "$fwvars" "$b/test-vars.fd"
	case ${DISK:-ahci} in
	ahci)	dev=(-drive file="$disk",format=raw,if=none,id=d0 -device ide-hd,drive=d0,bootindex=0) ;;
	virtio)	dev=(-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0,bootindex=0) ;;
	nvme)	dev=(-drive file="$disk",format=raw,if=none,id=d0 -device nvme,serial=plan2001,drive=d0,bootindex=0) ;;
	usb)	dev=(-device qemu-xhci,id=xhci -drive file="$disk",format=raw,if=none,id=d0
		     -device usb-storage,bus=xhci.0,drive=d0,bootindex=0) ;;
	*)	echo "test-ext4: DISK is ahci, virtio, nvme or usb" >&2; exit 2 ;;
	esac
	display=(-display none)
	[ "${DISPLAY_QEMU:-0}" = 1 ] && display=(-display gtk)
	# -nodefaults: no q35 CD drive (legacy); the display, serial and disks are given
	"$qemu" -name plan2001-ext4 -nodefaults "${qemuargs[@]}" -m "${MEM:-2048}" -smp "${SMP:-2}" \
	  -drive if=pflash,format=raw,readonly=on,file="$fwcode" \
	  -drive if=pflash,format=raw,file="$b/test-vars.fd" -nic none "${display[@]}" \
	  "${dev[@]}" -serial file:"$log" 2>"$b/qemu-ext4.log" &
	q=$!
	local end=$((SECONDS + ${SECONDS_TO_RUN:-150}))
	until clean 2>/dev/null | grep -q "^$until\|^panic" || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
		sleep 1
	done
	if [ "$until" = EXT4CRASH-GO ]; then
		# into the writes, then pulled out
		sleep $((2 + RANDOM % 4))
	else
		sleep 2
	fi
	if [ "${DISPLAY_QEMU:-0}" = 1 ]; then
		trap - EXIT; echo "QEMU window left running (pid $q)"
	else
		kill -9 $q 2>/dev/null || true
		wait $q 2>/dev/null || true
	fi
	q=
	cp "$log" "$b/ext4-serial-$name.log"
	echo "=== boot $name"
	clean | sed -n '/^#E\//,$p' | grep -v '^EXT4OK' | head -40
	local ok=$(clean | grep -c '^EXT4OK' || true)
	local bad=$(clean | grep -c '^EXT4FAIL' || true)
	clean | grep -q '^panic' && fail "$name: panic"
	clean | grep -q "^$until" || fail "$name: no $until"
	[ "$bad" = 0 ] || fail "$name: $bad checks failed"
	echo "boot $name: $ok checks ok, $bad failed"
}

# the root partition as Linux's e2fsck and debugfs see it
fsck() {
	local name=$1
	dd if="$disk" of="$part" bs=1M skip=65 status=none
	if "$e2fsck" -fn "$part" > "$b/ext4-fsck-$name.log" 2>&1 && ! grep -q 'Fix? no\|ERROR\|errors' "$b/ext4-fsck-$name.log"; then
		echo "$name: e2fsck -fn clean"
	else
		fail "$name: e2fsck -fn"; head -30 "$b/ext4-fsck-$name.log"
	fi
}

files() {
	local name=$1 want got n
	want=$(md5sum < "$b/../ext4root${dir}/plan2001test/big" | cut -d' ' -f1)
	got=$("$debugfs" -R "cat $dir/plan2001test/w/big2" "$part" 2>/dev/null | md5sum | cut -d' ' -f1)
	[ "$got" = "$want" ] && echo "$name: debugfs: big2 as written" || fail "$name: debugfs: big2 $got != $want"
	# ls -p: /inode/mode/uid/gid/name/size/; a removed first entry of a block is inode 0
	n=$("$debugfs" -R "ls -p $dir/plan2001test/w/d" "$part" 2>/dev/null | awk -F/ '$2 != 0 && $6 ~ /^f[0-9]+$/' | wc -l)
	[ "$n" = 189 ] && echo "$name: debugfs: 189 files in w/d" || fail "$name: debugfs: $n files in w/d, not 189"
}

if [ -n "$KTEST" ]; then
	if [ "$KTESTAT" = profile ]; then ini "ktestprofile=$KTEST"; else ini "ktest=$KTEST"; fi
	boot ktest KTEST-DONE
	clean | grep '^KTEST'
	clean | grep -q '^KTEST .* FAIL' && fail "ktest: $(clean | grep -c '^KTEST .* FAIL') failed"
	fsck ktest
elif [ -z "$RW" ]; then
	boot ro
elif [ -z "$CRASH" ]; then
	boot rw1
	fsck rw1; files rw1
	ini ext4halt=1
	boot rw2 'done halting'
	clean | grep -q '^EXT4OK persist' || fail "rw2: no persist check"
	clean | grep -q '#E/root: halted' || fail "rw2: fshalt did not halt #E/root"
	fsck rw2; files rw2
	# halted: clean, the journal empty, as Linux leaves it at umount
	"$debugfs" -R stats "$part" 2>/dev/null | grep -q '^Filesystem state: *clean$' && echo "rw2: state clean" || fail "rw2: state not clean"
	"$debugfs" -R stats "$part" 2>/dev/null | grep -q 'needs_recovery' && fail "rw2: needs_recovery" || echo "rw2: no needs_recovery"
	ini
else
	ini ext4crash=1
	boot crash EXT4CRASH-GO
	dd if="$disk" of="$part" bs=1M skip=65 status=none
	"$e2fsck" -fy "$part" > "$b/ext4-fsck-crash.log" 2>&1 || true
	# the journal replayed, the superblock's free counts: a crash's
	nfix=$(grep -c 'Fix?' "$b/ext4-fsck-crash.log" || true)
	nfree=$(grep -c '^Free \(blocks\|inodes\) count wrong' "$b/ext4-fsck-crash.log" || true)
	if [ "$nfix" != "$nfree" ] || grep -q 'ERROR\|corrupt\|UNEXPECTED' "$b/ext4-fsck-crash.log"; then
		fail "crash: e2fsck -fy found more than a crash's"; head -40 "$b/ext4-fsck-crash.log"
	else
		echo "crash: e2fsck -fy: $(grep -c 'recovering journal' "$b/ext4-fsck-crash.log") journal recovery, nothing else"
	fi
	ini
	boot replay
	fsck replay; files replay
fi
[ "$fails" = 0 ] && echo "PASS: root on #E (DISK=${DISK:-ahci}${RW:+, written}${CRASH:+, crash})" || { echo "FAIL: $fails"; exit 1; }
exit 0
