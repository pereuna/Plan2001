#!/bin/bash
# WireGuard interop test: a Plan2001 kernel with #W (build/amd64/9pc64, e.g.
# from tools/kbuild9p.rc) against Linux's own WireGuard, driven with 9pterm.
#
# The test machine is a point-in-time copy of the local CPU server's disk
# (tools/vm-cpu's cpu.qcow2, copied by the running VM's QEMU with
# drive_backup, so it may keep running), booted through the Plan2001 loader
# and the kernel under test as a CPU server on its own QEMU user network,
# rcpu and auth on 127.0.0.1:27019 and :25670.  Nothing is written back.
# On Linux (sudo) a WireGuard interface wgp9, 10.99.0.1/24 on UDP 51999;
# on Plan2001 #W/wg0, 10.99.0.2, its endpoint the host (10.0.2.2!51999)
# with a persistent keepalive, so the handshake starts from inside QEMU's
# NAT.  Then ping both ways, TCP both ways with md5, the peer state on both
# sides.  Removes wgp9 and stops the VM at the end.
#   KERNEL=FILE  another kernel
#   KEEP=1       leave both up (tools/wgtest.sh stop ends them)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
cache=${PLAN2001_CACHE:-$HOME/.cache/plan2001}
d=$root/build/wgvm b=$root/build/amd64
T=$root/monolith/build/9pterm/9pterm
sock=$d/9pterm.sock
p9() { "$T" -S "$sock" -T "${TMO:-60}" -c "$1"; }

