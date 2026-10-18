# MicroPython ensimmäisenä käyttäjäohjelmana (17.10.2026)

Tavoite: ensimmäinen käyttäjätilan ohjelma on MicroPython kernelin
kuvasta, ei juuren rc-skripti. Init on Pythonia ja kulkee kernelin
mukana, joten se toimii ilman juuritiedostojärjestelmää. Juuri (gefs),
laitteet ja konsoli kootaan Pythonista. Taustana `~/plan9mp`:n
GEFS-MP-TAVOITE (vaihe 2: init ilman juurimounttausta).

## Boot nyt

```
UEFI → loader → kernel
  userinit: prosessi 1 (proc0), initcode yhdelle tekstisivulle
  init0 (kernel, prosessi 1): rootmount (root=, gefs), plan9.ini → #ec,
        argv init=:stä (oletus /amd64/init -t)
  initcode (käyttäjätila, sivu ilman libc:tä): bind #c #d #e #p #s #σ,
        fd 0-2 = #c/cons (vain tuloste), exec argv[1]
→ /init (rc): laitteet, kbdfs, kbmap, tz, mntgen, verkko, ktest
→ exec rc -i </dev/cons
```

## Boot MicroPythonilla

```
UEFI → loader → kernel
  init0: rootmount, jos root=; init=/boot/mp... ilman juurtakin
  initcode: exec /boot/mp -B
→ mp (kernelin kuvassa, devroot /boot/mp): sisäänrakennettu boot.py
     laitteet, kbdfs (/boot/kbdfs), fd 0-2 = kbdfs:n /dev/cons,
     juuri, jos kernel mounttasi sen
→ Pythonin REPL konsolissa
```

- Kernelin kuvaan (pc64:n `bootdir`) tulevat `/amd64/bin/micropython`
  nimellä `mp` ja `/amd64/bin/aux/kbdfs` nimellä `kbdfs`, riisuttuina
  (`port/mkrootall`): noin 310 kt ja 150 kt.
- `init0`: plan9.ini:n `init=`, joka alkaa `/boot/`:lla (devroot), ajetaan myös ilman
  `root=`:a. `user=` asetetaan aina (ennen vain rootmountissa).
- initcode pysyy ennallaan tässä vaiheessa (bindit ja fd:t, joita myös
  rc-init tarvitsee). Seuraavaksi sen työt siirtyvät boot.py:hyn ja
  initcodesta jää pelkkä exec.
- Näppäimistö: kbdfs boot-tiedostona (vaihtoehto 3: ensin kbdfs, myöhemmin
  ajuri Pythonilla, `#b/scancode` ja kbmap). Kernelin `#c/cons` on vain
  tuloste.
- rc jää ohjelmaksi: mk:n reseptit, git, `fshalt` ja `reboot` ovat rc:tä.
  Juuren rc-`/init` on yhä oletus; MicroPython-init valitaan plan9.ini:llä.

## MicroPython puussa

`sys/src/cmd/micropython`: upstreamin v1.27.0 (commit `78ff170`) `py/`,
tarvittavat `shared/`- ja `extmod/`-tiedostot (`tools/micropython.import`)
sekä Plan2001:n portti (`main.c`, `p9*.c`, `modplan9.c`, `include/`:n
ISO C -otsakkeet libc:n päälle). Käännös `6c -p -u` (`-u`: ANSI C:n
kokonaislukujen promootiot, docs/status.md). Generoidut otsakkeet
(`genhdr/`: qstr:t, moduulit, juurimuuttujat, versio) ovat puussa, koska
niiden generointi tarvitsee CPythonin: Linuxissa `make genhdr` hakemistossa,
kun konfiguraatio muuttuu.

`plan9`-moduuli: syscallit Pythonille (open, create, read, write, close,
remove, seek, bind, mount, unmount, chdir, fd2path, dirread, dup, pipe,
spawn, exec, wait, rfork, sleep, getpid, stat, wstat, sync, alarm;
`sync` on tyhjä wstat eli fsync, docs/object-model.md). Virhe on
`OSError` Plan 9:n virhetekstillä. `alarm(ms)`:n päättyessä estävä
kutsu keskeytyy (`OSError: interrupted`), ja prosessi jatkaa.

## Mallille (18.10.2026)

