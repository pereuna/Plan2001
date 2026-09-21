#!/bin/bash
# Boot build/9pcboot through the UEFI loader in QEMU+OVMF and show the serial
# console. Needs qemu-system-x86_64 and /usr/share/ovmf/OVMF.fd (no KVM needed).
#   SECONDS_TO_RUN  default 60
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); b=$root/build
esp=$b/esp; rm -rf "$esp" "$b/serial.log" "$b/mon.sock" "$b/screen.ppm"; mkdir -p "$esp/EFI/BOOT"
cp "$b/bootx64.efi" "$esp/EFI/BOOT/BOOTX64.EFI"; cp "$b/9pcboot" "$esp/9pcboot"
printf 'bootfile=/9pcboot\nconsole=0\n' > "$esp/plan9.ini"
qemu-system-x86_64 -machine q35 -m 1024 -smp 2 -cpu max -bios /usr/share/ovmf/OVMF.fd -vga std \
  -drive file=fat:rw:"$esp",format=raw,if=none,id=esp -device ide-hd,drive=esp,bootindex=0 \
  -display none -serial file:"$b/serial.log" -monitor unix:"$b/mon.sock",server,nowait &
q=$!
sleep "${SECONDS_TO_RUN:-60}"
echo "screendump $b/screen.ppm" | nc -U -q1 "$b/mon.sock" >/dev/null 2>&1; sleep 1
kill $q 2>/dev/null || true
tr -d '\r' < "$b/serial.log" | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' | sed -n '/^acpi=/,$p'
grep -q 'boot: /boot/boot reached' "$b/serial.log" && echo "PASS: reached /boot/boot" || { echo "FAIL: /boot/boot not reached"; exit 1; }

# bootfb markers: five squares (24px, 8px gap, 16px margin) at the top left.
# white x4 then green = reached /boot/boot; a red bar on top = panic.
python3 - "$b/screen.ppm" "$b/screen.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); im.save(sys.argv[2])
name = {(255,255,255):'white',(0,255,0):'green',(48,48,48):'dark',(255,0,0):'RED'}
marks = [name.get(im.getpixel((16+i*32+12, 16+12)), str(im.getpixel((16+i*32+12, 16+12)))) for i in range(5)]
bar = im.getpixel((100, 5)) == (255,0,0)
print("markers:", " ".join(marks), "| panic bar:", "YES" if bar else "no")
ok = marks == ['white']*4+['green'] and not bar
print("PASS: framebuffer markers show /boot/boot reached" if ok else "FAIL: framebuffer markers")
sys.exit(0 if ok else 1)
PY
