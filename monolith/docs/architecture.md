# Arkkitehtuuri

## Mitä drawterm tekee

```
CPU-palvelin (Plan2001)
   │ rcpu: p9any/DP9IK-auth, TLS-PSK
   │ 9P
drawterm: exportfs → /mnt/term palvelimelle
   ├── /dev/draw   kern/devdraw.c → libmemdraw, libmemlayer → Memimage
   ├── /dev/mouse  /dev/kbd  /dev/cons  /dev/cursor  /dev/snarf
   └── flushmemscreen() → gui-x11 / gui-wl / gui-win32 / gui-cocoa ...
```

`devdraw.c`:n `drawmesg()` jäsentää Plan 9:n binäärisen grafiikkakielen:
`b` (kuva), `A` (screen), `d` (draw), `e`/`E` (ellipsi), `L` (viiva),
`p`/`P` (polygoni), `i`/`l` (fonttivälimuisti), `s`/`x` (merkkijono),
`O` (compositing), `o`/`t` (ikkunat), `y`/`Y` (kuvadata), `r` (luku) ja
`v` (flush). Tätä rajapintaa ei tarvitse keksiä uudelleen.

## Vaihe 1: koko drawterm WASMiksi

```
Selain
├── JS: sivu, canvas, syöte, Emscriptenin runtime
└── WASM (Emscripten, pthreadit): drawterm sellaisenaan
      main.c, cpu.c, kern/, exportfs/, libauth*, libsec, libmp,
      libdraw, libmemdraw, libmemlayer, posix-port/
      + gui-web/ (uusi): attachscreen, flushmemscreen → canvas, syöte
         │ gui-web/wsock.c + web/monolith.js: jokainen TCP-yhteys on WebSocket
         ▼
      Plan2001: tlssrv + webterm (17443, wss) → /rcpu (auth, ei sisempää TLS:ää),
      /567 auth; ilman TLS:ää portti 17080 → rcpu (17019) ja auth (567)
```

- **pthreadit:** drawtermin kprocit ovat POSIX-säikeitä. Emscripten tukee niitä
  SharedArrayBufferilla, joka vaatii HTTP-otsakkeet
  `Cross-Origin-Opener-Policy: same-origin` ja
  `Cross-Origin-Embedder-Policy: require-corp`.
- **Verkko:** selaimessa ei ole raakaa TCP:tä. Emscripten emuloi socketit
  WebSocketin yli, ja kehitysvaiheessa silta välittää ne VM:n TCP-portteihin.
- **gui-web** on drawtermin muiden `gui-*`-backendien kaltainen:
  `attachscreen()` antaa screen-Memimagen, ja `flushmemscreen(r)` kopioi
  muuttuneen alueen canvasiin. Syöte (pointer lock, näppäimet, rulla, koko,
  leikepöytä) kulkee drawtermin omiin rajapintoihin.

## Vaihe 2: JS + WASM -raja ja oma transportti (tehty)

JavaScript hoitaa selaimen asiat: WebSocketit, syötteen, näytön ja kursorin
(myöhemmin leikepöydän ja WebGPU:n). WASM hoitaa Plan 9 -asiat: 9P:n,
`/dev/draw`in, `/dev/mouse`n, `/dev/kbd`:n ja `/dev/cons`in sekä devdraw.c:n,
libmemdraw:n ja libmemlayerin. Koko raja on `gui-web/monolith.h`:

```
importit (wasm → sivu, pääsäikeessä; gui-web/library.js → Module.monolith)
  js_ready()                          sivu voi kutsua exportteja
  js_screensize(&w, &h)  js_resize(w, h)
  js_flush(base, stride, x0, y0, x1, y1)   XBGR32-suorakaide → present(ImageData)
  js_cursor(rgba16x16, hx, hy)
  js_netopen(conn, gen, url)  js_netsend(conn, p, n)  js_netclose(conn)
exportit (sivu → wasm)
  mo_input(type, a, b, c)             hiiri, näppäin, koko
  mo_netstate(conn, gen, state)  mo_netdata(conn, gen, p, n)
```

