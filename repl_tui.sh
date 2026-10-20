#!/usr/bin/env bash
cd ~/Plan2001
. tools/target.sh

cp "$fwvars" build/amd64/poc-vars.fd

"$qemu" -name plan2001-poc -nodefaults "${qemuargs[@]}" \
  -m 2048 -smp 8 \
  -drive if=pflash,format=raw,readonly=on,file="$fwcode" \
  -drive if=pflash,format=raw,file=build/amd64/poc-vars.fd \
  -drive file=build/amd64/repl.img,format=raw,if=none,id=d0 \
  -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0 \
  -nic none -display none -serial mon:stdio
