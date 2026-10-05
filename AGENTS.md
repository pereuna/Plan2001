# Ohje koodiagenteille (Codex, Claude): testaus ennen pushia

Tämä repo on Plan2001: 9front-pohjainen järjestelmä, jonka wasm32-ydin
ajaa 9frontin ohjelmia selaimessa (3c/3l-kääntäjä, `plan9/sys/src/9/wasm32`).
Suunnitelma ja vaiheet: `docs/architecture.md` (vaihe D). Lue se ennen
isoja muutoksia.

## Repon kerrokset (päätös 4.10.2026, `docs/plan9-fork.md`)

Plan2001 eriytetään yleisestä Plan 9 -forkista:
- `plan9/`: puhdas, Plan 9 -yhteensopiva fork 9frontin juuren muodossa.
  Siihen kuuluvat modernisoitu boot (UEFI, BootInfo) ja uudet
  arkkitehtuurit (wasm32 selainalustoineen, pc64, myöhemmin arm64 ja
  vrisc). Tavoite on, että sen voi tuoda 9frontiin ja kääntää natiivisti.
- 9Front-2001: koottu puu, joka on 9frontin julkaisu, sen päällä `plan9/`
  ja `plan9/patches` ajettuna.
- `plan2001/`: legacyä rikkovat muutokset ja Plan2001:n palvelut,
  overlayna 9Front-2001:n päälle.

Siirto on tehty 4.10.2026. Uusi tiedosto menee `plan9/`:ään tai
`plan2001/`:een `docs/plan9-fork.md`:n sijoitussääntöjen mukaan, ja
commitissa kerrotaan, kumpaan kerrokseen muutos kuuluu. Älä tuo forkiin
legacyä rikkovaa muutosta. Älä myöskään tee 9frontin tiedostosta
kokonaista kopiota, vaan diff `plan9/patches/`iin. `tools/overlay DIR`
kokoaa molemmat kerrokset yhdeksi 9front-juureksi (`-f`: vain fork).

## Säännöt, joita ei rikota

- **`subset/9front/` on 9frontin julkaisu sellaisenaan** (`tools/9front.release`),
  generoitu. Älä kopioi sinne tiedostoja käsin äläkä muokkaa niitä:
  `tools/subset/make.py` poistaa listaamattomat ja `tools/subset/check`
  hylkää muutetut. Uusi 9front-ohjelma tai -tiedosto: polku ja perustelu
  `tools/subset/extra`an, sitten VM:ssä `tools/subset/derive`,
  `python3 tools/subset/make.py build/subset/amd64 .` ja `tools/subset/check`
  (ks. `docs/install-subset.md`). Jos VM:ää ei ole, kerro se commitissa:
  käyttäjä ajaa derivoinnin.
- **Korjaus 9frontin koodiin** on diff `plan9/patches/9front/`iin (upstreamia
  varten) ja paikattu tiedosto repon `sys/`-puuhun saman polun alle
  (build laittaa `sys/`in VM:n `/sys/src`:n päälle). Ohje:
  `plan9/patches/9front/README`.
- **Vitsit ja sitaatit eivät tule mukaan**: `DENY` tiedostossa
  `tools/subset/derive.py`. Pelit tulevat.
- Tee työ omassa haarassasi; älä pushaa toisen haaraan.

## Ympäristö

- Linux, gcc/clang, Python 3 (`cryptography`-paketti), Node 22.
- Testit ajavat headless-Chromiumia komennolla `chromium`. Jos sitä ei ole
  PATHissa, tee symlinkki (esim. Playwrightin):
  `mkdir -p ~/bin && ln -sf /opt/pw-browsers/chromium-*/chrome-linux/chrome ~/bin/chromium && export PATH=~/bin:$PATH`
- VM-testit tarvitsevat 9front-VM:n (`tools/vm`, QEMU, `~/.cache/plan2001`).
  Ilman sitä ne ohitetaan; sano silloin commitissa, mitä jäi ajamatta.

## Rakennus (monolith/)

Selaimessa on vain wasm32-kone (D7): drawtermin selainosat, host3 ja
Monolithin JS on poistettu. `monolith/tools/pages` kokoaa sivun, jonka
CPU-palvelimen rc-httpd tarjoaa (`tools/vm-cpu --web`, `tools/cpu-live.rc
pages`). `tools/build` rakentaa enää vain 9ptermin (pilven hallinta) ja
natiivin drawtermin.

```
cd monolith
tools/build-libc3      # wasm32-kirjastot (libc, libthread, libsec, libauthsrv ...)
tools/build-9wasm32    # ydin: build/wasm32/kernel/9wasm32.wasm
tools/build-bin3       # ohjelmat ja juuri: build/wasm32/root, root.fs
```

