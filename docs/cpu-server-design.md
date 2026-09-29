# CPU-palvelin ja laskentapooli

> Plan2001:n CPU-palvelin ei ole kone, jossa suurin laskentateho on. Se on
> kone, joka omistaa laskentaympäristön. Käyttäjien päätteet eivät ole
> vain terminaaleja, vaan CPU-palvelimeen dynaamisesti liittyviä
> laskentasolmuja.

Tämä on Plan2001:n oma arkkitehtoninen ajatus, ei vain modernisoitu Plan 9.
Se on ratkaistava ennen Monolithin vaihetta 3b (`monolith/docs/architecture.md`).
Selainsovellusten turvamalli (origin = sovellus, nimiavaruuspohjat,
`/global/compute` ja `/compute`): `docs/app-origins.md`.

## Käänne Plan 9:stä

```
Plan 9 (1990)                         Plan2001
monta päätettä                        monta tehokasta päätettä
      ↓                                     ↕
iso CPU-palvelin                      tavallinen CPU-palvelin
      ↓                                     ↕
jaettu kallis laskenta                päätteiden yhteinen laskenta

käyttäjät → laskenta                  käyttäjät ↔ laskenta
```

Käyttäjä ei ole enää vain resurssin kuluttaja: jokainen liittyvä pääte voi
tuoda järjestelmään laskentakapasiteettia.

## Käsitteet

- **CPU-palvelin** säilyy Plan 9:n merkityksessä: natiivi Plan2001-kone,
  jossa ovat kernel, prosessit, nimiavaruudet, tiedostojärjestelmä,
  tunnistautuminen, ajoitus ja pääte- ja istuntopalvelut. Se omistaa
  järjestelmän tilan, käyttäjät ja resurssien koordinoinnin. Se voi olla
  pieni (Raspberry Pi) tai iso; sen ei tarvitse olla laskentateho.
- **Pääte** on käyttäjän laite: selain (Monolith), natiivi pääte
  (drawterm) tai toinen Plan2001-kone. Käyttäjän data on päätteen
  nimiavaruudessa ja käyttäjän hallinnassa.
- **Laskentaresurssi (Compute Resource, CR)** on mikä tahansa, mikä laskee ja suorittaa:
  CPU-säikeet, GPU, NPU tai jokin, jota ei vielä ole. Plan2001 ei oleta
  CR:n toteutuksesta muuta kuin että sille voi antaa työn ja siltä saa
  tuloksen.
- **Laskentapooli** on CPU-palvelimeen liitettyjen CR:ien joukko. CPU-palvelin
  jakaa sen kaikille omille asiakkailleen.

## CR:ien lähteet

CR on abstraktio, jonka takana voi olla:

| Lähde | Esimerkki | Liittyminen |
|---|---|---|
| paikallinen kortti | GPU- tai kiihdytinkortti CPU-palvelimessa | Plan2001:n ajuri |
| natiivi solmu | Linux-kone, joka jakaa CPU:nsa tai GPU:nsa | solmun palvelu, joka liittyy CPU-palvelimeen |
| selain | WASM-workerit, WebGPU, WebNN | Monolithin välilehti, ilman asennusta |
| tuleva laite | esim. kielimallitranspuuteri (LLMT) | ajuri tai palvelu; sama CR-rajapinta |

Uusi CR-tyyppi ei saa vaatia muutosta nimiavaruuteen tai ajoittajaan: se on
uusi `type`- ja `api`-arvo.

Selain on helpoin lähde: se hoitaa hiekkalaatikon, säikeet, GPU:n, muistin
ja salatun yhteyden, eikä päätteelle asenneta ajuria tai palvelua.
Välilehti ilmoittaa liittyessään, mitä se tarjoaa (esim. 24 WASM-workeria,
WebGPU-sovitin ja sen muisti, NPU), ja CPU-palvelin rekisteröi ne.
Resurssit ovat ohimeneviä: kun välilehti sulkeutuu, sen CR:t katoavat
poolista, mikä sopii Plan 9:n tapaan ajatella resursseja.

