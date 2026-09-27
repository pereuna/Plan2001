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
         │ gui-web/wsock.c: jokainen TCP-yhteys on WebSocket (vaihe 2a)
         ▼
      Plan2001: webterm (portti 17080) → rcpu (17019) ja auth (567)
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

## Vaihe 2: JS + WASM -raja ja oma transportti

JavaScript hoitaa selaimen asiat: WSS, syötteen, leikepöydän, koon,
canvasin ja WebGPU:n. WASM hoitaa Plan 9 -asiat: 9P:n, `/dev/draw`in,
`/dev/mouse`n, `/dev/kbd`:n ja `/dev/cons`in sekä devdraw.c:n, libmemdraw:n
ja libmemlayerin. Rajapinta on pieni:

```
drawinit(w, h)  drawwrite(buf, n)  drawread(buf, n)
drawmouse(x, y, buttons, time)  drawkey(rune)  drawresize(w, h)
JS:n importit: jsflush(rect, pixels, stride)  jscursor(...)  jssend(buf, n)
```

Verkko: `wss://plan2001/term`, jonka sisällä DP9IK-auth ja 9P. **Ei TLS:ää
TLS:n sisällä:** WSS on jo TLS, joten rcpun TLS-PSK jää pois. Plan2001:lle
tulee webterm-kuuntelija cpu-palvelun rinnalle, ja klassinen rcpu ja natiivi
drawterm toimivat edelleen.

## Vaihe 3: WebGPU

1. **Esitys:** libmemdraw rasteroi WASMissa, ja framebuffer ladataan
   WebGPU-tekstuuriksi.
2. **GPU-backend** drawmesgille: `d` → teksturoitu nelikulmio, `L`/`P` →
   geometria, `e`/`E` → shader, `s`/`x` → glyyfiatlas, `O` → blend-tila,
   `w` → matriisi. Tulosta verrataan pikseleittäin libmemdraw-referenssiin
   samalla komentovirralla.

Selain voi toimia samalla Plan2001:n GPU- ja NPU-solmuna (WebGPU, WebNN).
