# Plan2001 Boot ABI v1: AMD64-entry

Tämä on AMD64:n osuus Plan2001 Boot ABI v1:stä. Data-ABI (BootInfo-blob) on
yhteinen kaikille ISA:ille: `docs/boot-abi.md`.

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

UEFI-loader (`sys/src/boot/efi/archx64.c`) varmistaa lisäksi, että kernelin
muistialue on varattu `EfiLoaderCode`na ja että firmwaren sivutaulut eivät
ole kernelin boot-alueella (`archcheck()`). Nämä ovat UEFI-loaderin omia
ehtoja, eivät ABI:n.

## Kernelin puoli

- `_efi64` tallentaa `RDI`:n muuttujaan `bootinfopa` ensimmäisenä.
- Kernelin omat varhaiset sivutaulut kattavat vain kernelin itsensä, eikä
  `rampage()`ia voi vielä käyttää, koska se tarvitsee muistikartan, joka on
  blobissa. Siksi `bootearlymap()` (`sys/src/9/pc64/bootarch.c`) mappaa
  blobin kernelin omaan virtuaali-ikkunaan `BOOTMAPVA` (`KZERO`:n
  PML4-slotti, PDP-indeksi 1, jonka kaikki prosessorit jakavat) kahdella
  omalla sivutaulusivulla. Fyysinen osoite voi olla mikä tahansa.
- `fbmap()` (sama tiedosto) mappaa framebufferin `vmap()`illa ja asettaa
  sille PAT-attribuutiksi write-combining.
- Validoinnin ja muun käytön hoitaa yhteinen `sys/src/9/port/bootinfo.c`.
- **Kernelin sisäinen raja** (ei ABI:n osa): blob ≤ 2 MB (`BOOTMAPSIZE`,
  yhden sivutaulun kattama alue).
- **Framebuffer-merkit:** UEFI-loader piirtää kolme merkkiä (EBS alku,
  EBS valmis, hyppy), ja kernel jatkaa samaa riviä omillaan
  (`port/bootfb.c`). Merkit eivät ole ABI:n osa.

## Jäljellä oleva kiinteä alue

Kernelin boot-sivutaulut ja Mach ovat edelleen fyysisessä osoitteessa
`0x13000–0x1C000`, ja kernel lataa itsensä linkitysosoitteeseensa. Nämä eivät
ole handoffin osa (RDI hoitaa sen), mutta loaderin on vältettävä niitä.
UEFI-loader varaa blobin 16 MB:n yläpuolelta (`archblobok()`). Boot-taulujen
siirto kernelin omaan bss:ään poistaisi viimeisen kiinteän matalan alueen.

## Testattu

- QEMU/OVMF, 2, 8 ja 16 GB: blob osoitteessa `0x7e379000`, PASS.
- Kokeellinen build, jossa blob pakotettiin osoitteeseen `0x200000000`
  (8 GB, 16 GB:n kone): PASS. Kernel mappaa ja lukee blobin 4 GB:n
  yläpuolelta.
- Kokeellinen build, jossa blobia tarjottiin kernelin alueelta (4 MB): se
  hylättiin, ja boot jatkui normaalisti.
- USB-asennus (`tools/subset/test`): asennettu järjestelmä bootaa uudella ABI:lla.
- Yli 600 alueen muistikartta on mahdollinen rakenteen puolesta
  (kapasiteetti 2457 aluetta), mutta sitä ei ole ajettu, koska QEMUn kartta
  on pieni.
- Oikealla raudalla (T5600 ja kannettavat) ei vielä testattu.
