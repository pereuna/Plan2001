# Legacy pois: lista (vaihe 4, 7.10.2026)

Plan2001:llä on kaksi profiilia (`docs/plan9-wasm32.md`):

- **palvelin:** CPU, auth ja fs amd64:llä. Alusta on pc64 (UEFI): pilven
  VM (virtio) ja oma rauta (Dell T5600).
- **terminaali:** wasm32-kone selaimessa ja/tai sarjakonsoli.

Tähän on listattu se, mitä kumpikaan profiili ei tarvitse. Jokaisella
rivillä on suositus:

- **pois:** poistetaan.
- **jää:** profiili tarvitsee sen.
- **päätä:** käyttäjän päätös, perustelu vieressä.

Rivit ovat repossa olevasta koodista: pc64:n ytimen konfiguraatio
(`plan2001/sys/src/9/pc64/pc64`), `subset/9front` ja wasm32:n juuri.

**Huomio palvelimen userlandista.** Palvelin ajaa nyt 9frontin ISO:lta
asennettua järjestelmää, jonka päällä on Plan2001:n ydin ja palvelut
(`tools/vm-setup`, `tools/vm-cpu`, pilven image). Repo ei siis vielä
päätä palvelimen ohjelmista. Userlandin poistot (kohdat 4–7) vaativat
ensin Plan2001:n oman amd64-jakelun, esimerkiksi `subset/`ista
kootun. Ytimen poistot (kohdat 1–3) voi tehdä heti.

## 1. Arkkitehtuurit

| Mikä | Missä | Suositus |
|---|---|---|
| 386 (32-bit PC), `sys/src/9/pc` 32-bittisenä ytimenä | `subset/9front/386`, pc:n `l.s`, `apbootstrap.s`, `rebootcode.s` | **pois** 32-bittisenä alustana. Huom.: pc64 käyttää `../pc`:n C-tiedostoja, joten hakemisto jää, mutta 386-ydin, sen mkfile ja `/386` poistuvat |
| 68020, power (ja 9frontin muut: arm, mips, spim, sparc) | `subset/9front/68020`, `power` (vain mkfilet) | **pois** |
| amd64 | pc64 | **jää** (palvelin) |
| wasm32 | `plan2001/sys/src/9/wasm32` | **jää** (terminaali, myöhemmin myös CPU) |
| arm64 | `plan2001/sys/src/9/arm64` (boot-työ, QEMU) | **päätä:** jatketaanko, vai pois kunnes on rautaa |
| riscv64 | `tools/targets/riscv64` (vain kohde) | **päätä:** kuten arm64 |

## 2. Boot ja laiteohjelmisto

| Mikä | Missä | Suositus |
|---|---|---|
| BIOS-boot (pbs, mbr, 9bootfat, PXE, ISO-boot) | poistettu jo (vaihe A ja loaderin siivous) | tehty |
| `plan9.ini`-teksti boot-konfiguraationa | `port/bootargs.c` lukee BootInfon config-osiota | **jää** toistaiseksi: BootInfo kuljettaa sen. Myöhemmin rakenteiseksi |
| `bootfs.paq` ja paqfs bootin juurena | pc64-conf `bootdir` | **päätä:** wasm32 käyttää omaa `#R`-arkistoaan. Yhteinen boot-juuren muoto on mahdollinen myöhemmin |
| `apm.c`, `apmjump.s` (APM-virranhallinta), `aux/apm` | `pc/`, `subset` | **pois** (ACPI) |
| `bios32.c`, `pcibios.c` | `pc/` (pcibios jo pois confista) | **pois** tiedostoina |
| `vgavesa` ja `aux/realemu` (VESA BIOS -kutsut reaalitilaemulaatiolla) | pc64-conf `vgavesa`, `subset/aux/realemu` | **pois:** UEFI GOP (`bootfb`, `vgasoft`) korvaa ne |
| `aux/vga` (näyttötilan vaihto) | `subset/aux/vga` | **pois** palvelimelta. GOP antaa tilan, ja `vgaigfx`/`vgaradeon` jäävät vain, jos rauta tarvitsee |
| i8259 (PIC), i8253 (PIT) | `archgeneric` | **päätä:** APIC ja HPET riittävät nykyraudalla, mutta ydin kalibroi kellonsa PIT:llä tai HPET:llä (`docs/status.md`). Poisto vaatii testin raudalla |

