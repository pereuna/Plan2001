# Asennusaikainen osajoukko

Koneellisesti johdettu luettelo kaikista 9front-tiedostoista, joita järjestelmän
asentaminen 9frontin omalla tavalla (`inst/start`) tarvitsee. Versio on lukittu:
**9front-11952** (`tools/9front.release`). Tehty 25.9.2026
(`docs/plan-linux-env.md`, vaihe 2).

## Tulos

| | Täysi 9front-ISO | Osajoukon ISO |
|---|---|---|
| Koko | 505 Mt | **21 Mt** |
| Ajonaikaiset tiedostot | koko jakelu | **261** (21 Mt) |

Hyväksymistesti `tools/subset/test` asentaa osajoukon ISOlta tyhjälle levylle
tavallisilla vastauksilla ja käynnistää asennetun järjestelmän rc-kehotteeseen.
Tulos on **PASS**, ja koko ajo kestää noin 1 min 40 s. Asennusohjelman
`copydist` kopioi koko median uudelle levylle, joten asennettu järjestelmä
on täsmälleen tämä osajoukko.

## Tiedostot repossa

| Polku | Sisältö |
|---|---|
| `subset/proto` | 9front-proto (`mkfs`/`mk9660`): ajonaikaiset tiedostot moodeineen ja omistajineen. Media rakennetaan tästä ja `9bootproto`sta, kuten 9frontin oma `/sys/lib/dist/mkfile` tekee. |
| `subset/files` | Jokainen tiedosto: polku, laji (`runtime`/`source`), löytötapa, peruste ja md5 (`-`, kun tiedosto on käännetty binääri eikä sitä kopioida) |
| `subset/9front/` | **Koskematon kopio** 9front-11952:n tiedostoista 9frontin omissa poluissa: 2840 tiedostoa, 26 Mt. Mukana ovat ajonaikaiset rc-skriptit ja datatiedostot sekä kaikkien osajoukon binäärien lähteet. |

`sys/` on edelleen oma muutoskerroksemme. `subset/9front/` on upstream-kopio, jota
ei muokata. `tools/subset/check` vertaa sitä md5:llä VM:n 11952-puuhun, joten
jos kopio ja VM joskus eroavat, ero näkyy heti.

### Ajonaikaiset tiedostot (261)

| Ryhmä | Määrä | Mistä |
|---|---|---|
| `/amd64/bin/*` | 84 | ohjelmat: levytyökalut (`disk/prep`, `fdisk`, `edisk`, `mbr`, `mkfs`, `format`), tiedostopalvelimet (`cwfs64x`, `hjfs`, `gefs`, `dossrv`, `9660srv`, `paqfs`), `rc`, `awk`, `sed`, `grep`… |
| `/adm/timezone/*` | 74 | `tzsetup` tarjoaa aikavyöhykkeet tästä hakemistosta |
| `/sys/lib/kbmap/*` | 38 | `bootfs.proto` (näppäinkartat bootissa) |
| `/rc/bin/inst/*` | 22 | asennusohjelma |
| `/rc/bin`, `/rc/lib` | 16 | `termrc`, `screenrc`, `fshalt`, `fstype`, `rcmain`… |
| `/386/*`, `/amd64/9pc64` | 12 | loaderit (`9bootiso`, `efiboot.fat`, `bootx64.efi`, `pbs`, `mbr`…) ja kernel |
| muut | 15 | glendan `profile` ja muut kotihakemiston tiedostot, `/lib/namespace`, `/adm/users`, `allproto`, `bootrc`… |

Löytötavat: **boot** 85, **dynaaminen** 51, **staattinen** 124, **iteratiivinen** 1.

### Lähteet (2579 tiedostoa)

- `/sys/src/cmd/…`: 1143 tiedostoa, osajoukon ohjelmien lähteet ja mkfilet
- `/sys/src/lib*`: 1133 tiedostoa, 29 kirjastoa (`libc`, `libsec`, `libmp`,
  `libdisk`, `libthread`, `lib9p`, `libip`, `libndb`, `libdraw`…)
- `/sys/src/9/…`: 325 tiedostoa, kernel (`port`, `pc`, `pc64`, `ip`, `boot`)
- `/sys/src/boot/…`: 27 tiedostoa, loaderit (`efi`, `pc`)
- `/sys/include`, `/amd64/include`, mk-mallit: loput

## Menetelmä

Kolme toisiaan täydentävää tapaa. Jokainen tiedosto kirjataan sillä tavalla,
jolla se ensimmäisenä löytyi.

1. **Boot.** Kernelin `bootdir` (`pc64`-konfiguraatio: `paqfs`, `factotum`,
   `bootfs.paq`), `bootfs.proto`n sisältö (`bootrc`, `kbdfs`, `ipconfig`,
   `kbmap`…) sekä `9bootproto`n loader-tiedostot ja kernel.
