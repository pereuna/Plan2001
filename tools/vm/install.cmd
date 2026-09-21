@echo off
rem 9front VM, installer run: boots the official ISO, installs to 9front.qcow2 (same folder).
rem Needs -accel whpx,kernel-irqchip=off (default irqchip reboots 9front in a loop here).
"C:\Program Files\qemu\qemu-system-x86_64.exe" -name 9front -accel whpx,kernel-irqchip=off -machine q35 -cpu qemu64 -smp 4 -m 4096 -bios "%~dp0OVMF.fd" -vga std -display gtk -drive file="%~dp09front.qcow2",if=none,id=hd,format=qcow2 -nic user,hostfwd=tcp::17019-:17019 -device ide-hd,drive=hd,bus=ide.0,bootindex=2 -drive file="C:\Users\PetteriReunamo\Downloads\9front-11952.amd64.iso",if=none,id=cd,media=cdrom,readonly=on -device ide-cd,drive=cd,bus=ide.1,bootindex=1
