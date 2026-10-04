# Plan 9 -fork, 9Front-2001 ja Plan2001 (päätös 4.10.2026)

Plan2001:n kehitys eriytetään yleisestä Plan 9 -forkista. Tämä dokumentti
kirjaa päätöksen, kerrokset, sijoitussäännöt ja siirron. Siirto on tehty
4.10.2026: alla olevan taulukon "Ennen"-sarake on historia, "Uusi" on
tiedoston paikka nyt. `tools/overlay DIR` kokoaa `plan9/`:n ja
`plan2001/`:n yhdeksi puuksi 9frontin juuren muodossa (`-f`: vain fork);
VM-build (`tools/build.sh`) lähettää sen VM:lle `sys/`ina. Koottu puu
oli siirron jälkeen tavu tavulta sama kuin entinen `sys/`, `rc/`, `usr/`,
`lib/` ja `wasm32/` (lisänä `kernel.html` wasm32:n hakemistossa).

## Kerrokset

| Kerros | Missä | Mitä | Sääntö |
|---|---|---|---|
| 9front | `subset/9front/` (`tools/9front.release`) | 9frontin julkaisu, osajoukko | sellaisenaan, ei muokata (`tools/subset/check`) |
| Plan 9 -fork | `plan9/` | modernisoitu boot (UEFI, BootInfo) ja uudet arkkitehtuurit: wasm32, pc64, myöhemmin arm64 ja vrisc | Plan 9 -yhteensopiva; 9frontin juuren muodossa |
| 9Front-2001 | koottu, ei repossa kopiona | 9front + `plan9/` + `plan9/patches` | työkalu kokoaa sen (`build/`, VM) |
| Plan2001 | `plan2001/` | muutokset, jotka rikkovat legacy-Plan 9:ää liikaa, ja Plan2001:n omat palvelut | overlay 9Front-2001:n päälle, sama juuren muoto |

Plan2001-järjestelmä on siis **9Front-2001 + `plan2001/`**.

Repon muut osat eivät ole Plan 9 -puuta vaan rakennus- ja testityökaluja:
`tools/` (VM, osajoukko, pilvi, Linux-ristikäännös), `monolith/`
(ristikäännöksen ja testien skriptit sekä 9pterm; drawtermin
selainosat, host3 ja Monolithin JS poistettiin D7:ssä), `docs/`,
`subset/` ja `build/`.

## Tavoite: `plan9/` 9frontiin

1. **Natiivi arkkitehtuurilisäys.** `plan9/` kopioidaan tai bindataan
   9front-järjestelmän juuren päälle, `plan9/patches` ajetaan, ja sen
   jälkeen 9frontin omalla mk:lla käännetään kääntäjät (3a, 3c, 3l),
   kirjastot ja ytimet (`cd /sys/src/9/wasm32 && mk`,
   `cd /sys/src/9/pc64 && mk`, `objtype=wasm32 mk install` jne.).
2. **Jos tämä ei onnistu** jonkin osan kohdalla, se tuodaan
   9Front-2001:een ja käännetään siellä. Syy kirjataan tähän
   dokumenttiin.

`plan9/`:ssa ei ole Linux-skriptejä. Linuxin ristikäännös
(`build-libc3`, `build-9wasm32`, `build-bin3` ...) lukee `plan9/`:n
lähteet mutta asuu `tools/`issa.

## Sijoitussäännöt (voimassa heti uudelle koodille)

Kysy muutoksesta järjestyksessä:

1. **Korjaus 9frontin bugiin, käytös ennallaan?** Silloin diff menee
   `plan9/patches/`iin (upstreamia varten) ja paikattu tiedosto `plan9/`:n
   saman polun alle. Esimerkki: hjfs:n `auth.c`.
