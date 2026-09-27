# Vaiheet

| Vaihe | Tavoite | Hyväksyntä | Tila |
|---|---|---|---|
| 1a | Natiivi drawterm → Plan2001-VM cpu-palvelimena | `drawterm -G -c 'echo MONOLITH-OK'` tulostaa merkin | valmis 27.9. |
| 1b | `drawterm.wasm` ilman grafiikkaa (`-G`) Chromessa | headless Chromiumin konsolilokissa `MONOLITH-OK` | valmis 27.9. (`tools/test-headless`) |
| 1c | `gui-web`: rio selaimessa | headless-testi raportoi rion flushin, ja käyttäjä kokeilee Chromessa | headless valmis 27.9. (`tools/test-rio`), käyttäjän kokeilu odottaa |
| 2a | Oma WebSocket-transportti ja Plan2001:n `webterm` (WS → rcpu/auth) | rio ilman Emscriptenin socket-proxya | valmis 27.9. |
| 2b | WSS: Plan2001 tarjoilee sivun ja WebSocketit HTTPS:llä (tlssrv + webterm) | `https://`/`wss://` Chromessa ilman Chromen asetuksia | valmis 27.9. |
| 2c | Auth WSS:n sisällä ilman drawtermin omaa TLS:ää | ei TLS:ää TLS:n sisällä | valmis 27.9. |
| 2d | JS + WASM -raja: `gui-web/monolith.h`, `library.js`, `web/monolith.js` | ei selainkoodia C:ssä (`EM_ASM`) | valmis 27.9. |
| 2e | Uudelleenyhdistys: katkennut WebSocket (tausta-välilehti, mobiiliverkko) ei päätä istuntoa | selain taustalle ja takaisin, rio jatkuu | |
| 3 | WebGPU: ensin esitys, sitten GPU-backend pikselivertailulla | referenssikuvat vastaavat | |

## Kehitysympäristö

- Plan2001-repon VM: `tools/vm --net` ja `tools/vm-cpu` (cpu- ja
  auth-palvelin, porttiohjaukset rcpu 17019 ja auth 567).
- Emscripten 3.1.69 (Debian), Chromium (headless-testit), WS→TCP-silta
  vaiheissa 1b ja 1c.

## Käyttö

```
# Plan2001-repossa (kerran: tools/vm-cpu; webtermin päivitys: tools/vm-cpu --update), sitten:
tools/vm start --disk ~/.cache/plan2001/cpu.qcow2 --net

# Monolithissa
tools/build wasm          # build/wasm/drawterm.{js,wasm}
tools/build native        # build/native/drawterm (vertailukohta)
tools/test-headless       # vaiheen 1b hyväksyntätesti
tools/test-rio            # vaiheen 1c: rio, hiiri ja näppäimistö (DevTools)
tools/test-rio --mobile   # sama kännykkäemulaatiossa: kosketus ja IME
tools/deploy              # sivu Plan2001-VM:ään: https://127.0.0.1:17443/
tools/test-headless --vm  # testit sivulla VM:ltä (https, wss); myös test-rio --vm
tools/relay 10.77.0.5 17443   # VM:n https WireGuard-osoitteeseen
tools/serve               # http://127.0.0.1:8080/ ja ws://127.0.0.1:8081 → VM:n webterm
tools/serve --listen 10.77.0.5   # esim. WireGuard-osoitteessa (kännykkä)
```

Selaimessa `http://127.0.0.1:8080/#pass=SALASANA` avaa drawtermin
konsolin (`-h plan2001 -a tcp!plan2001!567 -u glenda`; osoitteet eivät merkitse, WebSocket menee aina webtermiin), ja `rio`
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

## Etäkäyttö (WireGuard)

`tools/serve --listen ADDR` kuuntelee muualla kuin loopbackissa.
`tools/proxy.patch` rajaa Emscriptenin proxyn: se yhdistää vain tämän
koneen 127.0.0.1:n portteihin `MONOLITH_ALLOW` (oletus VM:n 17019 ja 5670),
ei `bind`iä eikä `listen`iä sivun puolesta, ja kuuntelee vain ADDR:ssa.
Selain antaa SharedArrayBufferin vain localhostille ja HTTPS:lle, joten
Chromessa osoite lisätään kohtaan `chrome://flags` → *Insecure origins
treated as secure* (esim. `http://10.77.0.5:8080`). SSH-tunnelin kautta
(`http://localhost:8080`) asetusta ei tarvita.

## Kännykkä WireGuardin yli (27.9.): mitä selvisi

- Sivu, auth, konsoli ja näppäimistö toimivat kännykän Chromella
  (`tools/serve --listen 10.77.0.5`, `?log=1` lokiin `build/page.log`).
- Emscriptenin socket-silta tekee jokaisesta socket-kutsusta kiertomatkan
  proxyyn, ja drawterm lukee auth-merkkijonot tavu kerrallaan.
  `bridge.c` puskuroi `recv`:n, mutta 200 ms kiertoajalla `-c`-komento vie
  silti lähes kolme minuuttia (jokainen `send` odottaa vastauksen).
  Vaihe 2:n oma WSS-transportti poistaa tämän: tavut virtaavat, eikä
  jokainen kutsu odota.
