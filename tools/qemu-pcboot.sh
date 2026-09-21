#!/bin/bash
# Boot build/9pcboot through the UEFI loader in QEMU+OVMF and show the serial
# console. Needs qemu-system-x86_64 and /usr/share/ovmf/OVMF.fd (no KVM needed).
#   SECONDS_TO_RUN  default 60
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); b=$root/build
esp=$b/esp; rm -rf "$esp" "$b/serial.log"; mkdir -p "$esp/EFI/BOOT"
cp "$b/bootx64.efi" "$esp/EFI/BOOT/BOOTX64.EFI"; cp "$b/9pcboot" "$esp/9pcboot"
printf 'bootfile=/9pcboot\nconsole=0\n' > "$esp/plan9.ini"
qemu-system-x86_64 -machine q35 -m 1024 -smp 2 -cpu max -bios /usr/share/ovmf/OVMF.fd -vga std \
  -drive file=fat:rw:"$esp",format=raw,if=none,id=esp -device ide-hd,drive=esp,bootindex=0 \
  -display none -serial file:"$b/serial.log" &
q=$!
sleep "${SECONDS_TO_RUN:-60}"; kill $q 2>/dev/null || true
tr -d '\r' < "$b/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' | sed -n '/^\*acpi/,$p'
grep -q 'boot: /boot/boot reached' "$b/serial.log" && echo "PASS: reached /boot/boot" || { echo "FAIL: /boot/boot not reached"; exit 1; }
