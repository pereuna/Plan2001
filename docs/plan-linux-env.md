# Suunnitelma: kehitysympäristö Linuxiin, Windows/WSL pois

Laadittu 25.9.2026. **Vaihe 1 on toteutettu 25.9.2026** (lopputulos ja
havainnot: `docs/status.md`, "Kehitysympäristö Debian 13:een"). Toteutus poikkeaa
alla kuvatusta kahdessa kohdassa: lähteet viedään sisään raakana tar-levynä
(FAT ei hyväksy hakemistoa `aux`), ja käännös-VM:ssä ei ole näyttölaitetta
(`-vga none`), jotta rio ei vie konsolia. **Vaihe 2 on toteutettu
25.9.2026** (`docs/install-subset.md`). Päätökset: 9front pysyy lukittuna
versioon 11952, ja osajoukko kopioidaan repoon (`subset/9front/`).

## Miksi

Nykyinen työnkulku on liian hidas ja hauras:

- Käännös kulkee drawtermin/rcpu:n kautta Windows-VM:ään (WHPX). rcpu kaatuu
  ajoittain, ja silloin konetta ohjataan QEMU-monitorin `screendump`/`sendkey`-
  komennoilla, eli kuvakaappauksilla. Muutaman minuutin operaatioon on mennyt tunteja.
- Repossa on Windows- ja WSL-sidonnaista koodia (`.cmd`, `.ps1`, `/mnt/c`,
  `powershell.exe`, Windows-gatewayn haku `ip route`lla).

## Periaatteet

1. **Yksi alusta, ei fallbackeja.** Isäntä on Debian 13 (trixie). Windows- ja
   WSL-koodi poistetaan kokonaan. Rinnakkaisia "varalla"-polkuja ei jätetä.
2. **Vain tekstikanavia.** VM:ää ohjataan sarjakonsolin kautta. Tuloste ja
   exit-status luetaan tekstinä. Kuvakaappauksia ei käytetä ohjaukseen eikä
   testin läpäisyn tarkistamiseen. Näyttöä käytetään vain, jos ihminen haluaa
   katsoa.
3. **Ei drawtermia, ei rcpu:ta, ei auth-palvelinta** käännöksessä eikä
   testauksessa.
4. **Ei päällekkäistä dataa.** Yksi 9front-ISO ja yksi perusimage yhdessä
   välimuistihakemistossa. Testiajot tehdään kertakäyttöisillä qcow2-overlayilla.
   Ennen minkään lataamista tarkistetaan, onko tiedosto jo olemassa.
5. **Kiinnitetyt versiot.** Debian-julkaisu ja 9front-release nimellä ja
   tarkistussummalla repossa.
6. **Suunnittele ensin.** Tee jokainen alla oleva askel plan-tilassa, näytä
   suunnitelma käyttäjälle ja toteuta vasta hyväksynnän jälkeen. Älä asenna,
   lataa tai poista mitään isoa kysymättä.

## Ympäristö

| Asia | Arvo |
|---|---|
| Isäntä | Debian 13 "trixie", x86-64, KVM (`/dev/kvm`) |
| Paketit (vain Debianin omista repoista) | `qemu-system-x86 qemu-utils ovmf mtools python3 git curl` |
| OVMF | Debianin `ovmf`-paketin polut (`/usr/share/OVMF/OVMF_CODE_4M.fd` + oma kopio `OVMF_VARS_4M.fd`:stä). Tarkista todellinen polku paketista, älä arvaa. |
| 9front | Uusin release 9front.org:sta. Nimi, URL ja sha256 kirjataan tiedostoon `tools/9front.release`. |
| Välimuisti | `${PLAN2001_CACHE:-$HOME/.cache/plan2001}`: ISO, `base.qcow2`, OVMF-vars. Ei repossa. |
| Käyttäjä VM:ssä | `glenda`. Salasanaa ei tarvita, koska sarjakonsoli ei kysy sitä. |

Jos `/dev/kvm` puuttuu, skriptit pysähtyvät selkeään virheeseen. Ne eivät
siirry hiljaa hitaaseen TCG-tilaan.

## Vaihe 1a: poistot

Poistetaan tiedostot:

- `tools/vm/run.cmd`
- `tools/vm/install.cmd`
- `tools/vm/win-test.ps1`
- `tools/vm/wsl-run.sh`

Poistetaan kohdat:

- `tools/test-qemu.sh`: `/mnt/c/temp/esp`-kopio ja Windows-QEMU:n käynnistys
  (`powershell.exe`, `WIN_QEMU`)
