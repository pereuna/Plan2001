#!/bin/rc
# rcpu's rconnect for an app's origin (/boot/rconnect.app, bound on
# /bin/rconnect by /boot/app; D7): webterm's rcpu session, GET /rcpu
# (aux/wsrcpu: p9any, no TLS - the WebSocket is wss), the script as
# rconnect sends it; webterm reads and drops it and runs the app.
rfork e
keyspec=()
while(~ $1 -*){
	switch($1){
	case -k
		keyspec=($keyspec $2)
		shift
	case -u
		keyspec=($keyspec user'='$2)
		shift
	case -t
		shift
	}
	shift
}
host=$1
shift
fn sendscript {
	echo -n $host >/proc/$pid/args
	cat $1 >/env/v; wc -c </env/v; cat /env/v; rm /env/v
	shift
	$*
}
exec aux/wsrcpu -k $"keyspec tcp!$host!rcpuws /bin/rc -c 'sendscript $*' $*