2. **Boot, UEFI, BootInfo tai arkkitehtuuri (wasm32, pc64, arm64, vrisc),
   ja yhteensopiva?** Silloin `plan9/`. Tähän kuuluu wasm32-arkkitehtuurin
   selainalusta (`platform.js`, `kernel.html`, `devwsnet.c`,
   `devrootfs.c`): se on wasm32:n firmware ja laitteet, kuten pc64:n
   ajurit.
3. **Muuttaako se legacy-käytöstä tai onko se Plan2001:n palvelu?**
   Silloin `plan2001/`. Esimerkkejä: rivieditori, sovellukset, pilvi,
   webterm ja asentimen UEFI-only-polku.
4. **Työkalu tai testi, joka ei ole osa Plan 9 -puuta?** Silloin `tools/`,
   ja dokumentaatio `docs/`iin.

9frontin oma tiedosto, jota fork tarvitsee muutettuna (esimerkiksi
CPUS-lista, johon wasm32 lisätään), tehdään diffinä `plan9/patches/`iin.
Kokonaista kopiota ei tehdä, jotta 9frontin päivitys pysyy helppona
(`docs/upstream-scope-manifest.md`, `patches/9front/README`).

## Siirtotaulukko

Polku ennen siirtoa → uusi polku. ? = sijoitus voidaan vielä harkita
uudelleen (nyt taulukon mukaan).

### `plan9/` (fork)

| Ennen | Uusi | |
|---|---|---|
| `wasm32/include/`, `wasm32/mkfile` | `plan9/wasm32/` | ✓ |
| `sys/src/cmd/3a`, `3c`, `3l` | `plan9/sys/src/cmd/` | ✓ |
| `sys/src/libc/wasm32`, `sys/src/libthread/wasm32` | `plan9/sys/src/lib{c,thread}/wasm32` | ✓ |
| `sys/src/9/wasm32/` (myös `platform.js`, `test/`) | `plan9/sys/src/9/wasm32/` | ✓ |
| `monolith/web/kernel.html` | `plan9/sys/src/9/wasm32/kernel.html` | ✓ firmware |
| `sys/src/9/pc64/` (`bootarch.c`, `l.s`, `main.c`, `mem.h`, `fns.h`, `trap.c`, `mkfile`, conf `pc64` ilman `wg`ia) | `plan9/sys/src/9/pc64/` | ✓ |
| `sys/src/9/port/bootargs.c`, `bootfb.c`, `bootinfo.c` | `plan9/sys/src/9/port/` | ✓ |
| `sys/src/9/pc/memory.c`, `pcipc.c`, `screen.c`, `vga.c`, `devvga.c` | `plan9/sys/src/9/pc/` | ? `vga.c`:n saraketeinen boot-loki on käyttöliittymämuutos |
| `sys/src/boot/efi/`, `sys/include/bootinfo.h` | `plan9/sys/src/boot/efi/`, `plan9/sys/include/` | ✓ |
| `sys/src/9/arm64/` | `plan9/sys/src/9/arm64/` | ✓ kesken |
| `sys/src/cmd/hjfs/auth.c`, `patches/9front/*` | `plan9/sys/src/cmd/hjfs/`, `plan9/patches/` | ✓ 9frontin korjaus |
| `sys/src/9/pc/sdvirtio.c` (levy ensimmäisellä LUNilla, jolla on levy) | `plan9/sys/src/9/pc/` | ? korjaus; sopii myös upstreamiin |

### `plan2001/` (overlay)

