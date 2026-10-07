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
| **palvelin** | CPU-, auth- ja tiedostopalvelin: tunnukset, istunnot, sivusto, laskentapooli | amd64 (UEFI). wasm32 voi myöhemmin olla myös CPU-palvelin |
| **terminaali** | käyttäjän GUI ("selaintyyppinen" ympäristö) | vain wasm32 selaimessa ja/tai sarjakonsoli |

Terminaalin alusta muuttuu ajan myötä:

1. nyt selain (wasm32-kone sivulla),
2. sitten muokattu, koko ruudun Chromium,
3. lopulta esim. Linuxin tai OpenBSD:n prosessi kontissa, joka ajaa
   selaintyyppistä ympäristöä.

Terminaali on siis aina wasm32-kone selaintyyppisessä ympäristössä tai
sarjakonsoli. Muita terminaalialustoja (natiivi rio PC:llä, drawterm)
ei tehdä.

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

- Tämän repon wasm32-koodi (`plan9/sys/src/9/wasm32`, 3c/3l, wasm32:n
  kirjastot ja `monolith/`:n työkalut) on Plan2001:n omaa. Sitä ei
  poisteta eikä korvata Plan9-wasm32:n rakennuksella. Se on eriytynyt
  tuonnin jälkeen: 48 tiedostoa eroaa, 105 on samoja.
- Aiempi kerrosjako, jossa `plan9/` on puhdas 9front-yhteensopiva fork ja
  9Front-2001 koottu puu (`docs/plan9-fork.md`, päätös 4.10.), ei enää
  ole tavoite. Yhteensopiva fork on nyt Plan9-wasm32. Plan2001:n puun
  rakenne yksinkertaistetaan omana vaiheenaan (alla).
- `subset/` on edelleen 9frontin julkaisu sellaisenaan. Se on lähde,
  josta kopioidaan.

## Vaiheet

1. Päätös kirjattu (tämä dokumentti, README ja AGENTS.md).
2. **Erot Plan9-wasm32:een.** Käydään läpi tuonnin jälkeiset erot molempiin
   suuntiin:
   - Plan2001:n bugikorjaukset, jotka kuuluvat myös Plan9-wasm32:een,
     tarjotaan sinne.
   - Plan9-wasm32:n korjaukset ja tarpeelliset parannukset kopioidaan
     tänne.
3. **Puun rakenne.** `plan9/` ja `plan2001/`, 9Front-2001:n kokoaja,
   `plan9/patches` ja diff-sääntö korvataan yhdellä Plan2001:n puulla.
   Natiivi käännös säilyy.
4. **Legacy pois.** Listataan, mitä profiilit eivät tarvitse
   (arkkitehtuurit, bootloaderit, protokollat, ohjelmat), ja poistetaan
   ne kohta kerrallaan testien kanssa.
5. `docs/status.md`, `docs/architecture.md` ja AGENTS.md vastaamaan
   profiileja.

## Avoimet

- Mitkä 9frontin protokollat ja palvelut ovat "legacyä" Plan2001:lle?
  Ehdokkaita ovat ne, joita palvelin- ja terminaaliprofiili eivät käytä,
  esimerkiksi p9sk1 (dp9ik riittää), vanha `cpu` (rcpu riittää),
  `telnet`, `ftpfs`, 386 ja BIOS-polut. Lista tehdään vaiheessa 4.
- Pysyykö 9P2000 sellaisenaan vai muuttuuko sekin (esim. 2001P,
  `docs/ai/2026-09-30`)?
