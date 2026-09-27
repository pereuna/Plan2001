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
Chrome
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

## Vaihe 3: WebGPU

1. **Esitys:** libmemdraw rasteroi WASMissa, ja framebuffer ladataan
   WebGPU-tekstuuriksi.
2. **GPU-backend** drawmesgille: `d` → teksturoitu nelikulmio, `L`/`P` →
   geometria, `e`/`E` → shader, `s`/`x` → glyyfiatlas, `O` → blend-tila,
   `w` → matriisi. Tulosta verrataan pikseleittäin libmemdraw-referenssiin
   samalla komentovirralla.

Selain voi toimia samalla Plan2001:n GPU- ja NPU-solmuna (WebGPU, WebNN).
