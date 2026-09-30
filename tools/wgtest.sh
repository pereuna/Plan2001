#!/bin/bash
# WireGuard regression test: a Plan2001 kernel with #W (build/amd64/9pc64,
# e.g. from tools/kbuild9p.rc) against Linux's own WireGuard, driven with
# 9pterm.  Every check must pass: the exit status is the number that failed.
#
# The test machine is a point-in-time copy of the local CPU server's disk
# (tools/vm-cpu's cpu.qcow2, copied by the running VM's QEMU with
# drive_backup, so it may keep running), booted through the Plan2001 loader
# and the kernel under test as a CPU server on its own QEMU user network,
# rcpu and auth on 127.0.0.1:27019 and :25670.  Nothing is written back.
# On Linux (sudo) a WireGuard interface wgp9, 10.99.0.1/24 and fd99::1/64 on
# UDP 51999; on Plan2001 #W/wg0, 10.99.0.2 and fd99::2, its endpoint the
# host (10.0.2.2!51999), the handshake from inside QEMU's NAT.
#   KERNEL=FILE  another kernel
#   LONG=0       skip the rekey check (it takes over two minutes)
#   KEEP=1       leave both up (tools/wgtest.sh stop ends them)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
cache=${PLAN2001_CACHE:-$HOME/.cache/plan2001}
d=$root/build/wgvm b=$root/build/amd64
T=$root/monolith/build/9pterm/9pterm
sock=$d/9pterm.sock
p9() { "$T" -S "$sock" -T "${TMO:-60}" -c "$1"; }
fails=0 passes=0
ok() { echo "PASS  $1"; passes=$((passes+1)); }
bad() { echo "FAIL  $1${2:+: $2}"; fails=$((fails+1)); }
check() { local n=$1; shift; if "$@" >/dev/null 2>&1; then ok "$n"; else bad "$n"; fi; }
checknot() { local n=$1; shift; if "$@" >/dev/null 2>&1; then bad "$n"; else ok "$n"; fi; }
# a field of the Plan2001 status: stat wg0 wg0 replays, stat wg0 'peer 1' tx
stat() {
	p9 "cat '#W/$1/status'" | awk -v what="$2" -v key="$3" '
		index($0, what) == 1 { for(i = 1; i < NF; i++) if($i == key){ print $(i+1); exit } }'
}
# p9ping ADDR [MIN [N]]: at least MIN of N pings from Plan2001 answered
p9ping() { [ "$(p9 "ip/ping -n ${3:-3} $1" 2>/dev/null | grep -c 'rtt')" -ge "${2:-2}" ]; }