Arvio: portti käytetään sellaisenaan, Pythonin syntaksilla ja
semantiikalla; ei omaa murretta (esim. `{}`-lohkoja). Malli kirjoittaa
halvimmin ja virheettömimmin sitä, mitä se on nähnyt eniten, eli
CPythonia. Sisennys ei maksa tokeneita (välilyöntien jakso on yksi
token), mutta aaltosulkeet ja `;` maksavat; yksi välilyönti tai tabi on
yksi tavu tasoa kohden hitaalla yhteydellä. Konfiguraatio
(`mpconfigport.h`) seuraa samaa:

- Liukuluvut (double, libc:n math; ei complexia), isot kokonaisluvut
  (mpz), `math`, `json`, `sys.maxsize`. 6c:n bittikentät ovat enintään
  int:n levyisiä, joten liukuluvun bitit luetaan makroilla
  (`MP_FLOAT_NO_BITFIELD`, `py/misc.h`; upstreamin testit samat
  tulosteet Linuxissa kummallakin). libc:stä puuttuvat ISO C:n funktiot
  (`copysign`, `trunc`, `nearbyint`, ...) ovat `p9math.c`:ssä. Plan 9
  pysäyttää prosessin liukulukuvirheeseen (0/0, ylivuoto), joten `main`
  kytkee ne pois: tulos on inf tai nan kuten C:ssä. `math.inf`:iä ja
  `math.nan`:ia ei ole (alustettua dataa, libc:n NaN() on funktio);
  `float('inf')` toimii.
- `open()` ja tiedosto-oliot (`p9file.c`: io.FileIO, io.TextIOWrapper
  fd:n päällä; `with`, rivien iterointi, `read()` loppuun asti, tilat
  r, w, a, x, +, b; `encoding=` ym. hyväksytään, aina UTF-8) ja `io`
  (StringIO, BytesIO). Ilman puskuria: `readline` lukee tavun kerrallaan.
  Virhe on OSError Plan 9:n tekstillä kuten plan9-moduulissa. Sulkematon
  tiedosto suljetaan gc:ssä (`MICROPY_ENABLE_FINALISER`). Poikkeamat
  CPythonista: tekstitilan `write` palauttaa tavut, StringIO:lla ei ole
  `readlines`ia.
- Traceback kertoo rivin (`MICROPY_ENABLE_SOURCE_LINE`) ja virhe kohteen
  (`MICROPY_ERROR_REPORTING_DETAILED`): malli löytää virheensä ilman
  ylimääräistä kierrosta. Traceback puuttui ennen kokonaan: poikkeuksen
  `traceback_len` oli `size_t`-bittikenttä, jota 6c ei käsittele
  (`MP_BITS` kuten `mp_map_t`:ssä).
- `repr('ä')` on `'ä'` kuten CPythonissa, ei `'\xe4'`
  (`MICROPY_PY_STR_REPR_UTF8`, upstreamiin makrolla).
- Tabit ja välilyönnit: sekoitus, joka tekee lohkoista epäselviä, on
  `IndentationError` kuten CPython 3:n TabError
  (`MICROPY_LEXER_TAB_CHECK`, upstreamiin makrolla, oletus ennallaan):
  sisennysten järjestys on sama tabilla 8 ja 1 saraketta. Pelkät tabit
  tai pelkät välilyönnit (vaikka yksi tasoa kohden) käyvät.
- Rekursio: `MICROPY_STACKLESS`, joten Pythonin kutsu Pythonista ottaa
  kehyksensä pystackista (1 Mt), ei C-pinosta; C-pinon raja on 8 Mt
  käyttäjän 16 Mt:stä (C:n kautta kulkevat kutsut, kuten `__init__`).
  Syvyys on 8190 (CPythonin raja 1000). Ei Stackless Pythonin
  taskletteja; korutiinit ovat generaattoreita.
  STACKLESS vaatii pystackin: v1.27.0:ssa STACKLESS ilman sitä
  (kehykset keossa) kaatuu, kun C:n kautta kutsuttu funktio nostaa
  poikkeuksen ja moduuli palaa (`try: int('x')` ..., unix-portilla
  toistettu).
