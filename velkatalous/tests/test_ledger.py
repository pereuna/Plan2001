"""Vaihe A: kirjanpidon säännöt ja invariantit (määrittely luvut 2, 3 ja 5)."""

import io
import random
import unittest

from velka.__main__ import example
from velka.ledger import BLOCKED, CANCELLED, OPEN, SETTLED, Ledger, LedgerError


def two(limit_a=200, limit_b=200, **kw):
    g = Ledger(**kw)
    g.create(1, limit_a)
    g.create(2, limit_b)
    return g


class LedgerTest(unittest.TestCase):
    def test_example_from_zero(self):
        g = example(io.StringIO())
        self.assertEqual((g.debt(1), g.debt(2)), (40, 0))
        self.assertEqual((g.minted, g.extinguished, g.total), (100, 60, 40))

    def test_transfer_without_debt(self):
        g = two()
        with self.assertRaises(LedgerError):
            g.transfer(1, 2, 1)
        g.transfer(1, 2, 0)
        self.assertEqual(g.total, 0)

    def test_full_delivery_moves_existing_debt(self):
        g = two()
        g.sell(2, 1, 100, due=0)
        g.settle_due()
        self.assertEqual((g.debt(1), g.minted), (100, 100))
        p = g.sell(1, 2, 60, due=3)
        g.advance(2)
        self.assertEqual(g.settle_due(), [])
        g.advance(3)
        self.assertEqual(g.settle_due(), [(p, True)])
        self.assertEqual((g.debt(1), g.debt(2), g.minted), (40, 60, 100))

    def test_partial_delivery_mints_rest_for_buyer(self):
        g = two()
        g.sell(2, 1, 30, due=0)
        g.settle_due()
        p = g.sell(1, 2, 100, due=1)
        g.advance(1)
        g.settle_due()
        self.assertEqual((g.debt(1), g.debt(2), g.minted), (0, 100, 100))
        e = g.events[-1]
        self.assertEqual((e.type, e.data["transferred"], e.data["minted"]), ("PromiseSettled", 30, 70))
        self.assertEqual(g.promises[p].status, SETTLED)

    def test_buyer_limit_blocks_atomically(self):
        g = two(limit_b=50)
        g.sell(2, 1, 30, due=0)
        g.settle_due()
        p = g.sell(1, 2, 60, due=1)
        g.advance(1)
        g.settle_due()
        self.assertEqual(g.promises[p].status, BLOCKED)
        self.assertEqual((g.debt(1), g.debt(2), g.minted), (30, 0, 30))
        self.assertEqual(g.events[-1].type, "PromiseBlocked")

    def test_transfer_respects_limit(self):
        g = two(limit_b=10)
        g.sell(2, 1, 50, due=0)
        g.settle_due()
        with self.assertRaises(LedgerError):
            g.transfer(1, 2, 11)
        g.transfer(1, 2, 10)
        self.assertEqual((g.debt(1), g.debt(2)), (40, 10))

    def test_death_extinguishes_and_cancels(self):
        g = two()
        g.create(3, 200)
        g.sell(2, 1, 80, due=0)
        g.settle_due()
        p = g.sell(1, 3, 20, due=5)
        q = g.sell(3, 1, 20, due=5)
        self.assertEqual(g.death(1), 80)
        self.assertEqual((g.total, g.minted, g.extinguished), (0, 80, 80))
        self.assertEqual((g.promises[p].status, g.promises[q].status), (CANCELLED, CANCELLED))
        self.assertEqual(g.open_bought.get(3, 0), 0)
        for f in (lambda: g.transfer(1, 2, 0), lambda: g.sell(2, 1, 1, 9), lambda: g.death(1)):
            with self.assertRaises(LedgerError):
                f()

    def test_limit_not_below_debt(self):
        g = two()
        g.sell(2, 1, 80, due=0)
        g.settle_due()
        with self.assertRaises(LedgerError):
            g.set_limit(1, 79)
        g.set_limit(1, 80)

    def test_promise_rules(self):
        g = two(seller_cap=50, reserve=True)
        for f in (lambda: g.sell(1, 1, 10, 1), lambda: g.sell(1, 2, 0, 1),
                  lambda: g.sell(1, 2, 10, 1, accepted=False), lambda: g.sell(1, 2, 51, 1)):
            with self.assertRaises(LedgerError):
                f()
        g.advance(2)
        with self.assertRaises(LedgerError):
            g.sell(1, 2, 10, 1)
        g.sell(1, 2, 50, 3)
        with self.assertRaises(LedgerError):
            g.sell(1, 2, 1, 3)        # myyjän katto
        g2 = two(limit_b=100, reserve=True)
        g2.sell(1, 2, 60, 1)
        with self.assertRaises(LedgerError):
            g2.sell(1, 2, 41, 1)      # varaus: 60 + 41 > 100
        g2.sell(1, 2, 40, 1)

    def test_without_reserve_second_promise_blocks(self):
        g = two(limit_b=100)
        p = g.sell(1, 2, 60, 1)
        q = g.sell(1, 2, 60, 1)
        g.advance(1)
        self.assertEqual(g.settle_due(), [(p, True), (q, False)])
        self.assertEqual(g.debt(2), 60)

    def test_random_operations_keep_invariants_and_replay(self):
        for seed in range(20):
            rng = random.Random(seed)
            g = Ledger(strict=True, seller_cap=rng.choice([None, 100]), reserve=rng.random() < 0.5)
            alive = []
            for i in range(1, 8):
                g.create(i, rng.randint(0, 300))
                alive.append(i)
            nid = 8
            for t in range(1, 60):
                g.advance(t)
                for _ in range(rng.randint(0, 6)):
                    if len(alive) < 2:
                        break
                    a, b = rng.sample(alive, 2)
                    op = rng.random()
                    try:
                        if op < 0.45:
                            g.sell(a, b, rng.randint(1, 80), t + rng.randint(0, 4))
                        elif op < 0.8:
                            g.transfer(a, b, rng.randint(0, g.debt(a) + 5))
                        elif op < 0.9:
                            g.set_limit(a, rng.randint(0, 400))
                        else:
                            g.death(a)
                            alive.remove(a)
                            g.create(nid, rng.randint(0, 300))
                            alive.append(nid)
                            nid += 1
                    except LedgerError:
                        pass
                g.settle_due()
            g.check()
            self.assertEqual(g.total, g.minted - g.extinguished)
            self.assertTrue(all(p.status != OPEN or p.due >= g.time for p in g.promises.values()))
            r = Ledger.replay(g.events, seller_cap=g.seller_cap, reserve=g.reserve)
            self.assertEqual({i: a.debt for i, a in r.actors.items()},
                             {i: a.debt for i, a in g.actors.items()})
            self.assertEqual((r.minted, r.extinguished), (g.minted, g.extinguished))
            self.assertEqual({k: p.status for k, p in r.promises.items()},
                             {k: p.status for k, p in g.promises.items()})


if __name__ == "__main__":
    unittest.main()
