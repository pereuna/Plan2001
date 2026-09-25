#!/bin/bash
# Build the pc64 kernel and the UEFI loader in the 9front VM, with this
# repo's changed files laid over the VM's own (complete, current) source
# tree - see tools/build.rc for exactly what gets overlaid.
# Results: build/9pc64, build/bootx64.efi, build/*.log.
#
# Boots a throwaway overlay of the base VM (tools/vm, set up once by
# tools/vm-setup), hands it sys/ and build.rc as a tar on a raw disk image,
# runs build.rc over the serial console (tools/9run) and unpacks the tar the
# VM wrote onto the output disk.  (Raw tar, not FAT: FAT cannot hold a
# directory named aux.)  No network, no display.  The whole serial
# session is in build/vm/serial.log, the build's output in build/session.log.
#   BUILD_TIMEOUT  seconds, default 900
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); out=$root/build
mkdir -p "$out"; rm -f "$out"/9pc64 "$out"/bootx64.efi "$out"/*.log "$out"/in.img "$out"/out.img

tar cf "$out/in.img" --format=ustar -C "$root" sys -C "$here" build.rc
truncate -s 64M "$out/in.img" "$out/out.img"

trap '"$here/vm" stop' EXIT
"$here/vm" start
printf 'term%% \t!done\n' | "$here/9run" -t 180 --dialog - >/dev/null
set +e
"$here/9run" -t "${BUILD_TIMEOUT:-900}" '@{cd /tmp && tar xf /dev/sdE1/data build.rc} && rc /tmp/build.rc' 2>&1 | cut -c1-200 | tee "$out/session.log"
st=${PIPESTATUS[0]}
set -e
"$here/vm" stop; trap - EXIT

tar xmf "$out/out.img" -C "$out"
[ "$st" = 0 ] && grep -q BUILD-DONE "$out/session.log" && [ -s "$out/9pc64" ] && [ -s "$out/bootx64.efi" ] || { echo "build failed"; exit 1; }
echo "results in $out: $(cd "$out" && ls -l 9pc64 bootx64.efi | awk '{print $5, $9}' | tr '\n' ' ')"
