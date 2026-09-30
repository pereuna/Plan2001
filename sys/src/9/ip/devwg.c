/*
 * #W - WireGuard in the kernel: a cryptographic IP interface.
 *
 *	#W/wgN/ctl	configuration, a command a line, each complete in
 *			itself (no state kept between writes):
 *		private KEY		this interface's private key (base64); a new
 *					one ends every session made with the old
 *		listen PORT		UDP port, announced on /net/udp of the writer
 *		peer KEY [psk KEY] [allowed ADDR/LEN]... [endpoint ADDR!PORT] [keepalive SECS]
 *					add the peer with this public key, or change
 *					it: psk (a new one ends its sessions); an
 *					address prefix it may use, v4 or v6, owned by
 *					one peer only; where it is (it roams: the last
 *					authenticated packet sets it); persistent keepalive
 *		remove KEY		drop a peer
 *		cookie always|auto	cookie replies to every handshake, or only
 *					under load (auto, the default)
 *	#W/wgN/status	the interface and its peers, each with a stable id
 *
 * The IP stack sees it as the medium "wg":
 *	echo bind wg wgN > /net/ipifc/clone ctl
 * Packets routed to the interface go to the peer whose allowed prefixes
 * hold the destination; packets from a peer come in only with a source
 * address that peer is allowed.  The peer id is the identity a later layer
 * (9P, namespaces) can use: one key, one id.
 *
 * The protocol is WireGuard's (Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s):
 * handshake initiation and response, cookie replies both ways, transport
 * data, keepalives, rekeying by time.  Not yet: the rekey by message
 * count (2^60).
 *
 * Outer packets (the UDP ones to peers) are not sent through the UDP
 * conversation, which would route them like any other: they are made
 * here and given to ipoput4/6 with a route that goes out of no wg
 * interface (v4lookupskip, ip/iproute.c), so a tunnel may carry the
 * default route while its own packets still leave by the way to the peer.
 * One routed into a wg interface anyway (routes changed meanwhile) is
 * known by where it came from, not by its ports: dropped, counted (loops).
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include <libsec.h>

#include "ip.h"
#include "ipv6.h"

extern int enc64(char*, int, uchar*, int);

enum {
	Nwg		= 4,
	Npeer		= 64,
	Nallowed	= 16,
	Nqueue		= 32,		/* packets held for a handshake, a peer */
	Nwin		= 2048,		/* replay window, messages */

	Keylen		= 32,
	Taglen		= 16,
	Udphdr		= 52,		/* /net/udp "headers": raddr laddr ifcaddr rport lport */
	Hroom		= 128,		/* room for the outer headers */
	Dsttag		= IPaddrlen+2,	/* an outer message's destination, until sent */
	Udpproto	= 17,

	Tinit		= 1,
	Tresp		= 2,
	Tcookie		= 3,
	Tdata		= 4,
	Initlen		= 148,
	Resplen		= 92,
	Cookielen	= 64,
	Datahdr		= 16,
	Hsrate		= 20,		/* handshake messages a second before we are under load */

	Wgmtu		= 1420,

	/* WireGuard's timers, ms */
	Rekeyafter	= 120*1000,
	Rejectafter	= 180*1000,
	Rekeyattempt	= 90*1000,
	Rekeytimeout	= 5*1000,
	Keepalivetime	= 10*1000,
	Cookietime	= 120*1000,
	Tick		= 250,
};

static uvlong Rejectmsgs = ~0ULL - (1ULL<<13);

enum {
	Qtopdir,
	Qwgdir,
	Qctl,
	Qstatus,
};
#define QID(w, t)	(((w)<<8) | (t))
#define QW(q)		((int)(((q).path>>8) & 0xff))
#define QT(q)		((int)((q).path & 0xff))

typedef struct Keypair Keypair;
typedef struct Hs Hs;
typedef struct Allowed Allowed;
typedef struct Peer Peer;
typedef struct Wg Wg;

struct Keypair
{
	int	valid;
	int	initiator;
	ulong	born;		/* NOW */
	u32int	lidx;		/* ours: in the messages we get */
	u32int	ridx;		/* theirs: in the messages we send */
	uchar	send[Keylen];
	uchar	recv[Keylen];
	uvlong	sendctr;
	uvlong	recvmax;
	u32int	map[Nwin/32];
};

enum {
	Hsnone,
	Hssent,		/* our initiation is out */
};

struct Hs
{
	int	state;
	u32int	lidx;
	uchar	epriv[Keylen];
	uchar	epub[Keylen];
	uchar	c[Keylen];
	uchar	h[Keylen];
	ulong	sent;		/* NOW of the last initiation */
	ulong	first;		/* NOW of the first try */
};

struct Allowed
{
	uchar	ip[IPaddrlen];
	uchar	mask[IPaddrlen];
	int	len;		/* prefix length over the v6 form */
};

struct Peer
{
	int	id;		/* 0: slot free */
	uchar	pub[Keylen];
	uchar	psk[Keylen];
	uchar	ss[Keylen];	/* DH(our static, its static) */
	uchar	ih[Keylen];	/* the handshake hash to start from: HASH(HASH(C0||ID)||pub) */
	uchar	mac1key[Keylen];
	uchar	ckey[Keylen];	/* HASH(LABEL_COOKIE||its pub): its cookie replies */
	uchar	lastmac1[16];	/* of our last handshake message to it */
	uchar	cookie[16];	/* from its cookie reply: our mac2 */
	ulong	cookieborn;
	int	havecookie;
	Allowed	al[Nallowed];
	int	nal;

	uchar	eaddr[IPaddrlen];
	int	eport;		/* 0: no endpoint yet */
	int	keepalive;	/* s, persistent */

	Keypair	cur;
	Keypair	prev;
	Keypair	next;		/* ours as responder, until the initiator's first message */
	Hs	hs;
	uchar	lastts[12];	/* greatest TAI64N from it */

	Block	*q;		/* waiting for keys */
	int	nq;

	ulong	lastsent;
	ulong	lastrecv;
	ulong	lastdata;	/* a data packet in, not answered yet (passive keepalive) */
	int	owe;
	ulong	handshake;	/* seconds() of the last completed handshake */
	uvlong	rx, tx;
	int	nhs;
};

struct Wg
{
	QLock;
	int	n;
	int	havekey;
	uchar	priv[Keylen];
	uchar	pub[Keylen];
	uchar	ih[Keylen];		/* HASH(HASH(C0||ID)||our pub) */
	uchar	mac1key[Keylen];	/* HASH(LABEL_MAC1||our pub) */
	uchar	ckey[Keylen];		/* HASH(LABEL_COOKIE||our pub): our cookie replies */
	uchar	csecret[Keylen];	/* the cookies we give: MAC(csecret, addr||port) */
	ulong	csecretborn;
	int	csecretset;
	ulong	hsin;			/* handshake messages in during second hssec */
	long	hssec;
	int	cookiealways;
	int	port;
	Chan	*uctl;
	Chan	*udata;
	int	timer;			/* timer kproc started */
	Peer	peer[Npeer];
	int	nextid;

	Ipifc	*ifc;			/* bound as medium */
	Fs	*f;

