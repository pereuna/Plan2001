# Yksi kone, monta selainikkunaa (koe 7.10.2026)

## Ehdotus

Ehdotettu malli (Plan9-wasm32:n mainin pohjalta tehty analyysi):

- Kone (ydin, jaettu `WebAssembly.Memory`, proc-Workerit, levy-Worker,
  OPFS, verkko) siirretään sivulta **SharedWorkeriin**. Saman originin
  kaikki välilehdet liittyvät samaan koneeseen MessagePortilla, eikä
  toinen välilehti enää yritä käynnistää toista konetta samalta levyltä
  (`sdW0`:n sync handle on yksinomainen).
- Sivulle jää vain selaimen puoli: canvas, näppäimistö, hiiri, liitä,
  kosketus ja WebAuthn.
- Myöhemmin **selainikkuna on Plan 9 -ikkuna**: jokaisella on oma
  `/dev/cons` tai `/dev/draw`, `/dev/mouse` ja `/dev/wctl`. Ydin saa
  ikkunakohtaisen piirtopolun (`platwinnew`, `platwinflush(winid, r)`
  jne.), ja rio jää pois normaalista bootista.

## Koe

`monolith/tools/probe-windows/run` ajaa kokeen headless-Chromiumissa
samoilla COOP- ja COEP-otsakkeilla kuin koneen sivu.

| Kysymys | Chromium 153 |
|---|---|
| Liittyvätkö kaksi välilehteä samaan SharedWorkeriin? | **kyllä**, ja se jää eloon, kun ensimmäinen suljetaan |
| Onko SharedWorker cross-origin-isoloitu (`SharedArrayBuffer`, `Atomics.wait`)? | **ei**, vaikka sen skriptillä on COOP ja COEP |
| Voiko SharedWorker luoda Workerin (proc)? | **ei**: `Worker is not defined` |
| Voiko SharedWorker lähettää jaetun muistin sivulle? | **ei**: `DataCloneError` |
| Saako koneen välilehden `window.open`illa avaama ikkuna koneen jaetun muistin? | **kyllä**, ja se näkee proc-Workerin kirjoitukset suoraan |
| Saako käyttäjän itse avaama välilehti yhteyden koneen välilehteen? | **kyllä**, MessagePortilla SharedWorkerin välittämänä (viestit, ei jaettua muistia) |

Molemmat SharedWorker-rajoitukset ovat Chromiumin avoimia vikoja:
[SharedArrayBuffer disabled in SharedWorker and ServiceWorker (386633375)](https://issues.chromium.org/issues/386633375)
ja [Worker() SharedWorkerissa (40695450)](https://issues.chromium.org/issues/40695450),
ks. myös [mdn/browser-compat-data#30707](https://github.com/mdn/browser-compat-data/issues/30707).
Firefox sallii Workerin SharedWorkerissa, mutta testit ja terminaalin
alusta (koko ruudun Chromium) ovat Chromiumia.

**Johtopäätös.** Konetta ei voi siirtää SharedWorkeriin Chromiumissa:
ydin tarvitsee jaetun muistin ja proc-Workerit. Tavoite (yksi kone,
monta selainikkunaa, selainikkuna = Plan 9 -ikkuna) onnistuu silti näin:

- **Kone pysyy välilehdessä**, kuten nyt (`platform.js` ja Dedicated
  Workerit).
- **Koneen avaamat ikkunat** (`window.open`, sama selainkontekstiryhmä
  COOP same-originin ansiosta) saavat jaetun muistin. Ikkunan kehyspuskuri
  voi olla koneen muistissa, ja ikkuna piirtää sen ilman kopiota.
- **Käyttäjän avaamat välilehdet** saavat MessagePortin koneeseen.
  SharedWorker toimii tässä pelkkänä välittäjänä ilman jaettua muistia,
  ja kuva kulkee kopioina.
- **Yksi kone originia kohden:** Web Locks (`navigator.locks`). Välilehti,
  joka saa lukon, käynnistää koneen. Muut liittyvät siihen välittäjän
  kautta eivätkä yritä avata `sdW0`:aa.
- Rajoitus: kone elää niin kauan kuin sen välilehti. Sen sulkeminen
  sammuttaa koneen, kuten nyt.

Plan2001:ssä levyllä ei ole sivua eikä ydintä (sivu tulee CPU-palvelimelta,
levy on glendan koti), joten ehdotuksen kohta "firmwarea ei asenneta
Plan 9:n levylle" pätee jo.

## Järjestys

1. **Yksi kone originia kohden:** Web Locks ja välittäjä. Toinen välilehti
   liittyy käynnissä olevaan koneeseen eikä käynnistä toista. Proc- ja
   fork-koodiin ei kosketa.
2. **Ikkunarekisteri koneen välilehdessä:** winid, MessagePort tai
   ikkunaviite, tyyppi ja koko. Ensin kaksi terminaali-ikkunaa (oma
   `/dev/cons`), joiden teksti piirretään selaimessa.
3. **Ikkunakohtainen piirto:** `screen.c`:n yksi `gscreen` ja devdrawin
   yksi `screenimage` per ikkuna, `platwin*`-kutsut, hiiri, näppäimistö ja
   koko per ikkuna. Yksi libdraw-ohjelma omassa selainikkunassaan ilman
   riota.
4. Kun `/dev/cons`, `/dev/draw`, `/dev/mouse` ja `/dev/wctl` toimivat
   ikkunoittain, rio voi jäädä pois normaalista bootista.

Avoinna: puhelimet (Android-Chromen SharedWorker ja `window.open`) ovat
testaamatta.