## 3. Ytimen laitteet ja ajurit (pc64)

| Mikä | Suositus |
|---|---|
| `floppy`, `lpt`, `pccard`, `i82365`, `pcmciamodem` (jo kommentoitu), niiden tiedostot `pc/`:ssa | **pois** tiedostoina |
| ISA- ja vanhat PCI-verkkokortit: `ether2114x`, `ether79c970`, `ether8139`, `etheryuk`, `etherbcm`, kommentoidut (`ether8390`-perhe, `elnk3`, `82557`, `83815`, `ga620`, `vgbe`, `vt610x`, `smc`, `wavelan`) | **pois.** Jäävät `ethervirtio`/`virtio10` (pilvi), `etherigbe`, `ether82563`, `ether8169`, `etheri225`, `ether82598`, `etherx550` (rauta) |
| WLAN: `etheriwl`, `etherwpi`, `etherrt2860`, `wifi`, `aux/wpa`, factotumin `wpapsk` | **pois** palvelimelta |
| `sdide` (PATA), `sd53c8xx` (SCSI), `sdmylex`, `sdodin`, `sdmv50xx` | **pois.** Jäävät `sdiahci`, `sdnvme`, `sdvirtio`, `sdmmc`? (päätä), `sdram`, `sdloop` |
| `aoe`, `sdaoe` (ATA over Ethernet) | **pois** |
| `audio`, `audiohda`, `audiosb16`, `audioac97` | **pois** palvelimelta (terminaalin ääni on selaimen) |
| `vga`, `draw`, `mouse`, `kbd` pc64:llä | **päätä:** palvelimen paikallinen konsoli (GOP-kehyspuskuri, rivieditori, `bootfb`) tarvitsee `vga`:n ja `kbd`:n tekstikonsolina. `draw`, `mouse` ja rio palvelimella ovat terminaalin asioita, joten pois. Kaikki vga-korttiajurit paitsi `vgasoft` (ja ehkä `vgaigfx`) pois |
| `usb` ja HCI:t | **jää** (näppäimistö, levyt) |
| `vmx` (virtualisointi), `dtracy`, `kprof`, `segment` | **päätä:** `vmx` ja `dtracy` ovat isoja. Jos niitä ei käytetä, pois |
| `cputemp`, `pmmc`, `sdmmc` | **päätä** raudan mukaan |
| `bridge`, `gre`, `ipmux`, `igmp`, `rudp`, `inferno`-medium, `netdevmedium` | **pois** (`wg` korvaa tunnelit) |
| `il` (Plan 9:n IL-protokolla) | **pois** (legacy-protokolla) |

## 4. Tunnistus (factotum, authsrv, keyfs)

| Mikä | Suositus |
|---|---|
| `p9sk1` (DES-avaimet ja -liput) | **pois:** dp9ik korvaa sen. keyfs:n DES-avain (`key`) pois, `aeskey` jää. Vaatii authsrv:n ja keyfs:n kopion `plan2001/`:een ja `p9any`:n neuvottelun vain dp9ik:lle |
| `p9cr`, `vnc`, `apop`, `cram`, `chap`, `mschap`, `mschapv2`, `ntlm`, `ntlmv2`, `httpdigest` | **pois** (vanhat haaste-vastaus-protokollat; ei Plan2001:n palveluja) |
| `dp9ik`, `p9any`, `pass`, `rsa`, `ecdsa` (TLS, ACME), `totp` | **jää** (`totp` päätä: ehkä toinen tekijä) |
| secstore (PAK), WebAuthn (passkey, signupd, passkeyd) | **jää** |