## Nimiavaruus

Resurssi tulee prosessin näkyviin nimiavaruuden polun kautta, kuten Plan
9:ssä, mutta resurssi on nyt myös laskentakykyä. Näkymiä on kaksi:

- **`/global/compute`** sisältää kaikki CPU-palvelimeen liitetyt CR:t.
  Ajoittaja näkee sen kokonaan.
- **`/compute`** käyttäjän nimiavaruudessa: politiikan sallima osa, esim.
  `/compute/shared/*` ja omat `/compute/self/*`, mutta ei toisen käyttäjän
  CR:ien hallintaliittymää.

Jokainen CR on hakemisto, joka kertoo ominaisuutensa tiedostoina:

```
/global/compute/217/type      browser-gpu
/global/compute/217/api       webgpu
/global/compute/217/memory    8192M
/global/compute/217/owner     session-123
/global/compute/217/state     idle
```

Ajoittajan ei tarvitse tietää, onko CR Chrome-selaimessa Helsingissä vai
PCIe-väylällä CPU-palvelimessa. Resursseja ei hallita erillisellä
klusterinhallinnalla, vaan ne ovat nimiavaruudessa.

## Työt

Käyttötapaukset, joiden pitää sopia malliin:

- ohjelmien kääntäminen rinnakkain (`mk` jakaa käännökset CR:ille)
- matriisilaskenta, CAD
- FEM, 3D
- LLM-päättely

**Data pysyy käyttäjällä.** Vain laskentaresurssi jaetaan: työn syötteet
ja tulokset ovat käyttäjän nimiavaruudessa ja käyttäjän hallinnassa, eikä
CPU-palvelin tai CR omista niitä.

**Tulos on atominen.** Työn tulos on joko valmis tai sitä ei ole: `-o
foo.o` syntyy kokonaisena tai ei lainkaan, eikä puolikas tulos näy koskaan
käyttäjän nimiavaruudessa. Tämä on edellytys sille, että katoavan CR:n
(suljettu välilehti) työ voidaan antaa toiselle CR:lle.

**Kaista ei ole CPU-palvelimen ongelma.** CPU-palvelin koordinoi; raskas
data kulkee käyttäjän ja CR:n välillä, ja tulokset ovat usein pieniä
("osta osaketta RTH098", "FEM: 123.56 Nm"). Ethernet ja pieni
CPU-palvelin riittävät koordinointiin.

## Luottamus ja suostumus

- Aluksi CR:iä tarjoavat vain vapaaehtoiset, jotka tuntevat riskit.
- Luottamuskysymykset (kenen työtä CR ajaa, voiko tulokseen luottaa, näkeekö
  CR:n omistaja käsittelemänsä datan) jätetään ensimmäisestä toteutuksesta
  pois, mutta rakenne pitää ne mahdollisina: CR:llä on omistaja (`owner`),
  käyttäjän näkymä on politiikan rajaama (`/compute`), ja työ ja data
  pidetään erillään, jotta eristystä, varmistusta (esim. sama työ kahdelle
  CR:lle) ja rajoja voidaan lisätä myöhemmin.
- CR:n tarjoaja päättää, paljonko ja milloin se antaa kapasiteettiaan.

## Suhde Monolithiin

- Monolithin välilehti on sekä ohjelman suoritusympäristö että laskentasolmu.
  Vaiheen 3 ajatus (suoritus selaimessa on ohimenevää, Plan2001:n prosessi
  ja tila pysyvät) on tämän mallin erikoistapaus.
- drawterm tuo jo nyt päätteen laitteet CPU-palvelimelle (`/mnt/term`);
  CR:t ovat sama ajatus laajennettuna laskentaan. webtermin istunnot ja
  `apps` ovat valmiiksi rekisteri, josta `/global/compute/<istunto>` voi
  syntyä.
