# Plan 9 -fork, 9Front-2001 ja Plan2001 (päätös 4.10.2026)

Plan2001:n kehitys eriytetään yleisestä Plan 9 -forkista. Tämä dokumentti
kirjaa päätöksen, kerrokset, sijoitussäännöt ja siirtosuunnitelman.
Siirto tehdään omana työnään, kun keskeneräiset haarat (Codex) on
yhdistetty; siihen asti tiedostot ovat nykyisillä paikoillaan, ja alla
oleva taulukko kertoo jokaisen kohteen.

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
(selainkehitys; drawterm ja host3 poistuvat D7:ssä, ristikäännöksen
skriptit siirtyvät `tools/`iin), `docs/`, `subset/` ja `build/`.

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

Nykyinen polku → uusi polku. Merkinnät: ✓ selvä, ? tarkistetaan
siirrossa.

### `plan9/` (fork)

| Nyt | Uusi | |
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

| Nyt | Uusi | |
|---|---|---|
| `sys/src/cmd/aux/kbdfs/kbdfs.c` (rivieditori, historia) | `plan2001/sys/src/cmd/aux/kbdfs/` | ✓ |
| `sys/src/9/port/devcons.c` (ESC-rivit pois kmesgistä, rivieditorin takia) | `plan2001/sys/src/9/port/` | ✓ |
| `sys/src/9/ip/devwg.c`, `iproute.c`, pc64-confin `wg` | `plan2001/sys/src/9/ip/` | ? WireGuard on yhteensopiva lisäys: voi siirtyä forkiin myöhemmin |
| `sys/src/9/boot/devsd.proto`, `reimage.rc`, `sys/src/cmd/aux/reimage.c` | `plan2001/sys/src/...` | ✓ pilven uudelleenkirjoitus |
| `sys/src/cmd/webterm.c`, `crsrv.c`, `rcc.c` | `plan2001/sys/src/cmd/` | ✓ |
| `rc/bin/inst/*` (UEFI-only USB-asennin), `rc/bin/apps`, `lib/app/` | `plan2001/rc/bin/...`, `plan2001/lib/app/` | ✓ |
| `usr/glenda/` | `plan2001/usr/glenda/` | ✓ |

### Kernelin konfiguraatio

`pc64`-conf jaetaan kahtia. `plan9/sys/src/9/pc64/pc64` sisältää forkin
laitteet (bootinfo, bootarch, bootfb ilman wg:tä), ja `plan2001/` lisää
omansa, esimerkiksi omana conf-tiedostonaan, joka on pc64 ja `wg`.
Mekanismi päätetään siirrossa. Tavoite on, että `plan9/`:n conf kääntyy
9frontissa ilman Plan2001:n laitteita.

## Mitä natiivi käännös vielä vaatii

Tilanne 4.10.2026:
- **pc64 ja UEFI-lataaja** kääntyvät jo VM:n 9frontissa overlaylla
  (`tools/build.rc`). Siirrossa overlay-järjestys on `plan9/`, sitten
  `plan2001/`.
- **3a, 3c ja 3l:** mkfilet ovat olemassa, mutta niitä ei ole kokeiltu
  Plan 9:ssä, eikä 3c:tä ole käännetty Plan 9:ssä (`docs/wasm32.md`).
- **wasm32-ydin** käännetään nyt bash-skriptillä Linuxissa
  (`monolith/tools/build-9wasm32`). Natiivi käännös vaatii
  `plan9/sys/src/9/wasm32/mkfile`n ja confin.
- **libc/wasm32 ja libthread/wasm32** käännetään nyt `build-libc3`:lla.
  Ne on liitettävä 9frontin mkfileihin (`libc/mkfile`:n `CPUS`,
  `libthread`in arkkitehtuuritiedostot).
- **9frontin arkkitehtuurirekisteröinti** tehdään diffeinä
  `plan9/patches/`iin: `/sys/src/mkfile`:n ja muiden CPUS-listat
  (wasm32, objtype-kirjain `3`).
- **Juuren arkisto** (`#R`, `root.fs`) tehdään nyt Pythonilla
  `build-bin3`:ssa. Natiivisti tarvitaan Plan 9 -työkalu.
- **APE** (`/sys/src/ape`) tulee forkiin vasta, kun jokin ohjelma
  tarvitsee sitä.

## Siirron vaiheet

1. Keskeneräiset haarat (Codex) yhdistetään ensin, jotta polkujen
   muutos ei riitele niiden kanssa.
2. `git mv` taulukon mukaan. Polut korjataan: `tools/build.rc` (overlay
   ensin `plan9/` ja sitten `plan2001/`), `tools/build.sh`,
   `tools/subset/derive.py`:n `REPO`-overlayt (`/rc/` ja `/usr/` tulevat
   `plan2001/`sta), `tools/subset/mkusb*`, `monolith/tools/build-*`,
   `test-9wasm32` ja `tools/cloud/*`.
3. Tehdään 9Front-2001:n kokoaja, joka luo `build/9front-2001/`n:
   `subset/9front` + `plan9/` + `plan9/patches` ajettuna. VM-build
   käyttää samaa järjestystä.
4. Testit: `tools/test-9wasm32` kokonaan, VM:ssä `tools/build.sh`,
   `tools/subset/mkusb`, `tools/subset/test` ja `check`, sekä
   `test-3c`.
5. Natiivi käännös (yllä oleva lista) tehdään omana vaiheenaan.
