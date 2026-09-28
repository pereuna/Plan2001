# Asennusaikainen osajoukko ja USB-asennin

Koneellisesti johdettu luettelo kaikista 9front-tiedostoista, joita järjestelmän
asentaminen 9frontin omalla tavalla (`inst/start`) tarvitsee, ja siitä
rakennettu **USB-asennustikku, joka bootaa vain UEFI:llä** Plan2001:n omalla
loaderilla ja kernelillä. Versio on lukittu: **9front-11952**
(`tools/9front.release`). Tehty 25.9.2026 (`docs/plan-linux-env.md`, vaihe 2).

## Tulos

| | 9frontin ISO | Plan2001-asennustikku |
|---|---|---|
| Media | ISO9660, BIOS + UEFI | GPT-levykuva: ESP + Plan 9 -osio (hjfs), **vain UEFI** |
| Loader ja kernel | 9frontin | **Plan2001:n** (`build/amd64/bootx64.efi`, `build/amd64/9pc64`) |
| Koko | 505 Mt | 1 GiB:n kuva, josta käytössä **26 Mt** |
| Ajonaikaiset tiedostot | koko jakelu | **250** (20 Mt) |

`tools/subset/test` asentaa järjestelmän tikulta (QEMU: xHCI + `usb-storage`)
tyhjälle levylle tavallisilla vastauksilla. Sen jälkeen se käynnistää
asennetun levyn rc-kehotteeseen ja tarkistaa sarjalokista, että boot kulki
Plan2001:n loaderin kautta (`[P2 L..]`). Tulos on **PASS**, ja ajo kestää noin
1 min 35 s. Testi ajettiin kahdesti peräkkäin samalla tikulla, ja
molemmat menivät läpi, joten tikku ei tallenna asennuksen tilaa.

Asennettu järjestelmä on täsmälleen tikun sisältö, koska `copydist` kopioi koko
median. Asennusohjelman `bootsetup` kopioi uuden levyn ESP:hen tikun
`/386/bootx64.efi`:n ja `/amd64/9pc64`:n, jotka ovat Plan2001:n omat.
Se ohittaa BIOS-askeleet itse, koska `/386/pbs` ja `/386/9bootfat` puuttuvat.

## Tiedostot repossa

| Polku | Sisältö |
|---|---|
| `subset/<target>/proto` | 9front-proto (`mkfs`): tikun juuren koko sisältö moodeineen ja omistajineen. Ylimmän tason hakemistot tulevat 9frontin `distproto`sta (`tmp d555`, ks. alla). |
| `subset/<target>/files` | Jokainen tiedosto: polku, laji (`runtime`/`source`/`excluded`), löytötapa, peruste ja md5 (`-`, kun tiedostoa ei kopioida: käännetyt binäärit ja pois jätetty legacy) |
| `subset/9front/` | **Koskematon kopio** 9front-11952:n tiedostoista 9frontin omissa poluissa: 2815 tiedostoa, 26 Mt. Mukana ovat ajonaikaiset rc-skriptit ja datatiedostot sekä kaikkien osajoukon binäärien lähteet. |

`sys/` on edelleen oma muutoskerroksemme. `subset/9front/` on upstream-kopio, jota
ei muokata. `tools/subset/check` vertaa sitä md5:llä VM:n 11952-puuhun.

### Tikku (`tools/subset/mkusb`)

Tikku rakennetaan VM:ssä 9frontin `%.disk`-säännön mallin mukaan
(`/sys/lib/dist/mkfile`), mutta ilman MBR:ää, `pbs`:ää ja `9bootfat`ia:

- `disk/edisk -baw`: GPT, ESP ja Plan 9 -osio. `disk/prep -a^(nvram fs)`.
- ESP (`disk/format -d`): `efi/boot/bootx64.efi` ja `9pc64` (Plan2001) sekä
  `plan9.ini` (`bootfile=9pc64`, ei `nobootprompt`ia).