	ulong	loops;
	ulong	drops;
	ulong	bad;		/* failed checks: a source address its peer may not use, ... */
	ulong	replays;
	ulong	noroute;
	ulong	cookiesin;
	ulong	cookiesout;
};

static Wg *wgs[Nwg];
static Lock wglock;
static uchar c0[Keylen];	/* HASH(CONSTRUCTION) */
static uchar h0[Keylen];	/* HASH(C0 || IDENTIFIER) */
static uchar nine[Keylen] = {9};

static char construction[] = "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
static char identifier[] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
static char labelmac1[] = "mac1----";
static char labelcookie[] = "cookie--";

extern Route*	v4lookupskip(Fs*, uchar*, uchar*, Routehint*, Medium*);
extern Route*	v6lookupskip(Fs*, uchar*, uchar*, Routehint*, Medium*);
static Medium wgmedium;

/*
 * crypto: WireGuard's HASH, MAC, HMAC, KDF, AEAD over libsec
 */
static void
hash2(uchar out[Keylen], uchar *a, ulong na, uchar *b, ulong nb)
{
	DigestState *s;

	s = blake2s_256(a, na, nil, nil);
	blake2s_256(b, nb, out, s);
}

static void
mixhash(uchar h[Keylen], uchar *d, ulong n)
{
	hash2(h, h, Keylen, d, n);
}

static void
wghmac(uchar out[Keylen], uchar key[Keylen], uchar *d, ulong n)
{
	hmac_blake2s_256(d, n, key, Keylen, out, nil);
}

/* c = τ1; o1, o2 = τ2, τ3 (either nil) */
static void
kdf(uchar c[Keylen], uchar *in, ulong nin, uchar *o1, uchar *o2)
{
	uchar t0[Keylen], t[Keylen+1], t1[Keylen];

	wghmac(t0, c, in, nin);
	t[0] = 1;
	wghmac(t1, t0, t, 1);
	if(o1 != nil || o2 != nil){
		memmove(t, t1, Keylen);
		t[Keylen] = 2;
		wghmac(t, t0, t, Keylen+1);
		if(o1 != nil)
			memmove(o1, t, Keylen);
		if(o2 != nil){
			t[Keylen] = 3;
			wghmac(o2, t0, t, Keylen+1);
		}
	}
	memmove(c, t1, Keylen);
	memset(t0, 0, sizeof t0);
	memset(t, 0, sizeof t);
	memset(t1, 0, sizeof t1);
}

static void
mac16(uchar out[16], uchar key[Keylen], uchar *d, ulong n)
{
	mac_blake2s_128(d, n, key, Keylen, out, nil);
}

/* HASH(label || key) */
static void
labelhash(uchar out[Keylen], char *label, uchar key[Keylen])
{
	hash2(out, (uchar*)label, strlen(label), key, Keylen);
}

/* XChaCha20-Poly1305: HChaCha20 for the subkey, the nonce's last 8 bytes */
static void
xsetup(Chachastate *cs, uchar key[Keylen], uchar nonce[24])
{
	uchar sub[Keylen], iv[12];

	hchacha(sub, key, Keylen, nonce, 20);
	memset(iv, 0, 4);
	memmove(iv+4, nonce+16, 8);
	setupChachastate(cs, sub, Keylen, iv, 12, 20);
	memset(sub, 0, sizeof sub);
}

static void
xseal(uchar key[Keylen], uchar nonce[24], uchar *d, ulong n, uchar *aad, ulong naad)
{
	Chachastate cs;

	xsetup(&cs, key, nonce);
	ccpoly_encrypt(d, n, aad, naad, d+n, &cs);
	memset(&cs, 0, sizeof cs);
}

static int
xunseal(uchar key[Keylen], uchar nonce[24], uchar *d, ulong n, uchar *aad, ulong naad)
{
	Chachastate cs;
	int r;

	xsetup(&cs, key, nonce);
	r = ccpoly_decrypt(d, n, aad, naad, d+n, &cs);
	memset(&cs, 0, sizeof cs);
	return r;
}

static void
nonce(uchar iv[12], uvlong ctr)
{
	int i;

	memset(iv, 0, 4);
	for(i = 0; i < 8; i++)
		iv[4+i] = ctr >> 8*i;
}

/* d[0:n] encrypted in place, the tag at d[n] */
static void
seal(uchar key[Keylen], uvlong ctr, uchar *d, ulong n, uchar *aad, ulong naad)
{
	Chachastate cs;
	uchar iv[12];

	nonce(iv, ctr);
	setupChachastate(&cs, key, Keylen, iv, 12, 20);
	ccpoly_encrypt(d, n, aad, naad, d+n, &cs);
	memset(&cs, 0, sizeof cs);
}

/* d[0:n] and its tag at d[n]; 0 and decrypted in place, or -1 */
static int
unseal(uchar key[Keylen], uvlong ctr, uchar *d, ulong n, uchar *aad, ulong naad)
{
	Chachastate cs;
	uchar iv[12];
	int r;

	nonce(iv, ctr);
	setupChachastate(&cs, key, Keylen, iv, 12, 20);
	r = ccpoly_decrypt(d, n, aad, naad, d+n, &cs);
	memset(&cs, 0, sizeof cs);
	return r;
}

static int
dh(uchar out[Keylen], uchar priv[Keylen], uchar pub[Keylen])
{
	return x25519(out, priv, pub) ? 0 : -1;
}

static void
tai64n(uchar t[12])
{
	vlong ns;
	uvlong s;
	ulong n;
	int i;

	ns = todget(nil, nil);
	s = 0x400000000000000aULL + ns/1000000000LL;
	n = ns % 1000000000LL;
	for(i = 0; i < 8; i++)
		t[i] = s >> 8*(7-i);
	for(i = 0; i < 4; i++)
		t[8+i] = n >> 8*(3-i);
}

static void
put32le(uchar *p, u32int v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}

static u32int
get32le(uchar *p)
{
	return p[0] | p[1]<<8 | p[2]<<16 | (u32int)p[3]<<24;
}

static uvlong
get64le(uchar *p)
{
	return (uvlong)get32le(p+4)<<32 | get32le(p);
}

static void
put64le(uchar *p, uvlong v)
{
	put32le(p, v);
	put32le(p+4, v>>32);
}

static int
key64(uchar k[Keylen], char *s)
{
	return dec64(k, Keylen, s, strlen(s)) == Keylen ? 0 : -1;
}

/*
 * replay window
 */
static int
replayok(Keypair *k, uvlong c)
{
	if(c >= Rejectmsgs)
		return 0;
	if(c > k->recvmax)
		return 1;
	if(k->recvmax - c >= Nwin)
		return 0;
	return (k->map[(c/32) % (Nwin/32)] & (1<<(c%32))) == 0;
}

static void
replayset(Keypair *k, uvlong c)
{
	uvlong i;

	if(c > k->recvmax){
		if(c - k->recvmax >= Nwin)
			memset(k->map, 0, sizeof k->map);
		else
			for(i = k->recvmax+1; i < c; i++)
				k->map[(i/32) % (Nwin/32)] &= ~(1<<(i%32));
		k->map[(c/32) % (Nwin/32)] &= ~(1<<(c%32));
		k->recvmax = c;
	}
	k->map[(c/32) % (Nwin/32)] |= 1<<(c%32);
}

/*
 * peers, indices, allowed prefixes
 */
