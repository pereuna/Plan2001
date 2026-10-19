# graph: relations as class attributes, their inverse sides kept by
# themselves, and the human's view of an object: its identifier and
# #tags (docs/object-model.md 5.7, 5.8).  A module of a Space's
# Environment: its state (the classes' instances, the identifiers, the
# tags) is the Space's, committed and restored with it.
#
#   class Photo(graph.Node):
#       project = graph.link(Project, inverse='photos')    # many to one
#       people = graph.links('Person', inverse='photos')   # many to many
#   photo.project = p; photo.people.add(m)    # p.photos, m.photos follow
#   graph.find(Photo, project=p)              # from p.photos, no scan
#   graph.tag(n, '#kaviot #mirkku'); graph.search('#kaviot #mirkku')
import sys

_next = 1       # the next Node's identifier, never reused
_byid = {}      # identifier -> Node
_extent = {}    # class -> its own instances (its subclasses' apart)
_descs = []     # (class, name, descriptor): the links and inverse sides
_pending = []   # links whose target is a class's name, not resolved yet
_tags = {}      # folded name -> Tag

# ---- relations ----

def _resolve():
    """the targets given by name: the classes of that name in sys.modules"""
    while _pending:
        d = _pending.pop()
        for m in sys.modules.values():
            c = getattr(m, d.target, None)
            if isinstance(c, type):
                d.target = c
                d._bind()
                break
        else:
            _pending.append(d)
            raise NameError("graph: no class '%s' for %s.%s" % (d.target, d.owner.__name__, d.name))

def _invset(obj, d):
    """the set behind obj's inverse side of d"""
    s = getattr(obj, '_i_' + d.inverse, None)
    if s is None:
        s = set()
        setattr(obj, '_i_' + d.inverse, s)
    return s

class _View:
    """an inverse side: read only"""
    def __init__(self, s):
        self._s = s
    def __iter__(self):
        return iter(self._s)
    def __len__(self):
        return len(self._s)
    def __contains__(self, x):
        return x in self._s

class _Inverse:
    """the inverse side of a link or links, on its target class"""
    def __init__(self, src):
        self.src = src
    def __get__(self, inst, owner=None):
        if inst is None:
            return self
        return _View(getattr(inst, '_i_' + self.src.inverse, None) or ())
    def __set__(self, inst, value):
        raise AttributeError("graph: %s is the inverse of %s.%s: read only"
            % (self.src.inverse, self.src.owner.__name__, self.src.name))

class link:
    """one target or None"""
    def __init__(self, target, inverse=None):
        self.target = target
        self.inverse = inverse
        self.owner = self.name = None
    def __set_name__(self, owner, name):
        self.owner = owner
        self.name = name
        _descs.append((owner, name, self))
        if isinstance(self.target, str):
            _pending.append(self)
        else:
            self._bind()
    def _bind(self):
        if self.inverse is not None:
            inv = _Inverse(self)
            setattr(self.target, self.inverse, inv)
            _descs.append((self.target, self.inverse, inv))
    def _check(self, value):
        if isinstance(self.target, str):
            _resolve()
        if not isinstance(value, self.target):
            # another class, or an object not of this Space's graph
            raise TypeError('graph: %s.%s: not a %s: %r'
                % (self.owner.__name__, self.name, self.target.__name__, value))
    def __get__(self, inst, owner=None):
        if inst is None:
            return self
        return getattr(inst, '_l_' + self.name, None)
    def __set__(self, inst, value):
        if value is not None:
            self._check(value)
        old = getattr(inst, '_l_' + self.name, None)
        if old is value:
            return
        if old is not None and self.inverse is not None:
            _invset(old, self).discard(inst)
        setattr(inst, '_l_' + self.name, value)
        if value is not None and self.inverse is not None:
            _invset(value, self).add(inst)

class _LinkSet:
    """a links attribute of one object: add, discard, iteration"""
    def __init__(self, inst, d):
        self._inst = inst
        self._d = d
    def _set(self):
        s = getattr(self._inst, '_l_' + self._d.name, None)
        if s is None:
            s = set()
            setattr(self._inst, '_l_' + self._d.name, s)
        return s
    def add(self, x):
        self._d._check(x)
        self._set().add(x)
        if self._d.inverse is not None:
            _invset(x, self._d).add(self._inst)
    def discard(self, x):
        self._set().discard(x)
        if self._d.inverse is not None:
            _invset(x, self._d).discard(self._inst)
    def __iter__(self):
        return iter(list(self._set()))
    def __len__(self):
        return len(self._set())
    def __contains__(self, x):
        return x in self._set()

class links(link):
    """a set of targets"""
    def __get__(self, inst, owner=None):
        if inst is None:
            return self
        return _LinkSet(inst, self)
    def __set__(self, inst, value):
        raise AttributeError('graph: %s.%s is a set: add and discard'
            % (self.owner.__name__, self.name))

# ---- objects ----

class Tag:
    """a #tag: its first spelling and its folded key"""
    def __init__(self, name, key):
        self.name = name
        self.key = key

