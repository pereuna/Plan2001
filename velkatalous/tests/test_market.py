"""Vaihe B: markkinasimulaation kirjanpito ja determinismi."""

import unittest

from velka.ledger import Ledger
from velka.market import Market, scenario, summarize


def small(name="perus", **kw):
    kw = {"n": 80, "years": 60, "seed": 3, **kw}
    return Market(scenario(name, **kw))


class MarketTest(unittest.TestCase):
    def test_deterministic(self):
        self.assertEqual(small().run(), small().run())
        self.assertNotEqual(small().run(), small(seed=4).run())

    def test_ledger_consistent_and_replayable(self):
        m = small(log=True, deathbed=True, deathbed_from=30)
        h = m.run()
        g = m.ledger
        g.check()
        self.assertEqual(h[-1]["M"] - h[-1]["X"], h[-1]["total_debt"])
        self.assertGreater(g.minted, 0)
        self.assertGreater(sum(r["deathbed"] for r in h), 0)
        r = Ledger.replay(g.events, reserve=g.reserve, seller_cap=g.seller_cap)
        self.assertEqual({i: a.debt for i, a in r.actors.items()},
                         {i: a.debt for i, a in g.actors.items()})

    def test_no_work_ethic_freezes(self):
        h = small("ei-etiikkaa").run()
        self.assertEqual(sum(r["value"] for r in h), 0)
        self.assertEqual(h[-1]["M"], 0)
        self.assertGreater(sum(r["death_starved"] for r in h), 0)

    def test_summary(self):
        s = summarize(small().run(), 20)
        self.assertEqual(s["years"], "41-60")
        self.assertGreater(s["trade_per_capita"], 0)


if __name__ == "__main__":
    unittest.main()