Sivun puoli on `web/monolith.js` (`Monolith({canvas, size, ...})`): se omistaa
WebSocketit, canvasin ja kursorin. WebGPU-esitys on muutos vain siihen.

Verkko: jokainen drawtermin TCP-yhteys on WebSocket Plan2001:n webtermiin
(`wss://kone/17019`, `/567`; `/rcpu` WSS:n yli). **Ei TLS:ää TLS:n sisällä:**
`/rcpu` tekee DP9IK-kirjautumisen WSS:n sisällä ilman rcpun TLS-PSK:ta.
Webterm tarjoilee myös sivun (tlssrv, portti 17443), ja klassinen rcpu ja
natiivi drawterm toimivat edelleen.

## Vaihe 3: ajoympäristö selaimen hiekkalaatikossa

Monolith ei ole työpöytä eikä ikkunamanageri. **Ikkunamanageri on selain:**
sen ikkunat ja välilehdet sekä käyttöjärjestelmän ikkunanhallinta. Monolith
on Plan2001:n ajoympäristön rajapinta (execution/runtime ABI) selaimen
hiekkalaatikolle. Vaiheiden 1–2 drawterm, jossa rio piirtää ikkunansa yhden
canvasin sisään, on tämän esiaste. Vaihe 3a (tehty) poisti rion: sama drawterm.wasm ajaa
yhden ohjelman välilehteä kohden (`/app/APP`), ja Plan2001 tuntee istunnot
(`apps`; `docs/roadmap.md`). Alla oleva on vaiheen 3b tavoite.

### Yksi välilehti = yksi Plan2001-prosessi

```
Selain / käyttöjärjestelmä              Plan2001 (CPU-palvelin)
├── välilehti: editor.wasm  ──────────▶ prosessi 101: nimiavaruus A, oikeudet A, tila A
├── välilehti: calc.wasm    ──────────▶ prosessi 102: nimiavaruus B, oikeudet B, tila B
├── välilehti: term.wasm    ──────────▶ prosessi 103: nimiavaruus C
└── välilehti: cad.wasm     ──────────▶ prosessi 104: nimiavaruus D
```

Kun käyttäjä avaa `https://plan2001/app/editor`, Plan2001 luo istunnon: uuden
nimiavaruuden, oikeusjoukon (capabilities), stdout/stderrin ja pysyvän
tilan. Se palauttaa `editor.wasm`in ja istunnon capabilityn. Välilehti ajaa
WASMin, joka puhuu vain omalle istunnolleen todennetun kanavan yli eikä
tiedä muista välilehdistä. Selaimessa ei ole yhteistä Monolith-valvojaa,
joka omistaisi useita ohjelmia, eikä selaimessa tehdä useita "prosesseja".

Kaksi editoria ovat kaksi välilehteä ja kaksi prosessia (101 ja 117), kuten
Unixissa `editor & editor &`. Ohjelman ikkuna on välilehti tai
selainikkuna, ja sen piirtoalue on välilehden viewport ja canvas. Siksi
Plan2001 ja Monolith eivät tarvitse ikkunoiden luontia, siirtoa ja
koon muutosta, pinojärjestystä, kehyksiä, compositoria, alt-tabia eikä
monen näytön hallintaa: ne ovat jo selaimessa ja käyttöjärjestelmässä.

### Nimiavaruus

Plan2001:n tärkein tehtävä on antaa jokaiselle selainprosessille oma
maailma. Prosessin maailman määrää sen nimiavaruus ja oikeusjoukko, ei
globaali tiedostojärjestelmä:

```
editor (101)            cad (104)               calc (102)
/                       /                       /
├── home/               ├── project/            ├── tmp/
├── project/            ├── models/             └── service/
├── tmp/                ├── tmp/
├── clipboard           └── service/
├── net/                      └── renderer
└── service/
```