`build-bin3` kertoo lopuksi ohjelmien määrän; jokainen virhe tulostuu
`build-bin3: NIMI:` -rivinä ja skriptin paluuarvo on silloin nollasta
poikkeava. Uusi ohjelma lisätään `build-bin3`:een (yhden tiedoston
komennot listaan `for c in ...`), sen kirjastot `build-libc3`:een.

Natiivi käännös (`docs/plan9-fork.md`): `tools/9front-2001` kokoaa
9Front-2001:n `build/9front-2001`:een. `PLAN9=plan9port tools/native-wasm32`
kääntää siinä 9frontin mkfileillä wasm32:n kirjastot, ytimen ja juuren
9front-ohjelmat. `tools/test-native [NIMI...]` ajaa `test-9wasm32`:n
natiivilla ytimellä ja ohjelmilla. Aja se, kun muutat mkfilejä,
`plan9/patches/9front`ia tai 3c:n/3l:n objektimuotoa. Kun muutat
9frontin tiedostoa, korjaa sekä diff että `plan9/`:n kopio: kokoaja
hylkää puun, jos ne eroavat. Uusi juuren ohjelma lisätään myös
`native-wasm32`:n listaan. `tools/rccheck` tarvitsee niin ikään
`PLAN9`:n (plan9portin rc).

## Testit ennen pushia

Aja aina, kun muutos koskee ydintä, platform.js:ää, kirjastoja tai
juuren ohjelmia:

```
cd monolith
tools/test-9wasm32                 # kaikki (noin 25 min)
tools/test-9wasm32 NIMI [NIMI...]  # vain nämä, esim. rcpu tls notekill
```

Viimeinen rivi `test-9wasm32: ok` = kaikki ajetut menivät läpi. Joka testi
tulostaa `PASS` tai `FAIL`; FAIL:n tuloste on `build/wasmapp-kernel.txt`,
sivun loki `build/wasmapp-kernel.log` ja kuvakaappaus
`build/wasmapp-kernel.png`.

Testien ryhmät (`tools/test-9wasm32`in alussa tarkemmin):
- ydin ja ohjelmat: `c2a bootinfo badblob oldheader disk diskdead echo long fork forkloop rc rci c3a failfork rfmem
  threads preempt rfmemloop failhelper failrfmem init fault procargs`
- ruutu, näppäimistö, hiiri, rio: `clock keys mouse hello dclock bytes rc3
  rci3 rioclock riorc boot paste` (`boot` tarvitsee vga-alifontit, ks. alla)
- D3 tunnistus: `crypto dp9ik dp9iknoas secstore seckeys pagesecstore`
  (+ `dp9ikvm` ja `secstorevm` VM:llä, `secstorevm` vaatii `tools/vm-cpu
  --update`:n jälkeisen secstore-tilin; `secstore`: factotumin avaimet `test-authsrv`in secstoresta,
  `seckeys`: niiden salattu kopio levyllä ja haku siitä ilman verkkoa)
- D4 rcpu: `tls notekill netre nettimeout rcpu rcpuask rcpukeys` (+ `rcpuvm`
  VM:llä; `rcpukeys`: cpu-istunnon factotum on päätteen)
- WebAuthn: `webauthn signup passkeyoffline signupd passkeyc pagelogin` (+ `signupvm`
  VM:llä) (`#W`, virtuaalinen autentikaattori, `test-authsrv`in signupd ja
  passkeyd; `signupd` ja `passkeyc` ajavat oikeat C-daemonit koneessa,
  docs/webauthn.md)
- D7 sovelluksen origin: `app appresume sendq` (webterm:n `/rcpu`-istunto, katkos ja jatko, iso jatko-jono, sivun takaisinkytkentä)
- verkko VM:ää vastaan: `net netloop` (VM:llä)

Muut: `tools/rccheck` (rc-skriptien jäsennys plan9portin rc:llä, aja kun
muutat rc-skriptiä: `/boot`, rc-httpd, `tools/*.rc`; heittomerkki
viestissä, kuten `Plan2001's`, avaa lainauksen),
`tools/test-3c` (kääntäjä, nopea, aja kun 3c/3l muuttuu),
`tools/subset/check` ja `tools/subset/test` (VM: osajoukko ja asennus).

### Uusi testi

- Testiohjelma `plan9/sys/src/9/wasm32/test/NIMI.c` (lisää `build-bin3`:n
  testilistaan), odotettu tuloste `plan9/sys/src/9/wasm32/test/NIMI.out`,
  ajo `tools/test-9wasm32`iin (`want NIMI $names && run NIMI '[argv]'`).
