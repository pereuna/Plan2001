#!/bin/bash
# Boot a target's kernel through its UEFI loader (build/$TARGET, from
# tools/build.sh) in QEMU with UEFI firmware (no root disk, so it stops at
# the bootargs prompt once the kernel is up) and check the serial log: PASS
# when bootargs is reached with exactly one Plan 9 banner.  The ESP is a FAT
# image made with mtools (build/$TARGET/esp.img), ready to be dd'd to a USB
# stick for real-hardware testing.
#   TARGET          amd64 (default); see tools/targets
#   SECONDS_TO_RUN  give up after this many seconds, default 75
#   DISPLAY_QEMU=1  also show the screen in a GTK window
#   MEM             guest RAM in MiB, default 2048
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
case " ${qemuargs[*]} " in
*" kvm "*)
	[ -r /dev/kvm ] && [ -w /dev/kvm ] || {
		echo "test-qemu: no access to /dev/kvm (add yourself to group kvm and log in again)" >&2; exit 1; } ;;
esac
rm -f "$b/esp.img" "$b/serial.log" "$b/test-vars.fd"
printf 'bootfile=/%s\nconsole=0\n' "$kernel" > "$b/plan9.ini"
mformat -C -i "$b/esp.img" -T 65536 -h 64 -s 32 ::
mmd -i "$b/esp.img" ::/EFI ::/EFI/BOOT
mcopy -i "$b/esp.img" "$b/$loader" "::/EFI/BOOT/$espname"
mcopy -i "$b/esp.img" "$b/$kernel" "$b/plan9.ini" ::/
cp "$fwvars" "$b/test-vars.fd"

display=(-display none)
[ "${DISPLAY_QEMU:-0}" = 1 ] && display=(-display gtk)
"$qemu" -name plan2001-test "${qemuargs[@]}" -m "${MEM:-2048}" -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file="$fwcode" \
  -drive if=pflash,format=raw,file="$b/test-vars.fd" -nic none "${display[@]}" \
  -drive file="$b/esp.img",format=raw,if=none,id=esp -device ide-hd,drive=esp,bootindex=0 \
  -serial file:"$b/serial.log" 2>"$b/qemu-test.log" &
q=$!
trap 'kill $q 2>/dev/null || true' EXIT
end=$((SECONDS + ${SECONDS_TO_RUN:-75}))
until grep -q '^bootargs is' "$b/serial.log" 2>/dev/null || [ $SECONDS -ge $end ] || ! kill -0 $q 2>/dev/null; do
  sleep 1
done
sleep 1
[ "${DISPLAY_QEMU:-0}" = 1 ] || kill $q 2>/dev/null || true
tr -d '\r' < "$b/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' | sed -n '/^acpi=/,$p' | grep -v '^0x0' | head -45
n=$(tr -d '\r' < "$b/serial.log" | grep -c '^Plan 9')
grep -q '^bootargs is' "$b/serial.log" && [ "$n" = 1 ] && echo "PASS: kernel reached the bootargs prompt" || { echo "FAIL (banners: $n)"; exit 1; }
[ "${DISPLAY_QEMU:-0}" = 1 ] && { trap - EXIT; echo "QEMU window left running (pid $q)"; }
exit 0
