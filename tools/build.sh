#!/bin/bash
# Build the repo's tree, kernel and UEFI loader in the 9front VM, its
# compilers only the bootstrap: the repo's sources, headers and mk
# definitions replace the VM's (tools/build.rc).
#   TARGET  amd64 (default); see tools/targets
#   WHAT    all (default): the tree, the kernel and the loader
#           kernel: the kernel, loader, kbdfs and ktest only (quick)
#           loader: the UEFI loader alone
#   PASSES  2: the tree a second time with its own tools (WHAT=all)
# Results: build/$TARGET/<kernel>, <loader>, kbdfs, ktest, hjfs, *.log
# (amd64: 9pc64, bootx64.efi) and, WHAT=all, root/$objtype/{bin,lib}: the
# binaries of a root (tools/selfroot).
#
# Boots a throwaway overlay of the base VM (tools/vm, set up once by
# tools/vm-setup), hands it the repo's tree (tools/overlay) and build.rc as a tar on a raw disk image,
# runs build.rc over the serial console (tools/9run) and unpacks the tar the
# VM wrote onto the output disk.  (Raw tar, not FAT: FAT cannot hold a
# directory named aux.)  No network, no display.  The whole serial
# session is in build/vm/serial.log, the build's output in build/$TARGET/session.log.
#   BUILD_TIMEOUT  seconds, default 1800
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
out=$tbuild; b=$root/build
mkdir -p "$out"; rm -rf "$out/$kernel" "$out/$loader" "$out/hjfs" "$out/kbdfs" "$out/ktest" "$out/root" "$out"/*.log "$b"/in.img "$b"/out.img

cp "$here/targets/$TARGET" "$b/target"; printf 'what=%s\npasses=%s\n' "${WHAT:-all}" "${PASSES:-1}" >> "$b/target"
# the repo's Plan 9 tree
"$here/overlay" "$b/overlay"
tar cf "$b/in.img" --format=ustar -C "$b/overlay" . -C "$here" build.rc -C "$b" target
truncate -s $(( $(stat -c %s "$b/in.img") / 1048576 + 16 ))M "$b/in.img"
truncate -s 512M "$b/out.img"

trap '"$here/vm" stop' EXIT
"$here/vm" start
printf 'term%% \t!done\n' | "$here/9run" -t 180 --dialog - >/dev/null
set +e
"$here/9run" -t "${BUILD_TIMEOUT:-1800}" '@{cd /tmp && tar xf /dev/sdE1/data build.rc target} && rc /tmp/build.rc' 2>&1 | cut -c1-200 | tee "$out/session.log"
st=${PIPESTATUS[0]}
set -e
"$here/vm" stop; trap - EXIT

tar xmf "$b/out.img" -C "$out" 2>/dev/null || true
[ "${WHAT:-all}" = loader ] || results="$kernel hjfs kbdfs ktest"
results="$results $loader"
[ "$st" = 0 ] && grep -q BUILD-DONE "$out/session.log" || { echo "build failed"; exit 1; }
for f in $results; do [ -s "$out/$f" ] || { echo "build failed: no $f"; exit 1; }; done
echo "results in $out: $(cd "$out" && ls -l $results | awk '{print $5, $9}' | tr '\n' ' ')"