| Ennen | Uusi | |
|---|---|---|
| `sys/src/cmd/aux/kbdfs/kbdfs.c` (rivieditori, historia) | `plan2001/sys/src/cmd/aux/kbdfs/` | ✓ |
| `sys/src/9/port/devcons.c` (ESC-rivit pois kmesgistä, rivieditorin takia) | `plan2001/sys/src/9/port/` | ✓ |
| `sys/src/9/ip/devwg.c`, `iproute.c`, pc64-confin `wg` | `plan2001/sys/src/9/ip/` | ? WireGuard on yhteensopiva lisäys: voi siirtyä forkiin myöhemmin |
| `sys/src/9/boot/devsd.proto`, `reimage.rc`, `sys/src/cmd/aux/reimage.c` | `plan2001/sys/src/...` | ✓ pilven uudelleenkirjoitus |
| `sys/src/cmd/webterm.c`, `crsrv.c`, `rcc.c` | `plan2001/sys/src/cmd/` | ✓ |
| `rc/bin/inst/*` (UEFI-only USB-asennin), `rc/bin/apps`, `lib/app/` | `plan2001/rc/bin/...`, `plan2001/lib/app/` | ✓ |
| (D7) rc-httpd:n sivusto: `select-handler`, `handlers/plan2001-static`, `handlers/plan2001-app` | `plan2001/rc/bin/rc-httpd/` | ✓ Plan2001:n palvelu |
| (D7) sovelluksen origin wasm32-koneella: `/boot/app`, `/boot/rconnect.app`, `aux/wsrcpu` | `plan2001/sys/src/9/wasm32/`, `plan2001/sys/src/cmd/aux/wsrcpu.c` | ✓ Plan2001:n palvelu (devwsnet:n `rcpuws`-polku on forkin) |
| `usr/glenda/` | `plan2001/usr/glenda/` | ✓ |

### Kernelin konfiguraatio

`pc64`-conf on jaettu kahtia: `plan9/sys/src/9/pc64/pc64` on forkin
(bootinfo, bootarch, bootfb, ei `wg`:tä), ja `plan2001/sys/src/9/pc64/pc64`
on sen kopio, johon on lisätty `wg`. Koska tiedosto on molemmissa,
overlayssa voittaa `plan2001/`:n. Kun forkin confiin tulee muutos, sama
muutos on tehtävä myös Plan2001:n kopioon. Näin `plan9/`:n conf kääntyy
9frontissa ilman Plan2001:n laitteita.

## Natiivi käännös

Tilanne 4.10.2026: **wasm32 kääntyy 9frontin omilla mkfileillä.**
`tools/9front-2001` kokoaa 9Front-2001:n (`build/9front-2001`):
`subset/9front`, sen päälle `plan9/patches/9front/*.diff` ja sitten
`plan9/`. Kokoaja tarkistaa, että forkin kopio jokaisesta paikatusta
tiedostosta on täsmälleen diff ajettuna julkaisuun. Siinä puussa
`objtype=wasm32 mk install` kääntää:
- kirjastot `libc` … `libcontrol` (`build-libc3`:n kirjastot, jäsenmäärät
  samat),
- ytimen: `cd /sys/src/9/wasm32 && mk install` → `/wasm32/9wasm32.wasm`
  (`plan9/sys/src/9/wasm32/mkfile`; conf-tiedostoa ja mkdevc:tä ei ole,
  koska `devtab.c` on konfiguraatio),
- juuren 9front-ohjelmat: `rc`, `hjfs`, `rio`, `aux/kbdfs`,
  `auth/factotum`, `exportfs`, `plumb`, `syscall` ja yhden tiedoston
  komennot (`cd /sys/src/cmd && mk cat.install …`).

Mitä siihen tarvittiin:
- **3c ja 3l:** 3c kirjoittaa `#pragma lib`in objektiin (ANAME, D_FILE,
  numero 0, kuten 9frontin muut kääntäjät), ja 3l lataa ne kirjastot
  (`$O` → `3`, `/$objtype/lib/…`). Siksi mkonen `$LD -o … $OFILES` riittää.
- **Diffit** (`plan9/patches/9front/README`):
  `libc/9syscall/mkfile` tekee wasm32:n systeemikutsut C:nä (`_trap`), ja
  `libthread/mkfile` ottaa wasm32:n oman koneosan (`wasm32/`).
- **Forkin uudet mkfilet:** `libc/wasm32/mkfile` sekä tyhjät
  `libmp/wasm32/mkfile` ja `libsec/wasm32/mkfile` (ei assembler-osia).