- `.out` alkaa rivistä `ticks in it: ok` ja **sen rivinvaihto on `\r\n`**
  (ytimen konsoli), muut rivit `\n`. Python tekstitilassa kadottaa `\r`:n:
  kirjoita binäärinä tai `printf 'ticks in it: ok\r\n...'`.
- Tulosteen on oltava deterministinen: ei pid-numeroita (MASKPIDS hoitaa
  muodon `prog N:`), ei kilpailevia rivejä kahdesta prosessista.
- Kehotteeseen vastaava syöte: SEND-alkio `["kehote", "rivi\n"]`.
- Verkkoa tarvitseva testi ilman VM:ää: `tools/test-authsrv` (dp9ik-auth
  `/567`, rcpu-rele `/17019`↔`/17999`), `MONOLITH_WEBTERM=18099`.
- Tarkista, että testi oikeasti kaatuu ilman korjaustasi.

## Sudenkuopat

- `tools/test-9wasm32` ajaa `build-bin3`:n vain, jos juurta ei ole
  (`build/wasm32/root/bin/d2`). Kun poistat tai siirrät lähteitä, rakenna
  juuri puhtaalta ennen pushia: `rm -rf build/wasm32/root build/wasm32/o/cmd
  && tools/build-bin3`, ja sitten `tools/pages`. D7:n poisto jäi kerran
  kiinni vasta toisen buildissa (clock.c).
- **Älä muokkaa `tools/test-9wasm32`:ää sen ajon aikana** (bash lukee
  skriptiä ajon aikana: syntaksivirhe keskellä).
- Katkennut ajo voi jättää `tools/serve`n (portit 18092/18093) tai
  `test-authsrv`in (18099) henkiin, ja seuraava ajo puhuu vanhalle:
  `ps -eo pid,args | grep -E 'tools/serve|python3 - 127|test-authsrv'` ja
  lopeta ne. Älä käytä `pkill -f`:ää komentorivillä, joka sisältää saman
  merkkijonon (tappaa oman kuoresi).
- `boot`-testi tarvitsee rion fontin vga-alifontit
  (`build/subset/amd64/src/lib/font/bit/vga/vga.*`, derive tuo ne VM:stä);
  ilman niitä rion ikkunat jäävät tyhjiksi ja testi kaatuu.
- Kaksi saman koneen prosessia, jotka matkivat asiakasta ja palvelinta,
  tarvitsevat erilliset noteryhmät ja nimiavaruudet (`rfork(RFNOTEG|RFNAMEG)`),
  kuten aux/listen antaa oikealle palvelulle.
- Selaimen koneen ytimellä on 64 Mt muistia (image ja keko). Juuren
  arkisto (`root.fs`), BootInfo ja kehyspuskuri ovat sen yläpuolella
  (`docs/boot-abi-wasm32.md`). Jos tulee `no memory for allocb`, etsi
  vuoto tai silmukka ennen kuin kasvatat muistia.
- Koneen levy (D6) on OPFS-tiedosto `sdW0`. Testiselaimella on aina
  tuore profiili, joten levy on tyhjä ja `/boot/disk` reamaa sen. Jos
  testi kirjoittaa glendan kotiin, se menee levylle. Saman originin
  toinen välilehti jää ilman levyä, koska OPFS:n sync handle on
  yksinomainen.
- Initin argv on BootInfon configissa `init=`-rivinä (tokenizen
  lainaus). Argumentissa ei siis voi olla rivinvaihtoa: platform.js
  hylkää sellaisen.
- Taustapalvelin (lib9p:n `postsrv`: mntgen, factotum, ramfs) pitää auki
  ne fd:t, joilla se käynnistettiin. Jos se käynnistetään putken
  kirjoittavan pään sisällä (`{... factotum ...} | sed`), sed ei saa
  EOF:ia, eikä testi pääty. Käynnistä palvelimet ennen putkea.
- Pysyvä VM-levy (`cpu.qcow2`) suljetaan `fshalt`illa. `tools/vm stop`
  tekee sen itse (`NOHALT=1` ohittaa). Pelkkä QEMU:n quit jätti cwfs:n
  kirjoittamatta, eikä levy enää mountannut.

## Commit

- Viesti kertoo mitä ja miksi, repon tyyliin (`git log`).
- Kerro, mitkä testit ajoit ja mitkä jäivät ajamatta (VM, rauta).
- `docs/architecture.md`:n vaiheen tila ajan tasalle, kun vaihe etenee.