## 5. Etäkäyttö ja verkkopalvelut

| Mikä | Suositus |
|---|---|
| vanha `cpu` ja `import` (`-a`, p9sk1-aikainen salaus) | **pois:** `rcpu`, `rimport` ja `rexport` (TLS-PSK) jäävät |
| `telnet`, `telnetd`, `rlogin`, `rx`/`rexexec`, `ftpfs`, `ftpd` | **pois** |
| `srv` ja `9fs` ilman TLS:ää | **päätä:** `rimport` ja `srv` TLS:n kanssa riittävät |
| `cifs`, `nfs`, `smbfs`, `vncs`/`vncv` | **pois** |
| natiivi drawterm (`monolith/tools/build`) | **päätä:** pääte on wasm32 ja webterm. 9pterm (pilven hallinta) jää |
| `upas` (posti) | **päätä:** CPU-palvelimen ei tarvitse lähettää postia (`websession` estää sen web-tunnuksilta jo) |
| `ip/ppp`, `ip/pppoe` | **pois** |
| DNS (`ndb/dns`), DHCP-asiakas, `ip/ipconfig`, ACME (`certrenew`), `rc-httpd`, `tlssrv` | **jää** |

## 6. Tiedostojärjestelmät

| Mikä | Suositus |
|---|---|
| `cwfs` | **jää** (palvelimen fs) |
| `gefs` | **päätä:** 9frontin uusi fs. Valitaan yksi palvelimelle (cwfs on nyt käytössä ja testattu) |
| `hjfs` | **jää** (wasm32-koneen levy) |
| `dossrv` (FAT) | **jää** (UEFI:n esp) |
| `paqfs` | **päätä** (kohta 2) |
| `9660srv`, `cdfs`, ISO-työkalut | **pois** palvelimelta (asennus ei käytä CD:tä) |
| `cfs` (välimuisti-fs) | **pois** |
| `disk/mbr`, `disk/fdisk` (MBR) | **pois** (GPT: `disk/edisk`, `prep` jäävät) |
| fossil, venti | eivät ole osajoukossa: **pois** |

## 7. Ohjelmat ja ympäristö

| Mikä | Suositus |
|---|---|
| APE (ANSI/POSIX-ympäristö) ja sen ohjelmat | **päätä:** porttaukseen hyödyllinen, mutta iso. Plan2001:n omat ohjelmat eivät tarvitse sitä |
| `mothra` (vanha selain), `page`/`gs` | **pois** palvelimelta. Terminaalilla on selain |
| `aux/mouse` (PS/2- ja sarjahiiren asetus) | **pois** |
| pelit | **jää** (päätös: pelit tulevat) |
| vitsit ja sitaatit | jo pois (`DENY`) |
| `sam`, `acme`, `rio`, `rc`, perustyökalut | **jää** (terminaalin ohjelmat; palvelimella sovellusoriginien `lib/app`) |

## Ehdotettu järjestys

1. **Ydin (pc64-conf):** kohdan 3 "pois"-rivit. Tämä on yksi conf-muutos.
   Testi: VM:n käännös (`tools/build.sh`), boot ja palvelut
   (`tools/vm-cpu`, VM-testit), sitten T5600.
2. **Ydin, tiedostot:** kohtien 2 ja 3 tiedostot pois `plan2001/`:n
   kopioista. Niitä ei ole vielä kopioitu, joten tämä vaatii `pc/`:n ja
   `pc64/`:n mkfilen kopion.
3. **Tunnistus:** p9sk1 ja vanhat protokollat pois factotumista, authsrv:stä
   ja keyfs:stä (kopiot `plan2001/`:een). Testit: `dp9ik`, `rcpu`,
   `secstore`, VM-ketju.
4. **Palvelimen jakelu:** oma amd64-jakelu, jonka jälkeen kohdat 5–7.

Avoimet päätökset ovat yllä **päätä**-riveillä.
