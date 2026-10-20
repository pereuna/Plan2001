# Plan2001 Stage 4 demo: the module the host preparation tool compiles
# into an O code object.  Its top level runs only on the first boot; a
# restored Space must see the same objects without running it again.

class Counter:
    def __init__(self, n=0):
        self.n = n


def bump(c):
    c.n += 1
    return c.n


shared = Counter(41)
alias = shared


def run():
    return bump(shared)
