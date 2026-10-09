"""Vaihe B: agenttipohjainen markkinasimulaatio (määrittely luku 5).

Kierros on vuosi. Jokainen kauppa on kirjanpidon sitoumus (sell), joka
erääntyy `delay` vuoden päästä; velkaa ei liiku muuten kuin
sitoumusten, kuolinvuodesiirtojen (rasitustesti) ja kuolemien kautta.

Mallin oletukset (kaikki parametreja, ks. Config):

- Tuotteet: perus (ruoka ym., jokainen tarvitsee 1/v), panos (puu;
  perustuotteen valmistus vaatii 1 panoksen 2 yksikköä kohti) ja
  ylellisyys. Jokainen on erikoistunut yhteen.
- Perustuottaja ruokkii itsensä aina (1 yksikkö, ei kauppaa, ei panosta).
- Työ: työikäinen tuottaa koko kapasiteettinsa, jos hänellä on velkaa,
  jota ei ole jo luvattu pois (D - avoimet myynnit > 0), tai
  todennäköisyydellä `work_ethic` muuten. Nollasaldoinen ei hyödy
  myymisestä: hänen myyntinsä luovat ostajalle uutta velkaa.
- Kulutus: perus aina, jos raja sallii; ylellisyys strategian mukaan
  (minimi: ei; maksimi: niin paljon kuin raja sallii, enintään
  `luxury_max`; tasapaino: kun D + avoimet ostot + hinta <= tau * L).
- Raja L(t) = L_base + lam * hinta * kapasiteetti * jäljellä olevat
  työvuodet, mutta ei saldon alle (kirjanpidon invariantti).
- Ilman perustuotetta jäänyt kuolee seuraavana vuonna todennäköisyydellä
  `starve_death`.
- Kuolleiden tilalle syntyy birth_rate * kuolleet uutta (ikä enter_age, D=0).
"""

import math
import random
from dataclasses import dataclass, field, replace

from .ledger import Ledger, LedgerError

BASIC, INPUT, LUXURY = "perus", "panos", "ylellisyys"
GOODS = (BASIC, INPUT, LUXURY)
MINIMI, MAKSIMI, TASAPAINO = "minimi", "maksimi", "tasapaino"


@dataclass
class Config:
    n: int = 1000
    years: int = 400
    seed: int = 1
    enter_age: int = 20
    retire_age: int = 65
    life_mean: float = 78.0
    life_sd: float = 10.0
    life_min: int = 21
    life_max: int = 105
    shares: dict = field(default_factory=lambda: {BASIC: 0.35, INPUT: 0.15, LUXURY: 0.50})
    cap: dict = field(default_factory=lambda: {BASIC: 4, INPUT: 5, LUXURY: 2})
    price: dict = field(default_factory=lambda: {BASIC: 10, INPUT: 8, LUXURY: 15})
    basic_per_input: int = 2
    strategies: dict = field(default_factory=lambda: {TASAPAINO: 1.0})
    tau: float = 0.5
    luxury_max: int = 2
    work_ethic: float = 0.1
    limit_base: int = 200
    lam: float = 0.5
    delay: int = 1
    reserve: bool = True
    seller_cap: int = None
    starve_death: float = 0.5
    birth_rate: float = 1.0
    # rasitustestit
    mortality_shock: tuple = None   # (vuosi, osuus)
    luxury_shock: int = None        # vuosi, jolloin kaikki siirtyvät maksimiin
    deathbed: bool = False          # kuolevat ottavat vastaan muiden velkaa
    deathbed_from: int = 0          # vuosi, josta kuolinvuodesiirrot alkavat
    log: bool = False               # kirjanpidon tapahtumaloki (testit, replay)


@dataclass
class Agent:
    id: int
    born: int          # vuosi, jona tuli malliin
    age0: int          # ikä tullessa
    life: int          # kuolinikä
    skill: str
    strategy: str
    hungry: bool = False

    def age(self, t):
        return self.age0 + (t - self.born)


