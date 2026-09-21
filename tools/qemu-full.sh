#!/bin/bash
# Boot build/9pc64-full through the UEFI loader in QEMU+OVMF (no root disk, so
# it stops at the bootargs prompt) and take a screenshot of the console.
#   SECONDS_TO_RUN  default 75
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); b=$root/build
esp=$b/esp-full; rm -rf "$esp" "$b/serial-full.log" "$b/mon-full.sock" "$b/screen-full.ppm"; mkdir -p "$esp/EFI/BOOT"
cp "$b/bootx64.efi" "$esp/EFI/BOOT/BOOTX64.EFI"; cp "$b/9pc64-full" "$esp/9pc64"
printf 'bootfile=/9pc64\nconsole=0\n' > "$esp/plan9.ini"
qemu-system-x86_64 -machine q35 -m 2048 -smp 2 -cpu max -bios /usr/share/ovmf/OVMF.fd -vga std \
  -drive file=fat:rw:"$esp",format=raw,if=none,id=esp -device ide-hd,drive=esp,bootindex=0 \
  -display none -serial file:"$b/serial-full.log" -monitor unix:"$b/mon-full.sock",server,nowait &
q=$!
sleep "${SECONDS_TO_RUN:-75}"
echo "screendump $b/screen-full.ppm" | nc -U -q1 "$b/mon-full.sock" >/dev/null 2>&1; sleep 1
kill $q 2>/dev/null || true
tr -d '\r' < "$b/serial-full.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' | sed -n '/^acpi=/,$p' | grep -v '^0x0' | head -45
convert "$b/screen-full.ppm" "$b/screen-full.png" 2>/dev/null && echo "screenshot: $b/screen-full.png"
n=$(tr -d '\r' < "$b/serial-full.log" | grep -c '^Plan 9')
grep -q '^bootargs is' "$b/serial-full.log" && [ "$n" = 1 ] && echo "PASS: full kernel reached the bootargs prompt" || { echo "FAIL: full kernel (banners: $n)"; exit 1; }
