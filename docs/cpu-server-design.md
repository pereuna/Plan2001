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

## Selain: pääte, CR tai molemmat (30.9.)

Selain voi olla kahdessa toisistaan riippumattomassa roolissa: Plan2001-pääte
(Monolith/drawterm, `term.kone`) ja laskentaresurssin tarjoaja
(`compute.kone`). Roolit ovat pääte, CR tai molemmat. Vastavuoroisuutta
ei vaadita: resurssin tarjoaminen ja poolin käyttäminen ovat eri
oikeuksia, eli eri kykyjä (`docs/app-origins.md`: `rcpu`, `cr`,
`compute`).

```
                         PLAN2001 CPU SERVER
                       processes / namespaces
                    ┌─────────┴─────────┐
                 terminal           /compute  (policy compute)
                 sessions              │
                    │           crsrv: CR scheduling
        ┌───────────┼───────────┐       │
     Browser A   Browser B   Browser C  ◄┘
     terminal    CR only     terminal
       + CR                     + CR
```

**Koneella on 0 tai 1 CR ja 0..N päätettä.** Compute-sivun välilehti
tarjoaa laskentaa vain, kun se pitää Web Lockia `plan2001-cr`; saman
selaimen toinen compute-välilehti odottaa. Terminaalivälilehtiä voi olla
useita, ja jokainen on oma rcpu-istuntonsa ja nimiavaruutensa.

**Yksi provider on yksi globaalisti ajoitettu CR**, ei yksi CR ydintä
kohden. CR:n workers-luku on vain tieto.

**Ajoitus on kaksitasoinen:**
- crsrv päättää, mille CR:lle työ annetaan. CR ilmoittaa `hello`ssa
  credit-arvonsa, eli montako työtä se ottaa kerralla oma jononsa mukaan
  luettuna, ja voi muuttaa sitä viestillä `credits N`. crsrv ei pidä
  CR:llä enempää töitä kuin credit sallii, ja valitsee CR:n, jolla on
  eniten vapaata.
- Provider päättää, millä workerilla (myöhemmin GPU:lla tai NPU:lla) työ
  ajetaan. Selaimen oma jono ja web workerit ovat `cr.js`:ssä. Selain
  ilmoittaa credit-arvoksi workerit + ¼ (ainakin 1), jotta mikään worker
  ei odota verkkoa.

**Nimiavaruus:**
- `/global/compute/ID/`: `type`, `api`, `workers`, `credits`, `owner`,
  `state` (ready/full) ja `jobs`. Tunnisteen antaa palvelin (6
  heksanumeroa), ei CR itse.
- `/compute` on prosessin näkymä poolista. Se sidotaan istuntoon vain
  `compute`-kyvyllä.

**Luottamus:**
- Provider on epäluotettava, työn syöte on julkista ja tulos
  verifioimaton.
- Selaimen hiekkalaatikko (web worker + WebAssembly) suojaa provideria.
- Verifiointi suojaa käyttäjää: `crsrv -v none|duplicate|2of3`.
  - `duplicate`: työ ajetaan kahdella CR:llä ja hyväksytään, jos tulokset
    ovat samat; muuten työ epäonnistuu.
  - `2of3`: kun kaksi tulosta eroaa, kolmas CR ratkaisee.
  - Tuloksia verrataan SHA-1:llä ilman lokia.
- Työn tunniste on hash komennosta ja syötetiedostoista (`JID.SEQ`, jossa
  SEQ erottaa ajot).
- Kadonneen CR:n työt palaavat jonon kärkeen.
- `verify local` (CPU-palvelin laskee itse) on vielä tekemättä.

**Suostumus:** compute-sivu ei tarjoa mitään ennen kuin käyttäjä painaa
START SHARING. Sivulla näkyvät säiemäärä (esim. 11 / 12), työt, jaettu
aika ja STOP; jaon aikana taustalla putoavat merkit. Testeissä on
`?autostart`, ja `?corrupt` tekee tabista tarkoituksella virheellisen CR:n.

**Päätteenä ja CR:nä samaan aikaan:** päätteen käyttäjä avaa
`compute.kone`:n toiseen välilehteen. Kyky pysyy compute-originilla.
Päätteeseen upotettu jako-painike (iframe tai host.js) on tekemättä.

**Päivitys ilman uudelleenkäynnistystä:** `tools/cpu-live SOCK` päivittää
käynnissä olevan CPU-palvelimen 9pterm-istunnon kautta: sivut (+ .gz),
`lib/app`, webterm (aux/listen ajaa sen yhteyskohtaisesti), rcc ja crsrv,
joka käynnistetään uudelleen `cpustart`in argumenteilla. CR:t yhdistävät
itse uudelleen, ja kesken olevat työt epäonnistuvat asiakkailleen.
Pilvessä ydintä ei tarvitse vaihtaa.

**Testit** (paikallinen cirno, `monolith/tools/test-compute`, 3 × 2
workeria, acme):

| crsrv | CR:t | tulos |
|---|---|---|
| `-v none` | 3 hyvää | 21 työtä kolmelle CR:lle, 9 credit-arvoa, objektit samat kuin 6c:n |
| `-v duplicate` | 3 hyvää | 42 ajoa, 21 verifioitu |
| `-v 2of3` | 1 viallinen, 2 hyvää | 18 ristiriitaa, kaikki 21 verifioitu, objektit oikein |
| `-v duplicate` | 1 viallinen, 2 hyvää | 7 hylätty (differed), väärää objektia ei hyväksytty |

## Ensimmäinen koe: käännös selainten CR:illä (28.9.)

Haara `compute-pool`. Tavoite: käännöstyö jaetaan muutamalle asiakaskoneelle
selaimen kautta.

- **CR selaimessa:** `https://compute.kone:17443/cr.html` (Monolith,
  `monolith/web/cr.*`; laskentapoolin origin, `docs/app-origins.md`).
  Selain tarjoaa laitteensa kerran: saman selaimen compute-välilehdistä vain
  lukon (Web Locks) haltija on CR, muut odottavat. Workereita on oletuksena
  laitteen säikeet miinus yksi, joten yksi jää käyttäjälle (`?workers=N`).
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
