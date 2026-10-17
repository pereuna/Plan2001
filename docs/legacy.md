# Legacy pois: ytimen lista (vaihe 4, 7.10.2026)

> **16.10.2026:** Plan2001 on kernel-repo (`docs/repos.md`): UEFI-kernel ja
> -loader AMD64:lle, ARM64:lle ja RISC-V64:lle. Tähän jäivät ytimen ja
> bootin kohdat (1–3). Userlandin kohdat (tunnistus, verkkopalvelut,
> tiedostojärjestelmät ja ohjelmat) ovat Monolithissa
> (`docs/legacy-userland.md`), kuten 9frontin julkaisukopio (`subset/`)
> ja wasm32-kone, joihin alla olevat polut viittaavat.

Tähän on listattu se, mitä Plan2001:n kernel ei tarvitse. Kernel ajaa
CPU-, auth- ja tiedostopalvelinta: pilven VM:ää (virtio) ja omaa rautaa.
CPU-palvelin tarkoittaa myös CR-palvelimia (NPU, GPU jne.). Palvelin ei
ole vain räkissä oleva PC: se voi olla myös pieni, RPi:n kaltainen
kiinteistöautomaatiolaite. Jokaisella rivillä on suositus:

- **pois:** poistetaan.
- **jää:** kernel tarvitsee sen.
- **päätä:** käyttäjän päätös, perustelu vieressä.

## Päätökset (7.10.2026)

**WLAN.** Palvelin voi olla WLANissa (esimerkiksi
kiinteistöautomaatiolaite), joten WLAN jää: pc64:llä `etheriwl`, `wifi`
ja `aux/wpa`. Tavoite on lisätä WPA3. Nyt `aux/wpa` osaa vain
WPA/WPA2:n.

**Paikallinen näyttö (vaatimus).** pc64:n CPU-palvelimella on
toimiva paikallinen näyttö (HDMI tai muu näyttö, jolle firmware piirtää)
ja näppäimistö bootia, asennusta ja huoltoa varten, vaikka ajossa oleva
palvelin ei niitä tarvitse.

- Toteutus: UEFI:n GOP-kehyspuskuri. Loader ottaa firmwaren valitseman
  tilan eikä kutsu `SetMode`a (`docs/status.md`), ja ydin piirtää siihen
  `bootfb`:llä ja `vgasoft`illa. Konsoli on `vga` ja `kbd`, ja
  rivieditori on kbdfs.
- VESA (`vgavesa`, `aux/realemu`) oli 9frontissa yleisin toimiva
  näyttöajuri, mutta se kutsuu näytönohjaimen BIOSia, jota UEFI-bootissa
  ei ole. GOP toimii samoin kaikilla korteilla.
- Rajoitus: resoluutio on se, jonka firmware valitsee (sama, jossa sen
  asetusruutu näkyy). Ajon aikana sitä ei vaihdeta.
- Testi: ydin QEMU:ssa `-vga std`:llä ja näytön kuva QEMU:n
  `screendump`illa: konsoli ja rivieditori näkyvät. Testi ei vielä ole
  automaattinen (`tools/test-qemu.sh` tarkistaa vain sarjalokin).

## 1. Arkkitehtuurit

| Mikä | Missä | Suositus |
|---|---|---|
| 386 (32-bit PC), `sys/src/9/pc` 32-bittisenä ytimenä | 9frontin `386`, pc:n `l.s`, `apbootstrap.s`, `rebootcode.s` | **pois** 32-bittisenä alustana. Huom.: pc64 käyttää `../pc`:n C-tiedostoja, joten hakemisto jää, mutta 386-ydin, sen mkfile ja `/386` poistuvat |
| 68020, power (ja 9frontin muut: arm, mips, spim, sparc) | 9frontin `68020`, `power` (vain mkfilet) | **pois** |
| amd64 | pc64 | **jää** |
| arm64 | `sys/src/9/arm64` (boot-työ, QEMU) | **jää** (päätös 7.10. ja 16.10.) |
| riscv64 | `tools/targets/riscv64` (vain kohde) | **jää** (päätös 7.10. ja 16.10.) |
| wasm32 | Monolith | ei Plan2001:n kernel. `BootArchWasm32` jää Boot ABI:in (`docs/repos.md`) |

## 2. Boot ja laiteohjelmisto

| Mikä | Missä | Suositus |
|---|---|---|
| BIOS-boot (pbs, mbr, 9bootfat, PXE, ISO-boot) | poistettu jo (vaihe A ja loaderin siivous) | tehty |
| `plan9.ini`-teksti boot-konfiguraationa | `port/bootargs.c` lukee BootInfon config-osiota | **jää** toistaiseksi: BootInfo kuljettaa sen. Myöhemmin rakenteiseksi |
| `bootfs.paq` ja paqfs bootin juurena | pc64-conf `bootdir` | **päätä:** Monolithin wasm32-kone käyttää omaa `#R`-arkistoaan. Yhteinen boot-juuren muoto on mahdollinen myöhemmin |
| `apm.c`, `apmjump.s` (APM-virranhallinta), `aux/apm` | `pc/`, Monolithin `subset` | **pois** (ACPI) |
| `bios32.c`, `pcibios.c` | `pc/` (pcibios jo pois confista) | **pois** tiedostoina |
| `vgavesa` ja `aux/realemu` (VESA BIOS -kutsut reaalitilaemulaatiolla) | pc64-conf `vgavesa`, Monolithin `subset/aux/realemu` | **pois:** UEFI GOP (`bootfb`, `vgasoft`) korvaa ne: VESA tarvitsee BIOSin, jota UEFI-bootissa ei ole. Näyttövaatimus: Päätökset yllä. `vgavesa` pois confista 7.10., `aux/realemu` userlandin mukana |
| `aux/vga` (näyttötilan vaihto) | Monolithin `subset/aux/vga` | **pois** palvelimelta. GOP antaa tilan, ja `vgaigfx`/`vgaradeon` jäävät vain, jos rauta tarvitsee |
| i8259 (PIC), i8253 (PIT) | `archgeneric` | **päätä:** APIC ja HPET riittävät nykyraudalla, mutta ydin kalibroi kellonsa PIT:llä tai HPET:llä (`docs/status.md`). Poisto vaatii testin raudalla |

