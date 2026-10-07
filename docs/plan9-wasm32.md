# Plan2001, Plan9-wasm32 ja 9front (päätös 7.10.2026)

wasm32-kone siirrettiin omaksi projektikseen 5.10.2026:
[pereuna/plan9-wasm32](https://github.com/pereuna/plan9-wasm32)
(tuotu tämän repon commitista `59ee476`). Se on Plan 9 wasm32:lle ja
pysyy 9front-yhteensopivana. Live-image ja asennin toimivat osoitteessa
https://plan2001.com/plan9-wasm32/.

## Mikä Plan2001 on

- **Oma järjestelmä, ei yhteensopiva Plan 9:n tai 9frontin kanssa.**
  Plan2001 perustuu 9frontiin ja Plan9-wasm32:een ja kopioi niistä
  ideoita, protokollia ja koodia. Kopio on Plan2001:n omaa koodia, ja sitä
  muokataan vapaasti. Se ei ole riippuvuus, joka rakennetaan
  sellaisenaan.
- **Pieni, muokattu osajoukko.** Mukaan otetaan vain se, mitä profiilit
  tarvitsevat.
- **Moderni.** Legacy jää pois: vanhat protokollat, arkkitehtuurit,
  bootloaderit ja niiden varapolut. Jos jotain tarvitaan vain vanhan
  kanssa toimimiseen, sitä ei tuoda.

## Profiilit

| Profiili | Mikä | Arkkitehtuuri ja alusta nyt |
|---|---|---|
| **palvelin** | CPU-, auth- ja tiedostopalvelin: tunnukset, istunnot, sivusto, laskentapooli. CPU-palvelin tarkoittaa myös CR-palvelimia (NPU, GPU jne., `docs/cpu-server-design.md`) | amd64 (UEFI). Myöhemmin arm64, riscv64 ja wasm32 |
| **terminaali** | käyttäjän GUI ("selaintyyppinen" ympäristö) | vain wasm32 selaimessa ja/tai sarjakonsoli |

## Jakelu

Plan2001 on ohjelmistojakelu, josta tehdään jakeluversio kumpaankin
ympäristöön:

- **pc64:** CPU-palvelin (plan2001.com:n palvelin, pilvi ja oma rauta).
- **wasm32:** terminaali.

Myöhemmin tulevat arm- ja RISC-V-portit sekä wasm32:n CPU-palvelin.

## Terminaalin alusta

Terminaalin alusta muuttuu ajan myötä:

1. nyt selain (wasm32-kone sivulla),
2. sitten muokattu, koko ruudun Chromium,
3. lopulta kontti tai minimaalinen "firmware"-unix, jonka jälkeen
   terminaali on **vahvasti autentikoitu ja autorisoitu HMI-istunto**,
   jonka käytössä on riittävä GUI-resurssi. Alla oleva unix on vain
   firmwarea, kuten selain nyt: istunto ei näe sitä.

Terminaali ei siis ole laite vaan istunto: käyttäjä tunnistetaan
(passkey, dp9ik), istunnolle annetaan oikeudet, ja sillä on GUI-resurssi:
näyttö ja syöttölaitteet. Sarjakonsoli on terminaali ilman GUI-resurssia.
Muita terminaalialustoja (natiivi rio PC:llä, drawterm) ei tehdä.

## Suhde Plan9-wasm32:een ja 9frontiin

- **Koodi kulkee kopiona.** Plan2001 ottaa Plan9-wasm32:sta tai 9frontista
  sen, mitä tarvitsee. Commit-viestiin kirjataan lähde (repo ja commit),
  ja sen jälkeen koodi on Plan2001:n.
- **Takaisin menevät vain bugikorjaukset.** Kun Plan2001:ssä korjataan
  vika, joka on myös Plan9-wasm32:ssa, korjaus tarjotaan sinne
  (Plan9-wasm32:n omien sääntöjen mukaan). Plan2001:n ominaisuuksia,
  muutoksia ja poistoja ei viedä sinne.
- **Plan9-wasm32:n uudet ominaisuudet** otetaan Plan2001:een harkiten,
  kopiona, jos profiilit tarvitsevat niitä. Esimerkiksi 9frontin
  asennin ja ISO-live-image eivät kuulu Plan2001:een.

## Mitä tästä seuraa repossa

- Tämän repon wasm32-koodi (`plan2001/sys/src/9/wasm32`, 3c/3l, wasm32:n
  kirjastot ja `monolith/`:n työkalut) on Plan2001:n omaa. Sitä ei
  poisteta eikä korvata Plan9-wasm32:n rakennuksella. Se on eriytynyt
  tuonnin jälkeen: 48 tiedostoa eroaa, 105 on samoja.
- Aiempi kerrosjako, jossa `plan9/` oli puhdas 9front-yhteensopiva fork ja
  9Front-2001 koottu puu (`docs/plan9-fork.md`, päätös 4.10.), ei enää
  ole tavoite. Yhteensopiva fork on nyt Plan9-wasm32. Puu on yksi
  (vaihe 3, alla).
- `subset/` on edelleen 9frontin julkaisu sellaisenaan. Se on lähde,
  josta kopioidaan.

## Vaiheet

1. Päätös kirjattu (tämä dokumentti, README ja AGENTS.md).
2. **Erot Plan9-wasm32:een** (tehty 7.10., alla). Käydään läpi tuonnin jälkeiset erot molempiin
   suuntiin:
   - Plan2001:n bugikorjaukset, jotka kuuluvat myös Plan9-wasm32:een,
     tarjotaan sinne.
   - Plan9-wasm32:n korjaukset ja tarpeelliset parannukset kopioidaan
     tänne.
3. **Puun rakenne** (tehty 7.10., alla). `plan9/` ja `plan2001/`, 9Front-2001:n kokoaja,
   `plan9/patches` ja diff-sääntö korvataan yhdellä Plan2001:n puulla.
   Natiivi käännös säilyy.
4. **Legacy pois** (lista 7.10.: `docs/legacy.md`). Listataan, mitä profiilit eivät tarvitse
   (arkkitehtuurit, bootloaderit, protokollat, ohjelmat), ja poistetaan
   ne kohta kerrallaan testien kanssa.
5. `docs/status.md`, `docs/architecture.md` ja AGENTS.md vastaamaan
   profiileja.

## Vaihe 2: erot Plan9-wasm32:een (7.10.2026)

**Lähtökohta.** Plan9-wasm32 tuotiin commitista `59ee476`. Se on
`games`-haaran kärki, joka on neljä committia mainin (`fc40f74`) edellä,
eikä sitä ole yhdistetty mainiin. Tuonti itse oli tavu tavulta sama,
kun pc64-, arm64- ja boot/efi-hakemistoja ei lasketa. Plan2001:n mainissa
ei ole muutettu `plan9/`:ää haarautumisen jälkeen, joten erot ovat:

**`games`-haaran commitit** (45cc42f, d77b8f5, 0823c57, 59ee476): 9frontin
pelit, koko 9frontin julkaisu wasm32:lle (`tools/dist-wasm32`) ja
`/boot/install`, joka asentaa sen selaimen levylle (levy 2 Gt, distd).
Nämä ovat 9front-jakelua, joka on nyt Plan9-wasm32:n työtä, eikä niitä
oteta. Ainoa korjaus otetaan:
- **3l tunnistaa arkiston taikaluvusta**, ei vain `.a`-päätteestä
  (`cc.a$O`). Otettu.

**Plan9-wasm32:n omat commitit** (0da1dbd–819d14d):

| Muutos | Laji | Plan2001:een |
|---|---|---|
| 3l: funktion osoite on `TEXTBASE` (0xF0000000) + taulukon indeksi. rc erottaa koodinsa kokonaisluvuista funktioiden osoitteilla, ja pienet indeksit saivat sen keskeyttämään skriptin hiljaa (testi `rcif`). Mukana platform.js:n `fidx` | **bugi** | otettu (9f14449); `rcif` kaatui Plan2001:ssä ilman sitä |
| levy-Worker odottaa enintään 3 s edellisen sivun sync handlea: uudelleenladattu sivu jäi ilman levyä | **bugi** | otettu (5823c82) |
| host-mpc luki desimaaliluvut heksana (plan9portin strtomp) | bugi, ei koske | Plan2001:n natiivi käännös ei aja mpc:tä vaan käyttää julkaisun valmiita tiedostoja |
| d4 -B kirjoittaa 4 Mt:n paloina, sendq 24 Mt | testin vakaus | ei nyt; otetaan, jos `sendq` tai `d4` alkaa kaatuilla |
| `#c/reboot` (fshalt -r): levyt synkataan ja sivu latautuu | ominaisuus | ehdokas terminaalille myöhemmin |
| `#¶` swap (muistitilasto) | ominaisuus | ehdokas (stats) myöhemmin |
| devsdw: useita yksiköitä, `#S/sdctl`, ISO `sdW1` (HTTP range), osiot | 9front-asennus | ei |
| `/boot/init` 9frontin bootrc:nä, `#/` juurena, `inst/start`, bootsetup, boot levyltä, live-image | 9front-asennus | ei |
| sivu ottaa `plan9.ini`:n palvelimelta Plan2001:n `/app`-, `/secstore`- ja `/login`-hakujen sijaan | rakenne | ei nyt; Plan2001:n omat haut jäävät |
| WebAuthn `#W` → `#ω`, Plan2001:n nimet ja dokumenttiviitteet pois | nimet | ei |

**Plan2001:stä Plan9-wasm32:een:** haarautumisen jälkeen Plan2001:ssä
ei ole tehty alustan bugikorjauksia, joten vietävää ei ole.
Plan2001:n jälkeiset muutokset koskevat palveluja (`websession`,
`weblimitd`, `signupd` ja muita) eivätkä alustaa.

Seuraavalla kerralla erot lasketaan tästä eteenpäin: Plan9-wasm32
`819d14d` ja Plan2001:n tämän vaiheen commit.

## Vaihe 3: yksi puu (7.10.2026)

- `plan9/`:n tiedostot siirrettiin `plan2001/`:een samoihin polkuihin.
  Molemmissa oli vain `sys/src/9/pc64/pc64`. Overlayssa voimassa ollut
  `plan2001/`:n versio (wg mukana) jäi.
- `plan9/patches/9front` poistettiin (hjfs:n auth, libc:n 9syscall ja
  libthreadin mkfile wasm32:lle). Paikatut tiedostot ovat
  `plan2001/`:ssa kokonaisina, ja samat diffit ovat Plan9-wasm32:ssa.
- `tools/9front-2001` on nyt `tools/tree` (`build/tree`): julkaisu
  (`subset/9front`) ja `plan2001/` sen päällä. `tools/overlay` antaa
  `plan2001/`:n tiedostot (`-f`:ää ei enää ole).
- **Käytös ei muuttunut.** wasm32-koneen natiivi käännös käyttää
  julkaisun `kbdfs.c`:tä ja `devcons.c`:tä kuten ennenkin, koska
  Plan2001:n versiot on tehty pc64:n konsolille (rivieditori
  ESC-sekvensseineen). Terminaalin konsoli on oma työnsä.
- `monolith/`:n ja `tools/`:n polut, AGENTS.md ja dokumentit
  päivitettiin. `docs/plan9-fork.md` on historiaa.
- Seuraavaksi voi miettiä, siirretäänkö `monolith/tools` `tools/`:iin
  ja `plan2001/` juureen. Ne ovat nimiä ja polkuja, eivät rakennetta.

## Avoimet

- Mitkä 9frontin protokollat ja palvelut ovat "legacyä" Plan2001:lle?
  Ehdokkaita ovat ne, joita palvelin- ja terminaaliprofiili eivät käytä,
  esimerkiksi p9sk1 (dp9ik riittää), vanha `cpu` (rcpu riittää),
  `telnet`, `ftpfs`, 386 ja BIOS-polut. Lista tehdään vaiheessa 4.
- Pysyykö 9P2000 sellaisenaan vai muuttuuko sekin (esim. 2001P,
  `docs/ai/2026-09-30`)?
