# Plan2001 Boot ABI v1: ARM64-entry

Tämä on ARM64:n osuus Plan2001 Boot ABI v1:stä. Data-ABI (BootInfo-blob) on
yhteinen kaikille ISA:ille: `docs/boot-abi.md`. Tila: **QEMU virt + AAVMF
(EDK2): `bootaa64.efi` → `9qemu` → `bootargs`-kehote, PASS** (vaihe 4, haara
`phase4-arm64`). Oikealla ARM64-raudalla ei vielä testattu.

## Testaus

```
ALLOW_PLANNED=1 TARGET=arm64 tools/vm-target      # kerran: ARM64-käyttäjätila base-imageen
ALLOW_PLANNED=1 TARGET=arm64 tools/build.sh
ALLOW_PLANNED=1 TARGET=arm64 SECONDS_TO_RUN=300 tools/test-qemu.sh
```

QEMU (`tools/targets/arm64`): `virt,acpi=off,gic-version=3,highmem-ecam=off`,
`cortex-a72` ja virtio-blk ilman legacy-tilaa. Nämä vastaavat upstream-kernelin
oletuksia: FDT, GICv3:n järjestelmärekisterit, PCI-konfiguraatio osoitteessa
`0x3F000000` ja pelkkä `sdvirtio10`. Emulointi on TCG:tä, ja boot kestää
noin minuutin.

## Entry

Loader hyppää kernelin entryyn (`_start`, `sys/src/9/arm64/l.s`) seuraavassa tilassa:

| | |
|---|---|
| Poikkeustaso | EL1 tai EL2 (kernel laskeutuu itse EL1:een) |
| MMU ja välimuistit | pois |
| Keskeytykset | peitetty (DAIF) |
| `X0` | **BootInfo-blobin fyysinen osoite** |
| `X1` | 0 |
| muut rekisterit | määrittelemättömiä |

Koska MMU ja välimuistit ovat pois, kernel näkee muistin sellaisena kuin se
on RAMissa. Kernelin `_start` kirjoittaa välimuistit takaisin ja mitätöi ne
ennen omien sivutaulujensa rakentamista (`cachedwbinv`, `l2cacheuwbinv`,
`cacheiinv`), kuten upstreamissa. Oikealla raudalla loaderin kannattaa
lisäksi siivota kernelin kuvan ja blobin datavälimuisti PoC:hen ennen
MMU:n sammuttamista. Tämä on avoin kohta ennen raudalla testaamista.

## Muistin sijoittelu (QEMU virt)

- RAM alkaa osoitteesta `0x40000000`. Kernel on linkitetty osoitteeseen
  `KZERO + pa` ja ladataan fyysiseen osoitteeseen `0x40100000`
  (`archentry()`: entry − KZERO). Sen boot-sivutaulut ja Machit ovat sen
  alapuolella.
- UEFI-loader (`sys/src/boot/efi/archaa64.c`) varaa blobin RAMin ensimmäisten
  16 MB:n ulkopuolelta ja kernelin KZERO-ikkunan (`KLIMIT`,
  `0x140000000`) sisältä (`archblobok()`).
- `7l` aloittaa kernelin datan tekstin jälkeen seuraavalta **64 KB:n** rajalta
  (sen oletus `-R`), ei seuraavalta sivulta kuten `6l`. Loader kysyy tämän
  `archdataround()`ista. Ilman sitä data oli `0xE000` tavua väärässä kohdassa,
  ja kernel kaatui jo `bootearlymap()`issa.

## Kernelin puoli

- `_start` tallentaa `X0`:n muuttujaan `bootinfopa` bss:n nollauksen
  jälkeen (MMU pois, joten tallennus on fyysinen).
- `bootearlymap()` (`sys/src/9/arm64/bootarch.c`) mappaa blobin samaan
  virtuaaliosoitteeseen ja samoilla attribuuteilla, joihin `kmapram()` mappaa
  sen RAMin myöhemmin (`KZERO+pa` tai `KMAP`-ikkuna). Sivutaulusivuja on kaksi
  omaa. `l1map()` (`mmu.c`) hyväksyy identtisen olemassa olevan merkinnän.
- `meminit()` (`mem.c`) ottaa RAMin BootInfon muistikartasta: RAM-luokan
  alueet kernelin yläpuolelta `KLIMIT`iin asti, blob leikattuna pois,
  enintään 8 pankkia. Upstreamin `*maxmem`-oletus ei kelpaa UEFI:n alla,
  koska RAMissa on firmwaren ajonaikaisia alueita.
- Laitteiston kuvaus on FDT (`acpi=off`), ja se kopioidaan blobiin.
  `port/bootargs.c` ottaa siitä `*ncpu`:n ja `/chosen`-bootargsit.
- Poistui upstreamista: `DTBADDR` (0x40000000), `CONFADDR`/`BOOTARGS`
  (0x40010000) ja `writeconf()`. `reboot()` pysähtyy `panic`iin, kunnes
  kexec rakentaa uuden blobin.

## Löydökset ja avoimet asiat

- **Loader siirtyy muistissa:** ARM64:llä firmware voi ladata loaderin muualle
  kuin sen linkitysosoitteeseen. Silloin bss-muuttujan osoite arvona jää
  linkitysajan osoitteeksi, alustetun datan osoite ei. Siksi firmwarelle ei
  anneta bss-muuttujan osoitetta (`bootmapinit()`, ks. `rebase()` `efi.c`:ssä).
- **EDK2:n DTB** on pehmustettu 1 MB + 4 KB:n kokoiseksi, joten `FdtMax` on 2 MB.
- **Avoin:** PCI:n ECAM-osoite, GIC ja UART ovat upstreamissa kiinteitä QEMU
  virt -osoitteita. Plan2001:n suunta on lukea ne FDT:stä, joka on nyt
  blobissa.
- **Avoin:** datavälimuistin siivous PoC:hen loaderissa ennen hyppyä oikeaa
  rautaa varten.
- **Avoin:** asennusmedia ja osajoukko ARM64:lle (`trace` ja `test` vaativat
  ARM64-VM:n).