static int
idxused(Wg *w, u32int x)
{
	Peer *p;

	if(x == 0)
		return 1;
	for(p = w->peer; p < w->peer+Npeer; p++){
		if(p->id == 0)
			continue;
		if((p->cur.valid && p->cur.lidx == x) || (p->prev.valid && p->prev.lidx == x)
		|| (p->next.valid && p->next.lidx == x) || (p->hs.state != Hsnone && p->hs.lidx == x))
			return 1;
	}
	return 0;
}

static u32int
newidx(Wg *w)
{
	u32int x;

	do
		genrandom((uchar*)&x, sizeof x);
	while(idxused(w, x));
	return x;
}

static Peer*
peerbykey(Wg *w, uchar *pub)
{
	Peer *p;

	for(p = w->peer; p < w->peer+Npeer; p++)
		if(p->id != 0 && tsmemcmp(p->pub, pub, Keylen) == 0)
			return p;
	return nil;
}

/* the peer whose prefix holds a (v6 form), longest match */
static Peer*
peerbyaddr(Wg *w, uchar *a)
{
	Peer *p, *best;
	Allowed *al;
	uchar m[IPaddrlen];
	int bl;

	best = nil;
	bl = -1;
	for(p = w->peer; p < w->peer+Npeer; p++){
		if(p->id == 0)
			continue;
		for(al = p->al; al < p->al+p->nal; al++){
			maskip(a, al->mask, m);
			if(ipcmp(m, al->ip) == 0 && al->len > bl){
				best = p;
				bl = al->len;
			}
		}
	}
	return best;
}

static void
peerkeys(Wg *w, Peer *p)
{
	uchar b[sizeof labelmac1 - 1 + Keylen];

	hash2(p->ih, h0, Keylen, p->pub, Keylen);
	memmove(b, labelmac1, sizeof labelmac1 - 1);
	memmove(b + sizeof labelmac1 - 1, p->pub, Keylen);
	blake2s_256(b, sizeof b, p->mac1key, nil);
	labelhash(p->ckey, labelcookie, p->pub);
	if(w->havekey && dh(p->ss, w->priv, p->pub) < 0)
		memset(p->ss, 0, Keylen);
}

static void
flushq(Peer *p)
{
	freeblist(p->q);
	p->q = nil;
	p->nq = 0;
}

/* a key changed: its sessions, and what they were made with, go */
static void
peerreset(Peer *p)
{
	memset(&p->cur, 0, sizeof p->cur);
	memset(&p->prev, 0, sizeof p->prev);
	memset(&p->next, 0, sizeof p->next);
	memset(&p->hs, 0, sizeof p->hs);
	p->havecookie = 0;
}

static void
peerclear(Peer *p)
{
	flushq(p);
	memset(p, 0, sizeof *p);
}

/*
 * the UDP transport: a message to addr!port, the destination in front of
 * it until sendouter puts the UDP and IP headers there
 */
static Block*
rawblock(uchar *addr, int port, int n)
{
	Block *b;

	b = allocb(Hroom + Dsttag + n);
	b->rp += Hroom;
	b->wp = b->rp;
	ipmove(b->wp, addr);
	hnputs(b->wp + IPaddrlen, port);
	b->wp += Dsttag;
	return b;
}

static Block*
udpblock(Wg *, Peer *p, int n)
{
	return rawblock(p->eaddr, p->eport, n);
}

/*
 * the processes sending outer packets: a packet routed into a wg
 * interface while its process is one of them is a tunnel's own
 */
static Lock outerlk;
static Proc *outerp[64];

static int
outermark(int on)
{
	Proc **pp;

	lock(&outerlk);
	for(pp = outerp; pp < outerp+nelem(outerp); pp++)
		if(on ? *pp == nil : *pp == up){
			*pp = on ? up : nil;
			unlock(&outerlk);
			return 0;
		}
	unlock(&outerlk);
	return -1;
}

static int
isouter(void)
{
	Proc **pp;
	int r;

	r = 0;
	lock(&outerlk);
	for(pp = outerp; pp < outerp+nelem(outerp); pp++)
		if(*pp == up)
			r = 1;
	unlock(&outerlk);
	return r;
}

/* an outer message to its destination, by a route out of no wg interface */
static void
sendouter(Wg *w, Block *b)
{
	uchar dst[IPaddrlen], src[IPaddrlen], *h, *gate;
	int dport, n, ok;
	ushort csum;
	Routehint rh;
	Route *r;
	Ipifc *ifc;
	Fs *f;

	ipmove(dst, b->rp);
	dport = nhgets(b->rp + IPaddrlen);
	b->rp += Dsttag;
	n = BLEN(b);
	if((f = w->f) == nil)
		goto noroute;
	memset(&rh, 0, sizeof rh);
	if(isv4(dst)){
		r = v4lookupskip(f, dst+IPv4off, IPnoaddr+IPv4off, &rh, &wgmedium);
		if(r == nil || (ifc = r->ifc) == nil)
			goto noroute;
		gate = (r->type & (Rifc|Rbcast|Rmulti|Rv4)) == Rv4 ? r->v4.gate : dst+IPv4off;
		memmove(src, v4prefix, IPv4off);
		rlock(ifc);
		ok = ipv4local(ifc, src+IPv4off, 0, gate);
		runlock(ifc);
		if(!ok)
			goto noroute;
		/* as udp.c's udpkick: the pseudo header first */
		b = padblock(b, IP4HDR+8);
		h = b->rp;
		memset(h, 0, IP4HDR+8);
		h[9] = Udpproto;
		hnputs(h+10, n+8);
		memmove(h+12, src+IPv4off, 4);
		memmove(h+16, dst+IPv4off, 4);
		hnputs(h+20, w->port);
		hnputs(h+22, dport);
		hnputs(h+24, n+8);
		csum = ptclcsum(b, 8, n+8+12);
		hnputs(h+26, csum == 0 ? 0xffff : csum);
		h[0] = IP_VER4;
		outermark(1);
		if(!waserror()){
			ipoput4(f, b, nil, MAXTTL, DFLTTOS, &rh);
			poperror();
		}
		outermark(0);
	}else{
		r = v6lookupskip(f, dst, IPnoaddr, &rh, &wgmedium);
		if(r == nil || (ifc = r->ifc) == nil)
			goto noroute;
		rlock(ifc);
		ok = ipv6local(ifc, src, 0, dst);
		runlock(ifc);
		if(!ok)
			goto noroute;
		b = padblock(b, IP6HDR+8);
		h = b->rp;
		memset(h, 0, IP6HDR+8);
		hnputl(h, n+8);
		h[7] = Udpproto;
		ipmove(h+8, src);
		ipmove(h+24, dst);
		hnputs(h+40, w->port);
		hnputs(h+42, dport);
		hnputs(h+44, n+8);
		csum = ptclcsum(b, 0, n+8+IP6HDR);
		hnputs(h+46, csum == 0 ? 0xffff : csum);
		memset(h, 0, 8);
		h[0] = IP_VER6;
		hnputs(h+4, n+8);
		h[6] = Udpproto;
		outermark(1);
		if(!waserror()){
			ipoput6(f, b, nil, MAXTTL, DFLTTOS, &rh);
			poperror();
		}
		outermark(0);
	}
	return;
noroute:
	w->noroute++;
	freeb(b);
}