- **CPUS:** wasm32:ta ei lisätä 9frontin CPUS-listoihin. `mk installall`
  kääntää CPUS:n kaikki koneet, eivätkä kaikki 9frontin ohjelmat käänny
  wasm32:lle (APE, `vmx`, ytimet). wasm32 käännetään erikseen:
  `objtype=wasm32 mk install` niissä hakemistoissa, joita kone käyttää.

**Kokeiltu Linuxissa, ei vielä oikeassa 9frontissa.** `tools/native-wasm32`
ajaa 9frontin mkfilet Linuxissa: plan9portin mk, rc, sed, awk ja grep
(`PLAN9=…`), Linuxilla käännetyt 3c ja 3l, sekä puu bindattuna `/sys`:iin
ja `/wasm32`:een omassa mount-nimiavaruudessa (`unshare`). Isännän työkaluja
(`mpc`, `mklatin`) sillä ei ole, joten se käyttää julkaisun valmiiksi
tekemiä tiedostoja. Kokoaja ajoittaa ne lähteitään uudemmiksi, kuten ne
ovat 9front-koneella. `tools/test-native` panee natiivin ytimen ja
ohjelmat `build-bin3`:n juureen ja ajaa `test-9wasm32`:n niillä. 4.10.2026 koko
sarja meni läpi (49 PASS) natiivilla ytimellä ja 38 natiivilla
ohjelmalla. VM-testit (`dp9ikvm`, `rcpuvm`, `net`) ohitettiin.

Vielä tekemättä:
- **Ajo 9frontissa:** VM:ssä `objtype=wasm32 mk install` puussa, jossa
  `plan9/` on overlaynä (3c:n ja 3l:n käännös Plan 9:llä, `docs/wasm32.md`).
- **Juuren arkisto** (`#R`, `root.fs`) tehdään yhä Pythonilla
  `build-bin3`:ssa. Natiivisti tarvitaan Plan 9 -työkalu (esim.
  `mkfile`-kohde `/sys/src/9/wasm32`:een).
- **Plan2001:n ohjelmat** (`aux/wsrcpu`) ja testiohjelmat käännetään
  vain `build-bin3`:lla.
- **APE** (`/sys/src/ape`) tulee forkiin vasta, kun jokin ohjelma
  tarvitsee sitä.
- **pc64 ja UEFI-lataaja** kääntyvät VM:n 9frontissa overlayllä
  (`tools/build.rc`), kuten ennenkin.

## Siirron vaiheet

Vaiheet 1–4 on tehty 4.10.2026. Vaiheen 3 kokoajaa käyttävät natiivi
käännös ja sen testi, mutta VM-build ei vielä. VM:llä ajettavat osat
(`build.sh`, `mkusb`, `subset/test`, `check`) ovat tekemättä.

1. Keskeneräiset haarat (Codex) yhdistetään ensin, jotta polkujen
   muutos ei riitele niiden kanssa.
2. `git mv` taulukon mukaan. Polut korjataan: `tools/build.rc` (overlay
   ensin `plan9/` ja sitten `plan2001/`), `tools/build.sh`,
   `tools/subset/derive.py`:n `REPO`-overlayt (`/rc/` ja `/usr/` tulevat
   `plan2001/`sta), `tools/subset/mkusb*`, `monolith/tools/build-*`,
   `test-9wasm32` ja `tools/cloud/*`.
3. Tehdään 9Front-2001:n kokoaja, joka luo `build/9front-2001/`n:
   `subset/9front` + `plan9/` + `plan9/patches` ajettuna (`tools/9front-2001`,
   4.10.2026). VM-build käyttää samaa järjestystä.
4. Testit: `tools/test-9wasm32` kokonaan, VM:ssä `tools/build.sh`,
   `tools/subset/mkusb`, `tools/subset/test` ja `check`, sekä
   `test-3c`.
5. Natiivi käännös: wasm32 Linuxin harnessilla 4.10.2026 (yllä), oikea
   9front vielä tekemättä.
