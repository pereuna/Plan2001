# Plan2001 Boot ABI v1 (x86-64)

Sopimus siitä, missä tilassa loader luovuttaa koneen kernelille ja miten
loaderin keräämä tieto välitetään. Koneluettava puoli on
`sys/include/bootinfo.h`, jota molemmat osapuolet käyttävät. Voimassa
25.9.2026 alkaen.

Periaate: **loader kertoo osoitteen, kernel ei arvaa.** Sopimuksessa ei ole
yhtään kiinteää fyysistä osoitetta. UEFI-loader (`sys/src/boot/efi`) on
vain yksi ohjelma, joka tuottaa BootInfo v1:n ja käynnistää kernelin.
Tuleva kexec, VM-loader tai verkkoboot voi tehdä saman.

## Entry

Loader hyppää kernelin entryyn (`_efi64`, `sys/src/9/pc64/l.s`) seuraavassa tilassa:

| | |
|---|---|
| CPU | x86-64 long mode, paging päällä sivutauluilla, jotka identiteettimappaavat koko fyysisen muistin (UEFI:n omat), CR4.LA57 = 0 |
| Keskeytykset | pois (IF = 0), nollamittainen IDT |
| DF | 0 |
| Stack | käyttökelpoinen (loaderin) |
| `RDI` | **BootInfo-blobin fyysinen osoite** |
| `RSI` | 0 |
| `R12`, `R13` | valinnainen diagnostiikka: `R12` = neljännen framebuffer-merkin osoite tai 0, `R13` = pitch tavuina |
| muut | määrittelemättömiä |

Loader on varmistanut myös, että kernelin muistialue on varattu
`EfiLoaderCode`na ja että sen omat sivutaulut eivät ole kernelin
boot-alueella.

## BootInfo-blob

Yksi yhtenäinen, sivutasattu fyysinen alue:

```
RDI ──> +----------------------+  offset 0
        | BootInfo header      |  headersize
        +----------------------+  configoff
        | config (plan9.ini)   |  configlen, NUL-päätteinen
        +----------------------+  logoff
        | loader log           |  loglen
        +----------------------+  mmapoff
        | memory map           |  mmapcount × mmapentsize (BootMem)
        +----------------------+  totalsize
```

- **Offsetit** (`…off`) lasketaan blobin alusta, joten blob on relokoitava
  yhtenä kokonaisuutena. **Blobin ulkopuolelle** osoittavat arvot
  (`acpi`, `fbbase`) ovat fyysisiä osoitteita.
- **Header:** `magic` = `"P2BI"`, `version` = 1, `headersize`, `totalsize`,
  `flags` sekä osioiden kuvaajat ja kiinteät kentät: ACPI RSDP,
  framebuffer, TSC-taajuus, UTC-aika (`epoch`) ja RNG-siemen.
- **Muistikartta:** UEFI:n kartta juuri ennen `ExitBootServices`ia.
  Vierekkäiset saman tyypin ja attribuuttien alueet on yhdistetty.
  Kiinteää rajaa ei ole: loader mitoittaa osion firmwaren
  karttapuskurin mukaan.

## Omistajuus

Blob on kernelin entrystä alkaen. Se on `EfiLoaderData`-muistissa, jonka
kartta sanoo vapaaksi, joten kernelin on varattava `[RDI, RDI+totalsize)`
ennen kuin se jakaa vapaata muistia. Muuten kernel söisi oman
syötteensä. Blob on voimassa, kunnes kernel itse vapauttaa sen. Plan2001:n
kernel pitää sen koko elinaikansa, koska `confval[]` osoittaa
config-tekstiin ja konsoli toistaa loaderin lokin. Blob on noin 72 KB.

## Yhteensopivuus

Kernel hyväksyy blobin, jonka `magic` ja `version` ovat sen omat ja
`headersize` on vähintään sen oma `sizeof(BootInfo)`. Tuntemattomat
loppukentät ohitetaan. Muutos, joka rikkoo tämän, vaatii uuden
`BootInfoVersion`in.

## Toteutus Plan2001:ssä

- **Loader** (`sys/src/boot/efi/efi.c`): varaa blobin yhdellä
  `AllocateAnyPages`-kutsulla ennen ensimmäistä tulostusta. Loki kaapataan
  suoraan blobiin, plan9.ini luetaan blobin config-osioon, ja
  muistikartta kirjoitetaan `ExitBootServices`in jälkeen blobin
  mmap-osioon. `jump64(entry, bootinfo, …)` asettaa `RDI`:n. Mitään ei
  kopioida.
- **Kernel:** `_efi64` tallentaa `RDI`:n muuttujaan `bootinfopa`
  ensimmäisenä. Kernelin omat varhaiset sivutaulut kattavat vain kernelin
  itsensä, eikä `rampage()`ia voi vielä käyttää, koska se tarvitsee
  muistikartan, joka on blobissa. Siksi `bootinfoinit()`
  (`sys/src/9/pc/bootinfo.c`) mappaa blobin osoitteeseen `VMAP+pa`
  kuudella omalla sivutaulusivulla ja validoi jokaisen osion.
  `VMAP+pa` on sama osoite, jonka `vmap()` antaisi. Sen jälkeen
  `meminit0()` varaa blobin, `bootargsinit()` jäsentää configin
  ja `bootlogtext()` toistaa lokin.
- **Kernelin sisäinen raja** (ei ABI:n osa): blob ≤ 2 MB (`BootMapMax`).

## Mitä poistui

`CONFADDR` (0x1200), `BOOTINFO` (0x3000), `BOOTSCRATCHEND`,
`BOOTLINE`/`BOOTARGS`, `bootrelocate()`, `writeconf()`,
`BootInfoMaxMem` (600) ja `BootMem mem[600]`. T5600:n vuoksi tehty
`AllocateAnyPages` jää, mutta nyt se on arkkitehtuurin normaali osa, ei
yhteensopivuusratkaisun puolikas.

## Testattu

- QEMU/OVMF, 2, 8 ja 16 GB: blob osoitteessa `0x7e379000`, PASS.
- Kokeellinen build, jossa blob pakotettiin osoitteeseen `0x200000000`
  (8 GB, 16 GB:n kone): PASS. Kernel mappaa ja lukee blobin 4 GB:n
  yläpuolelta.
- USB-asennus (`tools/subset/test`): asennettu järjestelmä bootaa uudella ABI:lla.
- Yli 600 alueen muistikartta on mahdollinen rakenteen puolesta
  (kapasiteetti 2457 aluetta), mutta sitä ei ole ajettu, koska QEMUn kartta
  on pieni.
- Oikealla raudalla (T5600 ja kannettavat) ei vielä testattu.