/* send a list made under the lock, the lock no longer held */
static void
sendlist(Wg *w, Block *l)
{
	Block *b;

	while((b = l) != nil){
		l = b->list;
		b->list = nil;
		sendouter(w, b);
	}
}

static void
addout(Block ***tail, Block *b)
{
	**tail = b;
	*tail = &b->list;
}

/*
 * cookies (WireGuard's DoS defence): under load a handshake message is
 * answered only if its mac2 shows it came from where it says - else a
 * cookie reply, the cookie sealed to the mac1 of the message
 */

/* mac1 is at m[off]: our mac2 after it, with its cookie if fresh */
static void
macs2(Peer *p, uchar *m, int off)
{
	memmove(p->lastmac1, m+off, 16);
	if(p->havecookie && NOW - p->cookieborn < Cookietime)
		mac_blake2s_128(m, off+16, p->cookie, 16, m+off+16, nil);
	else
		memset(m+off+16, 0, 16);
}

/* the cookie for the sender at hdr (/net/udp's headers) */
static void
cookiefor(Wg *w, uchar *hdr, uchar c[16])
{
	uchar a[IPaddrlen+2];

	if(!w->csecretset || NOW - w->csecretborn >= Cookietime){
		genrandom(w->csecret, Keylen);
		w->csecretborn = NOW;
		w->csecretset = 1;
	}
	ipmove(a, hdr);
	memmove(a+IPaddrlen, hdr + 3*IPaddrlen, 2);
	mac16(c, w->csecret, a, sizeof a);
}

/*
 * a handshake message in, its mac1 at m[off]: 1 to go on with it; 0 to
 * drop it, when it may have been answered with a cookie reply
 */
static int
cookiecheck(Wg *w, uchar *hdr, uchar *m, int off, Block ***tail)
{
	uchar k[16], c[16], *r;
	Block *b;

	mac16(k, w->mac1key, m, off);
	if(tsmemcmp(k, m+off, 16) != 0)
		return 0;
	if(seconds() != w->hssec){
		w->hssec = seconds();
		w->hsin = 0;
	}
	if(++w->hsin <= Hsrate && !w->cookiealways)
		return 1;
	cookiefor(w, hdr, c);
	mac_blake2s_128(m, off+16, c, 16, k, nil);
	if(tsmemcmp(k, m+off+16, 16) == 0)
		return 1;
	b = rawblock(hdr, nhgets(hdr + 3*IPaddrlen), Cookielen);
	r = b->wp;
	memset(r, 0, Cookielen);
	r[0] = Tcookie;
	memmove(r+4, m+4, 4);		/* the sender's index */
	genrandom(r+8, 24);
	memmove(r+32, c, 16);
	xseal(w->ckey, r+8, r+32, 16, m+off, 16);
	b->wp += Cookielen;
	addout(tail, b);
	w->cookiesout++;
	return 0;
}

/* a cookie reply to our last handshake message to a peer */
static void
gotcookie(Wg *w, uchar *m)
{
	uchar c[16+Taglen];
	u32int x;
	Peer *p;

	x = get32le(m+4);
	for(p = w->peer; p < w->peer+Npeer; p++)
		if(p->id != 0 && ((p->hs.state == Hssent && p->hs.lidx == x) || (p->next.valid && p->next.lidx == x)))
			break;
	if(p == w->peer+Npeer)
		return;
	memmove(c, m+32, 16+Taglen);
	if(xunseal(p->ckey, m+8, c, 16, p->lastmac1, 16) < 0){
		w->bad++;
		return;
	}
	memmove(p->cookie, c, 16);
	p->cookieborn = NOW;
	p->havecookie = 1;
	w->cookiesin++;
}

/*
 * handshake
 */
static Block*
mkinit(Wg *w, Peer *p)
{
	Block *b;
	uchar *m, k[Keylen], dhr[Keylen];
	Hs *hs;

	if(!w->havekey || p->eport == 0)
		return nil;
	hs = &p->hs;
	genrandom(hs->epriv, Keylen);
	x25519(hs->epub, hs->epriv, nine);
	if(hs->state == Hsnone)
		hs->first = NOW;
	hs->state = Hssent;
	hs->lidx = newidx(w);
	hs->sent = NOW;

	b = udpblock(w, p, Initlen);
	m = b->wp;
	memset(m, 0, Initlen);
	m[0] = Tinit;
	put32le(m+4, hs->lidx);
	memmove(hs->c, c0, Keylen);
	memmove(hs->h, p->ih, Keylen);
	kdf(hs->c, hs->epub, Keylen, nil, nil);
	memmove(m+8, hs->epub, Keylen);
	mixhash(hs->h, hs->epub, Keylen);
	if(dh(dhr, hs->epriv, p->pub) < 0)
		goto bad;
	kdf(hs->c, dhr, Keylen, k, nil);
	memmove(m+40, w->pub, Keylen);
	seal(k, 0, m+40, Keylen, hs->h, Keylen);
	mixhash(hs->h, m+40, Keylen+Taglen);
	kdf(hs->c, p->ss, Keylen, k, nil);
	tai64n(m+88);
	seal(k, 0, m+88, 12, hs->h, Keylen);
	mixhash(hs->h, m+88, 12+Taglen);
	mac16(m+116, p->mac1key, m, 116);
	macs2(p, m, 116);
	b->wp += Initlen;
	memset(k, 0, sizeof k);
	memset(dhr, 0, sizeof dhr);
	p->nhs++;
	return b;
bad:
	freeb(b);
	hs->state = Hsnone;
	return nil;
}

static void
setkeys(Keypair *kp, uchar c[Keylen], int initiator, u32int lidx, u32int ridx)
{
	uchar t1[Keylen], t2[Keylen];

	memset(kp, 0, sizeof *kp);
	kdf(c, nil, 0, t2, nil);	/* c = τ1, t2 = τ2 */
	memmove(t1, c, Keylen);
	if(initiator){
		memmove(kp->send, t1, Keylen);
		memmove(kp->recv, t2, Keylen);
	}else{
		memmove(kp->recv, t1, Keylen);
		memmove(kp->send, t2, Keylen);
	}
	kp->valid = 1;
	kp->initiator = initiator;
	kp->born = NOW;
	kp->lidx = lidx;
	kp->ridx = ridx;
	memset(t1, 0, sizeof t1);
	memset(t2, 0, sizeof t2);
}

static void
setendpoint(Peer *p, uchar *hdr)
{
	ipmove(p->eaddr, hdr);
	p->eport = nhgets(hdr + 3*IPaddrlen);
}

