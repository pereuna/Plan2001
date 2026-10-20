# Plan2001 Stage 4: run in the build VM by tools/mkovol.  It compiles the
# demo source with the tree's MicroPython (space.define), then writes the
# seed files the userspace gefs -i reads to make the O objects.  These
# files are host preparation inputs, never runtime program storage.

import space

SRC = "/tmp/odemo.py"
SEED = "/tmp/seed"
CODE_OID = 0xffffffff00000000

h = space.define(SRC)
mpy = "/usr/glenda/lib/mod/" + h + ".mpy"
b = open(mpy, "rb").read()
env = ("runtime " + space.runtime + "\n"
       "odemo " + h + " " + space.hash(b) + " " + "%016x" % CODE_OID + "\n")
open(SEED + "/env", "w").write(env)
open(SEED + "/envhex", "w").write(space.hash(env))
open(SEED + "/mod/" + h + ".mpy", "wb").write(b)
print("seed: odemo", h, space.hash(b), "%016x" % CODE_OID, len(b))
