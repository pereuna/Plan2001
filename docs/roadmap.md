# Vaiheet

| Vaihe | Tavoite | Hyväksyntä | Tila |
|---|---|---|---|
| 1a | Natiivi drawterm → Plan2001-VM cpu-palvelimena | `drawterm -G -c 'echo MONOLITH-OK'` tulostaa merkin | valmis 27.9. |
| 1b | `drawterm.wasm` ilman grafiikkaa (`-G`) Chromessa | headless Chromiumin konsolilokissa `MONOLITH-OK` | valmis 27.9. (`tools/test-headless`) |
| 1c | `gui-web`: rio selaimessa | headless-testi raportoi rion flushin, ja käyttäjä kokeilee Chromessa | headless valmis 27.9. (`tools/test-rio`), käyttäjän kokeilu odottaa |
| 2 | JS + WASM -raja, oma WSS-transportti, Plan2001:n webterm-kuuntelija, DP9IK ilman sisäkkäistä TLS:ää | selain → WSS → rio ilman WS→TCP-siltaa | |
| 3 | WebGPU: ensin esitys, sitten GPU-backend pikselivertailulla | referenssikuvat vastaavat | |

## Kehitysympäristö

- Plan2001-repon VM: `tools/vm --net` ja `tools/vm-cpu` (cpu- ja
  auth-palvelin, porttiohjaukset rcpu 17019 ja auth 567).
- Emscripten 3.1.69 (Debian), Chromium (headless-testit), WS→TCP-silta
  vaiheissa 1b ja 1c.

## Käyttö

```
# Plan2001-repossa (kerran: tools/vm-cpu), sitten:
tools/vm start --disk ~/.cache/plan2001/cpu.qcow2 --net

# Monolithissa
tools/build wasm          # build/wasm/drawterm.{js,wasm}
tools/build native        # build/native/drawterm (vertailukohta)
tools/test-headless       # vaiheen 1b hyväksyntätesti
tools/test-rio            # vaiheen 1c: rio, hiiri ja näppäimistö (DevTools)
tools/test-rio --mobile   # sama kännykkäemulaatiossa: kosketus ja IME
tools/serve               # http://127.0.0.1:8080/ ja proxy ws://127.0.0.1:8081
```

Selaimessa `http://127.0.0.1:8080/#pass=SALASANA` avaa drawtermin
konsolin (`-h 127.0.0.1 -a tcp!127.0.0.1!5670 -u glenda`), ja `rio`
käynnistää rion. Muut argumentit kyselynä (`?a=-h&a=...`; `-G` näyttää vain
tekstin). Salasana on fragmentissa, joten se ei lähde palvelimelle. Jos
Chrome on toisella koneella: `ssh -L 8080:127.0.0.1:8080 -L
8081:127.0.0.1:8081 debian-kone`.

## Vaihe 1b: mitä selvisi

- Socketit: Emscriptenin oma SOCKFS ei blokkaa, mutta drawtermin kprocit
  lukevat blokkaavasti. Siksi `-sPROXY_POSIX_SOCKETS` ja
  `websocket_to_posix_proxy` (Emscriptenin lähdekoodista, `tools/serve`
  kääntää sen ja rajaa sen kuuntelemaan vain 127.0.0.1:tä, koska se
  yhdistää minne tahansa pyydetään).
- Emscripten 3.1.69:n proxy-asiakkaan `getsockname`/`getpeername` lähettää
  väärän mittaisen viestin, ellei puskurin koko ole 256; kierto
  `gui-web/bridge.c`:ssä.
- wasmissa `main(argc, argv)` on symboli `__main_argc_argv`, ja wasm-ld 19
  kaatuu `--wrap`-optioon; siksi `-Dmain=drawtermmain` ja bridge.c:n oma main.
- `getcallerpc` (`__builtin_return_address`) vaatisi
  `-sUSE_OFFSET_CONVERTER`in, joten se on 0 (vain lukkojen debug).
- drawtermin ylätason Makefile menee alihakemistoon vain, jos kirjasto
  puuttuu; `tools/build` poistaa kirjastot ennen makea.

## Vaihe 1c: mitä selvisi

- `gui-web/web.c`: näyttö on XBGR32-Memimage (tavut R G B x, eli canvasin
  järjestys), ja `flushmemscreen` kopioi muuttuneen suorakaiteen canvasiin
  pääsäikeessä (`webjs.c`, `MAIN_THREAD_EM_ASM`). ImageData ei voi katsoa
  jaettua muistia, joten se on kopio.
- Syöte: sivu laittaa hiiri-, näppäin- ja kokotapahtumat jonoon
  (`Module._webpush`), ja kproc antaa ne drawtermille (`absmousetrack`,
  `kbdkey`, `screenresize`). Napit: DOM 1/2/4 (vasen/oikea/keski) → Plan 9
  1/2/4 (vasen/keski/oikea), rulla 8/16. Kursori on 16×16 RGBA → CSS-kursori.
- `tools/test-rio` ajaa Chromiumia DevTools-protokollalla (oma pieni
  WebSocket-asiakas): konsoliin `rio`, napin 3 valikosta New ja ikkunan
  veto, komento ikkunaan, ja kuvakaappaukset `build/rio-*.png`.
- Näppäimet ruuhkassa: drawterm pudottaa näppäimiä, jos jono etä-rioon on
  täynnä (kuten natiivistikin), joten testi kirjoittaa 25 merkkiä/s.
- Kosketus: kosketusnäytöllä (tai `?touch=1`) ruudun alla on palkki:
  1/2/3 valitsee hiiren napin seuraavalle kosketukselle (sitten taas 1),
  Esc ja ⌨. Kännykän näppäimistö kirjoittaa piilotettuun textareaan, jonka
  arvo luetaan ja palautetaan yhdeksi välilyönniksi (lyhyempi =
  askelpalautin), koska Androidin näppäimistö ei lähetä näppäinkoodeja.
- Avoinna: leikepöytä (nyt vain drawtermin sisäinen), HiDPI
  (devicePixelRatio), pointer lock, ja nopeus (jokainen socket-kutsu kulkee
  proxyn kautta).