## 3. Ytimen laitteet ja ajurit (pc64)

Pois-rivit ovat poissa confista 7.10.2026 (järjestyksen kohta 1). Niiden
tiedostot `pc/`:ssa ja `port/`:ssa ovat kohdan 2 työ.

| Mikä | Suositus |
|---|---|
| `floppy`, `lpt`, `pccard`, `i82365`, `pcmciamodem` (jo kommentoitu), niiden tiedostot `pc/`:ssa | **pois** tiedostoina |
| ISA- ja vanhat PCI-verkkokortit: `ether2114x`, `ether79c970`, `ether8139`, `etheryuk`, `etherbcm`, kommentoidut (`ether8390`-perhe, `elnk3`, `82557`, `83815`, `ga620`, `vgbe`, `vt610x`, `smc`, `wavelan`) | **pois.** Jäävät `ethervirtio`/`virtio10` (pilvi), `etherigbe`, `ether82563`, `ether8169`, `etheri225`, `ether82598`, `etherx550` (rauta) |
| WLAN: `etheriwl`, `wifi`, `aux/wpa`, factotumin `wpapsk`, bootin `net.rc`:n `wifi` | **jää** (päätös 7.10.): palvelin voi olla myös kiinteistöautomaatiolaite WLANissa. Tavoite: WPA3 (nyt `aux/wpa` osaa WPA/WPA2:n). `etheriwl` tarvitsee Intelin firmwaren (`/lib/firmware/iwn-*`, `iwm-*`, `boot/etheriwl.proto`), jota build-VM:ssä ei ole: se on vielä tuotava |
| WLAN: `etherwpi` (Intel 3945, 2006), `etherrt2860` (Ralink RT28xx, 2007) | **pois** (vanhat piirit) |
| `sdide` (PATA), `sd53c8xx` (SCSI), `sdmylex`, `sdodin`, `sdmv50xx` | **pois.** Jäävät `sdiahci`, `sdnvme`, `sdvirtio`, `sdmmc`, `sdram`, `sdloop` |
| `aoe`, `sdaoe` (ATA over Ethernet) | **pois** |
| `audio`, `audiohda`, `audiosb16`, `audioac97` | **pois** palvelimelta (terminaalin ääni on selaimen) |
| `vga`, `draw`, `mouse`, `kbd` pc64:llä | **jää** toistaiseksi (päätös 7.10.): palvelimen paikallinen konsoli bootia, asennusta ja huoltoa varten (vaatimus: Päätökset yllä; GOP-kehyspuskuri, rivieditori, `bootfb`) tarvitsee `vga`:n ja `kbd`:n tekstikonsolina. `draw` ja `mouse` lähtevät vasta kohdassa 2, koska `pc/devvga.c` ja `screen.c` käyttävät `devdraw.c`:n `drawlock`ia ja hiiren `swcursor`ia. Näyttöajureista jää vain `vgasoft`: `vgaigfx`, `vgaradeon` ja `vgavesa` pois confista 7.10. |
| `usb` ja HCI:t | **jää** (näppäimistö, levyt) |
| `vmx` (virtualisointi), `dtracy` | **pois** (päätös 7.10.; confista ja pc64:n `main.c`:stä pois 7.10.) |
| `kprof`, `segment` | **jää** (päätös 7.10.: pieniä; profilointi ja jaettu muisti) |
| `cputemp`, `pmmc`, `sdmmc` | **jää** (päätös 7.10.) |
| `bridge`, `gre`, `ipmux`, `igmp`, `rudp`, `netdevmedium` | **pois** (`wg` korvaa tunnelit). `ip/inferno.c` jää: se ei ole medium vaan devip:n apufunktiot (`commonuser`, `bootpread`) |
| `il` (Plan 9:n IL-protokolla) | **pois** (legacy-protokolla). Myös bootista: `sys/src/9/boot/bootrc` ja `net.rc` (7.10.) |

## Ehdotettu järjestys

1. **Ydin (pc64-conf):** kohdan 3 "pois"-rivit. Tämä on yksi conf-muutos.
   Testi: VM:n käännös (`tools/build.sh`), boot (`tools/test-qemu.sh`),
   palvelut (Monolithin `tools/vm-cpu`), sitten oma rauta. **Tehty 7.10.**
   (VM). vmx ja dtracy samoin. 9pc64 oli silloin 4 926 985 tavua, ja
   etheriwlin paluun jälkeen 5 015 170. **Oma rauta 16.10.:** silli (Dell
   Latitude 7480) bootasi USB-tikulta (`docs/status.md`). Näyttö ilman vesaa: GOP-kehyspuskuri, konsoli ja rivieditori
   QEMU:n `-vga std`:llä (1280×800).
2. **Ydin, tiedostot:** kohtien 2 ja 3 tiedostot pois `plan2001/`:n
   kopioista. Niitä ei ole vielä kopioitu, joten tämä vaatii `pc/`:n ja
   `pc64/`:n mkfilen kopion.

Userlandin järjestys (tunnistus, palvelimen jakeluversio) on Monolithin
`docs/legacy-userland.md`:ssä. Avoimet päätökset ovat yllä **päätä**-riveillä.