class Node:
    """an object of the graph: an identifier, tags, its class's extent"""
    tags = links(Tag, inverse='objects')
    def __new__(cls, *args, **kw):
        global _next
        o = object.__new__(cls)
        o._id = _next
        _next += 1
        _byid[o._id] = o
        s = _extent.get(cls)
        if s is None:
            s = _extent[cls] = set()
        s.add(o)
        return o
    def __init__(self, *args, **kw):
        pass
    def __getattr__(self, name):
        # an inverse side of a link whose target was named, not bound yet
        if name[0] != '_' and _pending:
            _resolve()
            return getattr(self, name)
        raise AttributeError(name)

class Text(Node):
    """text with an identifier: what a file was"""
    def __init__(self, text=''):
        self.text = text

def find(cls, **cond):
    """the instances of cls (and its subclasses) meeting cond: a link's
    or links' condition from the target's inverse side, the rest by
    comparison"""
    cand = None
    rest = []
    for k, v in cond.items():
        d = getattr(cls, k, None)
        if isinstance(d, link) and d.inverse is not None:
            s = getattr(v, '_i_' + d.inverse, None) or set()
            cand = set(s) if cand is None else cand & s
        else:
            rest.append((k, v, d))
    if cand is None:
        cand = set()
        for c, s in _extent.items():
            if issubclass(c, cls):
                cand |= s
    r = []
    for o in cand:
        if not isinstance(o, cls):
            continue
        for k, v, d in rest:
            x = getattr(o, k, None)
            if isinstance(d, links):
                if v not in x:
                    break
            elif x is not v and x != v:
                break
        else:
            r.append(o)
    return sorted(r, key=_key)

def _key(o):
    return o._id

def delete(n):
    """n out of its relations, both sides, and its class's extent"""
    for c, name, d in list(_descs):
        if not isinstance(n, c):
            continue
        if isinstance(d, links):
            ls = d.__get__(n)
            for x in ls:
                ls.discard(x)
        elif isinstance(d, link):
            d.__set__(n, None)
        elif isinstance(d, _Inverse):
            for src in list(getattr(n, '_i_' + d.src.inverse, None) or ()):
                if isinstance(d.src, links):
                    d.src.__get__(src).discard(n)
                else:
                    d.src.__set__(src, None)
    s = _extent.get(type(n))
    if s is not None:
        s.discard(n)
    _byid.pop(n._id, None)

def byid(i):
    return _byid.get(i)

# ---- the human's view: the identifier and #tags ----

def _fold1(c):
    o = ord(c)
    if 0xC0 <= o <= 0xDE and o != 0xD7:
        return chr(o + 32)
    if 0x100 <= o <= 0x17F:
        if o == 0x130:
            return 'i'
        if o == 0x178:
            return '\xff'
        if o in (0x131, 0x138, 0x149, 0x17F):
            return c
        if 0x139 <= o <= 0x148 or 0x179 <= o <= 0x17E:
            return chr(o + 1) if o % 2 else c
        return c if o % 2 else chr(o + 1)
    return c

def fold(s):
    """the case-folded key: ASCII, Latin-1 and Latin Extended-A"""
    s = s.lower()
    for c in s:
        if ord(c) >= 0xC0:
            return ''.join([_fold1(c) for c in s])
    return s

def ident(n):
    """n's identifier as its tag: at least 7 digits"""
    return '%07d' % n._id

def _words(text):
    w = text.split()
    for x in w:
        if len(x) < 2 or x[0] != '#':
            raise ValueError('graph: not a #tag: %r' % x)
    return [x[1:] for x in w]

def _idof(word):
    """the identifier a word is (digits, as ident writes them), or None"""
    if word.isdigit() and '%07d' % int(word) == word:
        return int(word)
    return None

def tag(n, text):
    """n tagged with each #tag of text (the Tag made if new)"""
    for w in _words(text):
        k = fold(w)
        t = _tags.get(k)
        if t is None:
            t = _tags[k] = Tag(w, k)
        n.tags.add(t)

def untag(n, text):
    for w in _words(text):
        if w == ident(n):
            raise ValueError('graph: #%s is the identifier' % w)
        t = _tags.get(fold(w))
        if t is not None:
            n.tags.discard(t)

def tags(n):
    """n's tags, its identifier first, the rest by their keys"""
    return [ident(n)] + [t.name for t in sorted(n.tags, key=_tkey)]

def _tkey(t):
    return t.key

def line(n):
    return ' '.join(['#' + t for t in tags(n)])

def parse(text):
    """a tag line: (identifier, [tags]); the first is the identifier"""
    w = _words(text)
    if not w:
        raise ValueError('graph: an empty line')
    i = _idof(w[0])
    if i is None:
        raise ValueError('graph: #%s is not an identifier' % w[0])
    if i not in _byid:
        raise ValueError('graph: no object #%s' % w[0])
    return i, w[1:]

def search(text):
    """the objects with all of the #tags; an identifier is a tag too"""
    w = _words(text)
    if not w:
        raise ValueError('graph: an empty search')
    r = None
    for x in w:
        t = _tags.get(fold(x))
        s = set(t.objects) if t is not None else set()
        i = _idof(x)
        if i is not None and i in _byid:
            s.add(_byid[i])
        r = s if r is None else r & s
        if not r:
            break
    return sorted(r, key=_key)

def importfile(path):
    """a file as a Text, its name its tag (#koe.txt)"""
    with open(path) as f:
        t = Text(f.read())
    tag(t, '#' + path.split('/')[-1])
    return t