/* an initiation to us: the response, or nil */
static Block*
gotinit(Wg *w, uchar *hdr, uchar *m)
{
	uchar c[Keylen], h[Keylen], k[Keylen], dhr[Keylen], pub[Keylen+Taglen], ts[12+Taglen];
	uchar epriv[Keylen], epub[Keylen], t[Keylen], *r;
	Peer *p;
	Block *b;

	if(!w->havekey)
		return nil;
	mac16(k, w->mac1key, m, 116);
	if(tsmemcmp(k, m+116, 16) != 0)
		return nil;
	memmove(c, c0, Keylen);
	memmove(h, w->ih, Keylen);
	kdf(c, m+8, Keylen, nil, nil);
	mixhash(h, m+8, Keylen);
	if(dh(dhr, w->priv, m+8) < 0)
		return nil;
	kdf(c, dhr, Keylen, k, nil);
	memmove(pub, m+40, Keylen+Taglen);
	if(unseal(k, 0, pub, Keylen, h, Keylen) < 0)
		return nil;
	mixhash(h, m+40, Keylen+Taglen);
	if((p = peerbykey(w, pub)) == nil)
		return nil;
	kdf(c, p->ss, Keylen, k, nil);
	memmove(ts, m+88, 12+Taglen);
	if(unseal(k, 0, ts, 12, h, Keylen) < 0)
		return nil;
	mixhash(h, m+88, 12+Taglen);
	if(memcmp(ts, p->lastts, 12) <= 0)
		return nil;	/* a replay */
	memmove(p->lastts, ts, 12);
	setendpoint(p, hdr);

	/* the response */
	genrandom(epriv, Keylen);
	x25519(epub, epriv, nine);
	b = udpblock(w, p, Resplen);
	r = b->wp;
	memset(r, 0, Resplen);
	r[0] = Tresp;
	put32le(r+8, get32le(m+4));
	kdf(c, epub, Keylen, nil, nil);
	memmove(r+12, epub, Keylen);
	mixhash(h, epub, Keylen);
	if(dh(dhr, epriv, m+8) < 0)
		goto bad;
	kdf(c, dhr, Keylen, nil, nil);
	if(dh(dhr, epriv, p->pub) < 0)
		goto bad;
	kdf(c, dhr, Keylen, nil, nil);
	kdf(c, p->psk, Keylen, t, k);
	mixhash(h, t, Keylen);
	seal(k, 0, r+44, 0, h, Keylen);
	mixhash(h, r+44, Taglen);
	put32le(r+4, newidx(w));
	mac16(r+60, p->mac1key, r, 60);
	macs2(p, r, 60);
	b->wp += Resplen;
	setkeys(&p->next, c, 0, get32le(r+4), get32le(m+4));
	memset(epriv, 0, sizeof epriv);
	memset(dhr, 0, sizeof dhr);
	memset(c, 0, sizeof c);
	return b;
bad:
	freeb(b);
	return nil;
}

/* a response to our initiation: its peer, keys set, or nil */
static Peer*
gotresp(Wg *w, uchar *hdr, uchar *m)
{
	uchar c[Keylen], h[Keylen], k[Keylen], dhr[Keylen], t[Keylen], tag[Taglen];
	u32int x;
	Peer *p;

	mac16(k, w->mac1key, m, 60);
	if(tsmemcmp(k, m+60, 16) != 0)
		return nil;
	x = get32le(m+8);
	for(p = w->peer; p < w->peer+Npeer; p++)
		if(p->id != 0 && p->hs.state == Hssent && p->hs.lidx == x)
			break;
	if(p == w->peer+Npeer)
		return nil;
	memmove(c, p->hs.c, Keylen);
	memmove(h, p->hs.h, Keylen);
	kdf(c, m+12, Keylen, nil, nil);
	mixhash(h, m+12, Keylen);
	if(dh(dhr, p->hs.epriv, m+12) < 0)
		return nil;
	kdf(c, dhr, Keylen, nil, nil);
	if(dh(dhr, w->priv, m+12) < 0)
		return nil;
	kdf(c, dhr, Keylen, nil, nil);
	kdf(c, p->psk, Keylen, t, k);
	mixhash(h, t, Keylen);
	memmove(tag, m+44, Taglen);
	if(unseal(k, 0, tag, 0, h, Keylen) < 0)
		return nil;
	p->prev = p->cur;
	setkeys(&p->cur, c, 1, x, get32le(m+4));
	memset(&p->hs, 0, sizeof p->hs);
	setendpoint(p, hdr);
	p->handshake = seconds();
	memset(c, 0, sizeof c);
	memset(dhr, 0, sizeof dhr);
	return p;
}

/*
 * transport
 */
static Keypair*
sendkeys(Peer *p)
{
	Keypair *k;

	k = &p->cur;
	if(!k->valid || NOW - k->born >= Rejectafter || k->sendctr >= Rejectmsgs)
		return nil;
	return k;
}

/* the IP packet b (consumed), sealed for p; nil for a keepalive */
static Block*
seal4(Wg *w, Peer *p, Keypair *k, Block *b)
{
	Block *o;
	int n, pad;
	uchar *m;

	n = b != nil ? blocklen(b) : 0;
	pad = (n + 15) & ~15;
	if(pad > Wgmtu && n <= Wgmtu)
		pad = Wgmtu;
	o = udpblock(w, p, Datahdr + pad + Taglen);
	m = o->wp;
	m[0] = Tdata;
	m[1] = m[2] = m[3] = 0;
	put32le(m+4, k->ridx);
	put64le(m+8, k->sendctr);
	if(b != nil){
		readblist(b, m+Datahdr, n, 0);
		freeblist(b);
	}
	memset(m+Datahdr+n, 0, pad-n);
	seal(k->send, k->sendctr, m+Datahdr, pad, nil, 0);
	k->sendctr++;
	o->wp += Datahdr + pad + Taglen;
	p->lastsent = NOW;
	p->owe = 0;
	p->tx += n;
	return o;
}

/* start a handshake unless one is fresh */
static void
kick(Wg *w, Peer *p, Block ***tail)
{
	Block *b;

	if(p->hs.state == Hssent && NOW - p->hs.sent < Rekeytimeout)
		return;
	if((b = mkinit(w, p)) != nil)
		addout(tail, b);
}

/* p has keys now: what waited goes; the initiator confirms them with a keepalive at least */
static void
drain(Wg *w, Peer *p, Block ***tail, int confirm)
{
	Keypair *k;
	Block *b;

	if((k = sendkeys(p)) == nil)
		return;
	if(p->q == nil && confirm)
		addout(tail, seal4(w, p, k, nil));
	while((b = p->q) != nil){
		p->q = b->list;
		b->list = nil;
		addout(tail, seal4(w, p, k, b));
	}
	p->nq = 0;
}

