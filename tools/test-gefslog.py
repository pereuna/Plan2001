#!/usr/bin/env python3
"""Tests for tools/gefslog: the golden hash vectors, and images built
here block by block in gefs's on-disk format (gdat.h, pack.c, blk.c).

  tools/test-gefslog.py            run every test, exit 1 on a failure

The image builder writes what gefs writes, which is how the tests can
say a clean log has no committed tail and a crash has one of a known
size.  They need no Plan 9 toolchain and no QEMU: they check the parser
against the format, not gefs against its tests.

The golden hash vectors come from the repository's own
sys/src/cmd/gefs/hash.c, compiled and run (metrohash64_1 with seed
0x6765 over bytes b[i] = (x >> 16) & 0xff, x = x*1103515245 + 12345).
"""

import hashlib
import importlib.machinery
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)

spec = importlib.util.spec_from_loader(
    "gefslog", importlib.machinery.SourceFileLoader("gefslog", os.path.join(here, "gefslog")))
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)

Blksz = g.Blksz
MiB = g.MiB
Tlog = g.Tlog
Tarena = g.Tarena
Tsuper = g.Tsuper
Loghdsz = g.Loghdsz
Arenasz = g.Arenasz

# metrohash64_1(hash.c) with seed 0x6765, gcc (enum constant unsigned)
HASHVEC = [
    (0, "35d244c2ba0d952f"),
    (1, "99919c256588447e"),
    (7, "4e0ee55d4b435e13"),
    (14, "98ac307bd75dfb6b"),
    (31, "1a905286121509ed"),
    (32, "9d940048e60102c8"),
    (16384, "8196cfe0dfc24fd2"),
]


def lcg(n):
    b = bytearray(n)
    x = 12345
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        b[i] = (x >> 16) & 0xFF
    return bytes(b)


def p64(v):
    return struct.pack(">Q", v & g.M64)


def p32(v):
    return struct.pack(">I", v)


def p16(v):
    return struct.pack(">H", v)


def ent_sync(gen):
    return p64((gen << 8) | g.LogSync)


def ent_alloc1(off):
    assert off & 0xFF == 0
    return p64(off | g.LogAlloc1)


def ent_free1(off):
    assert off & 0xFF == 0
    return p64(off | g.LogFree1)


def ent_alloc(off, n):
    assert off & 0xFF == 0
    return p64(off | g.LogAlloc) + p64(n)


def ent_free(off, n):
    assert off & 0xFF == 0
    return p64(off | g.LogFree) + p64(n)


