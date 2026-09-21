#!/bin/bash
# Run the installed 9front VM under WSL's own QEMU (software emulation, no KVM here).
# rcpu is forwarded to WSL's 127.0.0.1:17019; the monitor is a unix socket so
# screendump/sendkey work while nobody sits at the machine.
#   VMDIR   directory with 9front.qcow2 (default ~/vm9front)
#   MEM     guest RAM in MB (default 2048)     SMP  vCPUs (default 2)
set -e
VMDIR=${VMDIR:-$HOME/vm9front}; cd "$VMDIR"
rm -f mon.sock serial.log
exec qemu-system-x86_64 -name 9front -machine q35 -m "${MEM:-2048}" -smp "${SMP:-2}" -cpu max \
  -bios /usr/share/ovmf/OVMF.fd -vga std \
  -drive file=9front.qcow2,if=none,id=hd,format=qcow2 -device ide-hd,drive=hd,bus=ide.0,bootindex=1 \
  -nic user,hostfwd=tcp:127.0.0.1:17019-:17019 \
  -display none -monitor unix:"$VMDIR/mon.sock",server,nowait -serial file:"$VMDIR/serial.log"