/* a transport message: the IP packet in it (b's data), or nil */
static Block*
gotdata(Wg *w, Block *b, uchar *hdr, uchar *m, long n, Block ***tail)
{
	Peer *p;
	Keypair *k;
	u32int x;
	uvlong ctr;
	long len, iplen;
	uchar src[IPaddrlen];

	x = get32le(m+4);
	ctr = get64le(m+8);
	k = nil;
	for(p = w->peer; p < w->peer+Npeer; p++){
		if(p->id == 0)
			continue;
		if(p->cur.valid && p->cur.lidx == x)
			k = &p->cur;
		else if(p->next.valid && p->next.lidx == x)
			k = &p->next;
		else if(p->prev.valid && p->prev.lidx == x)
			k = &p->prev;
		if(k != nil)
			break;
	}
	if(k == nil || NOW - k->born >= Rejectafter)
		return nil;
	if(!replayok(k, ctr)){
		w->replays++;
		return nil;
	}
	len = n - Datahdr - Taglen;
	if(unseal(k->recv, ctr, m+Datahdr, len, nil, 0) < 0)
		return nil;
	replayset(k, ctr);
	if(k == &p->next){
		/* the initiator's first message: its keys are confirmed */
		p->prev = p->cur;
		p->cur = p->next;
		memset(&p->next, 0, sizeof p->next);
		p->handshake = seconds();
		k = &p->cur;
	}
	setendpoint(p, hdr);
	p->lastrecv = NOW;
	drain(w, p, tail, 0);
	/* the initiator's keys getting old: a new handshake before they must go */
	if(k == &p->cur && k->initiator && NOW - k->born >= Rejectafter - Keepalivetime - Rekeytimeout)
		kick(w, p, tail);
	if(len == 0)
		return nil;	/* a keepalive */
	m += Datahdr;
	switch(m[0]>>4){
	case 4:
		if(len < IP4HDR)
			return nil;
		iplen = nhgets(m+2);
		v4tov6(src, m+12);
		break;
	case 6:
		if(len < IP6HDR)
			return nil;
		iplen = IP6HDR + nhgets(m+4);
		ipmove(src, m+8);
		break;
	default:
		return nil;
	}
	if(iplen > len || peerbyaddr(w, src) != p){
		w->bad++;
		return nil;
	}
	p->rx += iplen;
	p->lastdata = NOW;
	p->owe = 1;
	b->rp = m;
	b->wp = m + iplen;
	b->flag &= ~(Bipck|Budpck|Btcpck|Bpktck);
	return b;
}

/*
 * the UDP reader: handshakes and data from peers
 */
static void
wgreader(void *a)
{
	Wg *w;
	Block *b, *in, *out, **tail, *r;
	uchar *hdr, *m;
	long n;
	Peer *p;
	Ipifc *ifc;
	Chan *c;

	w = a;
	c = w->udata;
	if(waserror())
		pexit("hangup", 1);	/* the UDP conversation went */
	for(;;){
		b = devtab[c->type]->bread(c, 64*1024, 0);
		if(b == nil)
			break;
		b = concatblock(b);
		n = BLEN(b) - Udphdr;
		if(n < 4){
			freeb(b);
			continue;
		}
		hdr = b->rp;
		m = hdr + Udphdr;
		out = nil;
		tail = &out;
		in = nil;
		qlock(w);
		if(m[1] != 0 || m[2] != 0 || m[3] != 0)
			w->bad++;
		else switch(m[0]){
		case Tinit:
			if(n == Initlen && cookiecheck(w, hdr, m, 116, &tail)
			&& (r = gotinit(w, hdr, m)) != nil)
				addout(&tail, r);
			break;
		case Tresp:
			if(n == Resplen && cookiecheck(w, hdr, m, 60, &tail)
			&& (p = gotresp(w, hdr, m)) != nil)
				drain(w, p, &tail, 1);
			break;
		case Tcookie:
			if(n == Cookielen)
				gotcookie(w, m);
			break;
		case Tdata:
			if(n >= Datahdr + Taglen)
				in = gotdata(w, b, hdr, m, n, &tail);
			break;
		}
		ifc = w->ifc;
		qunlock(w);
		sendlist(w, out);
		if(in == nil){
			freeb(b);
			continue;
		}
		if(ifc == nil){
			freeb(in);
			continue;
		}
		rlock(ifc);
		if(waserror()){
			runlock(ifc);
			continue;
		}
		if((in->rp[0]>>4) == 6)
			ipiput6(w->f, ifc, in);
		else
			ipiput4(w->f, ifc, in);
		runlock(ifc);
		poperror();
	}
}

/*
 * timers: handshake retries, keepalives, old keys
 */
static void
wgtimer(void *a)
{
	Wg *w;
	Peer *p;
	Block *out, **tail;
	Keypair *k;
	ulong now;

	w = a;
	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, 0, Tick);
		out = nil;
		tail = &out;
		qlock(w);
		now = NOW;
		for(p = w->peer; p < w->peer+Npeer; p++){
			if(p->id == 0)
				continue;
			if(p->hs.state == Hssent && now - p->hs.sent >= Rekeytimeout){
				if(now - p->hs.first >= Rekeyattempt){
					memset(&p->hs, 0, sizeof p->hs);
					flushq(p);
				}else
					kick(w, p, &tail);
			}
			k = sendkeys(p);
			if(p->keepalive > 0 && now - p->lastsent >= p->keepalive*1000){
				if(k != nil)
					addout(&tail, seal4(w, p, k, nil));
				else if(p->eport != 0)
					kick(w, p, &tail);
			}
			if(p->owe && k != nil && now - p->lastdata >= Keepalivetime)
				addout(&tail, seal4(w, p, k, nil));
			if(p->prev.valid && now - p->prev.born >= 3*Rejectafter)
				memset(&p->prev, 0, sizeof p->prev);
			if(p->next.valid && now - p->next.born >= Rejectafter)
				memset(&p->next, 0, sizeof p->next);
			if(p->cur.valid && now - p->cur.born >= 3*Rejectafter)
				memset(&p->cur, 0, sizeof p->cur);
		}
		qunlock(w);
		sendlist(w, out);
	}
}

/*
 * the medium: IP packets routed to the interface
 */
static Wg*
wgnamed(char *s)
{
	char *p;
	int n;

	p = s + strlen(s);
	while(p > s && p[-1] >= '0' && p[-1] <= '9')
		p--;
	if(*p == 0)
		return nil;
	n = atoi(p);
	if(n < 0 || n >= Nwg)
		return nil;
	return wgs[n];
}

static void
wgbind(Ipifc *ifc, int argc, char **argv)
{
	Wg *w;

	if(argc < 3 || (w = wgnamed(argv[2])) == nil)
		error("usage: bind wg wgN (#W/wgN configured first)");
	qlock(w);
	if(w->ifc != nil){
		qunlock(w);
		error(Einuse);
	}
	w->ifc = ifc;
	w->f = ifc->conv->p->f;
	qunlock(w);
	ifc->arg = w;
}

static void
wgunbind(Ipifc *ifc)
{
	Wg *w;

	w = ifc->arg;
	if(w == nil)
		return;
	qlock(w);
	if(w->ifc == ifc)
		w->ifc = nil;
	qunlock(w);
}

static void
wgbwrite(Ipifc *ifc, Block *b, int, uchar*, Routehint*)
{
	Wg *w;
	Peer *p;
	Keypair *k;
	Block *out, **tail;
	uchar dst[IPaddrlen];

	w = ifc->arg;
	b = concatblock(b);
	if(w == nil || BLEN(b) < 1){
		freeblist(b);
		return;
	}
	switch(b->rp[0]>>4){
	case 4:
		if(BLEN(b) < IP4HDR)
			goto drop;
		v4tov6(dst, b->rp+16);
		break;
	case 6:
		if(BLEN(b) < IP6HDR)
			goto drop;
		ipmove(dst, b->rp+24);
		break;
	default:
		goto drop;
	}
	if(isouter()){
		/* a tunnel's own packet routed into a tunnel */
		w->loops++;
		goto drop;
	}
	out = nil;
	tail = &out;
	qlock(w);
	if((p = peerbyaddr(w, dst)) == nil || p->eport == 0){
		w->drops++;
		qunlock(w);
		goto drop;
	}
	if((k = sendkeys(p)) != nil){
		addout(&tail, seal4(w, p, k, b));
		if(k->initiator && NOW - k->born >= Rekeyafter)
			kick(w, p, &tail);
	}else{
		if(p->nq >= Nqueue){
			Block *o;

			o = p->q;
			p->q = o->list;
			o->list = nil;
			freeb(o);
			p->nq--;
		}
		b->list = nil;
		{
			Block **l;

			for(l = &p->q; *l != nil; l = &(*l)->list)
				;
			*l = b;
		}
		p->nq++;
		kick(w, p, &tail);
	}
	qunlock(w);
	sendlist(w, out);
	return;
drop:
	freeblist(b);
}

