@echo off
rem 9front VM, normal run from the installed disk.
"C:\Program Files\qemu\qemu-system-x86_64.exe" -name 9front -accel whpx,kernel-irqchip=off -machine q35 -cpu qemu64 -smp 4 -m 4096 -bios "%~dp0OVMF.fd" -vga std -display gtk -drive file="%~dp09front.qcow2",if=none,id=hd,format=qcow2 -nic user,hostfwd=tcp::17019-:17019 -device ide-hd,drive=hd,bus=ide.0,bootindex=1
