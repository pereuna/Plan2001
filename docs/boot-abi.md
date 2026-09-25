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
| CPU | x86-64 long mode, 4-tasoinen paging päällä (CR4.LA57 = 0) |
| Keskeytykset | pois (IF = 0) |
| DF | 0 |
| Stack | käyttökelpoinen |
| `RDI` | **BootInfo-blobin fyysinen osoite** |
| `RSI` | 0 |
| muut rekisterit | määrittelemättömiä |

**Sivutaulut.** Niiden tarvitsee identiteettimapata vain se, mihin kernel
koskee ennen kuin se vaihtaa omiin tauluihinsa:
- kernelin image siinä kohdassa, johon se on ladattu (suoritettavana)
- kernelin boot-sivutaulut ja Mach fyysisessä osoitteessa `0x13000–0x1C000`
  (`CPU0PML4..CPU0END`), jotka kernel nollaa ensimmäisenä.

Siksi loaderin omat sivutaulut, stack tai blob eivät saa olla tällä alueella
eivätkä kernelin imagessa. Koko fyysisen muistin identiteettimappausta ei
vaadita: kernel ei lue blobia loaderin tauluilla, vaan mappaa sen itse.
Esimerkiksi tuleva kexec voi siis rakentaa minimaaliset taulut.

UEFI-loader varmistaa lisäksi, että kernelin muistialue on varattu
`EfiLoaderCode`na ja että firmwaren sivutaulut eivät ole kernelin
boot-alueella. Nämä ovat UEFI-loaderin omia ehtoja, eivät ABI:n.

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
  **ABI:ssa ei ole alueiden määrän rajaa.** Nykyinen UEFI-loader tukee
  enintään 96 KiB raakaa UEFI-muistikarttaa (noin 2457 kuvaajaa).
  Suurempi kartta ei katkea hiljaa, vaan `GetMemoryMap` epäonnistuu ja
  boot pysähtyy virheeseen.
- **Osiot** eivät saa mennä päällekkäin toistensa eivätkä headerin kanssa.

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
  (`sys/src/9/pc/bootinfo.c`) mappaa blobin kernelin omaan
  virtuaali-ikkunaan `BOOTMAPVA` (`KZERO`:n PML4-slotti, PDP-indeksi 1, jonka
  kaikki prosessorit jakavat) kahdella omalla sivutaulusivulla. Fyysinen
  osoite voi olla mikä tahansa. Validointi tehdään ylivuototurvallisesti:
  jokainen osio on headerin jälkeen ja `totalsize`n sisällä, osiot eivät mene
  päällekkäin, config on NUL-päätteinen ja `mmapentsize ≥ sizeof(BootMem)`.
  Sen jälkeen `meminit0()` varaa blobin, `bootargsinit()` jäsentää configin
  ja `bootlogtext()` toistaa lokin.
- **Kernelin sisäinen raja** (ei ABI:n osa): blob ≤ 2 MB (`BOOTMAPSIZE`,
  yhden sivutaulun kattama alue).
- **Framebuffer-merkit:** UEFI-loader piirtää kolme merkkiä (EBS alku,
  EBS valmis, hyppy), ja kernel jatkaa samaa riviä omillaan (`bootfb.c`).
  Merkit eivät ole ABI:n osa.

## Mitä poistui

`CONFADDR` (0x1200), `BOOTINFO` (0x3000), `BOOTSCRATCHEND`,
`BOOTLINE`/`BOOTARGS`, `bootrelocate()`, `writeconf()`,
`BootInfoMaxMem` (600), `BootMem mem[600]` sekä R12/R13-rekisterimerkki
(kernelin entryn ensimmäinen framebuffer-merkki, joka olisi pakottanut
jokaisen tulevan loaderin tuntemaan Plan2001:n debug-merkit). T5600:n vuoksi tehty
`AllocateAnyPages` jää, mutta nyt se on arkkitehtuurin normaali osa, ei
yhteensopivuusratkaisun puolikas.

## Jäljellä oleva kiinteä alue

Kernelin boot-sivutaulut ja Mach ovat edelleen fyysisessä osoitteessa
`0x13000–0x1C000`, ja kernel lataa itsensä linkitysosoitteeseensa. Nämä eivät
ole handoffin osa (RDI hoitaa sen), mutta loaderin on vältettävä niitä.
UEFI-loader varaa blobin 16 MB:n yläpuolelta (`bloballoc()`). Boot-taulujen
siirto kernelin omaan bss:ään poistaisi viimeisen kiinteän matalan alueen.

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