- `fs`: hjfs, käyttäjät kuten `%.disk`issä, ja
  `disk/mkfs -U -s / subset/amd64/proto`. Plan2001:n kernel ja loader on sidottu
  polkuihin `/amd64/9pc64` ja `/386/bootx64.efi`.

Juurilevy valitaan `bootargs`-kehotteessa kuten 9frontin alkuperäisessä
asennustavassa. bootrc tarjoaa oletukseksi tikun `fs`-osion (QEMUssa
`local!/dev/sdU0fc4d/fs`; USB-levyn nimi vaihtelee koneittain).
Oikealle tikulle kuva kirjoitetaan komennolla
`dd if=build/subset/amd64/plan2001-inst.img of=/dev/sdX bs=4M`.

### Pois jätetty legacy (5 tiedostoa, `subset/amd64/files`: `excluded`)

| Tiedosto | Kuka sitä pyytäisi | Miksi pois |
|---|---|---|
| `/386/pbs`, `/386/9bootfat` | `bootsetup` (BIOS-bootlohko, 9fat-loader) | vain UEFI |
| `/386/mbr` | `partdisk` (mbr-vaihtoehto) | vain GPT/UEFI |
| `/386/bootia32.efi` | `bootsetup` (kopioi `/386/*.efi`) | vain x86-64 |
| `/amd64/bin/9660srv` | `mountdist` (iso9660-haara) | media on USB-levy |

Niiden lisäksi ISO-bootin tiedostoja (`9bootiso`, `efiboot.fat`,
`9boothyb`, `9bootpxe`) ja BIOS-loaderien lähteitä (`/sys/src/boot/pc`) ei
enää pyydä mikään. Asennusohjelman legacy-haaroja (mbr-vaihtoehto,
pbs/9bootfat-käsittely, 9660/cdboot) ei ole vielä poistettu skripteistä:
tämä on seuraava vaihe.

### Ajonaikaiset tiedostot (250)

Löytötavat: **boot** 75, **dynaaminen** 51, **staattinen** 123, **iteratiivinen** 1.
Suurimmat ryhmät: ohjelmat `/amd64/bin` (levytyökalut, tiedostopalvelimet,
`rc`, `awk`, `sed`…), `/adm/timezone` (74, `tzsetup`), `/sys/lib/kbmap` (38,
`bootfs.proto`), `/rc/bin/inst` (22).

### Lähteet (2651 tiedostoa)

`/sys/src/cmd` (osajoukon ohjelmat), 29 kirjastoa `/sys/src/lib*`, kernel
`/sys/src/9/{port,pc,pc64,ip,boot}`, UEFI-loader `/sys/src/boot/efi` sekä
`/sys/include` ja mk-mallit.

## Menetelmä

1. **Boot.** Kernel ja sen `bootdir` (`paqfs`, `factotum`, `bootfs.paq`:
   `bootrc`, `kbdfs`, `nusb`…) sekä UEFI-loader.
2. **Dynaaminen** (`tools/subset/trace`). Oikea asennus täyden järjestelmän
   overlaysta tyhjälle levylle. cwfs päivittää tiedoston atimen aina, kun
   tiedosto luetaan tai ajetaan (`fs_read` → `accessdir`). Aika merkitään,
   VM käynnistetään uudelleen, ja tilannekuvat otetaan ennen `copydist`iä
   ja sen jälkeen. `copydist` itse jätetään pois, koska `mkfs` lukee koko
   puun. Tilannekuvat otetaan asennusohjelman `!komento`-paolla.
3. **Staattinen** (`tools/subset/derive`). Joukon rc-skriptit käydään läpi
   rekursiivisesti: komennot ja kirjaimelliset polut (`$cputype`/`$objtype`
   korvataan). Polut `/n/newfs/X` ja `/n/dist/X` muunnetaan muotoon `/X`, ja
   uudelta levyltä luettu hakemisto otetaan mukaan tiedostoineen. Pelkkä
   `test -d` vaatii vain hakemiston.
