"""Velkatalouden kirjanpito (määrittely v0.1, luvut 2 ja 6).

Velka on kokonaislukuyksiköitä. Jokainen toimija aloittaa saldolla 0.
Ainoat tavat, joilla kokonaisvelka muuttuu: erääntyvän sitoumuksen
puuttuvan osan luonti (M kasvaa) ja kuolema (X kasvaa). Invariantti
sum(D) == M - X tarkistetaan.

Jokainen onnistunut tai estynyt operaatio kirjaa tapahtuman lokiin
(luvun 6 tapahtumatyypit). Loki on deterministinen: sama syöte, sama
loki, ja replay() rakentaa siitä saman tilan.
"""

from dataclasses import dataclass, field

OPEN, SETTLED, BLOCKED, CANCELLED = "open", "settled", "blocked", "cancelled"


class LedgerError(Exception):
    """Operaatio rikkoisi säännön; tilaa ei muutettu."""


@dataclass
class Actor:
    id: int
    limit: int
    debt: int = 0
    alive: bool = True


@dataclass
class Promise:
    id: int
    seller: int
    buyer: int
    amount: int
    due: int
    status: str = OPEN
    created: int = 0


@dataclass
class Event:
    seq: int
    time: int
    type: str
    data: dict = field(default_factory=dict)


