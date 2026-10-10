# WebRTC: selainkoneiden välinen johto (kokeilu 10.10.2026)

Selainvälilehti ei voi kuunnella TCP- tai UDP-porttia. Siksi wasm32-kone on
tähän asti ollut vain asiakas: jokainen `/net/tcp`-keskustelu on WebSocket
koneen webtermiin (D2). WebRTC DataChannel on selaimen ainoa kanava, jonka
toinen selain voi avata suoraan, ja ICE muodostaa sen suoraan laitteiden
välille aina kun reitti on olemassa (julkiset IPv6-osoitteet). Tämä kokeilu
tekee siitä `#I`:n toisen kuljettimen: wasm32-kone voi `announce`lla
ottaa yhteyksiä vastaan, eli selainkoneesta voi tulla palvelin toiselle
selainkoneelle ilman, että data kulkee palvelimen kautta.

Tämä ei ole D8 (laskentapooli), joka odottaa omaa suunnitteluaan
(`docs/architecture.md`). Se on D2:n jatke, jota D8 voi myöhemmin käyttää.

## Käyttö koneessa

    NAME.rtc!port        toisen selainkoneen portti: sivun DataChannel vertaiselle NAME
    announce *!port      sivu ottaa vastaan vertaisten DataChannelit, joiden nimi on port
    /net/tcp/n/listen    avaus odottaa seuraavaa: uusi keskustelu, kuten clone

libc:n `announce`, `listen`, `accept` ja `dial` toimivat sellaisinaan:
`dial("tcp!a.rtc!exportfs", ...)` ja `announce("tcp!*!exportfs", ...)`.
`accept` ja `reject` eivät tee mitään: kanava on sivun jo valmiiksi.
`local` kertoo ilmoitetun portin, `status` on `Announced`, ja kuunnellun
keskustelun `remote` on `rtc!port` (vertaisen nimeä ydin ei tiedä).

Sivun parametrit (kernel.html):

    ?signal=wss://kone:8443/   signalointipalvelin (tools/rtcsignal)
    &rtcname=a                 tämän koneen nimi huoneessa, [a-z0-9-], enintään 32
    &rtcroom=koe1              huone (oletus plan2001)
    &stun=stun:host:3478       STUN-palvelin, toistettavissa (oletus: ei yhtään)

## Rakenne

- **Ydin (`devwsnet.c`).** Keskustelu, rengas, gen ja sendq ovat samat kuin
  WebSocketilla. Uutta ovat ctl:n `announce`, tiedosto `listen` ja
  `NAME.rtc`-isännät. `listen`in avaus tekee uuden keskustelun ja pyytää
  sivulta seuraavan kanavan (`platnetaccept`); odotus päättyy noteen.
  Ilmoitetun keskustelun sulkeminen poistaa ilmoituksen sivulta
  (`platnetclose`).
- **Sivu (`platform.js`, `rtc`).** Yksi `RTCPeerConnection` vertaista
  kohden, neuvoteltuna MDN:n "perfect negotiation" -mallilla (kohtelias on
  aakkosissa ensimmäinen nimi). Jokainen keskustelu on oma järjestetty
  DataChannel, jonka nimi on portti. `Sock` näyttää kanavan net.openille
  WebSocketina (readyState, bufferedAmount, send, close, on*), joten
  rengas, jono ja vastapaine ovat samat. Ennen kuin ydin ottaa
  kanavan vastaan, sen viestit pidetään tallessa. Kanava porttiin, jota
  ei ole ilmoitettu, suljetaan.
- **Signalointi (`monolith/tools/rtcsignal`).** Pythonin standardikirjasto.
  Huoneet ja nimet muistissa; se välittää vain tarjoukset, vastaukset ja
  ICE-kandidaatit nimetylle vertaiselle. Sen sulkeminen ei katkaise
  avoimia kanavia. `--web DIR` jakaa lisäksi koneen sivun
  (`tools/rtcsite DIR`) COOP/COEP-otsakkeineen, joten yksi pieni palvelin
  riittää puhelimelle.

## Luottamus

Signalointipalvelin näkee DTLS-sormenjäljet ja voisi asettua väliin, kuten
`?ws=` olisi voinut (D7:n katselmus). Siksi kanavan yli kulkevan palvelun
on tunnistettava itse (dp9ik, TLS); sovelluksen rcpu (`/rcpu`, ilman omaa
TLS:ää) ei kulje tätä tietä. Testin exportfs ilman tunnistusta on vain
testi. Tämä on README:n periaate: epäluotettava kerros, luotettava
verifiointi.

## Testi

`tools/test-9wasm32 rtc`: kaksi konetta samassa Chromiumissa, b
(127.0.0.1) ja a omassa välilehdessään toisesta originista (localhost, oma
OPFS). test-wasmapp käynnistää rtcsignalin (`PEERARGS`). a ilmoittaa portin
17007 ja antaa ensimmäiselle soittajalle `exportfs -r /env`:n; b dialaa
`tcp!a.rtc!17007`, mounttaa sen `/mnt`:iin ja lukee a:n muuttujan `peer`.
Ilman ytimen muutosta testi kaatuu (`announce writing /net/tcp: bad
process or channel control request`).

## Oikea verkko

`jedi.ydns.eu` (OpenBSD) ajaa rtcsignalin portissa 8443 omalla
Let's Encrypt -varmenteellaan ja jakaa koneen sivun (`--web`). Kaksi
laitetta avaa saman huoneen eri nimillä:

    https://jedi.ydns.eu:8443/kernel.html?signal=wss://jedi.ydns.eu:8443/&rtcroom=koe1&rtcname=puhelin
    https://jedi.ydns.eu:8443/kernel.html?signal=wss://jedi.ydns.eu:8443/&rtcroom=koe1&rtcname=kuha

Chromium piilottaa host-kandidaattien osoitteet mDNS-nimillä, jotka eivät
ratkea verkkojen välillä. Siksi kahden verkon välillä tarvitaan `&stun=`,
vaikka molemmilla olisi julkinen IPv6-osoite: STUN paljastaa
osoitteen kandidaattina, ja yhteys on silti suora.

## Avoimet

- Vertaisen nimi kuunnellun keskustelun `remote`iin (sivu tietää sen).
- Nimet ovat signalointipalvelimen antamia, eivät todennettuja: vertaisen
  tunnistus kuuluu kanavan yli kulkevalle protokollalle. Pitäisikö nimen
  olla avaimen tiiviste (kuten certiration PoC:ssa)?
- Reitin mittaus (suora IPv6, suora IPv4, TURN) `getStats()`illa, kuten
  certiration-repon `poc1/web/src/classify.js`.
- Kanavan katkos: nyt keskustelu päättyy; `/rcpu`-istunnon kaltainen jatko
  puuttuu.
