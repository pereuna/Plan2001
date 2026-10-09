"""python3 -m velka esimerkki | markkina [skenaario] [--n N] [--years Y] [--seed S] [--csv TIED] | rasitus"""

import argparse
import csv
import sys

from .ledger import Ledger
from .market import SCENARIOS, Market, scenario, shock_year, summarize, window


def example(out=sys.stdout):
    """Määrittelyn luku 3: nollasta käynnistys."""
    g = Ledger()
    A, B = 1, 2
    g.create(A, 200)
    g.create(B, 200)

    def show(what):
        print(f"{what:<48} D=({g.debt(A)},{g.debt(B)})  M={g.minted} X={g.extinguished} "
              f"sum={g.total}", file=out)

    g.advance(1)
    p = g.sell(A, B, 100, due=5)
    show("päivä 1: A myy B:lle puuta, lupaa 100 päivänä 5")
    g.advance(5)
    g.settle_due()
    show("päivä 5: A:lla ei velkaa, B:lle luodaan 100")
    g.advance(6)
    g.transfer(B, A, 40)
    show("päivä 6: B myy A:lle laatikoita, siirtää 40")
    g.death(B)
    show("B kuolee: 60 poistuu")
    assert (g.debt(A), g.minted, g.extinguished, g.total) == (40, 100, 60, 40)
    assert g.promises[p].status == "settled"
    r = Ledger.replay(g.events)
    assert (r.debt(A), r.minted, r.extinguished) == (40, 100, 60)
    print(f"tapahtumia {len(g.events)}, replay antaa saman tilan", file=out)
    return g


KEYS = [
    ("startup_hungry", "nälkä v. 1-5", "{:.0%}"),
    ("pop", "väestö", "{:.0f}"),
    ("debt_per_capita", "velka/hlö", "{:.0f}"),
    ("debt_trend", "trendi", "{:+.1%}"),
    ("debt_cv", "vaihtelu (CV)", "{:.2f}"),
    ("minted_per_capita", "luotu/hlö/v", "{:.1f}"),
    ("extinguished_per_capita", "poistui/hlö/v", "{:.1f}"),
    ("trade_per_capita", "kauppa/hlö/v", "{:.1f}"),
    ("mint_share", "luodun osuus", "{:.0%}"),
    ("luxury", "ylell./hlö/v", "{:.2f}"),
    ("hungry", "nälkä", "{:.1%}"),
    ("hungry_short", "  ei tarjontaa", "{:.1%}"),
    ("hungry_limit", "  raja", "{:.1%}"),
    ("starved", "nälkäkuolemat/v", "{:.2%}"),
    ("idle", "toimettomat", "{:.0%}"),
    ("at_limit", "rajalla", "{:.0%}"),
    ("zero", "saldo 0", "{:.0%}"),
    ("gini", "gini", "{:.2f}"),
    ("top10", "top10 %", "{:.0%}"),
    ("blocked", "estyneet/v", "{:.1f}"),
    ("deathbed", "kuolinvuode/hlö/v", "{:.1f}"),
]


def table(rows, out=sys.stdout):
    names = list(rows)
    w = max(len(k[1]) for k in KEYS)
    cw = max(10, max(len(n) for n in names) + 1)
    print(" " * w + "".join(f"{n:>{cw}}" for n in names), file=out)
    for key, label, fmt in KEYS:
        print(f"{label:<{w}}" + "".join(f"{fmt.format(rows[n][key]):>{cw}}" for n in names), file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="velka")
    ap.add_argument("cmd", choices=["esimerkki", "markkina", "rasitus"])
    ap.add_argument("scenario", nargs="*", default=None)
    ap.add_argument("--n", type=int)
    ap.add_argument("--years", type=int)
    ap.add_argument("--seed", type=int)
    ap.add_argument("--last", type=int, default=200, help="yhteenvedon vuodet lopusta")
    ap.add_argument("--csv", help="vuosittaiset mittarit (yksi skenaario)")
    a = ap.parse_args(argv)
    if a.cmd == "esimerkki":
        example()
        return 0
    kw = {k: v for k, v in dict(n=a.n, years=a.years, seed=a.seed).items() if v is not None}
    names = a.scenario or (["perus"] if a.cmd == "markkina" else list(SCENARIOS))
    for s in names:
        if s not in SCENARIOS:
            ap.error(f"tuntematon skenaario {s}; on: {' '.join(SCENARIOS)}")
    rows, shocks, base = {}, {}, {}
    for s in names:
        cfg = scenario(s, **kw)
        hist = Market(cfg).run()
        if s == "perus":
            base[s] = hist
        rows[s] = summarize(hist, a.last)
        y = shock_year(cfg)
        if y and y + 19 <= cfg.years:
            if "perus" not in base:
                base["perus"] = Market(scenario("perus", **kw)).run()
            shocks[s] = (y, window(base["perus"], y, y + 19), window(hist, y, y + 19))
        if a.csv:
            with open(a.csv, "w", newline="") as f:
                keys = sorted({k for r in hist for k in r})
                w = csv.DictWriter(f, keys, restval=0)
                w.writeheader()
                w.writerows(hist)
    print(f"keskiarvot vuosilta {next(iter(rows.values()))['years']}")
    table(rows)
    if shocks:
        print("\nshokit: vuodet y..y+19, perus (sama siemen, ei shokkia) -> skenaario")
        for s, (y, b, c) in shocks.items():
            print(f"{s} (v. {y}): velka/hlö {b['debt_per_capita']:.0f} -> {c['debt_per_capita']:.0f}, "
                  f"poistui/hlö/v {b['extinguished_per_capita']:.1f} -> {c['extinguished_per_capita']:.1f}, "
                  f"nälkä {b['hungry']:.1%} -> {c['hungry']:.1%}, "
                  f"nälkäkuolemat/v {b['starved']:.2%} -> {c['starved']:.2%}, "
                  f"toimettomat {b['idle']:.0%} -> {c['idle']:.0%}, "
                  f"ylell./hlö/v {b['luxury']:.2f} -> {c['luxury']:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