Calc ei näe `project/`ia lainkaan. Tämä on Plan 9:n prosessikohtaisen
nimiavaruuden ajatus, lähempänä sitä kuin selaimen compositor-malli.

### Turvallisuusraja on Plan2001:ssä

Saman originin välilehdet eivät ole toisistaan vahvasti eristettyjä: ne
voivat kommunikoida mm. BroadcastChannelin, SharedWorkerin,
localStoragen (storage-tapahtumat), IndexedDB:n ja ServiceWorkerin kautta.
Siksi turvallisuusperiaate ei ole "eri välilehti = eri käyttäjä" vaan
**eri istunnon capability = eri turvallisuusprinsipaali**. Jokainen
RPC-kutsu kantaa vain oman prosessinsa capabilityn, ja Plan2001 tarkistaa
sen (istunto 101 → nimiavaruus 101). Vaikka välilehti 101 saisi tietää
välilehdestä 102, se ei voi avata istunnon 102 tiedostoja, koska sillä ei ole
sen capabilitya.

Vahvempi selainpuolen eristys (eri originit, esim. `p101.apps.plan2001`,
tai opaque-origin-hiekkalaatikko) on mahdollinen myöhemmin, mutta
ensimmäinen versio ei nojaa siihen.

### %term

Pääte on tavallinen selainprosessi (`term.wasm` → Plan2001:n
pääteistunto). Se ei katso muiden WASM-prosessien muistia selaimessa vaan
kysyy Plan2001:ltä, joka loi istunnot. `ps` näyttää vain prosessit, joihin
päätteen nimiavaruus ja oikeudet ulottuvat; toisen käyttäjän prosessit
eivät näy.

### stdout Plan2001:een

WASM-ajoympäristö vie `write(1, ...)`:n prosessin kanavaa pitkin
Plan2001:een, joka pitää siitä rengaspuskuria (esim. `/proc/101/stdout`,
`/proc/101/stderr`, tai mieluummin Plan2001:n oma rajapinta). `term 101`
näyttää editorin tulosteen ottamatta yhteyttä editorin välilehden
JavaScriptiin. Se toimii, vaikka välilehti olisi jäätynyt, ladattu
uudelleen, suljettu tai toisella koneella.

### Prosessi elää välilehden yli

Suoritus selaimessa on ohimenevää, Plan2001:n prosessi ja sen tila
pysyviä. Kun välilehti suljetaan, prosessi jää tilaan `detached`; kun
editori avataan uudelleen, uusi `editor.wasm`-instanssi liittyy samaan
prosessiin ja palauttaa tilan. Tämä vastaa Plan 9:n CPU-palvelimen ja
päätteen suhdetta: suoritusympäristö vaihtuu, prosessin looginen tila
säilyy.

```
% ps
PID   STATE       APP
101   detached    editor
102   attached    calc
104   attached    cad
% attach 101          # antaa URL:n / uuden välilehden editorille
```

Prosessin voi siirtää laitteelta toiselle (kannettavan selain → puhelin,
työpöytä → tabletti) ilman että sen identiteetti muuttuu. Vaiheen 2
jatkettava `/rcpu`-istunto (webterm) on tämän pienempi esiaste.

### Prosessin määritelmä

Prosessi ei ole WASM-instanssi eikä selaimen Worker:

```
Plan2001-prosessi = identiteetti
                  + nimiavaruus
                  + capabilityt
                  + pysyvä tila
                  + stdin/stdout/stderr
                  + ohjelman image ja versio
                  + mahdollinen liitetty suoritusympäristö (välilehti)
```

### Kokonaisuus

