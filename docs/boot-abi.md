# Plan2001 Boot ABI v1

Sopimus siitä, miten loader luovuttaa koneen kernelille. Sopimus on kaksiosainen:

1. **Data-ABI** (tämä dokumentti): BootInfo-blob, **sama kaikille ISA:ille**.
   Koneluettava puoli on `sys/include/bootinfo.h`, jota loader ja kernel
   käyttävät.
2. **Entry-ABI**: CPU:n tila ja rekisterit hypyn hetkellä, **ISA-kohtainen**:

| ISA | Entry | Dokumentti | Tila |
|---|---|---|---|
| AMD64 | `RDI` = BootInfo PA | `docs/boot-abi-amd64.md` | käytössä |
| ARM64 | `X0` = BootInfo PA | `docs/boot-abi-arm64.md` | suunniteltu (vaihe 4) |
| RV64 | `a0` = BootInfo PA, `a1` = boot-hartin id | `docs/boot-abi-riscv64.md` | tulevaisuus |

Periaate: **loader kertoo osoitteen, kernel ei arvaa.** Kummassakaan osassa
ei ole yhtään kiinteää fyysistä osoitetta. UEFI-loader (`sys/src/boot/efi`) on
vain yksi ohjelma, joka tuottaa BootInfo v1:n ja käynnistää kernelin.
Tuleva kexec, VM-loader tai verkkoboot voi tehdä saman.

Rajaperiaate: **BootInfo on koneesta riippumaton protokolla. Kernelin entry,
MMU, trap, keskeytykset, SMP ja cache ovat ISA-portin asioita.** Loader
välittää firmwaren raakakuvauksen (ACPI, myöhemmin FDT) eikä normalisoi
keskeytysohjaimia, ajastimia tai prosessorien käynnistystä.

## BootInfo-blob

Yksi yhtenäinen, sivutasattu fyysinen alue, jonka osoite annetaan entryssä:

```
entry ──> +----------------------+  offset 0
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
  framebuffer (GOP), UTC-aika (`epoch`) ja RNG-siemen.
- **`tscfreq`** on AMD64:n kenttä (TSC-taajuus, ristiintarkistukseksi). Muilla
  ISA:illa loader kirjoittaa siihen 0, eikä kernel saa nojata siihen. CPU:n
  ajastimet (TSC, ARM generic timer, RISC-V timebase) kuuluvat kernelin
  ISA-portille. Kentän poistaminen headerin keskeltä vaatisi v2:n, joten se
  jää paikalleen.
- **Muistikartta:** UEFI:n kartta juuri ennen `ExitBootServices`ia.
  Vierekkäiset saman tyypin ja attribuuttien alueet on yhdistetty. Tyypit ovat
  UEFI:n omat (`BootMem*`), jotka ovat jo ISA-riippumattomia.
  **ABI:ssa ei ole alueiden määrän rajaa.** Nykyinen UEFI-loader tukee
  enintään 96 KiB raakaa UEFI-muistikarttaa (noin 2457 kuvaajaa).
  Suurempi kartta ei katkea hiljaa, vaan `GetMemoryMap` epäonnistuu ja
  boot pysähtyy virheeseen.
- **Osiot** eivät saa mennä päällekkäin toistensa eivätkä headerin kanssa.

## Omistajuus

Blob on kernelin entrystä alkaen. Se on `EfiLoaderData`-muistissa, jonka
kartta sanoo vapaaksi, joten kernelin on varattava `[blob, blob+totalsize)`
ennen kuin se jakaa vapaata muistia. Muuten kernel söisi oman
syötteensä. Blob on voimassa, kunnes kernel itse vapauttaa sen. Plan2001:n
kernel pitää sen koko elinaikansa, koska `confval[]` osoittaa
config-tekstiin ja konsoli toistaa loaderin lokin. Blob on noin 72 KB.

## Yhteensopivuus

Kernel hyväksyy blobin, jonka `magic` ja `version` ovat sen omat ja
`headersize` on vähintään sen oma `sizeof(BootInfo)`. Tuntemattomat
loppukentät ohitetaan, joten **uudet kentät lisätään headerin loppuun
ilman versionnostoa** (seuraavaksi `fdtoff`/`fdtlen` ja `arch`, vaihe 2).
Muutos, joka rikkoo tämän, vaatii uuden `BootInfoVersion`in. Tällainen olisi
esimerkiksi kentän poisto keskeltä tai `BootMem.type`-kentän merkityksen muutos.

## Toteutus: mikä on yhteistä ja mikä ISA:n

| | Yhteinen | ISA:n (AMD64) |
|---|---|---|
| Kernel | `sys/src/9/port/bootinfo.c`: headerin ja osioiden validointi, `bootmem()`, `bootconfig()`, `bootmemclass()` (UEFI-tyyppi → RAM/ACPI/varattu), RNG-siemen, epoch. `port/bootargs.c`: plan9.ini, `*acpi`, `*bootscreen`. `port/bootfb.c`: merkit ja lokin toisto. | `pc64/bootarch.c`: `bootearlymap(pa, size)` (blobin mappaus ennen muistinhallintaa) ja `fbmap(pa, size)` (framebufferin cache-tapa). Entry: `pc64/l.s`. Muistin tyypit ja PC:n muistikartta: `pc/memory.c`. |
| Loader | `efi.c`, `sub.c`: boot-taltio, plan9.ini, kernelin a.out, blob, muistikartta, `ExitBootServices`. | `archx64.c`: `archconf()` (TSC), `archentry()`, `archblobok()`, `archcheck()`, `archjump()`. Asm: `x64.s`. |

Uusi ISA toteuttaa kernelissä `bootearlymap()`- ja `fbmap()`-hookit sekä
entryn, joka tallentaa blobin osoitteen muuttujaan `bootinfopa`. Loaderissa
se toteuttaa viisi `arch*()`-funktiota ja hypyn.

## Mitä poistui

`CONFADDR` (0x1200), `BOOTINFO` (0x3000), `BOOTSCRATCHEND`,
`BOOTLINE`/`BOOTARGS`, `bootrelocate()`, `writeconf()`,
`BootInfoMaxMem` (600), `BootMem mem[600]` sekä R12/R13-rekisterimerkki
(kernelin entryn ensimmäinen framebuffer-merkki, joka olisi pakottanut
jokaisen tulevan loaderin tuntemaan Plan2001:n debug-merkit). T5600:n vuoksi tehty
`AllocateAnyPages` jää, mutta nyt se on arkkitehtuurin normaali osa, ei
yhteensopivuusratkaisun puolikas.
