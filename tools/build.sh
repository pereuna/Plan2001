#!/bin/bash
# Build the pc64 kernel and the UEFI loader in the 9front VM, with this
# repo's changed files laid over the VM's own (complete, current) source
# tree - see tools/build.rc for exactly what gets overlaid.
# Results: build/9pc64, build/bootx64.efi.
#
# The VM must already be running as a cpu server (tools/vm/run.cmd on
# Windows by default; tools/vm/wsl-run.sh for the slower WSL/TCG fallback,
# needed only when debugging with QEMU's -d int,cpu_reset, which WHPX does
# not support).
#
#   NINE_HOST   default: this WSL session's Windows gateway (`ip route`),
#               ie the Windows-hosted VM. Set to 127.0.0.1 for the WSL VM.
#   NINE_USER   default glenda
#   NINE_PASS   default: contents of ~/.9front-pass
#   DRAWTERM    drawterm binary (default: drawterm in PATH, else ~/.local/bin)
#
# drawterm exports the host file system to the VM as /mnt/term, so sources
# go in and results come out through it - no extra ports, no tar streams.
# -a and -s MUST stay 127.0.0.1: drawterm dials $auth:567 and secstore :5356
# when there is no local factotum, and a silently dropped SYN there (which
# is what a Windows firewall does to WSL, and what WSL's own QEMU does to a
# random host) makes it hang for minutes instead of failing fast.
set -e
HOST=${NINE_HOST:-$(ip route show default | awk '{print $3}')}
USER_=${NINE_USER:-glenda}
PASS=${NINE_PASS:-$(tr -d '\r\n' < ~/.9front-pass)}
DT=${DRAWTERM:-$(command -v drawterm || echo "$HOME/.local/bin/drawterm")}
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here"); out=$root/build
mkdir -p "$out"; rm -f "$out"/9pc64 "$out"/bootx64.efi "$out"/*.log
cmd="hostsrc=/mnt/term$root
hostbuild=/mnt/term$out
$(cat "$here/build.rc")"
PASS=$PASS timeout "${BUILD_TIMEOUT:-900}" "$DT" -G -h "$HOST" -a 127.0.0.1 -s 127.0.0.1 -u "$USER_" -c "$cmd" 2>&1 | tr -d '\r' | cut -c1-200 | tee "$out/session.log"
grep -q BUILD-DONE "$out/session.log" && [ -s "$out/9pc64" ] && [ -s "$out/bootx64.efi" ] || { echo "build failed"; exit 1; }
echo "results in $out: $(cd "$out" && ls -l 9pc64 bootx64.efi | awk '{print $5, $9}' | tr '\n' ' ')"