```
                   Plan2001 CPU-palvelin
                 ┌─────────────────────┐
                 │ PID 101 nimiav. A   │
                 │ PID 102 nimiav. B   │
                 │ PID 103 nimiav. C   │
                 │ pysyvä tila         │
                 │ capabilityt         │
                 │ stdin/out/err       │
                 └──────────┬──────────┘
                            │ todennetut istunnot
         ┌──────────────────┼──────────────────┐
         ▼                  ▼                  ▼
   välilehti           välilehti          välilehti
   editor.wasm         calc.wasm          term.wasm
   canvas/WebGPU       canvas/WebGPU      tekstikäyttöliittymä
         └──────── selaimen / käyttöjärjestelmän ikkunanhallinta ───┘
```

### Avoin kysymys: missä ohjelman näyttö ja tila elävät (vaihe 3b)

Vaihe 3a ajaa drawterm.wasmia välilehdessä: ohjelma pyörii Plan2001:ssä,
mutta sen näyttö (`/dev/draw`in kuvat) on välilehden drawtermissa. Siksi
suljetun välilehden istuntoon ei voi liittää uutta välilehteä, vaikka
istunto elää vielä. Vaihe 3b ratkaisee tämän; toteutustapa on
päättämättä (28.9.). Vaihtoehdot:

| | A: näyttö palvelimella | B: ohjelma wasmina välilehdessä | C: drawterm jää, lisätään turva |
|---|---|---|---|
| Ohjelma ja tila | Plan2001:ssä | selaimessa | selaimessa (drawterm) |
| Liittyminen suljetun välilehden istuntoon, laitteen vaihto | toimii | ei ilman ohjelmien uudelleenkirjoitusta | ei |
| Välilehti | ohut JS-asiakas (kuva ja syöte wss:n yli), ei wasmia | wasm-ohjelma, järjestelmäkutsut RPC:nä Plan2001:lle | drawterm.wasm kuten nyt |
| Piirto | palvelimen CPU (libmemdraw) | selain (WebGPU hyödyllinen) | selain |
| Vaihe 4 (WebGPU) | vain esitys | keskeinen | ennallaan |
| Pohja | 9frontin `vncs` (oma devdraw, ajaa komennon virtuaalinäytölle) | ei valmista Plan 9 C → wasm -ketjua eikä libc-sovitusta | nykyinen 3a |
| Työmäärä | muutama päivä | viikkoja–kuukausia | päivä |

- **A** toteuttaa ylläolevan ytimen suorimmin: prosessi, sen näyttö ja
  nimiavaruus ovat Plan2001:ssä, ja välilehti on vain liitetty
  suoritusympäristö, jonka voi vaihtaa (capability-tunniste, `apps`,
  `attach`). Hinta: piirto palvelimella ja enemmän kaistaa.
- **B** on ylläolevan kuvan `editor.wasm` kirjaimellisesti: laskenta ja
  piirto selaimessa. Ohjelman tila on kuitenkin selaimessa, joten jatko
  toiselta laitteelta vaatii, että ohjelmat pitävät tilansa Plan2001:ssä
  (tiedostoina), eli ohjelmat on tehtävä sitä varten.
- **C** lisää vain istuntokohtaisen nimiavaruuden
  (`/lib/app/APP/namespace`), palvelimen valitseman ohjelman ja
  capability-tunnisteet; uudelleenliittyminen ei onnistu.
- A ja B eivät sulje toisiaan pois: A ensin kaikille ohjelmille, B
  myöhemmin niille, joille selaimessa laskeminen sopii.

Suositus (Claude): A. Päätös: avoin.

## Vaihe 4: WebGPU

Välilehden piirto (vaiheen 3 canvas/WebGPU):

1. **Esitys:** libmemdraw rasteroi WASMissa, ja framebuffer ladataan
   WebGPU-tekstuuriksi.
2. **GPU-backend** drawmesgille: `d` → teksturoitu nelikulmio, `L`/`P` →
   geometria, `e`/`E` → shader, `s`/`x` → glyyfiatlas, `O` → blend-tila,
   `w` → matriisi. Tulosta verrataan pikseleittäin libmemdraw-referenssiin
   samalla komentovirralla.

Selain voi toimia samalla Plan2001:n GPU- ja NPU-solmuna (WebGPU, WebNN).