- Tunnelin MTU 1280 (mobiiliosuus pudotti isommat), gzip (1 Mt → 310 kt),
  HTTP/1.1 keep-alive.

## Vaihe 2a: oma transportti (27.9.)

- Plan2001: `sys/src/cmd/webterm.c`, aux/listenin palvelu portissa 17080
  (`tools/vm-cpu` kääntää ja asentaa sen VM:ssä). `GET /17019` on rcpu ja
  `GET /567` auth, muita ei. Tavut kulkevat muuttumattomina binäärikehyksissä,
  joten drawtermin auth ja TLS toimivat sen sisällä kuten TCP:n yli.
- Selain: `gui-web/wsock.c`. drawtermin `socket`, `connect`, `send`,
  `recv`, `close` ... ohjataan (Make.emscripten `-D`) omaan kerrokseen:
  jokainen yhteys on WebSocket `MONOLITH_WS/PORTTI`, `recv` odottaa
  puskuria, jonka sivun `onmessage` täyttää, ja `send` ei odota.
  Emscriptenin socket-proxy ja `tools/proxy.patch` poistuivat;
  `tools/serve` välittää portin 8081 suoraan VM:n webtermiin.
- Mittaus 200 ms kiertoajalla (netem loopbackissa): kirjautuminen ja
  `-c`-komento 13 s, proxyn kautta noin 170 s.

## Vaihe 2b: WSS (27.9.)

- Plan2001:n `webterm -w /sys/lib/monolith` tarjoilee myös sivun
  (index.html, drawterm.js, drawterm.wasm, gzip-versiot, COOP/COEP,
  keep-alive), ja `tlssrv` sen edessä portissa 17443 tekee siitä
  `https://` ja `wss://` samaan originiin. `tools/serve`:a ei tarvita.
- Varmenne: Plan2001:n `tools/vm-cpu` tekee kehitys-CA:n
  (`~/.cache/plan2001/ca/ca.pem`) ja sillä allekirjoitetun varmenteen
  (`CPU_CERT_SANS`, esim. `IP:127.0.0.1,IP:10.77.0.5,...`). Avain menee
  factotumiin (`proto=rsa service=tls role=client owner=*`,
  `/cfg/cirno/cpustart`). CA on ladattavissa: `/plan2001-ca.crt`.
- Kun CA on asennettu selaimeen tai kännykkään, sivu on suojattu
  konteksti, joten SharedArrayBuffer toimii ilman `chrome://flags`-asetusta.
- Testit: `--vm` lataa sivun VM:ltä, ja Chromium luottaa vain tämän
  varmenteen avaimeen (`--ignore-certificate-errors-spki-list`, `tools/spki`).

## Vaihe 2c: ei TLS:ää TLS:n sisällä (27.9.)

- Plan2001: `webterm -s` (vain tlssrv:n takana, portti 17443) tarjoaa
  polun `/rcpu`: webterm tekee itse sen, minkä `tlssrv -a` tekee rcpu:lle
  (p9any/dp9ik palvelimena, `auth_chuid`, rcpu-skripti), mutta ilman
  TLS-PSK:ta. Salaamattomassa portissa 17080 `/rcpu`:ta ei ole (404).
- Selain: kun `MONOLITH_WS` on `wss:`, rcpu (portti 17019) menee polkuun
  `/rcpu`, ja cpu.c:n `tlsClient` (vain cpu.c, `-DtlsClient=monolithtlsclient`)
  jättää p9authtls:n TLS-PSK:n (`pskID p9secret`) pois. Tavallisella
  `ws://`:llä (tools/serve) sisempi TLS säilyy.
- Auth (DP9IK) kulkee WSS:n sisällä; auth-palvelimen yhteys (`/567`) on
  ennallaan.

## Uudelleenyhdistys (suunnitelma, vaihe 2e)

Kännykässä selain katkaisee tausta-välilehden WebSocketit, ja mobiiliverkko
katkeilee; nyt kumpikin päättää istunnon (`exportfs: short write in reply`).
drawtermissa on tähän valmis mekanismi, **aan** (`-p`, `aan.c`): se numeroi
datan ja lähettää kuittaamattoman uudelleen uudella yhteydellä, ja
palvelimella rcpu ajaa `aan`-suodattimen. Suunnitelma:

1. Selvitä, toimiiko drawtermin `-p` (aan) webtermin kautta sellaisenaan:
   aan tekee uuden yhteyden rcpu-porttiin, mikä on WebSocketeilla uusi
   `/rcpu`. Sisempi TLS: aan ajetaan TLS:n alla, joten 2c:n ohitus pitää
   sovittaa (`startaan` + `p9authtls`).
2. Sivu: `visibilitychange` ja `online`-tapahtumat; kun sivu palaa,
   katkennut yhteys avataan uudelleen (`js_netopen` samalle connille),
   ja aan jatkaa.
3. Jos aan ei sovi: oma kevyt jatko webtermissä (istuntotunniste,
   kuittaamattomien kehysten puskuri molemmissa päissä).
4. Testi: `test-rio --vm` katkaisee WebSocketit kesken (DevTools
   `Network.emulateNetworkConditions offline`) ja tarkistaa, että rio
   jatkuu.
