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
spawn, exec, wait, rfork, sleep, getpid). Virhe on `OSError` Plan 9:n
virhetekstillä.

## Testi

QEMU, ilman juurilevyä: ESP:ssä kernel ja plan9.ini
`init=/boot/mp -B`, `console=0`, `user=glenda`. Sarjakonsolin kautta
(`tools/test-mpinit.sh`):

1. Kehote `>>> ` tulee ilman juurta: kernelistä suoraan Pythoniin.
2. `import plan9`: `/boot`:ssa `mp` ja `kbdfs`, `/dev`:ssä kbdfs:n
   `cons` ja `kbmap`, `#c/sysname` luettavissa.
3. Laskutoimitus, tiedoston kirjoitus ja luku (`/tmp` ramfs:n sijaan
   `#e`-ympäristö: `/env/x`), lapsiprosessi `spawn` + `wait`.
4. Ctrl-D lopettaa REPLin: boot.py käynnistää sen uudelleen (init ei kuole).

Samaa kuvaa vasten myöhemmin: `root=` mukaan, boot.py mounttaa gefs-juuren
ja ajaa sieltä rc:n (`spawn(['/bin/rc', '-c', ...])`), sitten ruudulla ja
näppäimistöllä (kbmap=fi) kuten `~/plan9mp/tools/p5-screen.sh`.
