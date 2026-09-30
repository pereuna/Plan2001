# AI-etäkäyttö, 9P, 2001P ja drawterm — jatkomuistio

Tämä hakemisto säilyttää 30.9.2026 käydyn suunnittelukeskustelun ja sen
muistiot komentorivi-Codexia ja muita projektin kehittäjiä varten.
Dokumentointihetken tarkastettu lähdekoodipohja on Plan2001 commit 807a12a.
Dokumentaatio ei tarkoita suunnitelman käyttöönottoa tai kaikkien ehdotusten hyväksymistä.

## Lue tässä järjestyksessä

1. Tämä jatkomuistio: nykyinen tilanne ja avoimet päätökset.
2. [Drawterm-katselmointi](drawterm-review.md): historia, versiot, löydökset ja näyttö.
3. [2001P v0.1](2001p-v0.1.md): pysyvä tehtäväkanta, worker-pooli ja binäärinen rajapinta.
4. [Keskustelutallenne](conversation.md): keskustelun eteneminen ja perustelut.

Projektin nykyistä toteutusta kuvaavat myös [cloud.md](../../cloud.md),
[cpu-server-design.md](../../cpu-server-design.md),
[app-origins.md](../../app-origins.md) ja
[Monolithin roadmap](../../../monolith/docs/roadmap.md).

## Miten jatkaa Codex CLI:ssä

Selain-/API-keskustelu ei siirry itsestään Codex CLI:n keskustelumuistiin.
Commitoidut tiedostot siirtyvät tavallisen Gitin kautta. Repon juuressa:

```sh
git pull --ff-only
codex "Lue docs/ai/2026-09-30/README.md sekä sen linkittämät muistiot. Jatka AI-etäkäytön ja 2001P:n suunnittelua niiden pohjalta."
```

Uusi Codex-istunto saa näistä kontekstin, mutta ei aiemman istunnon
prosessimuistia, selainta, tunnuksia, työkaluistuntoja tai väliaikaisia tiedostoja.

## Käyttäjän tavoite

- Nopeasti havaittavat virheet ja reaaliaikainen tuloste; pieni virhe ei saa johtaa minuuttien odotukseen.
- Natiivi AI-komento- ja tiedostoyhteys ilman Chromiumia, kuvakaappauksia tai näppäimistön simulointia.
- Binäärinen 2001P-rajapinta ja kohdekoneen pysyvä tehtäväkanta.
- Kohdekoneella rinnakkainen worker-pooli; hyväksytty ajo jatkuu asiakkaan verkkokatkosta huolimatta.
- Tyypitetyt yleiset AI-operaatiot sekä runsas, rakenteinen loki.
- Drawterm nähdään mahdollisena väliaikaisena yhteensopivuusratkaisuna; sen korvaaminen on harkittava.

## Todettu nykytilanne

- Pilvipalvelun term-osoite on https://term.cpu.plan2001.com/.
- HTTPS/TLS ja kirjautuminen glenda-käyttäjänä toimivat keskustelun kokeissa.
- Palvelimella ei ole selainta. Natiivi `tlssrv`/`webterm` tarjoaa HTTPS/WSS-reitin.
- Monolithin `drawterm.wasm` toimii käyttäjän selaimessa. Sen tarjoamat konsoli-/näyttöresurssit ovat eri asia kuin palvelimen tiedostopalvelu.
- Selainten tarjoama CR-laskentapooli on erillinen toiminto; AI:n natiivi asiakas voi toimia sen rinnalla.
- 9P toimii jo. Natiivin Linux-asiakkaan toimivaa yhteyttä pilven nykyiseen WSS-reittiin ei tämän keskustelun aikana toteutettu.
- Plan 9/9front tarjoaa `cpu`/`rcpu`-etäkäytön. Natiivissa drawtermissa on `-G` ja `-c`.
- `webterm` käyttää omaa sovellusskriptiään; tavallisen drawtermin `-c` ei siksi välttämättä toimi sellaisenaan WSS-reitin kautta.
- CPU-palvelin ajaa prosessit, FS-palvelin palvelee tiedostot 9P:llä. Nykyisen pilvikuvan dokumentaatio kuvaa paikallista cwfs-palvelua.
- Acme käännettiin erillisessä `/tmp/acme-ai-test-20260930`-hakemistossa. `mk` onnistui; `6.out` oli 501810 tavua ja `amd64 plan 9 executable`. Sitä ei asennettu järjestelmän Acmen päälle.
- Chromium-kokeessa konsoliteksti saatiin ilman kuvakaappauksia lukemalla WSS:n 9P `Twrite`-viestit, jotka kohdistuivat `cons`-tiedostoon. Koe ei vielä poistanut Chromiumia.

