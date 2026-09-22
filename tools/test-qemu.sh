#!/bin/bash
# Boot build/9pc64 through build/bootx64.efi in QEMU+OVMF (no root disk, so
# it stops at the bootargs prompt once the kernel is up) and check the boot
# progress markers (see sys/src/9/pc/bootfb.c) via a screenshot.
#   SECONDS_TO_RUN  default 75
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); b=$root/build
esp=$b/esp; rm -rf "$esp" "$b/serial.log" "$b/mon.sock" "$b/screen.ppm"; mkdir -p "$esp/EFI/BOOT"
cp "$b/bootx64.efi" "$esp/EFI/BOOT/BOOTX64.EFI"; cp "$b/9pc64" "$esp/9pc64"
printf 'bootfile=/9pc64\nconsole=0\n' > "$esp/plan9.ini"
qemu-system-x86_64 -machine q35 -m 2048 -smp 2 -cpu max -bios /usr/share/ovmf/OVMF.fd -vga std \
  -drive file=fat:rw:"$esp",format=raw,if=none,id=esp -device ide-hd,drive=esp,bootindex=0 \
  -display none -serial file:"$b/serial.log" -monitor unix:"$b/mon.sock",server,nowait &
q=$!
sleep "${SECONDS_TO_RUN:-75}"
echo "screendump $b/screen.ppm" | nc -U -q1 "$b/mon.sock" >/dev/null 2>&1; sleep 1
kill $q 2>/dev/null || true
tr -d '\r' < "$b/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' | sed -n '/^acpi=/,$p' | grep -v '^0x0' | head -45
convert "$b/screen.ppm" "$b/screen.png" 2>/dev/null && echo "screenshot: $b/screen.png"
n=$(tr -d '\r' < "$b/serial.log" | grep -c '^Plan 9')
grep -q '^bootargs is' "$b/serial.log" && [ "$n" = 1 ] && echo "PASS: kernel reached the bootargs prompt" || { echo "FAIL (banners: $n)"; exit 1; }