- `tools/build.sh`: drawterm-kutsu, `NINE_HOST`-oletus `ip route`sta,
  `~/.9front-pass`. Tiedosto kirjoitetaan uusiksi (ks. 1c).
- `tools/build.rc`: `$hostsrc`/`$hostbuild` via `/mnt/term`. Korvataan
  levykanavalla (ks. 1c).
- `docs/README.md`: VM-osion WHPX-, WSL- ja drawterm-ohjeet sekä maininta
  "rcpu kaatuu ajoittain". Korvataan uusilla ohjeilla.

Tee poistot **samassa commitissa** kuin korvaavat työkalut (1b–1d). Näin repo ei
ole välissä tilassa, jossa vanha käännöstapa on poistettu eikä uusi toimi vielä.

## Vaihe 1b: ohjauskanava `tools/9run`

Python 3 -skripti, joka käyttää vain standardikirjastoa.

- QEMU käynnistetään `-display none -serial unix:<sock>,server,nowait`
  (tai `-chardev socket,...` + lokitus tiedostoon, jolloin koko istunto jää talteen).
- 9front käynnistetään `plan9.ini`n `console=0`-asetuksella, jolloin konsoli ja
  rc ovat COM1:ssä.
- `9run 'komento'` kirjoittaa sokettiin
  `komento; echo __END_<uuid>__ $status` ja lukee, kunnes loppumerkki tulee
  (aikakatkaisu parametrina). Se tulostaa välissä tulleen tekstin ja palauttaa
  exit-koodin 0, jos `$status` on tyhjä, muuten 1 ja statuksen stderriin.
- Skripti hoitaa myös bootin kysymykset (`bootargs`, `user`), jos ne tulevat
  sarjakonsoliin. Odotettavat kehotteet ovat vakioina skriptin alussa.
- Kaikki sarjaliikenne kirjoitetaan lokiin `build/serial-<aika>.log`.

**Varmistettavaa ensimmäisenä, ennen kuin muuta rakennetaan:**
- Antaako uusimman 9frontin `console=0` rc-kehotteen COM1:een myös ilman rioa?
  Jos termrc yrittää käynnistää rion, tarvitaanko `plan9.ini`hin esim.
  `monitor=`- tai `service=`-asetus, tai asennetaanko VM cpu-palvelimeksi?
  Selvitä kokeilemalla ja lukemalla `/rc/bin/termrc`/`cpurc` VM:stä, älä muistista.
- Toimiiko asennusohjelma (`inst/start`) sarjakonsolilla?

## Vaihe 1c: tiedostot sisään ja ulos ilman verkkoa

- **Sisään:** isäntä rakentaa lähdeoverlayn (`sys/`-puun muutetut tiedostot ja
  `build.rc`) FAT-imageksi `mtools`illa (`mformat` + `mcopy`, ei rootia, ei
  QEMU:n `vvfat`ia). Image liitetään read-only toisena levynä, ja VM:ssä se
  mountataan `dossrv`illä. Tarkista oikea `mount`-syntaksi ja levyn nimi
  (`/dev/sdXX`) VM:ssä: q35-koneen levynimet selvitetään `ls /dev/sd*`:lla.
- **Ulos:** kolmas levy on tyhjä raakaimage (esim. 64 MiB). VM kirjoittaa
  tulokset `tar c ... >/dev/sdXX/data`, ja isäntä purkaa ne `tar xf out.img`:llä
  (tar ohittaa loppunollat). Tarkista kirjoitetun datan pituus/md5 molemmin puolin.
- 9P-mountia isännästä ei käytetä: se vaatisi isäntään lisäpalvelimen, eikä
  levykanava tarvitse mitään.

## Vaihe 1d: skriptit