- Vaiheen 3b vaihtoehto A (näyttö ja piirto CPU-palvelimella) sotii tätä
  mallia vastaan: se siirtäisi laskennan juuri sille koneelle, jonka ei
  tarvitse olla laskentateho.

## Avoimet kysymykset

- Työn muoto: mitä CR:lle annetaan (esim. WASM-moduuli tai WebGPU-shader ja
  syötteet 9P-tiedostoina) ja miten tulos palaa atomisesti.
- CR:n rajapinta nimiavaruudessa: `ctl`-, `job`- ja tulostiedostot, joita
  kaikki lähteet (ajuri, natiivi solmu, selain, tuleva CR) toteuttavat.
- Ajoittaja: miten työt jaetaan, kun CR:t tulevat ja menevät.
- Politiikka: mitä käyttäjän `/compute` näyttää.

## Ensimmäinen koe: käännös selainten CR:illä (28.9.)

Haara `compute-pool`. Tavoite: käännöstyö jaetaan muutamalle asiakaskoneelle
selaimen kautta.

- **CR selaimessa:** `https://compute.kone:17443/cr.html` (Monolith,
  `monolith/web/cr.*`; laskentapoolin origin, `docs/app-origins.md`).
  Välilehti liittyy pooliin, ja sen web workerit ajavat 9frontin C-kääntäjää
  `6c` WebAssemblyna (`monolith/tools/build-cc wasm`: 9frontin cc, 6c ja
  libbio, `monolith/third_party/9cc`, POSIX-liima `monolith/cc9`,
  `6c.wasm` 218 kt). Välilehti on ohimenevä: kun se sulkeutuu, CR katoaa.
- **CPU-palvelin:** `crsrv` (`sys/src/cmd/crsrv.c`) kuuntelee CR:iä
  (tcp 17030; selaimen yhteys tulee webtermin WebSocketin `/17030` kautta) ja
  tarjoaa poolin nimiavaruuteen: `/srv/compute` → palvelimen `/global/compute`
  ja sovelluksen nimiavaruudessa `/compute` (`lib/app/term/namespace`):
  `status`, `cc` (työt) ja jokaiselle CR:lle `N/{type,api,workers,owner,state,jobs}`.
  CR saa liittyessään palvelimen otsikot (`/sys/include`, `/amd64/include`).
- **Käyttäjä:** `rcc` (`sys/src/cmd/rcc.c`) on `6c`:n tilalla: `NPROC=12 mk
  'CC=rcc'`. Työ sisältää lähteen ja sen hakemistojen `.h`-tiedostot, eli
  data tulee käyttäjän nimiavaruudesta. CR kääntää samannimisessä
  hakemistossa kuin käyttäjä, joten objekti on sama kuin palvelimen `6c`:n.
  Tulos kirjoitetaan atomisesti (`NAME.tmp` → `NAME`). Jos poolissa ei ole
  CR:iä, `rcc` ajaa `6c`:n itse. Katoavan CR:n työ annetaan toiselle.
- **Testi:** `monolith/tools/test-compute`: kolme headless-Chromium-välilehteä
  (2 workeria kukin), acme käännetään VM:llä ensin `6c`:llä ja sitten
  poolissa samassa hakemistossa. Tulos: työt jakautuivat kaikille kolmelle
  CR:lle (6, 6, 9), ja kaikki 21 objektia ja linkitetty `6.out` ovat tavu
  tavulta samat. PASS.
- **Rajat:** vain `6c`. Otsikoista mukana ovat vain lähteen, nykyisen ja
  `-I`-hakemistojen `.h`:t. Ei tunnistautumista (`crsrv -k` ja `#key=` ovat
  vain vahinkoyhteyksiä vastaan), ei luottamusta tuloksiin, eikä
  keskeytettyjen töiden uudelleenyrityksiä ole vielä testattu. Plan 9:n
  kääntäjä on nopea, joten acmen kokoisessa työssä pooli ei vielä nopeuta
  (paikallisesti noin 1 s, poolissa noin 1 s). Hyöty tulee raskaista töistä ja
  suurista käännöksistä.