class Builder:
    """Builds an image in gefs's format, block by block."""

    def __init__(self, arena_size=16 * MiB, nblk=None, mode="u32"):
        self.mode = mode
        self.arena_size = arena_size
        if nblk is None:
            # superblock, the arena's two headers, then the arena's blocks
            nblk = 2 + 2 + arena_size // Blksz
        self.img = bytearray(nblk * Blksz)
        self.nblk = nblk

    # -- blocks -------------------------------------------------------

    def put(self, addr, buf):
        self.img[addr:addr + len(buf)] = buf

    def logblk(self, addr, payload, logp=(-1, -1, -1)):
        """a Tlog block; returns its bp.hash"""
        buf = bytearray(Blksz)
        struct.pack_into(">H", buf, 0, Tlog)
        struct.pack_into(">H", buf, 2, len(payload))
        struct.pack_into(">Q", buf, 4, g.bufhash(payload, self.mode))
        struct.pack_into(">QQQ", buf, 12, logp[0] & g.M64, logp[1] & g.M64, logp[2] & g.M64)
        buf[Loghdsz:Loghdsz + len(payload)] = payload
        h = g.blkhash(bytes(buf), self.mode)
        self.put(addr, buf)
        return h

    def logchain(self, addrs, payloads, tp=(-1, -1, -1)):
        """write a chain of log blocks; returns (head addr, head hash)"""
        assert len(addrs) == len(payloads)
        nxt = tp
        for addr, payload in reversed(list(zip(addrs, payloads))):
            h = self.logblk(addr, payload, nxt)
            nxt = (addr, h, -1)
        return (addrs[0], nxt[1])

    def arena(self, hdr_addr, loghd, size, used):
        buf = bytearray(Blksz)
        buf[0:2] = p16(Tarena)
        buf[2:2 + Arenasz] = p64(loghd[0]) + p64(loghd[1]) + p64(size) + p64(used)
        self.put(hdr_addr, buf)
        self.put(hdr_addr + Blksz, buf)   # h0 and h1 hold the same

    def super(self, snapbp=(0x4000, 0x11), snapdl=((-1, -1), (-1, -1)),
              flag=0, nextqid=10, nextgen=20, qgen=1, arenabp=None, at=0):
        sb = bytearray(Blksz)
        sb[0:8] = b"gefs9.00"
        p = 8
        sb[p:p + 4] = p32(Blksz); p += 4
        sb[p:p + 4] = p32(8192); p += 4
        sb[p:p + 4] = p32(len(arenabp)); p += 4
        sb[p:p + 4] = p32(2); p += 4
        sb[p:p + 8] = p64(snapbp[0]); p += 8
        sb[p:p + 8] = p64(snapbp[1]); p += 8
        for bp in snapdl:
            sb[p:p + 8] = p64(bp[0]); p += 8
            sb[p:p + 8] = p64(bp[1]); p += 8
        sb[p:p + 8] = p64(flag); p += 8
        sb[p:p + 8] = p64(nextqid); p += 8
        sb[p:p + 8] = p64(nextgen); p += 8
        sb[p:p + 8] = p64(qgen); p += 8
        for bp in arenabp:
            sb[p:p + 8] = p64(bp[0]); p += 8
            sb[p:p + 8] = p64(bp[1]); p += 8
        sb[p:p + 8] = p64(g.bufhash(bytes(sb[:p]), self.mode))
        self.put(at, sb)

    def write(self, path):
        with open(path, "wb") as f:
            f.write(self.img)
        return path


def blkaddrs(n, start=4 * Blksz):
    """n block addresses, each Blksz apart, from start"""
    return [start + i * Blksz for i in range(n)]


def one_arena(mode="u32", arena_size=8 * MiB, qgen=1, payloads=None,
              tp=(-1, -1, -1), used=None):
    """an image with one arena whose log chain is `payloads`

    Layout: superblock at 0, arena headers at Blksz, blocks from 2*Blksz.
    """
    b = Builder(arena_size=arena_size, mode=mode)
    logaddr0 = 1 * Blksz          # arena headers at h0=Blksz, h1=2*Blksz
    addrs = [3 * Blksz + i * Blksz for i in range(len(payloads))]
    loghd = b.logchain(addrs, payloads, tp)
    size = arena_size
    if used is None:
        used = size
    b.arena(logaddr0, loghd, size, used)
    b.super(qgen=qgen, arenabp=[(logaddr0, 0)], at=0)
    return b


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)


# -- tests -------------------------------------------------------------


def test_hash_golden_vectors():
    """metrohash64_1 must be hash.c's, for every length class it has."""
    for n, want in HASHVEC:
        got = "%016x" % g.metrohash64_1(lcg(n), 0x6765, "u32")
        check(got == want, "len %d: want %s, got %s" % (n, want, got))
        # the other tail variants must at least be deterministic
        for mode in ("s32", "u64"):
            a = g.metrohash64_1(lcg(n), 0x6765, mode)
            b = g.metrohash64_1(lcg(n), 0x6765, mode)
            check(a == b, "len %d mode %s is not deterministic" % (n, mode))