stop() {
	[ -S "$sock" ] && "$T" -S "$sock" -X >/dev/null 2>&1 || true
	if [ -f "$d/qemu.pid" ] && pid=$(cat "$d/qemu.pid" 2>/dev/null) && kill -0 "$pid" 2>/dev/null; then
		python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.sendall(b"quit\n"); s.recv(100)' "$d/mon.sock" 2>/dev/null || true
		sleep 0.5; kill "$pid" 2>/dev/null || true
	fi
	[ -n "$httpd" ] && kill "$httpd" 2>/dev/null || true
	sudo ip link del wgp9 2>/dev/null || true
	rm -f "$d"/*.sock "$d/qemu.pid" "$d/disk.qcow2"
}
[ "$1" = stop ] && { stop; exit 0; }

mkdir -p "$d"; stop
kernel=${KERNEL:-$b/9pc64}
for f in "$kernel" "$b/bootx64.efi" "$T"; do [ -s "$f" ] || { echo "wgtest: no $f" >&2; exit 1; }; done
[ -S "$root/build/vm/mon.sock" ] || { echo "wgtest: the CPU server VM (tools/vm-cpu) is not running" >&2; exit 1; }

# the disk: a copy of the running CPU server's, made by its QEMU
python3 - "$root/build/vm/mon.sock" "$d/disk.qcow2" <<'EOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(5)
def cmd(c):
    s.sendall((c + "\n").encode()); time.sleep(0.5)
    try: return s.recv(65536).decode(errors="replace")
    except socket.timeout: return ""
cmd("")
print(cmd("drive_backup -f hd %s qcow2" % sys.argv[2]).strip().splitlines()[-1:])
for i in range(600):
    out = cmd("info block-jobs")
    if "No active jobs" in out: break
    time.sleep(1)
EOF
[ -s "$d/disk.qcow2" ] || { echo "wgtest: no disk copy" >&2; exit 1; }

# keys
lpriv=$(wg genkey); lpub=$(echo "$lpriv" | wg pubkey)
ppriv=$(wg genkey); ppub=$(echo "$ppriv" | wg pubkey)

# ESP: the loader, the kernel, the CPU server's plan9.ini
printf 'bootfile=/9pc64\nbootargs=local!/dev/sdE0/fscache\nmouseport=ask\nmonitor=ask\nvgasize=text\nconsole=0\nnobootprompt=local!/dev/sdE0/fscache\nuser=glenda\nservice=cpu\n' > "$d/plan9.ini"
rm -f "$d/esp.img"
mformat -C -i "$d/esp.img" -T 65536 -h 64 -s 32 ::
mmd -i "$d/esp.img" ::/EFI ::/EFI/BOOT
mcopy -i "$d/esp.img" "$b/bootx64.efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$d/esp.img" "$kernel" ::/9pc64
mcopy -i "$d/esp.img" "$d/plan9.ini" ::/
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$d/vars.fd"
trap '[ "$KEEP" = 1 ] || stop' EXIT
qemu-system-x86_64 -name plan2001-wgtest -accel kvm -cpu host -machine q35 -m 2048 -smp 2 \
	-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	-drive if=pflash,format=raw,file="$d/vars.fd" \
	-display none -vga none -rtc base=utc \
	-nic user,hostfwd=tcp:127.0.0.1:27019-:17019,hostfwd=tcp:127.0.0.1:25670-:567 \
	-serial file:"$d/serial.log" -monitor unix:"$d/mon.sock",server=on,wait=off \
	-drive file="$d/disk.qcow2",if=none,id=hd,format=qcow2 -device ide-hd,drive=hd,bus=ide.0 \
	-drive file="$d/esp.img",if=none,id=esp,format=raw -device ide-hd,drive=esp,bus=ide.1,bootindex=0 \
	-daemonize -pidfile "$d/qemu.pid"

# a 9pterm session to it, once rcpu answers
sleep 15
for i in $(seq 20); do
	setsid "$T" -h 'tcp!127.0.0.1!27019' -a 'tcp!127.0.0.1!25670' -u glenda -P "$cache/cpu.pass" -W 10 -M "$sock" >"$d/session.log" 2>&1 &
	for j in $(seq 30); do [ -S "$sock" ] && break; kill -0 $! 2>/dev/null || break; sleep 0.5; done
	[ -S "$sock" ] && p9 'cat /dev/osversion' >/dev/null 2>&1 && break
	sleep 3
done
[ -S "$sock" ] || { echo "wgtest: no session"; tail -20 "$d/serial.log" "$d/session.log"; exit 1; }
p9 "cat /dev/osversion; cat /net/ipifc/0/status | sed 2q; ls '#W'"

# Linux side
sudo ip link add wgp9 type wireguard
printf '%s\n' "$lpriv" > "$d/lpriv"
sudo wg set wgp9 listen-port 51999 private-key "$d/lpriv" peer "$ppub" allowed-ips 10.99.0.2/32
rm -f "$d/lpriv"
sudo ip addr add 10.99.0.1/24 dev wgp9
sudo ip link set wgp9 up mtu 1420

# Plan2001 side
p9 "{ echo 'private $ppriv'; echo 'listen 51820'; echo 'peer $lpub'; echo 'allowed 10.99.0.1/32'; echo 'endpoint 10.0.2.2!51999'; echo 'keepalive 5' } >'#W/wg0/ctl'"
p9 '{ n=`{read}; echo bind wg wg0 >/net/ipifc/$n/ctl; echo add 10.99.0.2 255.255.255.0 >/net/ipifc/$n/ctl; echo ifc $n } <>/net/ipifc/clone'
sleep 2
echo '### status (Plan2001)'; p9 "cat '#W/wg0/status'"
echo '### ping Plan2001 -> Linux'; p9 'ip/ping -n 5 10.99.0.1' || true
echo '### ping Linux -> Plan2001'; ping -c 5 -W 2 10.99.0.2 || true
echo '### TCP Linux -> Plan2001 (16 MB)'
head -c 16000000 /dev/urandom > "$d/big"
lsum=$(md5sum < "$d/big" | cut -d' ' -f1)
p9 "aux/listen1 -1t 'tcp!*!7778' /bin/rc -c 'cat >/tmp/wgin' >/dev/null >[2=1] &" || true
sleep 1
s=$(date +%s.%N); timeout 120 nc -q 2 10.99.0.2 7778 < "$d/big"; e=$(date +%s.%N)
sleep 1
psum=$(p9 'md5sum </tmp/wgin' | tail -1)
echo "linux $lsum plan2001 $psum  $(echo "16 / ($e - $s)" | bc -l | cut -c1-5) MB/s"
echo '### TCP Plan2001 -> Linux (16 MB)'
p9 "dd -if /dev/random -of /tmp/wgbig -bs 1000000 -count 16 >[2]/dev/null; md5sum /tmp/wgbig"
p9 "aux/listen1 -1t 'tcp!*!7777' /bin/cat /tmp/wgbig >/dev/null >[2=1] &" || true
sleep 1
s=$(date +%s.%N); got=$(timeout 120 python3 -c 'import socket,sys; s=socket.create_connection(("10.99.0.2",7777)); f=sys.stdout.buffer
while (b:=s.recv(65536)): f.write(b)' | md5sum | cut -d' ' -f1); e=$(date +%s.%N)
echo "linux got $got  $(echo "16 / ($e - $s)" | bc -l | cut -c1-5) MB/s"
echo '### status (Plan2001)'; p9 "cat '#W/wg0/status'"
echo '### status (Linux)'; sudo wg show wgp9
[ "$lsum" = "$psum" ] && echo "WGTEST TCP-IN OK" || echo "WGTEST TCP-IN FAILED"
