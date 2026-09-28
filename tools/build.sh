#!/bin/bash
# Build a target's kernel and UEFI loader in the 9front VM, with this
# repo's changed files laid over the VM's own (complete, current) source
# tree - see tools/build.rc for exactly what gets overlaid.
#   TARGET  amd64 (default); see tools/targets
#   WHAT    all (default) or loader: the UEFI loader alone
# Results: build/$TARGET/<kernel>, <loader>, *.log (amd64: 9pc64, bootx64.efi).
#
# Boots a throwaway overlay of the base VM (tools/vm, set up once by
# tools/vm-setup), hands it sys/ and build.rc as a tar on a raw disk image,
# runs build.rc over the serial console (tools/9run) and unpacks the tar the
# VM wrote onto the output disk.  (Raw tar, not FAT: FAT cannot hold a
# directory named aux.)  No network, no display.  The whole serial
# session is in build/vm/serial.log, the build's output in build/$TARGET/session.log.
#   BUILD_TIMEOUT  seconds, default 900
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
out=$tbuild; b=$root/build
mkdir -p "$out"; rm -f "$out/$kernel" "$out/$loader" "$out"/*.log "$b"/in.img "$b"/out.img

cp "$here/targets/$TARGET" "$b/target"; echo "what=${WHAT:-all}" >> "$b/target"
tar cf "$b/in.img" --format=ustar -C "$root" sys -C "$here" build.rc -C "$b" target
truncate -s 64M "$b/in.img" "$b/out.img"

trap '"$here/vm" stop' EXIT
"$here/vm" start
printf 'term%% \t!done\n' | "$here/9run" -t 180 --dialog - >/dev/null
set +e
"$here/9run" -t "${BUILD_TIMEOUT:-900}" '@{cd /tmp && tar xf /dev/sdE1/data build.rc target} && rc /tmp/build.rc' 2>&1 | cut -c1-200 | tee "$out/session.log"
st=${PIPESTATUS[0]}
set -e
"$here/vm" stop; trap - EXIT

tar xmf "$b/out.img" -C "$out"
[ "${WHAT:-all}" = loader ] || results="$kernel"
results="$results $loader"
[ "$st" = 0 ] && grep -q BUILD-DONE "$out/session.log" || { echo "build failed"; exit 1; }
for f in $results; do [ -s "$out/$f" ] || { echo "build failed: no $f"; exit 1; }; done
echo "results in $out: $(cd "$out" && ls -l $results | awk '{print $5, $9}' | tr '\n' ' ')"