def test_clean_log_has_no_tail():
    """allocations followed by a committed barrier leave no tail."""
    payloads = [ent_alloc1(0x10000) + ent_alloc1(0x20000) + ent_sync(5)]
    b = one_arena(qgen=5, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        p = b.write(os.path.join(d, "img"))
        r = g.analyse(p)
    lg = r["arena"][0]["log"]
    check(lg["has_barrier"], "the committed barrier was not found")
    check(lg["replay_gen"] == 5, "replay gen %s, want 5" % lg["replay_gen"])
    check(lg["tail_alloc_blocks"] == 0, "clean log has %d tail allocs" % lg["tail_alloc_blocks"])
    check(r["totals"]["tail_alloc_mib"] == 0, "clean log tail %.2f MiB" % r["totals"]["tail_alloc_mib"])
    check(r["unverified"] == 0, "%d header hashes did not verify" % r["unverified"])


def test_crash_tail_is_counted():
    """blocks allocated after the barrier are the tail a crash leaves."""
    n = 30
    after = b"".join(ent_alloc1(a) for a in blkaddrs(n, 0x10000))
    payloads = [ent_alloc1(0x10000) + ent_sync(7) + after]
    b = one_arena(qgen=7, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["replay_gen"] == 7, "replay gen %s" % lg["replay_gen"])
    check(lg["tail_alloc_blocks"] == n, "tail %d allocs, want %d" % (lg["tail_alloc_blocks"], n))
    check(lg["tail_alloc_bytes"] == n * Blksz, "tail %d bytes, want %d"
          % (lg["tail_alloc_bytes"], n * Blksz))
    # the analysis rounds to 3 decimals for the report
    check(abs(r["totals"]["tail_alloc_mib"] - n * Blksz / MiB) < 1e-3,
          "tail %.6f MiB, want %.6f" % (r["totals"]["tail_alloc_mib"], n * Blksz / MiB))


def test_tail_reaches_the_documented_200_mib():
    """the ~200 MiB per crash in docs/gefs-obj.md is a tail of this shape.

    A full log block holds (Blksz-Loghdsz)/8 one-wide entries, so 200 MiB
    of 16 KiB blocks spans several log blocks, as it would on a disk.
    """
    per = (Blksz - Loghdsz) // 8
    want = 200 * 1024 // 16          # 16 KiB blocks in 200 MiB
    nblk = (want // per) + 1
    addrs = [0x40000 + i * Blksz for i in range(nblk)]
    payloads = []
    left = want
    for i in range(nblk):
        n = min(per, left)
        left -= n
        payloads.append(b"".join(ent_alloc1(a) for a in blkaddrs(n, 0x40000 + i * 0x1000000)))
    # a committed barrier, then the writing that the kill interrupted
    payloads[0] = ent_sync(9) + payloads[0][8:]
    b = one_arena(arena_size=256 * MiB, qgen=9, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(len(b.img) >= (3 + nblk) * Blksz, "image too small for the chain")
    # the sync replaced one of the alloc entries, so the count is unchanged
    check(lg["entries"] == want, "entries %d, want %d" % (lg["entries"], want))
    got = r["totals"]["tail_alloc_mib"]
    check(abs(got - (200 - 16 / 1024)) < 0.2,
          "the tail is %.2f MiB, want about 200" % got)


def test_stale_barrier_does_not_stop_the_replay():
    """a barrier whose gen is below qgen is a sync the load has moved past."""
    # loadlog keeps going: the gen >= qgen test (blk.c loadlog)
    after = b"".join(ent_alloc1(a) for a in blkaddrs(4, 0x30000))
    payloads = [ent_sync(3) + ent_alloc1(0x10000) + ent_alloc1(0x20000) + ent_sync(9) + after]
    b = one_arena(qgen=9, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["replay_gen"] == 9, "replay gen %s, want 9 (the stale 3 skipped)" % lg["replay_gen"])
    check(lg["tail_alloc_blocks"] == 4, "tail %d, want 4" % lg["tail_alloc_blocks"])
    replay = [x for x in lg["barriers"] if x["replay"]]
    check(len(replay) == 1 and replay[0]["gen"] == 9, "replay point is not the gen-9 barrier")


def test_no_barrier_replays_the_whole_log():
    """loadlog's other return: no durable barrier sets logtl and returns.

    loadarena leaves the free tree empty and the replay is the only
    filler, so with no barrier the whole log is replayed and there is
    no tail at all.
    """
    payloads = [b"".join(ent_alloc1(a) for a in blkaddrs(6, 0x10000))]
    b = one_arena(qgen=1, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(not lg["has_barrier"], "a barrier was found in a log with none")
    check(lg["tail_alloc_blocks"] == 0,
          "no barrier means a full replay, but the tail is %d" % lg["tail_alloc_blocks"])


def test_the_replayed_prefix_is_not_the_tail():
    """allocs before the barrier are in the free set, so they are no tail.

    Only what follows the first durable barrier is unreplayed; the
    prefix is exactly what loadlog applies (grabrange and freerange).
    """
    ahead = b"".join(ent_alloc1(a) for a in blkaddrs(10, 0x10000))
    after = b"".join(ent_alloc1(a) for a in blkaddrs(2, 0x900000))
    payloads = [ahead + ent_sync(3) + after]
    b = one_arena(qgen=3, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["entries"] == 13, "entries %d, want 13" % lg["entries"])
    check(lg["tail_alloc_blocks"] == 2,
          "the tail is %d, want only the 2 after the barrier" % lg["tail_alloc_blocks"])


def test_multiblock_chain_counts_every_block():
    """entries are counted across the whole chain, not just the head."""
    per = (Blksz - Loghdsz) // 8
    payloads = [ent_sync(2) + b"".join(ent_alloc1(a) for a in blkaddrs(per - 1, 0x50000)),
                b"".join(ent_alloc1(a) for a in blkaddrs(5, 0x900000))]
    b = one_arena(arena_size=8 * MiB, qgen=2, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    a = r["arena"][0]
    check(a["chain_blocks"] == 2, "chain %d blocks, want 2" % a["chain_blocks"])
    check(a["log"]["tail_alloc_blocks"] == (per - 1) + 5,
          "tail %d, want %d" % (a["log"]["tail_alloc_blocks"], (per - 1) + 5))


def test_free_entries_net_out():
    """an alloc and a free after the barrier cancel in the net."""
    after = (ent_alloc1(0x10000) + ent_alloc1(0x20000) + ent_free1(0x10000))
    payloads = [ent_sync(4) + after]
    b = one_arena(qgen=4, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["tail_alloc_blocks"] == 2, "allocs %d" % lg["tail_alloc_blocks"])
    check(lg["tail_free_blocks"] == 1, "frees %d" % lg["tail_free_blocks"])
    check(lg["tail_net_bytes"] == Blksz, "net %d bytes, want %d" % (lg["tail_net_bytes"], Blksz))


def test_two_wide_entries():
    """LogAlloc/LogFree (op 4/5) carry a length and are read in 16 bytes."""
    after = ent_alloc(0x10000, 8 * Blksz) + ent_free(0x18000, 2 * Blksz)
    payloads = [ent_sync(6) + after]
    b = one_arena(qgen=6, payloads=payloads)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["tail_alloc_bytes"] == 8 * Blksz, "alloc bytes %d" % lg["tail_alloc_bytes"])
    check(lg["tail_free_bytes"] == 2 * Blksz, "free bytes %d" % lg["tail_free_bytes"])
    check(lg["tail_net_bytes"] == 6 * Blksz, "net %d" % lg["tail_net_bytes"])


def test_arenas_sum_into_the_total():
    """every arena's tail reaches the totals."""
    b = Builder(arena_size=8 * MiB)
    a0 = 1 * Blksz
    a1 = 20 * Blksz
    hd0 = b.logchain([30 * Blksz], [ent_sync(1) + b"".join(ent_alloc1(x) for x in blkaddrs(3, 0x800000))])
    hd1 = b.logchain([40 * Blksz], [ent_sync(1) + b"".join(ent_alloc1(x) for x in blkaddrs(5, 0x800000))])
    b.arena(a0, hd0, 8 * MiB, 8 * MiB)
    b.arena(a1, hd1, 8 * MiB, 8 * MiB)
    b.super(qgen=1, arenabp=[(a0, 0), (a1, 0)], at=0)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    check(len(r["arena"]) == 2, "%d arenas" % len(r["arena"]))
    check(r["totals"]["tail_alloc_blocks"] == 8,
          "total %d allocs, want 8" % r["totals"]["tail_alloc_blocks"])


def test_primary_superblock_falls_back_to_the_backup():
    """loadfs reads the backup when the primary does not verify."""
    b = one_arena(qgen=3, payloads=[ent_sync(3) + ent_alloc1(0x10000)])
    # the backup goes at the last whole block; the primary is then spoiled
    last = (len(b.img) // Blksz - 1) * Blksz
    b.super(qgen=3, arenabp=[(1 * Blksz, 0)], at=last)
    b.img[0:8] = b"XXXXXXXX"
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    check(r["super"]["label"] == "backup", "used the %s superblock" % r["super"]["label"])
    check(r["arena"][0]["log"]["tail_alloc_blocks"] == 1, "tail from the backup image")


def test_blob_hash_verifies_too():
    """a bp.hash set from bufhash over the payload also verifies."""
    mode = "u32"
    payload = ent_sync(2) + b"".join(ent_alloc1(x) for x in blkaddrs(2, 0x10000))
    b = Builder(arena_size=8 * MiB, mode=mode)
    addr = 3 * Blksz
    # write the block with logh = its payload's bufhash, which is what
    # the tool accepts and what the golden-image path carries
    buf = bytearray(Blksz)
    struct.pack_into(">H", buf, 0, Tlog)
    struct.pack_into(">H", buf, 2, len(payload))
    struct.pack_into(">Q", buf, 4, g.bufhash(payload, mode))
    struct.pack_into(">QQQ", buf, 12, g.M64, g.M64, g.M64)
    buf[Loghdsz:Loghdsz + len(payload)] = payload
    b.put(addr, buf)
    loghd = (addr, g.bufhash(payload, mode), 0)
    b.arena(1 * Blksz, loghd, 8 * MiB, 8 * MiB)
    b.super(qgen=2, arenabp=[(1 * Blksz, 0)], at=0)
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    check(r["unverified"] == 0, "%d headers did not verify" % r["unverified"])
    check(r["verified"] >= 1, "nothing verified")
    check(any(v and v.startswith("bufhash") for v in r["arena"][0]["verified"]),
          "the blob hash was not the one that verified: %s" % r["arena"][0]["verified"])


def test_analysis_does_not_touch_the_image():
    """the tool is read-only: the bytes and the digest are unchanged."""
    b = one_arena(qgen=2, payloads=[ent_sync(2) + b"".join(ent_alloc1(x) for x in blkaddrs(9, 0x10000))])
    with tempfile.TemporaryDirectory() as d:
        p = b.write(os.path.join(d, "img"))
        before = hashlib.sha256(open(p, "rb").read()).hexdigest()
        st = os.stat(p)
        g.analyse(p, do_census=True)
        after = hashlib.sha256(open(p, "rb").read()).hexdigest()
        st2 = os.stat(p)
    check(before == after, "the image changed")
    check(st.st_size == st2.st_size and st.st_mtime == st2.st_mtime, "the image was written")


def test_json_and_cli():
    """the CLI's --json is the analysis, and a bad image exits non-zero."""
    b = one_arena(qgen=2, payloads=[ent_sync(2) + ent_alloc1(0x10000)])
    with tempfile.TemporaryDirectory() as d:
        p = b.write(os.path.join(d, "img"))
        out = subprocess.run([sys.executable, os.path.join(here, "gefslog"), p, "--json"],
                             capture_output=True, text=True)
        check(out.returncode == 0, "gefslog exited %d: %s" % (out.returncode, out.stderr))
        r = json.loads(out.stdout)
        check(r["totals"]["tail_alloc_blocks"] == 1, "json tail %d" % r["totals"]["tail_alloc_blocks"])
        check(r["super"]["qgen"] == 2, "json qgen %s" % r["super"]["qgen"])

        bad = os.path.join(d, "bad")
        open(bad, "wb").write(b"not a gefs image" * 1000)
        out = subprocess.run([sys.executable, os.path.join(here, "gefslog"), bad],
                             capture_output=True, text=True)
        check(out.returncode != 0, "a bad image exited 0")


def test_census_counts_every_block():
    """the census sees the blocks the builder wrote."""
    b = one_arena(qgen=2, payloads=[ent_sync(2) + ent_alloc1(0x10000)])
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")), do_census=True)
    c = r["census"]
    check(c.get("Tlog") == 1, "census Tlog %s, want 1" % c.get("Tlog"))
    check(c.get("Tarena", 0) >= 2, "census Tarena %s, want at least 2" % c.get("Tarena"))


def test_loop_in_the_chain_is_an_error():
    """a chain that points at itself is reported, not followed forever."""
    b = Builder(arena_size=8 * MiB)
    addr = 3 * Blksz
    payload = ent_alloc1(0x10000)
    # a block whose logp points back at itself
    buf = bytearray(Blksz)
    struct.pack_into(">H", buf, 0, Tlog)
    struct.pack_into(">H", buf, 2, len(payload))
    struct.pack_into(">Q", buf, 4, g.bufhash(payload, b.mode))
    struct.pack_into(">QQQ", buf, 12, addr & g.M64, 0, 0)
    buf[Loghdsz:Loghdsz + len(payload)] = payload
    b.put(addr, buf)
    b.arena(1 * Blksz, (addr, g.blkhash(bytes(buf), b.mode), 0), 8 * MiB, 8 * MiB)
    b.super(qgen=2, arenabp=[(1 * Blksz, 0)], at=0)
    with tempfile.TemporaryDirectory() as d:
        try:
            g.analyse(b.write(os.path.join(d, "img")))
        except ValueError as e:
            check("loops" in str(e), "wrong error: %s" % e)
            return
    raise AssertionError("a looping chain was followed")


def test_bad_images_are_reported_not_traced():
    """a file that is not a gefs image exits with a message, not a traceback."""
    cases = {
        "empty": b"",
        "short": b"gefs9.00",
        "random": bytes(range(256)) * 4096,
    }
    for name, data in cases.items():
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "img")
            open(p, "wb").write(data)
            out = subprocess.run([sys.executable, os.path.join(here, "gefslog"), p],
                                 capture_output=True, text=True)
        check(out.returncode != 0, "%s: exited 0" % name)
        check("Traceback" not in out.stderr, "%s: traced back:\n%s" % (name, out.stderr))
        check(out.stderr.strip() != "", "%s: said nothing" % name)


def test_missing_file_is_reported():
    """a path that does not exist is a message and an exit status."""
    out = subprocess.run([sys.executable, os.path.join(here, "gefslog"), "/nonexistent/gefs.img"],
                         capture_output=True, text=True)
    check(out.returncode != 0, "a missing file exited 0")
    check("Traceback" not in out.stderr, "traced back: %s" % out.stderr)


def test_a_completed_sync_leaves_no_tail():
    """Measured on the real crash image of 19.10.2026.

    build/test-leak-measure.log: a kill -9 that lands after the last sync
    still leaves entries after the barrier in the log block, but every one
    is applied by the replay, so the tail is zero.  tools/gefslog on
    /tmp/crashmeas/crash1.img printed 0.00 MiB for all eight arenas
    (barrier gen 48 == superblock qgen 48).

    The crash-leak hypothesis needs the kill to land between blkalloc's
    LogAlloc and sync()'s LogSync; a kill after a whole sync has nothing
    for a reclaim to free, which is why the two measurements disagree.
    """
    # a durable barrier, then allocations that a later sync never covered
    after = b"".join(ent_alloc1(a) for a in blkaddrs(8, 0x60000))
    b = one_arena(arena_size=64 * MiB, qgen=48, payloads=[ent_sync(48) + after])
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["replay_gen"] == 48, "replay gen %s, want 48" % lg["replay_gen"])
    check(lg["tail_alloc_blocks"] == 8,
          "tail %d allocs, want 8" % lg["tail_alloc_blocks"])
    # the same writing with its own barrier at the end has no tail at all
    b = one_arena(arena_size=64 * MiB, qgen=48,
                  payloads=[after + ent_sync(48)])
    with tempfile.TemporaryDirectory() as d:
        r = g.analyse(b.write(os.path.join(d, "img")))
    lg = r["arena"][0]["log"]
    check(lg["tail_alloc_blocks"] == 0,
          "a completed sync left a tail of %d" % lg["tail_alloc_blocks"])
    check(r["totals"]["tail_alloc_mib"] == 0,
          "a completed sync left %.2f MiB" % r["totals"]["tail_alloc_mib"])


TESTS = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_") and callable(f)]


def main():
    fails = 0
    for name, fn in TESTS:
        try:
            fn()
            print("PASS %s" % name)
        except AssertionError as e:
            fails += 1
            print("FAIL %s: %s" % (name, e))
        except Exception as e:
            fails += 1
            print("FAIL %s: %s: %s" % (name, type(e).__name__, e))
    print("%d test(s), %d failed" % (len(TESTS), fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