2. **Dynaaminen** (`tools/subset/trace`). Oikea asennus täyden järjestelmän
   overlaysta tyhjälle levylle. cwfs päivittää tiedoston atimen aina, kun
   tiedosto luetaan tai ajetaan (`fs_read` → `accessdir`). Aika merkitään,
   VM käynnistetään uudelleen (jotta myös boot tulee mukaan), ja sen jälkeen
   otetaan kaksi tilannekuvaa: ennen `copydist`iä ja sen jälkeen.
   `copydist` itse jätetään pois, koska `mkfs` lukee koko puun. Sen omat
   tiedostot löytyvät staattisesti. Tilannekuvat otetaan asennusohjelman
   omalla `!komento`-paolla, eikä asennusta tarvitse keskeyttää.
3. **Staattinen** (`tools/subset/derive`). Joukon jokainen rc-skripti käydään
   läpi rekursiivisesti: komennot ratkaistaan polkuihin (`/amd64/bin`,
   `/rc/bin`, `./x`), samoin kirjaimelliset polut (`$cputype` korvataan).
   Asennusohjelman polut uudella levyllä (`/n/newfs/X`) ja medialla
   (`/n/dist/X`) muunnetaan muotoon `/X`. Jos asennusohjelma lukee
   hakemistoa uudelta levyltä (`/n/newfs/adm/timezone`), hakemisto otetaan
   mukaan tiedostoineen. Pelkkä `test -d` vaatii vain hakemiston.
4. **Iteratiivinen** (`tools/subset/test` + `tools/subset/extra`). Rakennetaan
   ISO ja asennetaan siltä. Jos jokin puuttuu, se näkyy virheenä. Ensisijaisesti
   korjataan analyysiä. Vain tiedostot, joita mikään analyysi ei voi nähdä,
   lisätään `extra`-listaan perusteen kanssa.
5. **Lähteet.** Binääristä lähteeseen (`/sys/src/cmd/<nimi>.c` tai `…/<nimi>/`;
   poikkeukset taulukossa `derive.py`: `SRCMAP`). Sen jälkeen suljetaan
   `#include`, `#pragma lib`, mkfilen `<`-mallit ja `LIB=`-rivit. VM:stä
   haetaan kierroksittain vain se, mitä tarvitaan, ja kierroksia kertyi 7.

### Iteraatioiden löydöt

| Löydös | Miten | Korjaus |
|---|---|---|
| `/amd64/init` puuttui (`bootrc`: `/$cputype/init`) | testi: `bootrc` pysähtyi | staattinen analyysi korvaa `$cputype`/`$objtype`, ja `bootrc` luetaan analyysiin |
| `/lib/namespace` | jäljityksen tarkistus: atime ei päivittynyt, vaikka init ja `newns(2)` lukevat tiedoston joka bootissa | `extra` (ainoa iteratiivinen tiedosto) |
| `/adm/timezone/*` puuttui | testi: `tzsetup` kiersi silmukkaa | `/n/newfs/X` → `/X`, ja hakemisto tiedostoineen |
| joukko paisui 279 Mt:uun ja sitten 149 Mt:uun | testi meni läpi, mutta ISO oli liian suuri | hakemistoja laajennetaan vain uuden levyn luennoista. `test -d` ja `/dist/9front` (9frontin git-repo, 130 Mt) vaativat vain hakemiston. |

### Tunnetut rajat

- Staattinen analyysi arvioi joukon ylöspäin: rc-skriptien kaikki haarat
  (esim. `termrc`n `reform`- ja `nusb`-kohdat, `hjfs`/`gefs`) tulevat mukaan,
  vaikka tavallinen asennus ei niitä käytä. Osajoukko ei siis ole minimaalinen
  vaan riittävä ja perusteltu.
- atime-jäljitys näkee vain cwfs:n kautta luetut tiedostot. Kernelin ja
  `bootfs.paq`in sisällöt katetaan boot-säännöillä, ja alkuvaiheen lukujen
  aukko (`/lib/namespace`) iteraatiolla.
- Vain `amd64`. 386-loaderit ovat mukana, koska ISO bootaa niillä
  (`9bootproto`).

## Uudelleentuottaminen

```
tools/subset/trace     # dynaaminen jäljitys -> build/subset/trace/     (~2 min)
tools/subset/derive    # staattinen + lähdesulkeuma -> build/subset/  (~25 s)
python3 tools/subset/make.py build/subset .   # subset/{proto,files,9front}
tools/subset/mkiso     # build/subset/subset.iso                       (~15 s)
tools/subset/test      # PASS/FAIL                                     (~2 min)
tools/subset/check     # subset/9front vs. VM:n puu (md5)              (~15 s)
```