4. **Iteratiivinen** (`tools/subset/test` + `tools/subset/extra`). Jos testi
   löytää puuttuvan tiedoston, korjataan ensisijaisesti analyysiä. `extra`
   on vain tiedostoille, joita mikään analyysi ei voi nähdä.
5. **Lähteet.** Binääristä lähteeseen (poikkeukset: `SRCMAP` tiedostossa
   `derive.py`). Sen jälkeen suljetaan `#include`, `#pragma lib`, mkfilen
   `<`-mallit ja `LIB=`. Tiedostot haetaan VM:stä kierroksittain.
6. **Legacy pois** (`LEGACY` tiedostossa `derive.py`): sääntö voittaa kaikki
   löytötavat, ja pois jätetty kirjataan perusteineen.

### Löydöt

| Löydös | Miten | Korjaus |
|---|---|---|
| `/amd64/init` puuttui (`bootrc`: `/$cputype/init`) | testi | `$cputype`/`$objtype` korvataan, ja `bootrc` luetaan analyysiin |
| `/lib/namespace` | jäljityksen tarkistus: atime ei päivittynyt, vaikka init ja `newns(2)` lukevat tiedoston joka bootissa | `extra` (ainoa iteratiivinen) |
| `/adm/timezone/*` puuttui | testi: `tzsetup` kiersi silmukkaa | `/n/newfs/X` → `/X`, ja hakemisto tiedostoineen |
| joukko paisui 279 Mt:uun | testi meni läpi, mutta joukko oli liian suuri | hakemistoja laajennetaan vain uuden levyn luennoista. `test -d` ja `/dist/9front` (git-repo, 130 Mt) vaativat vain hakemiston. |
| tikku muisti edellisen asennuksen (`/tmp/copydone`), ja toinen asennus jätti kopioinnin väliin | testi: asennettu levy oli tyhjä | ylimmän tason hakemistot `distproto`n mukaan: `tmp d555`, joten profiili käynnistää `ramfs`in kuten ISOlla |

### Tunnetut rajat

- Staattinen analyysi arvioi joukon ylöspäin: rc-skriptien kaikki haarat
  (esim. `termrc`n `reform`- ja `nusb`-kohdat, `hjfs`/`gefs`) tulevat mukaan.
  Osajoukko on riittävä ja perusteltu, mutta ei minimaalinen.
- atime-jäljitys näkee vain cwfs:n kautta luetut tiedostot. Kernel ja
  `bootfs.paq` katetaan boot-säännöillä, ja alkuvaiheen lukujen aukko
  (`/lib/namespace`) iteraatiolla.
- Tikulla ei ole `/usr/glenda/tmp`:tä, joten myös asennetun järjestelmän
  `/tmp` on ramfs (9frontin ISO-asennuksessa se on levyllä). Tämä korjataan
  asennusohjelman siivousvaiheessa.
- Vain `amd64`.

## Uudelleentuottaminen

Kaikki työkalut ottavat kohteen ympäristömuuttujasta `TARGET` (oletus `amd64`,
ks. `tools/targets/`). Alla on amd64:n polut.

```
tools/build.sh         # Plan2001:n 9pc64 ja bootx64.efi -> build/        (~25 s)
tools/subset/trace     # dynaaminen jäljitys -> build/subset/amd64/trace/ (~2 min)
tools/subset/derive    # staattinen + lähdesulkeuma -> build/subset/    (~25 s)
python3 tools/subset/make.py build/subset/amd64 .   # subset/amd64/{proto,files}, subset/9front
tools/subset/mkusb     # build/subset/amd64/plan2001-inst.img             (~20 s)
tools/subset/test      # PASS/FAIL                                        (~1,5 min)
tools/subset/check     # subset/9front vs. VM:n puu (md5)                 (~15 s)
```