| Skripti | Tehtävä |
|---|---|
| `tools/9front.release` | release-nimi, ISO:n URL, sha256 |
| `tools/vm-setup` | Tarkistaa paketit ja `/dev/kvm`. Lataa ISOn välimuistiin vain, jos sitä ei ole, ja tarkistaa sha256:n. Luo `base.qcow2`:n ja asentaa 9frontin siihen sarjakonsolin kautta automaattisesti (`9run` + asennusohjelman kysymysten vastaukset skriptissä). Ajetaan kerran. |
| `tools/vm` | Käynnistää/pysäyttää VM:n: overlay `base.qcow2`:n päälle (`qemu-img create -f qcow2 -b base.qcow2 -F qcow2`), sokettipolut `build/`-hakemistossa. Overlay poistetaan pysäytyksessä, ellei `KEEP=1`. |
| `tools/9run` | ks. 1b |
| `tools/build.sh` | Käynnistää overlay-VM:n, vie lähteet sisään (1c), ajaa `build.rc`:n `9run`illa, tuo `9pc64`, `bootx64.efi` ja lokit `build/`-hakemistoon ja pysäyttää VM:n. |
| `tools/build.rc` | Sama logiikka kuin nyt (overlay VM:n `/sys/src`:n päälle, kbdfs, kernel, loader), mutta lähde on mountattu FAT-levy ja kohde tar-levy. |
| `tools/test-qemu.sh` | Nykyinen OVMF-testi Debianin OVMF-poluilla ja KVM:llä. ESP rakennetaan `mtools`illa FAT-imageksi. Läpäisy tarkistetaan pelkästään sarjalokista (`bootargs is`, yksi `Plan 9`-banneri). Näyttö on valinnainen (`DISPLAY_QEMU=1` → `-display gtk`). |

## Hyväksymiskriteerit (vaihe 1 valmis)

1. Puhtaalla Debian 13 -koneella ajetaan peräkkäin, ilman yhtään käsin tehtyä
   askelta: `tools/vm-setup` → `tools/build.sh` → `tools/test-qemu.sh`. Viimeinen
   tulostaa `PASS`.
2. `build.sh` kestää minuutteja, ei kymmeniä minuutteja, ja koko istunto näkyy
   tekstilokina.
3. `git grep -niE 'wsl|windows|powershell|whpx|/mnt/c|drawterm|\.cmd|\.ps1'`
   ei löydä työkaluista eikä ohjeista mitään. Historiallisissa kappaleissa
   (`docs/status.md`) mainintoja saa olla.
4. Nykyinen kernel/loader kääntyy ja bootaa `bootargs`-kehotteeseen asti kuten
   ennen (vertaa `size 9pc64` ja loaderin `[P2 Lxx]`-rivit).
5. `docs/README.md` kuvaa vain uuden työnkulun.

## Vaihe 2 (vasta vaiheen 1 jälkeen): asennusaikainen osajoukko

Tavoite on koneellisesti johdettu luettelo kaikista 9front-tiedostoista, joita
järjestelmän asentaminen 9frontin omalla tavalla tarvitsee: loader, `9pc64`,
`bootfs.paq` (`bootrc`, `kbdfs` …), `/rc/bin/inst/*` ja jokainen ohjelma,
kirjasto ja otsake, jota ne käyttävät.

- **Staattisesti:** lähdetään `/rc/bin/inst/*`-skripteistä. Kutsutut komennot
  kuljetaan `/sys/src/cmd/...`-lähteisiin, niiden mkfileihin ja kirjastoihin.
- **Dynaamisesti:** oikea asennus overlay-VM:ssä, jossa `inst/start` ajetaan
  `rc -x`-jäljityksellä sarjakonsoliin. Näin jokainen oikeasti ajettu komento
  kirjautuu. Sama skriptattu asennus toimii jatkossa regressiotestinä.
- **Tuotos:** `docs/install-subset.md` (tiedosto, rooli, staattinen vai
  dynaaminen löydös) ja skripti, joka tuottaa listan uudelleen.
- **Avoin päätös käyttäjälle ennen toteutusta:** kopioidaanko osajoukko repoon
  omaksi codebasekseen, vai pidetäänkö nykyinen linja (repossa vain muutetut
  tiedostot plus manifesti)? README:n mukaan aiempi peilikopio vanheni hiljaa.
- **Hyväksymiskriteeri:** osajoukosta rakennettu asennusmedia asentaa
  järjestelmän QEMUssa, ja asennettu järjestelmä käynnistyy. Vasta sen jälkeen
  aletaan muokata asennusohjelmaa.

Samalla menetelmällä voidaan myöhemmin johtaa kehitysosajoukko (kääntäjät,
`mk`, kirjastot). Siihen asti kehitys-VM on tavallinen täysi 9front-asennus,
joka sisältää jo kääntäjät.

## Aloitusohje paikalliselle Claude Code -sessiolle

```
git clone <repo> && cd Plan2001
git checkout claude/clever-wozniak-ci0fxa
claude            # Shift+Tab → plan mode
```

Anna ensimmäiseksi viestiksi: *"Lue docs/plan-linux-env.md ja docs/README.md.
Tee vaihe 1 plan-tilassa: tarkista ensin mitä koneella jo on (paketit, /dev/kvm,
olemassa olevat ISOt ja imaget), älä lataa tai asenna mitään kysymättä."*