class Market:
    def __init__(self, cfg):
        self.cfg = cfg
        self.rng = random.Random(cfg.seed)
        self.ledger = Ledger(strict=False, seller_cap=cfg.seller_cap, reserve=cfg.reserve, log=cfg.log)
        self.agents = {}
        self.next_id = 1
        self.t = 0
        self.history = []
        self._last = dict(minted=0, extinguished=0)
        for _ in range(cfg.n):
            life = self._life()
            self._spawn(self.rng.randint(cfg.enter_age, max(cfg.enter_age, life - 1)), life)

    # -- väestö --

    def _life(self):
        c = self.cfg
        x = round(self.rng.gauss(c.life_mean, c.life_sd))
        return max(c.life_min, min(c.life_max, x))

    def _pick(self, d):
        r = self.rng.random() * sum(d.values())
        for k, w in d.items():
            r -= w
            if r < 0:
                return k
        return k

    def _spawn(self, age, life=None):
        a = Agent(self.next_id, self.t, age, life or self._life(),
                  self._pick(self.cfg.shares), self._pick(self.cfg.strategies))
        self.next_id += 1
        self.agents[a.id] = a
        self.ledger.create(a.id, self._limit(a))
        return a

    def _limit(self, a):
        c = self.cfg
        work = max(0, c.retire_age - a.age(self.t))
        return c.limit_base + int(c.lam * c.price[a.skill] * c.cap[a.skill] * work)

    def _working(self, a):
        return a.age(self.t) < self.cfg.retire_age

    def _die(self, a, cause, m):
        m["deaths"] += 1
        m["death_" + cause] += 1
        m["extinguished_" + cause] += self.ledger.death(a.id)
        del self.agents[a.id]

    # -- kauppa --

    def _buy(self, buyer, sellers, stock, good, m):
        """Ostaa yhden yksikön satunnaiselta myyjältä. Palauttaa
        'ok', 'none' (ei tarjontaa) tai 'limit' (raja ei salli)."""
        g = self.ledger
        p = self.cfg.price[good]
        if g.headroom(buyer.id) < p or g.debt(buyer.id) + p > g.actors[buyer.id].limit:
            m["refused_" + good] += 1
            return "limit"
        while sellers:
            k = self.rng.randrange(len(sellers))
            s = sellers[k]
            if s == buyer.id:
                if len(sellers) == 1:
                    break
                continue
            try:
                g.sell(s, buyer.id, p, self.t + self.cfg.delay)
            except LedgerError:
                m["capped_" + good] += 1
                sellers[k] = sellers[-1]
                sellers.pop()
                continue
            stock[s] -= 1
            if stock[s] == 0:
                sellers[k] = sellers[-1]
                sellers.pop()
            m["sold_" + good] += 1
            m["value"] += p
            return "ok"
        m["short_" + good] += 1
        return "none"

    def _offer(self, workers, good, units):
        stock = {a.id: units(a) for a in workers if a.skill == good}
        stock = {k: v for k, v in stock.items() if v > 0}
        return list(stock), stock

    # -- kierros --

    def step(self):
        c, g, rng = self.cfg, self.ledger, self.rng
        self.t += 1
        t = self.t
        g.advance(t)
        m = _Counter()

        # 1. erääntymiset
        for _, ok in g.settle_due(t):
            m["settled" if ok else "blocked"] += 1

        # 2. kuolemat
        shock = c.mortality_shock and c.mortality_shock[0] == t
        for a in sorted(self.agents.values(), key=lambda a: a.id):
            if a.age(t) >= a.life:
                self._die(a, "natural", m)
            elif a.hungry and rng.random() < c.starve_death:
                self._die(a, "starved", m)
            elif shock and rng.random() < c.mortality_shock[1]:
                self._die(a, "shock", m)

        # 3. syntymät
        births = int(m["deaths"] * c.birth_rate + rng.random())
        for _ in range(births):
            self._spawn(c.enter_age)
        m["births"] = births

        if c.luxury_shock == t:
            for a in self.agents.values():
                a.strategy = MAKSIMI

        agents = [self.agents[k] for k in sorted(self.agents)]

        # 4. rajat
        for a in agents:
            g.set_limit(a.id, max(self._limit(a), g.debt(a.id)))

        # 5. kuolinvuodesiirrot (rasitustesti): vuoden sisällä kuoleva ottaa
        #    vastaan velkaa eniten velkaantuneilta niin paljon kuin raja sallii.
        if c.deathbed and t >= c.deathbed_from:
            dying = [a for a in agents if a.age(t) + 1 >= a.life]
            if dying:
                donors = sorted((a for a in agents if a.age(t) + 1 < a.life and g.debt(a.id) > 0),
                                key=lambda a: -g.debt(a.id))
                for d in dying:
                    while donors and g.headroom(d.id) > 0:
                        src = donors[0]
                        x = min(g.headroom(d.id), g.debt(src.id) - g.open_sold.get(src.id, 0))
                        if x > 0:
                            g.transfer(src.id, d.id, x)
                            m["deathbed"] += x
                        if g.debt(src.id) - g.open_sold.get(src.id, 0) <= 0:
                            donors.pop(0)

        # 6. kuka tekee työtä
        workers = []
        for a in agents:
            if not self._working(a):
                continue
            m["working_age"] += 1
            if g.debt(a.id) - g.open_sold.get(a.id, 0) > 0 or rng.random() < c.work_ethic:
                workers.append(a)
        m["workers"] = len(workers)

        # 7. panokset perustuottajille
        sellers, stock = self._offer(workers, INPUT, lambda a: c.cap[INPUT])
        inputs = {}
        for a in workers:
            if a.skill != BASIC:
                continue
            need = math.ceil((c.cap[BASIC] - 1) / c.basic_per_input)
            got = 0
            for _ in range(need):
                if self._buy(a, sellers, stock, INPUT, m) != "ok":
                    break
                got += 1
            inputs[a.id] = got
        m["produced_" + INPUT] = m["sold_" + INPUT]

        # 8. perustuotteet
        sellers, stock = self._offer(workers, BASIC,
                                     lambda a: min(c.cap[BASIC] - 1, c.basic_per_input * inputs[a.id]))
        m["produced_" + BASIC] = sum(stock.values())
        buyers = [a for a in agents if not (a.skill == BASIC and self._working(a))]
        rng.shuffle(buyers)
        for a in agents:
            a.hungry = False
        for a in buyers:
            r = self._buy(a, sellers, stock, BASIC, m)
            if r != "ok":
                a.hungry = True
                m["hungry"] += 1
                m["hungry_" + r] += 1
        m["unsold_" + BASIC] = sum(stock.values())

        # 9. ylellisyys
        sellers, stock = self._offer(workers, LUXURY, lambda a: c.cap[LUXURY])
        m["produced_" + LUXURY] = sum(stock.values())
        p = c.price[LUXURY]
        buyers = [a for a in agents if a.strategy != MINIMI]
        rng.shuffle(buyers)
        for a in buyers:
            for _ in range(c.luxury_max):
                if a.strategy == TASAPAINO:
                    lim = g.actors[a.id].limit
                    if g.debt(a.id) + g.open_bought.get(a.id, 0) + p > c.tau * lim:
                        break
                if self._buy(a, sellers, stock, LUXURY, m) != "ok":
                    break
        m["unsold_" + LUXURY] = sum(stock.values())

        # 10. mittarit
        g.check()
        g.forget_closed()
        self.history.append(self._metrics(m))
        return self.history[-1]

    def _metrics(self, m):
        g, c = self.ledger, self.cfg
        debts = sorted(g.debt(i) for i in self.agents)
        n = len(debts)
        total = sum(debts)
        r = _Counter(m)
        r.update(
            year=self.t,
            pop=n,
            total_debt=total,
            debt_per_capita=total / n if n else 0.0,
            minted=g.minted - self._last["minted"],
            extinguished=g.extinguished - self._last["extinguished"],
            M=g.minted,
            X=g.extinguished,
            gini=_gini(debts),
            top10=(sum(debts[-max(1, n // 10):]) / total) if total else 0.0,
            zero=sum(1 for d in debts if d == 0) / n if n else 0.0,
            at_limit=sum(1 for i in self.agents if g.headroom(i) < c.price[BASIC]) / n if n else 0.0,
            idle=1 - m["workers"] / m["working_age"] if m["working_age"] else 0.0,
            open_promises=sum(g.open_sold.values()),
        )
        self._last = dict(minted=g.minted, extinguished=g.extinguished)
        return r

    def run(self):
        for _ in range(self.cfg.years):
            self.step()
        return self.history


class _Counter(dict):
    def __missing__(self, k):
        return 0


def _gini(xs):
    """xs nousevassa järjestyksessä."""
    n, s = len(xs), sum(xs)
    if n == 0 or s == 0:
        return 0.0
    cum = sum((i + 1) * x for i, x in enumerate(xs))
    return (2 * cum) / (n * s) - (n + 1) / n


def _cv(xs):
    m = sum(xs) / len(xs)
    return (sum((x - m) ** 2 for x in xs) / len(xs)) ** 0.5 / m if m else 0.0


def summarize(hist, last=200):
    """Viimeisten `last` vuoden keskiarvot, velan trendi (jakson jälkipuolisko
    vs. alkupuolisko) ja käynnistyksen nälkä (vuodet 1-5)."""
    h = hist[-last:]
    k = len(h)

    def avg(f):
        return sum(f(r) for r in h) / k

    pop = avg(lambda r: r["pop"])
    first, second = h[: k // 2], h[k // 2:]
    d1 = sum(r["debt_per_capita"] for r in first) / max(1, len(first))
    d2 = sum(r["debt_per_capita"] for r in second) / max(1, len(second))
    s = hist[:5]
    return dict(
        years=f"{h[0]['year']}-{h[-1]['year']}",
        startup_hungry=sum(r["hungry"] / r["pop"] for r in s) / len(s),
        debt_cv=_cv([r["debt_per_capita"] for r in h]),
        pop=pop,
        debt_per_capita=avg(lambda r: r["debt_per_capita"]),
        debt_trend=(d2 - d1) / d1 if d1 else 0.0,
        minted_per_capita=avg(lambda r: r["minted"] / r["pop"] if r["pop"] else 0),
        extinguished_per_capita=avg(lambda r: r["extinguished"] / r["pop"] if r["pop"] else 0),
        trade_per_capita=avg(lambda r: r["value"] / r["pop"] if r["pop"] else 0),
        mint_share=avg(lambda r: r["minted"] / r["value"] if r["value"] else 0),
        hungry=avg(lambda r: r["hungry"] / r["pop"] if r["pop"] else 0),
        hungry_short=avg(lambda r: r["hungry_none"] / r["pop"] if r["pop"] else 0),
        hungry_limit=avg(lambda r: r["hungry_limit"] / r["pop"] if r["pop"] else 0),
        starved=avg(lambda r: r["death_starved"] / r["pop"] if r["pop"] else 0),
        idle=avg(lambda r: r["idle"]),
        at_limit=avg(lambda r: r["at_limit"]),
        zero=avg(lambda r: r["zero"]),
        gini=avg(lambda r: r["gini"]),
        top10=avg(lambda r: r["top10"]),
        luxury=avg(lambda r: r["sold_" + LUXURY] / r["pop"] if r["pop"] else 0),
        blocked=avg(lambda r: r["blocked"]),
        deathbed=avg(lambda r: r["deathbed"] / r["pop"] if r["pop"] else 0),
    )


SCENARIOS = {
    "perus": dict(),
    "ei-etiikkaa": dict(work_ethic=0.0),
    "etiikka-0.5": dict(work_ethic=0.5),
    "minimi": dict(strategies={MINIMI: 1.0}),
    "maksimi": dict(strategies={MAKSIMI: 1.0}),
    "seka": dict(strategies={MINIMI: 1 / 3, MAKSIMI: 1 / 3, TASAPAINO: 1 / 3}),
    "ei-varausta": dict(reserve=False),
    "ikaantyva": dict(birth_rate=0.8),
    "kuolleisuus": dict(mortality_shock=(300, 0.2)),
    "luksuspiikki": dict(luxury_shock=300),
    "kuolinvuode": dict(deathbed=True, deathbed_from=300),
    "myyjakatto": dict(seller_cap=20),
}


def shock_year(cfg):
    if cfg.mortality_shock:
        return cfg.mortality_shock[0]
    if cfg.luxury_shock:
        return cfg.luxury_shock
    if cfg.deathbed:
        return cfg.deathbed_from
    return None


def window(hist, a, b):
    """Keskiarvot vuosilta a..b (mukaan lukien)."""
    h = [r for r in hist if a <= r["year"] <= b]
    k = len(h)
    return dict(
        debt_per_capita=sum(r["debt_per_capita"] for r in h) / k,
        hungry=sum(r["hungry"] / r["pop"] for r in h) / k,
        starved=sum(r["death_starved"] / r["pop"] for r in h) / k,
        idle=sum(r["idle"] for r in h) / k,
        luxury=sum(r["sold_" + LUXURY] / r["pop"] for r in h) / k,
        extinguished_per_capita=sum(r["extinguished"] / r["pop"] for r in h) / k,
    )


def scenario(name, **kw):
    return replace(Config(), **SCENARIOS[name], **kw)