- Agentin protokolla on raw REPL (mpremoten): Ctrl-A, koodi, Ctrl-D;
  takaisin `OK`, tuloste, `\x04`, virhe, `\x04`, `>`. Ei kaikua eikä
  automaattista sisennystä, tuloste ja poikkeus erikseen; Ctrl-B palaa
  tavalliseen REPLiin. Paste-tila (Ctrl-E) on ihmiselle.

Seuraavaksi: `os`, `sys.stdout`/`stderr`, socket-tyyppinen
`/net`-kääre ja `plan9.pyi` (tyypit mallille, ei valvontaa).

## Testi

`tools/test-mpinit.sh`: QEMU, plan9.ini `init=/boot/mp -B`, `console=0`,
`user=glenda`, sarjakonsolin kautta. Askeleet raw REPLissä; lopuksi
Ctrl-B ja tavallinen REPL (Ctrl-C, Ctrl-D, `rc -i`).

Kummassakin: liukuluvut, `2**100`, `round` (puolikkaat parilliseen),
`math`, `json`, `open()` (`with`, w, a, x, rivit, binääri, `/boot/mp`
kerralla), `io.StringIO`, tracebackin rivi ja kohde, yhden välilyönnin
ja tabin sisennys, sekoituksen IndentationError, `g(1000)`-rekursio ja
rekursion syvyys (RuntimeError, vähintään 1000).

Ilman juurilevyä (ESP:ssä vain kernel):

1. Kehote `>>> ` tulee ilman juurta: kernelistä suoraan Pythoniin.
2. `import plan9`: `/boot`:ssa `mp` ja `kbdfs`, `/dev`:ssä kbdfs:n
   `cons`, `consctl` ja `kbmap`, `#c/user` luettavissa.
3. Tiedoston kirjoitus ja luku (`/tmp` ramfs:n sijaan `#e`-ympäristö:
   `/env/x`), lapsiprosessi `spawn` + `wait`, f-string, UTF-8, OSError.
4. Ctrl-C keskeyttää silmukan; Ctrl-D lopettaa REPLin, joka alkaa
   uudelleen (init ei kuole).

`ROOT=1`: gefs-juuri (`tools/gefsdisk`, `root=-w PARTUUID=`):

1. boot.py näkee juuren ja sitoo `/amd64/bin` ja `/rc/bin` `/bin`:iin.
2. rc Pythonista: `spawn(['/bin/rc', '-c', ...])` + `wait`, virhestatus,
   ohjelma käännettynä (6c, 6l) ja ajettuna.
3. Tiedosto juureen Pythonilla.
4. `rc -i` konsolissa Pythonin odottaessa (konsoli cooked), Ctrl-D palaa
   REPLiin.
5. `fshalt` rc:llä: gefs pysähtyy, REPL toimii yhä (kernelin kuvasta).
6. Lopuksi `tools/gefsck` tarkistaa osion ja lukee tiedoston.

`NET=1` (kummassakin): verkko (QEMU:n user-verkko, isännän vapaa portti
VM:n porttiin 80) ja HTTP-palvelin pelkillä syscalleilla,
`sys/lib/python/httpd.py`:

1. Tiedosto lähetetään raw REPLiin kerralla, kuten agentti sen tekee
   (testi kertoo lähetetyt ja takaisin tulleet tavut).
2. `net()`: `#l` ja `#I` `/net`:iin, `/net/ipifc/clone`:iin `bind ether
   /net/ether0` ja osoite, `/net/iproute`:en reitti (ip/ipconfigin työ).
3. `serve()`: `/net/tcp/clone`, `announce *!80`, silmukassa `listen`;
   jokainen yhteys omassa prosessissaan (`rfork(RFPROC|RFFDG|RFNOWAIT)`
   kopioi tulkin), joten selaimen tyhjä ennakkoyhteys ei estä muita.
4. Isännältä HTTP:llä `/`, `/boot`, `/env/cputype`, virhe 500, tyhjä
   yhteys auki toisen pyynnön ajan, 20 pyyntöä; lokirivit eri pid:istä.
5. Ctrl-C pysäyttää palvelimen, REPL on yhä prosessi 1.

Juurella sama `import httpd` (`sys.path`: `/sys/lib/python`).

Myöhemmin: ruutu ja näppäimistö (kbmap=fi) kuten
`~/plan9mp/tools/p5-screen.sh`, ja p5 raudalla.