Pilven tila ja tilapäisen käännöshakemiston olemassaolo on tarkistettava
uudelleen ennen niiden käyttöä uudessa istunnossa. Salasanaa ei tallennettu repoon.

## Keskeiset havainnot

`tools/9run` puskuroi tavallisen komennon tulosteen lopetusmerkkiin/timeoutiin
asti. Timeout ei itsessään keskeytä vieraskoneen prosessia. `tools/build.sh`
käyttää oletuksena 900 sekunnin aikarajaa.

`sys/src/cmd/crsrv.c` tarjoaa työjonon ja CR-kapasiteetin seurannan, mutta
tehtävät ja tulokset ovat muistissa. CR:n kadotessa työ voidaan jonottaa
uudelleen. Mielivaltaisen EXECin pysyvyyteen ja sivuvaikutuksiin tarvitaan
2001P-luonnoksessa kuvattu tarkempi yritys- ja palautumismalli.

Plan2001:n drawterm on pinnattu 9front-commitiin `64dcc24` (12.9.2026).
Uusin katselmoitu upstream oli `2840502` (26.9.2026); siinä toistettiin uusi
piirtoparserin rajatarkistusvirhe. Muita löydöksiä: puuttuvat 9fansin vuoden
2024 merkkijonomuotoilukorjaukset, selainportin hiljainen syötteen pudotus,
WSS-kättelyn puuttuva oma aikaraja ja puutteelliset build-riippuvuudet.
Katso katselmoinnista, mikä toistettiin ja mikä todettiin vain koodista.

## 2001P:n suunnitteluperiaatteet

Hyväksymiskuittaus vasta pysyvän tallennuksen jälkeen. Sama submission_key
palauttaa saman tehtävän, vaikka kuittaus katoaisi. Tila, lokikursori ja
artefaktit eivät kuulu TCP/WebSocket-istunnon elinkaareen. Verkkokatko ei
peruuta ajoa. Koneen uudelleenkäynnistys säilyttää tehtävätiedot, mutta ei
mielivaltaisen prosessin muistia. Yleiselle EXECille ei luvata exactly-once-
sivuvaikutuksia tai turvallista automaattista uusintaa.

9P voi tarjota `/jobs`-tiedostorajapinnan samalle pysyvälle tehtäväpalvelulle.
2001P:n oma binäärinen siirtokerros on vaihtoehto, jota arvioidaan tämän
rinnalla. TLS/TCP ja WSS voivat kuljettaa samaa sovellusprotokollaa.
Nykyinen WSS käyttää jo palvelimen natiivia TLS:ää; mitään suorituskykyeroa
suoraan TCP/TLS:ään ei mitattu tässä keskustelussa.

## Avoimet päätökset ja ehdotettu seuraava työ

1. Selvitä pienin natiivi asiakas nykyiseen autentikoituun WSS/rcpu-yhteyteen ja arvioi drawterm-riippuvuuden rajaus.
2. Vertaile 9P `/jobs` -palvelua ja 2001P-kehystystä. Älä oleta, että uuden protokollan toteutus on jo päätetty.
3. Valitse kohdekoneella toimiva tallennusratkaisu ja varmista todellinen durability-semanttiikka. SQLitea ei ole valittu eikä sen Plan2001-porttia vahvistettu.
4. Toteuta erikseen sovitun laajuuden mukaan SUBMIT/LOOKUP/STATUS/WATCH/CANCEL ja paikallinen pooli.
5. Testaa kuittauksen katoaminen, yhteyskatko käännöksen aikana, workerin kaatuminen, rinnakkaisuus ja lokitulva.
6. Mittaa omat AI-operaatiot. Yleisimpien komentojen ehdotus ei ole tilastollisesti mitattu järjestys.

Keskustelussa valtuutettiin kirjautumis- ja Acme-käännöskokeet sekä tämän
dokumentaation commit/push. Tuotantopalvelun muuttamista tai protokollan
käyttöönottoa ei vielä pyydetty. Dokumentointicommit ei päivitä drawtermia.