stop() {
	[ -S "$sock" ] && "$T" -S "$sock" -X >/dev/null 2>&1 || true
	if [ -f "$d/qemu.pid" ] && pid=$(cat "$d/qemu.pid" 2>/dev/null) && kill -0 "$pid" 2>/dev/null; then
		python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.sendall(b"quit\n"); s.recv(100)' "$d/mon.sock" 2>/dev/null || true
		sleep 0.5; kill "$pid" 2>/dev/null || true
	fi
	sudo ip link del wgp9 2>/dev/null || true
	rm -f "$d"/*.sock "$d/qemu.pid" "$d/disk.qcow2" "$d/lpriv" "$d/psk"
}
[ "$1" = stop ] && { stop; exit 0; }

mkdir -p "$d"; stop
kernel=${KERNEL:-$b/9pc64}
for f in "$kernel" "$b/bootx64.efi" "$T"; do [ -s "$f" ] || { echo "wgtest: no $f" >&2; exit 100; }; done
[ -S "$root/build/vm/mon.sock" ] || { echo "wgtest: the CPU server VM (tools/vm-cpu) is not running" >&2; exit 100; }

# the disk: a copy of the running CPU server's, made by its QEMU
python3 - "$root/build/vm/mon.sock" "$d/disk.qcow2" <<'EOF'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(5)
def cmd(c):
    s.sendall((c + "\n").encode()); time.sleep(0.5)
    try: return s.recv(65536).decode(errors="replace")
    except socket.timeout: return ""
cmd("")
cmd("drive_backup -f hd %s qcow2" % sys.argv[2])
for i in range(600):
    if "No active jobs" in cmd("info block-jobs"): break
    time.sleep(1)
EOF
[ -s "$d/disk.qcow2" ] || { echo "wgtest: no disk copy" >&2; exit 100; }

# keys
lpriv=$(wg genkey); lpub=$(echo "$lpriv" | wg pubkey)
ppriv=$(wg genkey); ppub=$(echo "$ppriv" | wg pubkey)
p1priv=$(wg genkey); p1pub=$(echo "$p1priv" | wg pubkey)

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
[ -S "$sock" ] || { echo "wgtest: no session"; tail -20 "$d/serial.log" "$d/session.log"; exit 100; }

# Linux side
sudo ip link add wgp9 type wireguard
printf '%s\n' "$lpriv" > "$d/lpriv"
sudo wg set wgp9 listen-port 51999 private-key "$d/lpriv" peer "$ppub" allowed-ips 10.99.0.2/32,fd99::2/128
rm -f "$d/lpriv"
sudo ip addr add 10.99.0.1/24 dev wgp9
sudo ip -6 addr add fd99::1/64 dev wgp9 nodad
sudo ip link set wgp9 up mtu 1420

# Plan2001 side
p9 "echo 'private $ppriv' >'#W/wg0/ctl'; echo 'listen 51820' >'#W/wg0/ctl'"
p9 "echo 'peer $lpub allowed 10.99.0.1/32 allowed fd99::1/128 endpoint 10.0.2.2!51999 keepalive 5' >'#W/wg0/ctl'"
p9 '{ n=`{read}; echo bind wg wg0 >/net/ipifc/$n/ctl; echo add 10.99.0.2 255.255.255.0 >/net/ipifc/$n/ctl; echo add fd99::2 /64 >/net/ipifc/$n/ctl } <>/net/ipifc/clone'
sleep 3
p9 "cat '#W/wg0/status'"

echo '### basic'
check "handshake with Linux" sh -c "sudo wg show wgp9 latest-handshakes | awk '{exit !(\$2 > 0)}'"
check "IPv4 ping Plan2001 -> Linux" p9ping 10.99.0.1
check "IPv4 ping Linux -> Plan2001" ping -c 3 -W 2 10.99.0.2
check "IPv6 ping Plan2001 -> Linux" p9ping fd99::1
check "IPv6 ping Linux -> Plan2001" ping -6 -c 3 -W 2 fd99::2

echo '### TCP, 16 MB each way'
head -c 16000000 /dev/urandom > "$d/big"
lsum=$(md5sum < "$d/big" | cut -d' ' -f1)
p9 "aux/listen1 -1t 'tcp!*!7778' /bin/rc -c 'cat >/tmp/wgin' >/dev/null >[2=1] &" || true
sleep 1
timeout 120 nc -q 2 10.99.0.2 7778 < "$d/big" || true
sleep 1
psum=$(p9 'md5sum </tmp/wgin' | tail -1)
[ "$lsum" = "$psum" ] && ok "TCP Linux -> Plan2001 md5" || bad "TCP Linux -> Plan2001 md5" "$lsum $psum"
psum=$(p9 "dd -if /dev/random -of /tmp/wgbig -bs 1000000 -count 16 >[2]/dev/null; md5sum </tmp/wgbig" | tail -1)
p9 "aux/listen1 -1t 'tcp!*!7777' /bin/cat /tmp/wgbig >/dev/null >[2=1] &" || true
sleep 1
got=$(timeout 120 python3 -c 'import socket,sys; s=socket.create_connection(("10.99.0.2",7777)); f=sys.stdout.buffer
while (b:=s.recv(65536)): f.write(b)' | md5sum | cut -d' ' -f1)
[ "$got" = "$psum" ] && ok "TCP Plan2001 -> Linux md5" || bad "TCP Plan2001 -> Linux md5" "$psum $got"

echo '### an inner UDP packet from the WireGuard port itself'
got=$( (timeout 10 python3 -c 'import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("10.99.0.1", 9999)); d,a=s.recvfrom(100); print(a[1], d.decode().strip())' &
	sleep 1; p9 '{ n=`{read}; echo connect 10.99.0.1!9999 51820 >/net/udp/$n/ctl; echo from-51820 >/net/udp/$n/data } <>/net/udp/clone' >/dev/null 2>&1; wait) )
[ "$got" = "51820 from-51820" ] && ok "UDP with source port 51820 goes through" || bad "UDP with source port 51820 goes through" "$got"

echo '### a source address the peer may not use'
b0=$(stat wg0 wg0 bad)
sudo ip addr add 10.99.0.9/32 dev wgp9
checknot "ping from 10.99.0.9 dropped" ping -I 10.99.0.9 -c 2 -W 2 10.99.0.2
b1=$(stat wg0 wg0 bad)
[ "$b1" -gt "$b0" ] && ok "counted as bad ($b0 -> $b1)" || bad "counted as bad" "$b0 -> $b1"
sudo ip addr del 10.99.0.9/32 dev wgp9

echo '### a replayed packet'
port=$(sudo wg show wgp9 endpoints | awk '{n = split($2, a, ":"); print a[n]}')
r0=$(stat wg0 wg0 replays)
sudo python3 - "$port" <<'EOF' &
import socket, sys, struct
port = int(sys.argv[1])
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800)); s.bind(("lo", 0)); s.settimeout(10)
while True:
    f = s.recv(65536)
    ip = f[14:]; hl = (ip[0] & 15) * 4
    if ip[9] != 17: continue
    sp, dp = struct.unpack("!HH", ip[hl:hl+4])
    pay = ip[hl+8:]
    if sp == 51999 and dp == port and pay[0] == 4 and len(pay) > 32: break
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for i in range(3): u.sendto(pay, ("127.0.0.1", port))
EOF
sniff=$!
sleep 1; ping -c 2 -W 2 10.99.0.2 >/dev/null || true; wait $sniff || true
sleep 1
r1=$(stat wg0 wg0 replays)
[ "$r1" -ge $((r0+3)) ] && ok "replays dropped ($r0 -> $r1)" || bad "replays dropped" "$r0 -> $r1"
check "tunnel still up after replays" p9ping 10.99.0.1

echo '### ctl checks'
checknot "a prefix another peer owns" p9 "echo 'peer $p1pub allowed 10.99.0.1/32' >'#W/wg0/ctl'"
checknot "IPv4 prefix /-1" p9 "echo 'peer $lpub allowed 10.0.0.0/-1' >'#W/wg0/ctl'"
checknot "IPv4 prefix /33" p9 "echo 'peer $lpub allowed 10.0.0.0/33' >'#W/wg0/ctl'"
checknot "IPv6 prefix /129" p9 "echo 'peer $lpub allowed fd98::/129' >'#W/wg0/ctl'"
checknot "endpoint port 70000" p9 "echo 'peer $lpub endpoint 1.2.3.4!70000' >'#W/wg0/ctl'"
checknot "keepalive -1" p9 "echo 'peer $lpub keepalive -1' >'#W/wg0/ctl'"
checknot "listen port 0" p9 "echo 'listen 0' >'#W/wg1/ctl'"
check "the same prefix again for its own peer" p9 "echo 'peer $lpub allowed 10.99.0.1/32' >'#W/wg0/ctl'"
checknot "a bad line does nothing" p9 "echo 'peer $p1pub allowed 10.97.0.0/24 endpoint 1.2.3.4!0' >'#W/wg0/ctl'"
checknot "... no such peer made" p9 "grep -s 10.97.0.0 '#W/wg0/status'"

echo '### cookies: Linux answered with one'
p9 "echo 'peer $lpub keepalive 0' >'#W/wg0/ctl'; echo 'cookie always' >'#W/wg0/ctl'"
psk=$(wg genpsk); printf '%s\n' "$psk" > "$d/psk"
p9 "echo 'peer $lpub psk $psk' >'#W/wg0/ctl'"
sudo wg set wgp9 peer "$ppub" preshared-key "$d/psk"
c0=$(stat wg0 wg0 out)
up=0
for i in $(seq 40); do ping -c 1 -W 1 10.99.0.2 >/dev/null 2>&1 && { up=1; break; }; done
c1=$(stat wg0 wg0 out)
[ "$up" = 1 ] && ok "Linux gets through with our cookie" || bad "Linux gets through with our cookie"
[ "$c1" -gt "$c0" ] && ok "cookie replies sent ($c0 -> $c1)" || bad "cookie replies sent" "$c0 -> $c1"
p9 "echo 'cookie auto' >'#W/wg0/ctl'; echo 'peer $lpub keepalive 5' >'#W/wg0/ctl'"

echo '### cookies: Plan2001 to Plan2001 (wg1 answers wg0 with one)'
p9 "echo 'private $p1priv' >'#W/wg1/ctl'; echo 'listen 51821' >'#W/wg1/ctl'; echo 'cookie always' >'#W/wg1/ctl'; echo 'peer $ppub allowed 10.97.0.1/32 endpoint 10.0.2.15!51820' >'#W/wg1/ctl'"
p9 '{ n=`{read}; echo bind wg wg1 >/net/ipifc/$n/ctl; echo add 10.97.0.2 255.255.255.0 >/net/ipifc/$n/ctl } <>/net/ipifc/clone'
p9 "echo 'peer $p1pub allowed 10.97.0.2/32 endpoint 10.0.2.15!51821 keepalive 1' >'#W/wg0/ctl'"
for i in $(seq 20); do [ "$(stat wg1 'peer 1' keys)" = up ] && break; sleep 1; done
[ "$(stat wg0 wg0 in)" -ge 1 ] && ok "wg0 took wg1's cookie" || bad "wg0 took wg1's cookie"
[ "$(stat wg1 'peer 1' keys)" = up ] && ok "wg0 -> wg1 handshake with the cookie" || bad "wg0 -> wg1 handshake with the cookie"
p9 "echo 'remove $p1pub' >'#W/wg0/ctl'" || bad "remove a peer"

echo '### full tunnel: 0/1 and 128/1 through wg0'
host=$(hostname -I | cut -d' ' -f1)
t0=$(stat wg0 'peer 1' tx)
l0=$(stat wg0 wg0 loops)
# cryptokey routing: the peer must be allowed every address it carries
p9 "echo 'peer $lpub allowed 0.0.0.0/0' >'#W/wg0/ctl'"
p9 "echo add 0.0.0.0 128.0.0.0 10.99.0.1 >/net/iproute; echo add 128.0.0.0 128.0.0.0 10.99.0.1 >/net/iproute"
check "ping $host through the tunnel" p9ping "$host"
t1=$(stat wg0 'peer 1' tx)
[ "$t1" -ge $((t0+3*64)) ] && ok "it went through wg0 (tx $t0 -> $t1)" || bad "it went through wg0" "tx $t0 -> $t1"
[ "$(stat wg0 wg0 loops)" = "$l0" ] && ok "no loops" || bad "no loops"
check "the tunnel itself still up" p9ping 10.99.0.1
p9 "echo del 0.0.0.0 128.0.0.0 10.99.0.1 >/net/iproute; echo del 128.0.0.0 128.0.0.0 10.99.0.1 >/net/iproute" || bad "removing the tunnel routes"

echo '### private key change'
newpriv=$(wg genkey)
p9 "echo 'private $newpriv' >'#W/wg0/ctl'"
[ "$(stat wg0 'peer 1' keys)" = "-" ] && ok "sessions gone" || bad "sessions gone"
checknot "the old session is dead" p9ping 10.99.0.1 1 3
p9 "echo 'private $ppriv' >'#W/wg0/ctl'"
up=0; for i in $(seq 20); do p9ping 10.99.0.1 1 1 && { up=1; break; }; sleep 1; done
[ "$up" = 1 ] && ok "back with the old key" || bad "back with the old key"

echo '### PSK change'
psk2=$(wg genpsk); printf '%s\n' "$psk2" > "$d/psk"
p9 "echo 'peer $lpub psk $psk2' >'#W/wg0/ctl'"
[ "$(stat wg0 'peer 1' keys)" = "-" ] && ok "sessions gone" || bad "sessions gone"
checknot "the old session is dead" p9ping 10.99.0.1 1 3
sudo wg set wgp9 peer "$ppub" preshared-key "$d/psk"
up=0; for i in $(seq 20); do p9ping 10.99.0.1 1 1 && { up=1; break; }; sleep 1; done
[ "$up" = 1 ] && ok "back with the PSK on both" || bad "back with the PSK on both"

if [ "${LONG:-1}" = 1 ]; then
	echo '### rekey: 130 s of pings'
	h0=$(stat wg0 'peer 1' tries)
	loss=$(ping -q -i 1 -c 130 -W 2 10.99.0.2 | awk '/packet loss/ {print $6}')
	h1=$(stat wg0 'peer 1' tries)
	[ "$loss" = "0%" ] && ok "no loss over the rekey" || bad "no loss over the rekey" "$loss"
	[ "$h1" -gt "$h0" ] && ok "rekeyed (handshakes $h0 -> $h1)" || bad "rekeyed" "$h0 -> $h1"
fi

echo '### state'
p9 "cat '#W/wg0/status'"
sudo wg show wgp9
echo "WGTEST: $passes passed, $fails failed"
exit $fails