class Ledger:
    """Keskitetty deterministinen kirjanpito.

    strict=True tarkistaa kaikki invariantit jokaisen tapahtuman jälkeen
    (O(N), vaihe A). Markkinasimulaatio käyttää strict=False ja kutsuu
    check():iä kierroksen lopussa.

    seller_cap rajaa myyjän avoimien sitoumusten summan (luku 2.3:
    "myyjälle on syytä asettaa avoimien sitoumusten katto"); None = ei rajaa.
    reserve=True: sell() vaatii, että ostajan saldo, hänen avoimet
    ostositoumuksensa ja uusi summa mahtuvat rajaan (luku 7:n "varaus").
    """

    def __init__(self, strict=True, seller_cap=None, reserve=False, log=True):
        self.actors = {}
        self.promises = {}
        self.minted = 0       # M
        self.extinguished = 0  # X
        self.total = 0        # sum(D) elävät, ylläpidetään juoksevasti
        self.time = 0
        self.strict = strict
        self.seller_cap = seller_cap
        self.reserve = reserve
        self.events = [] if log else None
        self.counts = {}
        self._seq = 0
        self._next_promise = 1
        self._due = {}            # due -> [promise id]
        self.open_sold = {}      # actor -> avointen myyntisitoumusten summa
        self.open_bought = {}    # actor -> avointen ostositoumusten summa
        self._open_by = {}       # actor -> avointen sitoumusten id:t

    # -- apu --

    def _emit(self, type, **data):
        self._seq += 1
        self.counts[type] = self.counts.get(type, 0) + 1
        if self.events is not None:
            self.events.append(Event(self._seq, self.time, type, data))
        if self.strict:
            self.check()

    def _actor(self, i):
        a = self.actors.get(i)
        if a is None:
            raise LedgerError(f"tuntematon toimija {i}")
        if not a.alive:
            raise LedgerError(f"toimija {i} on kuollut")
        return a

    def check(self):
        s = 0
        for a in self.actors.values():
            if a.debt < 0:
                raise AssertionError(f"D_{a.id} = {a.debt} < 0")
            if a.debt > a.limit:
                raise AssertionError(f"D_{a.id} = {a.debt} > L = {a.limit}")
            if not a.alive and a.debt != 0:
                raise AssertionError(f"kuolleella {a.id} velkaa {a.debt}")
            s += a.debt
        if s != self.minted - self.extinguished:
            raise AssertionError(f"sum(D) = {s} != M - X = {self.minted - self.extinguished}")
        if s != self.total:
            raise AssertionError(f"sum(D) = {s} != juokseva {self.total}")

    def debt(self, i):
        return self.actors[i].debt

    def headroom(self, i):
        """Kuinka paljon velkaa i voi vielä vastaanottaa (varaukset huomioiden, jos reserve)."""
        a = self.actors[i]
        h = a.limit - a.debt
        if self.reserve:
            h -= self.open_bought.get(i, 0)
        return h

    # -- operaatiot --

    def advance(self, t):
        if t < self.time:
            raise LedgerError("aika ei kulje taaksepäin")
        self.time = t

    def create(self, i, limit):
        if i in self.actors:
            raise LedgerError(f"toimija {i} on jo olemassa")
        if limit < 0:
            raise LedgerError("raja < 0")
        self.actors[i] = Actor(i, limit)
        self._emit("IdentityCreated", actor=i, limit=limit)

    def set_limit(self, i, limit):
        """Raja ei voi laskea saldon alle (invariantti D <= L)."""
        a = self._actor(i)
        if limit < a.debt:
            raise LedgerError(f"raja {limit} < saldo {a.debt}")
        if limit != a.limit:
            a.limit = limit
            self._emit("LimitUpdated", actor=i, limit=limit)

    def transfer(self, src, dst, x):
        """Luku 2.1. Vastaanottajan suostumus on kutsujan vastuulla."""
        a, b = self._actor(src), self._actor(dst)
        if src == dst:
            raise LedgerError("siirto itselle")
        if x < 0 or x > a.debt:
            raise LedgerError(f"siirto {x}: lähettäjällä {a.debt}")
        if b.debt + x > b.limit:
            raise LedgerError(f"siirto {x}: vastaanottajan raja {b.limit}, saldo {b.debt}")
        a.debt -= x
        b.debt += x
        self._emit("DebtTransferred", src=src, dst=dst, amount=x)

    def sell(self, seller, buyer, x, due, accepted=True):
        """Luku 2.2. Kirjaa sitoumuksen; saldot eivät muutu."""
        self._actor(seller)
        self._actor(buyer)
        if seller == buyer:
            raise LedgerError("kauppa itsensä kanssa")
        if not accepted:
            raise LedgerError("ostaja ei hyväksynyt")
        if x <= 0:
            raise LedgerError("summa <= 0")
        if due < self.time:
            raise LedgerError("eräpäivä menneisyydessä")
        if self.seller_cap is not None and self.open_sold.get(seller, 0) + x > self.seller_cap:
            raise LedgerError("myyjän sitoumuskatto")
        if self.reserve and x > self.headroom(buyer):
            raise LedgerError("ostajan raja (varaus)")
        p = Promise(self._next_promise, seller, buyer, x, due, OPEN, self.time)
        self._next_promise += 1
        self.promises[p.id] = p
        self._due.setdefault(due, []).append(p.id)
        self.open_sold[seller] = self.open_sold.get(seller, 0) + x
        self.open_bought[buyer] = self.open_bought.get(buyer, 0) + x
        self._open_by.setdefault(seller, set()).add(p.id)
        self._open_by.setdefault(buyer, set()).add(p.id)
        self._emit("PromiseCreated", promise=p.id, seller=seller, buyer=buyer, amount=x, due=due)
        return p.id

    def _close(self, p, status):
        p.status = status
        self.open_sold[p.seller] -= p.amount
        self.open_bought[p.buyer] -= p.amount
        self._open_by[p.seller].discard(p.id)
        self._open_by[p.buyer].discard(p.id)

    def settle(self, pid):
        """Luku 2.3: siirto min(x, D_A), puuttuva luodaan B:lle, atomisesti tai ei ollenkaan."""
        p = self.promises[pid]
        if p.status != OPEN:
            raise LedgerError(f"sitoumus {pid} ei ole avoin")
        a, b = self.actors[p.seller], self.actors[p.buyer]
        if b.debt + p.amount > b.limit:
            self._close(p, BLOCKED)
            self._emit("PromiseBlocked", promise=pid, reason="limit")
            return False
        t = min(p.amount, a.debt)
        r = p.amount - t
        a.debt -= t
        b.debt += p.amount
        self.minted += r
        self.total += r
        self._close(p, SETTLED)
        self._emit("PromiseSettled", promise=pid, transferred=t, minted=r)
        return True

    def settle_due(self, t=None):
        """Käsittelee kaikki avoimet sitoumukset, joiden eräpäivä <= t."""
        if t is None:
            t = self.time
        done = []
        for d in sorted(k for k in self._due if k <= t):
            for pid in self._due.pop(d):
                p = self.promises[pid]
                if p.status == OPEN:
                    done.append((pid, self.settle(pid)))
        return done

    def death(self, i):
        """Luku 2.4. Avoimet sitoumukset perutaan."""
        a = self._actor(i)
        for pid in sorted(self._open_by.get(i, ())):
            p = self.promises[pid]
            self._close(p, CANCELLED)
            self._emit("PromiseCancelled", promise=pid, reason="death", actor=i)
        self._open_by.pop(i, None)
        self.open_sold.pop(i, None)
        self.open_bought.pop(i, None)
        x = a.debt
        self.extinguished += x
        self.total -= x
        a.debt = 0
        a.alive = False
        self._emit("DeathRegistered", actor=i, extinguished=x)
        return x

    def forget_closed(self):
        """Pudottaa suljetut sitoumukset muistista (pitkät simulaatiot)."""
        self.promises = {k: p for k, p in self.promises.items() if p.status == OPEN}

    # -- loki --

    @classmethod
    def replay(cls, events, **kw):
        """Rakentaa tilan lokista. Tarkistaa, että siirrot ja erääntymiset
        tuottavat saman tuloksen kuin lokiin on kirjattu."""
        g = cls(**kw)
        for e in events:
            g.advance(e.time)
            d = e.data
            if e.type == "IdentityCreated":
                g.create(d["actor"], d["limit"])
            elif e.type == "LimitUpdated":
                g.set_limit(d["actor"], d["limit"])
            elif e.type == "DebtTransferred":
                g.transfer(d["src"], d["dst"], d["amount"])
            elif e.type == "PromiseCreated":
                pid = g.sell(d["seller"], d["buyer"], d["amount"], d["due"])
                assert pid == d["promise"], (pid, d)
            elif e.type in ("PromiseSettled", "PromiseBlocked"):
                ok = g.settle(d["promise"])
                assert ok == (e.type == "PromiseSettled"), e
                if ok:
                    assert g.events[-1].data == d, (g.events[-1], e)
            elif e.type == "PromiseCancelled":
                pass  # DeathRegistered peruu ne uudelleen
            elif e.type == "DeathRegistered":
                assert g.death(d["actor"]) == d["extinguished"], e
            else:
                raise LedgerError(f"tuntematon tapahtuma {e.type}")
        return g