static Medium wgmedium = {
.name=		"wg",
.hsize=		0,
.mintu=		576,
.maxtu=		Wgmtu,
.maclen=	0,
.bind=		wgbind,
.unbind=	wgunbind,
.bwrite=	wgbwrite,
.unbindonclose=	0,
};

/*
 * the device
 */
static Wg*
wgget(int n)
{
	Wg *w;

	if(n < 0 || n >= Nwg)
		error(Enonexist);
	lock(&wglock);
	if((w = wgs[n]) == nil){
		w = malloc(sizeof *w);
		if(w == nil){
			unlock(&wglock);
			error(Enomem);
		}
		memset(w, 0, sizeof *w);
		w->n = n;
		w->nextid = 1;
		wgs[n] = w;
	}
	unlock(&wglock);
	return w;
}

static int
wggen(Chan *c, char *, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	int n;

	switch(QT(c->qid)){
	case Qtopdir:
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, Qtopdir), 0, QTDIR);
			devdir(c, q, "#W", 0, eve, 0555, dp);
			return 1;
		}
		if(s >= Nwg)
			return -1;
		snprint(up->genbuf, sizeof up->genbuf, "wg%d", s);
		mkqid(&q, QID(s, Qwgdir), 0, QTDIR);
		devdir(c, q, up->genbuf, 0, eve, 0555, dp);
		return 1;
	default:
		n = QW(c->qid);
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, Qtopdir), 0, QTDIR);
			devdir(c, q, "#W", 0, eve, 0555, dp);
			return 1;
		}
		switch(s){
		case 0:
			mkqid(&q, QID(n, Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve, 0600, dp);
			return 1;
		case 1:
			mkqid(&q, QID(n, Qstatus), 0, QTFILE);
			devdir(c, q, "status", 0, eve, 0444, dp);
			return 1;
		}
		return -1;
	}
}

static void
wgreset(void)
{
	blake2s_256((uchar*)construction, strlen(construction), c0, nil);
	hash2(h0, c0, Keylen, (uchar*)identifier, strlen(identifier));
	addipmedium(&wgmedium);
}

static Chan*
wgattach(char *spec)
{
	Chan *c;

	c = devattach('W', spec);
	mkqid(&c->qid, QID(0, Qtopdir), 0, QTDIR);
	return c;
}

static Walkqid*
wgwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, wggen);
}

static int
wgstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, wggen);
}

static Chan*
wgopen(Chan *c, int omode)
{
	if(QT(c->qid) == Qctl || QT(c->qid) == Qstatus)
		wgget(QW(c->qid));
	return devopen(c, omode, nil, 0, wggen);
}

static void
wgclose(Chan*)
{
}

static char*
b64(char *buf, int n, uchar *k)
{
	enc64(buf, n, k, Keylen);
	return buf;
}

static long
wgstatus(Wg *w, char *buf, long len)
{
	char *s, *e, kb[64];
	Peer *p;
	Allowed *al;

	s = buf;
	e = buf + len;
	qlock(w);
	s = seprint(s, e, "wg%d pub %s port %d ifc %s loops %lud drops %lud noroute %lud bad %lud replays %lud cookies in %lud out %lud%s\n",
		w->n, w->havekey ? b64(kb, sizeof kb, w->pub) : "-", w->port,
		w->ifc != nil ? w->ifc->dev : "-", w->loops, w->drops, w->noroute, w->bad,
		w->replays, w->cookiesin, w->cookiesout, w->cookiealways ? " cookie always" : "");
	for(p = w->peer; p < w->peer+Npeer; p++){
		if(p->id == 0)
			continue;
		s = seprint(s, e, "peer %d %s", p->id, b64(kb, sizeof kb, p->pub));
		if(p->eport != 0)
			s = seprint(s, e, " endpoint %I!%d", p->eaddr, p->eport);
		else
			s = seprint(s, e, " endpoint -");
		s = seprint(s, e, " allowed");
		for(al = p->al; al < p->al+p->nal; al++)
			if(isv4(al->ip))
				s = seprint(s, e, " %V/%d", al->ip+IPv4off, al->len-96);
			else
				s = seprint(s, e, " %I/%d", al->ip, al->len);
		if(p->handshake != 0)
			s = seprint(s, e, " handshake %lds", seconds() - p->handshake);
		else
			s = seprint(s, e, " handshake -");
		s = seprint(s, e, " keys %s rx %llud tx %llud keepalive %d queued %d tries %d\n",
			sendkeys(p) != nil ? "up" : "-", p->rx, p->tx, p->keepalive, p->nq, p->nhs);
	}
	qunlock(w);
	return s - buf;
}

static long
wgread(Chan *c, void *a, long n, vlong off)
{
	char *buf;
	long m;

	switch(QT(c->qid)){
	case Qtopdir:
	case Qwgdir:
		return devdirread(c, a, n, nil, 0, wggen);
	case Qctl:
		return 0;
	case Qstatus:
		buf = smalloc(16*1024);
		if(waserror()){
			free(buf);
			nexterror();
		}
		m = wgstatus(wgget(QW(c->qid)), buf, 16*1024);
		buf[m] = 0;
		m = readstr(off, a, n, buf);
		poperror();
		free(buf);
		return m;
	}
	error(Egreg);
}

/* a decimal number in [lo, hi], or error */
static long
number(char *s, long lo, long hi, char *what)
{
	char *e;
	long n;

	n = strtol(s, &e, 10);
	if(*s == 0 || *e != 0 || n < lo || n > hi)
		error(what);
	return n;
}

/* ADDR/LEN, v4 (LEN 0-32) or v6 (0-128) */
static void
parseprefix(Allowed *al, char *s)
{
	char *l;
	int n, i;

	if((l = strchr(s, '/')) == nil)
		error("allowed: ADDR/LEN");
	*l++ = 0;
	if(parseip(al->ip, s) == -1)
		error("allowed: bad address");
	if(isv4(al->ip))
		n = 96 + number(l, 0, 32, "allowed: an IPv4 prefix length is 0-32");
	else
		n = number(l, 0, 128, "allowed: an IPv6 prefix length is 0-128");
	memset(al->mask, 0, IPaddrlen);
	for(i = 0; i < n; i++)
		al->mask[i/8] |= 0x80 >> (i%8);
	maskip(al->ip, al->mask, al->ip);
	al->len = n;
}

static void
wglisten(Wg *w, int port)
{
	Chan *c, *d;
	char buf[64];
	long n;

	if(w->udata != nil)
		error("already listening");
	c = namec("/net/udp/clone", Aopen, ORDWR, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	n = devtab[c->type]->read(c, buf, sizeof buf - 1, 0);
	if(n <= 0)
		error("udp clone");
	buf[n] = 0;
	n = atoi(buf);
	snprint(buf, sizeof buf, "announce %d", port);
	devtab[c->type]->write(c, buf, strlen(buf), 0);
	devtab[c->type]->write(c, "headers", 7, 0);
	snprint(buf, sizeof buf, "/net/udp/%ld/data", n);
	d = namec(buf, Aopen, ORDWR, 0);
	poperror();
	w->uctl = c;
	w->udata = d;
	w->port = port;
	kproc("wgreader", wgreader, w);
}

/* peer KEY [psk KEY] [allowed ADDR/LEN]... [endpoint ADDR!PORT] [keepalive SECS] */
static void
wgpeer(Wg *w, char **f, int nf)
{
	uchar k[Keylen], psk[Keylen], eaddr[IPaddrlen];
	Allowed al[Nallowed], *a;
	Peer *p, *q;
	int i, nal, eport, keepalive, setpsk;
	char *port;

	if(key64(k, f[1]) < 0)
		error("peer: a base64 key of 32 bytes");
	/* all of the line is checked before any of it is done */
	nal = 0;
	eport = -1;
	keepalive = -1;
	setpsk = 0;
	for(i = 2; i < nf; i += 2){
		if(i+1 >= nf)
			error("peer: a value missing");
		if(strcmp(f[i], "psk") == 0){
			if(key64(psk, f[i+1]) < 0)
				error("psk: a base64 key of 32 bytes");
			setpsk = 1;
		}else if(strcmp(f[i], "allowed") == 0){
			if(nal >= Nallowed)
				error("too many allowed prefixes");
			parseprefix(&al[nal++], f[i+1]);
		}else if(strcmp(f[i], "endpoint") == 0){
			if((port = strchr(f[i+1], '!')) == nil)
				error("endpoint: ADDR!PORT");
			*port++ = 0;
			if(parseip(eaddr, f[i+1]) == -1)
				error("endpoint: bad address");
			eport = number(port, 1, 65535, "endpoint: a port is 1-65535");
		}else if(strcmp(f[i], "keepalive") == 0)
			keepalive = number(f[i+1], 0, 65535, "keepalive: 0-65535 seconds");
		else
			error(Ebadctl);
	}
	p = peerbykey(w, k);
	/* a prefix has one owner (cryptokey routing): another peer's is an error */
	for(a = al; a < al+nal; a++)
		for(q = w->peer; q < w->peer+Npeer; q++){
			if(q->id == 0 || q == p)
				continue;
			for(i = 0; i < q->nal; i++)
				if(q->al[i].len == a->len && ipcmp(q->al[i].ip, a->ip) == 0){
					snprint(up->genbuf, sizeof up->genbuf, "allowed: that prefix is peer %d's", q->id);
					error(up->genbuf);
				}
		}
	if(p == nil){
		for(p = w->peer; p < w->peer+Npeer; p++)
			if(p->id == 0)
				break;
		if(p == w->peer+Npeer)
			error("too many peers");
		memset(p, 0, sizeof *p);
		memmove(p->pub, k, Keylen);
		p->id = w->nextid++;
		peerkeys(w, p);
	}
	for(a = al; a < al+nal; a++){
		for(i = 0; i < p->nal; i++)
			if(p->al[i].len == a->len && ipcmp(p->al[i].ip, a->ip) == 0)
				break;
		if(i < p->nal)
			continue;	/* has it already */
		if(p->nal >= Nallowed)
			error("too many allowed prefixes");
		p->al[p->nal++] = *a;
	}
	if(setpsk && tsmemcmp(p->psk, psk, Keylen) != 0){
		memmove(p->psk, psk, Keylen);
		peerreset(p);
	}
	if(eport > 0){
		ipmove(p->eaddr, eaddr);
		p->eport = eport;
	}
	if(keepalive >= 0)
		p->keepalive = keepalive;
	memset(psk, 0, sizeof psk);
}

static void
wgctl(Wg *w, char *line)
{
	char *f[2+2*(Nallowed+4)];
	int nf;
	uchar k[Keylen];
	Peer *p;

	nf = tokenize(line, f, nelem(f));
	if(nf == 0)
		return;
	if(strcmp(f[0], "private") == 0 && nf == 2){
		if(key64(k, f[1]) < 0)
			error("private: a base64 key of 32 bytes");
		if(w->havekey && tsmemcmp(w->priv, k, Keylen) == 0)
			return;
		memmove(w->priv, k, Keylen);
		x25519(w->pub, w->priv, nine);
		hash2(w->ih, h0, Keylen, w->pub, Keylen);
		{
			uchar b[sizeof labelmac1 - 1 + Keylen];

			memmove(b, labelmac1, sizeof labelmac1 - 1);
			memmove(b + sizeof labelmac1 - 1, w->pub, Keylen);
			blake2s_256(b, sizeof b, w->mac1key, nil);
		}
		labelhash(w->ckey, labelcookie, w->pub);
		w->havekey = 1;
		/* a new identity: nothing made with the old one goes on */
		for(p = w->peer; p < w->peer+Npeer; p++)
			if(p->id != 0){
				peerreset(p);
				peerkeys(w, p);
			}
		memset(k, 0, sizeof k);
		return;
	}
	if(strcmp(f[0], "listen") == 0 && nf == 2){
		wglisten(w, number(f[1], 1, 65535, "listen: a port is 1-65535"));
		return;
	}
	if(strcmp(f[0], "peer") == 0 && nf >= 2){
		wgpeer(w, f, nf);
		return;
	}
	if(strcmp(f[0], "remove") == 0 && nf == 2){
		if(key64(k, f[1]) < 0 || (p = peerbykey(w, k)) == nil)
			error("remove: no such peer");
		peerclear(p);
		return;
	}
	if(strcmp(f[0], "cookie") == 0 && nf == 2){
		if(strcmp(f[1], "always") == 0)
			w->cookiealways = 1;
		else if(strcmp(f[1], "auto") == 0)
			w->cookiealways = 0;
		else
			error(Ebadctl);
		return;
	}
	error(Ebadctl);
}

static long
wgwrite(Chan *c, void *a, long n, vlong)
{
	Wg *w;
	char *buf, *l, *nl;
	int start;

	if(QT(c->qid) != Qctl)
		error(Eperm);
	w = wgget(QW(c->qid));
	buf = smalloc(n+1);
	memmove(buf, a, n);
	buf[n] = 0;
	qlock(w);
	if(waserror()){
		qunlock(w);
		free(buf);
		nexterror();
	}
	for(l = buf; l != nil && *l != 0; l = nl){
		if((nl = strchr(l, '\n')) != nil)
			*nl++ = 0;
		wgctl(w, l);
	}
	start = !w->timer;
	w->timer = 1;
	poperror();
	qunlock(w);
	free(buf);
	if(start)
		kproc("wgtimer", wgtimer, w);
	return n;
}

Dev wgdevtab = {
	'W',
	"wg",

	wgreset,
	devinit,
	devshutdown,
	wgattach,
	wgwalk,
	wgstat,
	wgopen,
	devcreate,
	wgclose,
	wgread,
	devbread,
	wgwrite,
	devbwrite,
	devremove,
	devwstat,
};
