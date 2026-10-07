# Plan2001:n kerrokset: arkkitehtuuri, alusta, rooli, ohjelmat

Suunnitelma 1.10.2026. Lähtökohta: 3c ja 3l (docs/wasm32.md) tekivät
WebAssemblystä Plan2001:n arkkitehtuurin. Siitä seuraa, että selain ei ole
päätelaite eikä erikoistapaus vaan yksi alusta muiden joukossa.

## Perussääntö

> wasm32 ei ole Plan2001:n web-versio. Se on Plan2001:n
> CPU-arkkitehtuuri. Selain on yksi wasm32-alusta. Terminal, CPU, file
> ja auth ovat koneen rooleja, eivät arkkitehtuureja eivätkä
> selainkonsepteja.

## Neljä toisistaan riippumatonta kerrosta

| Kerros | Arvot |
|---|---|
| 1. arkkitehtuuri | amd64, arm64, riscv64, wasm32 |
| 2. alusta | PC/UEFI, SBC (RPi tms.), selain, ... |
| 3. koneen rooli | terminal, cpu, file, auth |
| 4. ohjelmat | rio, rc, sam, acme, ... |

drawterm, webterm ja Monolith sekoittavat näitä kerroksia: ne syntyivät,
kun selain nähtiin ensin päätelaitteena.

    nyt:     Plan 9 -ohjelma -> drawterm -> Monolith -> webterm -> selain
    tavoite: selaimen rajapinnat <- wasm32-alustan ajurit <- Plan2001-ydin
             <- Plan2001:n järjestelmäkutsu-ABI <- 3c/3l-ohjelmat

drawterm on host-ohjelma eikä arkkitehtuuri. Sitä voi verrata QEMUun
abstraktiotasolla, vaikka se ei emuloi suoritinta: se tarjoaa Plan 9
-ympäristön toisen käyttöjärjestelmän päällä. Kun wasm32 on oikea
arkkitehtuuri, tätä välikerrosta ei tarvita.

## Malli

                         Plan2001
          ┌──────────────┼───────────────┐
        amd64          arm64           wasm32
          │              │                │
       PC/UEFI          SBC             selain
          │              │                │
        ajurit         ajurit           ajurit

Rooli tulee näiden päälle: wasm32/selain voi olla terminal tai cpu (ja
myöhemmin ehkä muutakin), amd64/PC terminal, cpu, file tai auth.

Terminal ei ole webterm. Se on Plan2001:n terminal, joka tässä
tapauksessa ajaa wasm32:lla. Nimiavaruus ja 9P toimivat samoin riippumatta
siitä, missä muut palvelut ovat:

    wasm32-terminal
        ├── mount -> amd64-tiedostopalvelin
        ├── rcpu  -> arm64-CPU-palvelin
        └── auth  -> riscv64-auth-palvelin

Arkkitehtuuri ei näy 9P:n yli, eikä sen pidä näkyä.

## Selain on wasm32:n alusta

Selain vastaa PC:n UEFI-, PCI- ja alustakerrosta, ei käyttöjärjestelmää:

| wasm32-ytimen ajuri | Selaimen rajapinta |
|---|---|
| näyttö | Canvas, WebGPU |
| hiiri | Pointer Events |
| näppäimistö | Keyboard Events |
| kello ja ajastin | selaimen ajastimet |
| verkko | WebSocket, WebTransport |
| tallennus | OPFS, IndexedDB tarvittaessa |

Niiden päällä on sama Plan2001 kuin muillakin koneilla: /dev/draw,
/dev/mouse, /dev/kbd, /net, nimiavaruus, 9P, rc, rio.

    rio -> /dev/draw -> devdraw -> wasm32-näyttöajuri -> Canvas/WebGPU
    rio -> /dev/draw -> devdraw -> PC:n näyttöajuri  -> kehyspuskuri/GPU

Canvas ei kuulu rioon eikä ohjelmaan, vaan selainalustan näyttöajurin
taustaksi. Selaimen JavaScript on ytimelle samaa kuin firmware ja
alustaliima fyysiselle koneelle, ja `plan9.syscall` on tavallinen
järjestelmäkutsu eikä selaimen RPC.

Tällä määrittelyllä Monolith, drawterm ja webterm voidaan poistaa
lopullisesta arkkitehtuurista.

## Kommentit ja tarkennukset (Claude, 1.10.2026)

Malli on oikea, ja se on myös yksinkertaisempi toteuttaa kuin nykyinen.
Toteutuksen kannalta seuraavat kohdat ratkaisevat, miten sinne päästään.

1. **Prosessit ovat Workereita, ei ytimen vaihtamia pinoja.**
   WebAssemblyssa ei voi pysäyttää käynnissä olevaa pinoa ja jatkaa toista,
   ainakaan ennen kuin stack switching -ehdotus on kaikissa selaimissa.
   Plan 9 -ytimen sched() ja kontekstin vaihto eivät siis siirry
   sellaisenaan. wasm32-kone on moniprosessori, jossa jokainen proc on oma
   Workerinsa. sleep ja wakeup ovat `Atomics.wait` ja `Atomics.notify`.
   Drawtermin ydin toimii jo näin: kprocit ovat pthreadeja.

2. **Osoiteavaruudet.** Jokaisella prosessilla on oma WebAssembly-muisti,
   mikä vastaa omaa osoiteavaruutta MMU:lla. Ydin ei voi osoittaa toisen
   moduulin muistiin suoraan, joten copyin ja copyout (validaddr ja
   sen kumppanit) ovat alustan primitiivejä: JavaScript kopioi muistista
   toiseen kuin DMA. Tämä raja on ytimessä jo valmiina. Vaihtoehto, yksi
   jaettu muisti kaikille, olisi kuin Plan 9 ilman MMU:ta: yksinkertaisempi,
   mutta ilman suojausta.

3. **Ydin tarvitsee setjmp/longjmp:n ennen kuin 3c voi kääntää sen.**
   9frontin port/-ydin käyttää waserror()- ja nexterror()-kutsuja noin 460
   kohdassa. 3c:n ja 3l:n on siis ensin tuettava WebAssemblyn poikkeuksia
   (try/catch, throw). Toinen edellytys on atomics `_tas`- ja lock-
   kutsuille, koska Workerit ovat oikeita rinnakkaisia suorittimia.

4. **Verkko selaimessa.** Selain ei avaa raakoja TCP- tai UDP-yhteyksiä,
   joten /net ei voi olla tavallinen devip. Plan 9:n oma vastaus on
   valmiina: terminal tuo /net:n CPU-palvelimelta (`import -E ... /net`)
   WebSocketin yli kulkevan 9P:n kautta. Alustan "verkkokortti" on silloin
   yksi 9P-yhteys, ei uusi ajuri.

5. **Käynnistys noudattaa Boot ABI:a.** Selainalustan firmware on sivu, joka
   hakee kernel.wasm:n ja juurilevyn ja antaa ytimelle BootInfon:
   config, kehyspuskuri (canvasin koko), RNG (`crypto.getRandomValues`),
   RTC (`Date.now`). Se on sama data-ABI kuin amd64:llä ja arm64:llä
   (docs/boot-abi.md); vain entry-ABI on wasm32:n oma.

6. **Siirtymä ilman katkoa.** Pilvi ja nykyiset sovellukset käyttävät
   Monolithia. Drawtermin ydin on jo toimiva Plan 9 -ydin WebAssemblyna
   (emcc), ja siinä ovat devdraw, devmouse ja devcons. Se kelpaa
   ensimmäiseksi wasm32-ytimeksi, jonka alla 3c/3l-prosessit ajetaan. Sen
   jälkeen se vaihdetaan 9frontin port/-ytimeen, jonka 3c kääntää. Monolith,
   drawterm ja webterm poistuvat lopussa, eivät alussa.

7. **Rooli cpu selaimessa** on se, mitä compute pool tekee jo nyt: selain
   on laskentaresurssi (CR). Mallissa se on vain wasm32-kone cpu-roolissa.

## Vaiheet

| Vaihe | Sisältö | Valmis kun |
|---|---|---|
| A | 3c/3l-prosessit drawtermin ytimen alla: prosessi on Worker, syscall on drawtermin sysopen, sysread jne., copyin ja copyout alustassa | 3c:llä käännetty clock ja rc toimivat selaimessa (1.10.: rc, fork, exec, putket ja wait sekä 9frontin muuttamaton clock event-kirjastoineen toimivat) |
| B | 3c/3l: poikkeukset (setjmp, waserror), atomics, rfork(RFMEM) eli säikeet samassa muistissa, libthread | libthreadia käyttävä ohjelma toimii |
| C | wasm32-ydin: 9frontin port/ ja `plan2001/sys/src/9/wasm32` (alusta, ajurit) 3c:llä käännettynä, JavaScript vain alustaliimana | ydin käynnistää rc:n selaimessa ilman drawtermia |
| D | Boot ABI wasm32/selaimelle, terminal- ja cpu-roolit | Monolith, drawterm ja host3 poistettu; webtermistä jää WebSocket-kuljetin |

## Vaihe D: wasm32-kone Plan2001:n terminalina (suunnitelma 2.10.2026)

Selaimen wasm32-kone on Plan2001:n terminal ja myöhemmin cpu. Monolith,
drawterm ja host3 poistuvat. webterm ei katoa kokonaan: selain ei avaa
TCP-yhteyksiä, joten palvelimen puolella jokin ottaa WebSocketin vastaan,
ja webterm tekee sen jo (`GET /17019`, `/567`, `/rcpu`). Siitä jää
kuljetin, ja sivujen tarjoilu ja Monolithin erityispolut poistuvat.

Repon rakenne (päätös 4.10.2026, korvattu 7.10.: yksi puu `plan2001/`, `docs/plan9-wasm32.md`): Plan 9 -fork (`plan9/`: boot ja
arkkitehtuurit, 9front-yhteensopiva), 9Front-2001 (9front + fork,
koottu) ja Plan2001 (`plan2001/`: legacyä rikkovat muutokset ja
palvelut) - `docs/plan9-fork.md`. Vaiheiden D työ tehdään sen
sijoitussääntöjen mukaan.

| | Sisältö | Valmis kun |
|---|---|---|
| D1 | paikallinen terminal: rio (libframe, libplumb) 3c:llä ytimen päälle; exec RFMEM-procista (rion ikkunat); `/boot/init` voi käynnistää rion; ramfs `/tmp`:ksi | rio, ikkunat, rc ikkunassa ja clock ikkunassa selaimessa ilman verkkoa (valmis 2.10.) |
| D2 | verkko: wasm32:n oma `/net` (tcp: clone, ctl, data, local, remote, status); `dial tcp!kone!17019` avaa WebSocketin webtermin samaan polkuun kuin drawtermin wsock.c; pieni `/net/cs`; samat sallitut palvelut kuin webtermillä | `dial` webtermin kautta: auth (567) ja rcpu (17019) vastaavat (valmis 2.10.) |
| D3 | tunnistus: libmp, libsec, libauthsrv 3c:llä; factotum selaimen koneeseen, salasana ensin kysymällä; avainnippu secstoresta (päätös 4.10., alla) | factotum hoitaa dp9ik:n VM:n auth-palvelimelle (valmis 4.10.; VM:ssä testattu) |
| D4 | rcpu: 9frontin `rcpu`, `tlsclient` ja `exportfs`; terminal vie ruutunsa, näppäimistönsä ja hiirensä cpu-palvelimelle kuten drawterm; wss-polulla `/rcpu` ilman TLS-PSK:ta kuten Monolithissa | VM:n rio näkyy selaimen wasm32-ytimen ruudulla rcpu:n kautta (toteutettu 4.10. `/17019`:n TLS-PSK-polulla; koneen sisäinen rcpu testattu, VM tarkistamatta; `/rcpu` tekemättä) |
| D5 | Boot ABI: `plat*`-käynnistyskutsujen tilalle BootInfo (config, kehyspuskuri, RNG, RTC, juuren arkisto) docs/boot-abi.md:n data-ABI:na; wasm32:n entry-ABI on `_start` ja BootInfon osoite; `getconf` configista | sama BootInfo-data kuin amd64:llä ja arm64:llä; sivu on firmware (valmis 4.10.) |
| D6 | tallennus: OPFS koneen levynä, pysyvä ja kirjoitettava juuri tai `/usr/$user` | tiedosto säilyy sivun uudelleenlatauksen yli (valmis 4.10.) |
| D7 | siirtymä: index.html (Monolith) korvataan wasm32-terminalilla; app-originit (docs/app-origins.md) wasm32-koneina; host3, third_party/drawterm ja Monolithin JS poistetaan; webtermistä poistetaan sivujen tarjoilu | pilvi ja sovellukset toimivat ilman drawtermia (toteutettu 4.10.; koneen sisäinen sovellusistunto testattu, VM ja pilvi tarkistamatta) |
| D8 | cpu-rooli: selain compute poolissa wasm32-koneena | **odottaa (päätös 4.10.)**: suunnitellaan uudelleen XCPU-arkkitehtuuri huomioiden, kun 9front + wasm32 on viimeistelty ja testattu (alla) |

Järjestys (päätös 2.10.): rio paikallisesti ensin, sillä se on ytimen ja
ruudun luonteva koe ja terminal tarvitsee sen joka tapauksessa. host3
poistetaan vasta D7:ssä.

D1 valmis (2.10.), Plan 9:n tapaan:
- Rio on taas perusjärjestelmää. Se poistettiin 28.9. (a7aaebf, Monolithin
  vaihe 3a), ja jäänteet on purettu: `derive.py`:n "no rio" -sääntö,
  pilven `setup.rc` ja `check.rc` sekä `vm-cpu`, jotka poistivat rion ja
  `riostart`in. Glendan profiili käynnistää terminaalissa jälleen
  `rio -i riostart`:n kuten 9front (asennustikulla rc-kehote).
- Subset on johdettu uudelleen (`tools/subset/derive`, `make.py`; `test`
  PASS, `check` 0 eroa): rio, libframe, libcomplete, `frame.h` ja
  `complete.h` 9frontin muuttamattomina sekä `extra`ssa perustellut, joita
  analyysi ei näe: `window`, `wloc`, glendan `riostart` ja `plumbing`,
  `stats` ja `unicode.font`in vga-alifontit (binääreinä ne tulevat
  tikulle VM:n puusta, wasm32:n juureen `build/subset`ista).
- wasm32: rio, plumber, plumb, stats, ramfs, syscall, sed, mount, pwd ja ps
  3c:llä; `/rc/bin` (window, wloc) `/bin`in perään kuten `/lib/namespace`;
  `#d` `/fd`ksi; 9frontin `/mnt`-liitospisteet; tyhjät hakemistot
  juuren arkistossa (`a/b/`); ytimen keko koko muisti (9frontin poolit
  4 ja 16 Mt); libc:n `Along`/`Aptr`-atomit; exec RFMEM-procista (argv
  kopioidaan kutsun mukana, ja procin apuWorkerista tulee uuden ohjelman
  Worker) - rion ikkunat käynnistyvät niin.
- `/boot/init` kuten 9front: aikavyöhyke, kbdfs, glendan login-rc
  (`rc -l`), jonka profiili käynnistää ramfs:n `/tmp`:ksi, plumberin ja
  rion riostartilla (stats ja rc-ikkuna). Sarjakonsolin rc on
  `/boot/console`. webcookies ja webfs tulevat D2:ssa.
- Testit: `rioclock` (clock rion ikkunassa), `riorc` (selaimen näppäimet
  rion rc-ikkunaan, äåö) ja `boot` (oletusinit: riostart, napsautus
  ikkunaan ja kirjoitus siihen); yhteensä 27.

D2 valmis (2.10.): `#I` (`devwsnet.c`) on wasm32:n verkko, sidottuna
`/net`iin. `/net/tcp/clone` ja `n/{ctl,data,local,remote,status}`:
`connect kone!portti` avaa WebSocketin osoitteeseen `ws` + `/portti`
(kernel.html: https:llä wss samaan originiin, http:llä vain loopbackilla
seuraava portti, jonka `tools/serve` välittää VM:n webtermille; `?ws=`
poistettiin D7:n katselmuksessa). Isäntä ei ratkaise mitään: WebSocket-palvelin on kone.
Sivu omistaa WebSocketit, koska Workerit odottavat `Atomics.wait`issa:
saapuvat tavut menevät keskustelun 64 kt:n renkaaseen ytimessä, ja täysi
rengas katkaisee yhteyden (ei tavujen menetystä), lähtevät kulkevat
viestinä sivulle. `/net/cs` vastaa kuten ndb/cs: `net` tai `tcp`, isäntä
sellaisenaan, palvelu nimellä (`rcpu`, `ticket`, `exportfs`
`/lib/ndb/common`ista) tai numerona; authdialin `net!...!ticket` toimii.
Palvelut rajaa webterm (17019, 567; `-s`:llä 17030 ja `/rcpu`).
Testi `net` (tarvitsee CPU-VM:n, `tools/vm start --disk
~/.cache/plan2001/cpu.qcow2 --net`, origin 127.0.0.1:18080): rcpu:n
p9any-tervehdys (dp9ik), haaste ja palvelimen tikettipyyntö takaisin,
auth `net!cpu!ticket`illa, ja portti, jota webterm ei salli, torjutaan.
Syöte (2.10., katselmus): mikään ei katoa ylivuodossa. Sivun näppäimistö-
ja hiirijonot odottavat, kun ytimen rengas on täynnä; ytimen kbdin-kproc
kirjoittaa kbdfs:n jonoon estävästi (`qwrite`). Hiiressä vain peräkkäiset
liikkeet samoilla painikkeilla yhdistyvät, painallus, vapautus ja rulla
eivät katoa. Liitetty teksti kulkee näppäiminä (`r`/`R`, muokkausnäppäimet
ensin ylös, CR ja CRLF rivinvaihdoksi), sillä kbdfs:n `c`-polku pudottaa
(`nbsend`) eikä mene riolle; Meta yksin on Kmod4. kbdfs heittää pois
syötteen, jota kukaan ei lue `/dev/cons`ista - 9frontin tapa.
Katselmuksen korjaukset (2.10.):
- Renkaiden laskurit (verkko, eia0, näppäimistö, hiiri) ovat
  etumerkittömiä modulo 2^32 ja renkaat kahden potensseja: indeksi on
  maski sekä C:ssä että JS:ssä (`>>> 0`, `& (N-1)`). Ennen 2 Gt:n
  liikenne olisi tehnyt JS:n Int32-laskurista negatiivisen ja kirjoittanut
  ytimen muistiin renkaan eteen. Testi: `?ringstart=` aloittaa
  verkkorenkaan laskurit 2^31:n ja 2^32:n alta, ja liikenne kulkee rajan
  yli.
- `/net/tcp/n` näkyy vasta, kun keskustelu on valmis: `newconv` ottaa
  paikan lukon alla (`taken`), alustaa sen ja vasta sitten julkaisee
  (`used`).
- Keskustelun WebSocket on sivun (n, gen): uusi gen joka kerta, ja
  open, send ja close kantavat sen, joten vanhan WebSocketin close, data
  tai loppu ei koske uuteen. Renkaan tyhjentää sivu uuden genin avauksessa,
  ja vain sivu kirjoittaa sitä, kaikki samassa säikeessä; ydin lukee vasta,
  kun avaus on kuitattu. Avaus luopuu 60 s:n jälkeen kuten TCP:n connect,
  sillä selain voi pidätellä WebSocketia pitkään epäonnistumisten jälkeen.
  Testi `netloop`: neljä procia avaa ja sulkee rcpu-yhteyden 20 kertaa,
  eikä yhdessäkään ole toisen tavuja. VM:n webterm sulkee nopeista
  yhteyksistä noin kolmanneksen tai jättää hiljaiseksi (alarm rajaa
  luvun); se on palvelimen asia.
- Ohjelman virhe (WebAssemblyn trap, kelvoton taulun indeksi, pinon
  loppuminen) lopettaa vain ohjelman: `sys: trap: ...` exitsinä kuten
  Plan 9:ssä, kone jatkaa (testi `fault`). Uusi ohjelma aloittaa ilman
  edellisen ohjelman odottavaa notea, ja note, jonka käsittelijä ei ole
  ohjelman taulussa, lopettaa ohjelman eikä konetta. (plan2001.com:ssa
  `echo` sai rc:n käsittelijän (240, echon taulussa 170); syytä ei saatu
  toistettua.)

Sivu (2.10.): kernel.html on koneen ruutu koko ikkunassa (ruudun koko
ikkunan mukaan käynnistyksessä, `?screen=LxK` muu), rio täyttää sen;
sarjakonsoli `?console=1`:llä alla. Juuri on yksi tiedosto `w3root.fs`
(rootfs.c:n arkisto, `tools/build-bin3`), koska webterm tarjoilee vain
litteitä tiedostoja; ilman sitä sivu kokoaa juuren `w3root.txt`:n
listasta. `net`-testissä auth-yhteys (`/567`) torjutaan joskus heti
rcpu-yhteyden jälkeen: VM:n webterm sulkee osan nopeista yhteyksistä
(mitattu myös suoraan Linuxista, 26/80).
webfs ja webcookies (profiili) tarvitsevat yleisen verkon, jota webterm ei
anna: ne odottavat selaimen fetchin päälle tehtävää palvelua (myöhemmin).

D3 toteutettu (4.10.), VM-testi ajamatta: tunnistus 9frontin koodilla.
- libmp, libsec, libauthsrv, libndb, libip ja libString 3c:llä
  (`tools/build-libc3`: tiedostot mkfilen listasta, port/:n C), libc:hen
  `ucd/` (Unicode-taulut: `toupperrune` ...). Testi `crypto` (`d3`):
  md5, sha1, sha2, hmac, AES-CBC, ChaCha20, ccpoly, PBKDF2, HKDF,
  curve25519 ja libmp:n mpexp/mpmul/mpdiv tavu tavulta samat kuin
  Pythonin hashlib ja cryptography; `passtokey` (AES ja DES) samoin;
  AuthPAK:n puolet sopivat, väärä salasana ei; form1-lippu kulkee ja
  muutettu torjutaan.
- `auth/factotum` (9frontin, mkfilen tiedostot) ja `mntgen`; `/boot/init`
  käynnistää ne kuten 9frontin bootrc terminaalilla: `/n`, `/mnt`,
  `/mnt/exportfs` ja `factotum -n -sfactotum` ilman avaimia. Avainta
  factotum pyytää tarvitessaan (`needkey`; libauthin `auth_getkey` ajaa
  `factotum -g`:n konsolille) - kysely tulee käyttöön D4:n rcpu:ssa, ja
  OPFS (secstore) D6:ssa.
- Ydin: `/proc/n/ctl` hyväksyy `private` ja `noswap` (factotum): ne
  pätevät sellaisinaan, sillä procin muistia ei voi lukea eikä mitään
  swapata. `/net/cs`:n ja `/net/tcp/clone`n stat ei löytänyt tiedostoa
  (devstat haki sitä keskustelun tiedostoista), joten factotumin
  `access("/net/cs")` epäonnistui; korjattu. `/proc`in ja `/net/tcp`:n
  hakemistonimet tehtiin pinon puskuriin, jonka `devdir` vain osoittaa
  (`ls /proc` antoi tyhjiä nimiä); nyt `up->genbuf` kuten 9frontissa.
- Testi `dp9ik`: glenda (asiakas) ja bootes (palvelin) tunnistautuvat
  putken yli p9any/dp9ik:lla (`auth_proxy` molemmin puolin, `d3 -p`).
  Käyttäjät eroavat, joten asiakkaan factotumin on haettava liput
  auth-palvelimelta (samalle käyttäjälle se tekee ne itse,
  `mkservertickets`): `net!p9auth.plan2001!ticket` -> `/net/cs` ->
  WebSocket `/567` -> `tools/test-authsrv`, joka on webtermin ja
  9frontin authsrv:n (AuthPAK, AuthTreq, form1) Python-toteutus
  9frontin mpc-lähteistä, C:stä erillään. Molemmat saavat saman
  AuthInfon ja 256 tavun salaisuuden; väärällä salasanalla factotum
  vastaa `needkey`; ilman auth-palvelinta (`dp9iknoas`) lippuja ei tule.
- Katselmus (4.10.): `/net/tcp`:n gen kasvoi vain keskustelua
  tehtäessä, eikä connect, hangup tai kirjoitus käyttänyt Conv:n
  QLockia. Hangup ja uusi connect samassa keskustelussa kantoivat siksi
  saman (n, gen):n, ja sivu, joka saa Workerien viestit missä
  järjestyksessä tahansa, saattoi sulkea uuden WebSocketin vanhalla
  closella tai lähettää vanhan yhteyden tavut uuteen. Nyt gen kasvaa
  joka connectissa, ja connect, hangup ja kirjoitus ovat QLockin alla
  (kirjoitus kantaa sen yhteyden gen:n, joka oli auki). Data-lukija ei
  herännyt hangupiin ollenkaan (se odotti vain renkaan `w`:tä), ja sivu
  nollaa renkaan avatessaan: vanhan yhteyden lukija olisi voinut
  kirjoittaa `r`:n uuteen renkaaseen. Nyt lukijat lasketaan, lukija
  lähtee, kun gen vaihtuu tai yhteys menee (viimeistään sekunnissa), ja
  connect pyytää avausta vasta, kun lukijoita ei ole. Testi `netre`
  (`d3 -n 20 4`): neljä procia yhdistää oman keskustelunsa 20 kertaa
  (connect, AuthPAK-pyyntö, vastaus, hangup samalla ctl:llä), jokainen
  vastaus ehjä; odottava lukija herää toisen procin hangupiin, ja
  keskustelu yhdistyy uudelleen. Vanhalla koodilla lukija jää odottamaan;
  järjestyskilpaa testi ei saa varmasti esiin (ajoitus).
- Testi `dp9ikvm` (`d3 -r cpu`: glenda VM:n rcpu:lle, VM:n factotum ja
  auth-palvelin, salasana `~/.cache/plan2001/cpu.pass`) on kirjoitettu,
  mutta ajamatta: kehitysympäristössä ei ollut CPU-VM:ää. Se on D3:n
  "valmis kun" -ehto, joten D3 on valmis, kun `dp9ikvm` menee läpi.
  (VM:ssä testattu ja kunnossa, käyttäjä 4.10.)

D4 toteutettu (4.10.), VM:n rio ruudulla tarkistamatta: rcpu 9frontin
ohjelmilla, `/17019`-polulla TLS-PSK:n kanssa kuten 9frontissa.
- Ydin: 9frontin `port/devtls.c` (`#a`, `/net/tls`, sidottu `/net`in
  perään kuten bootrc) ja libsec ytimeen; satunnaisuus on alustan
  (`genrandom`). Testi `tls` (`d4`): asiakas ja palvelin putken yli
  ennalta jaetulla avaimella (pskID `p9secret`, kuten rcpu dp9ik:n
  salaisuudella), rivi kumpaankin suuntaan ja megatavu, sha256 sama
  molemmissa päissä; väärä avain torjutaan.
- Ohjelmat: `tlsclient`, `exportfs`, `read`, `test`, `chmod` ja
  `/rc/bin/{rcpu,rconnect}` 9frontin sellaisinaan; factotum myös
  `/boot/factotum`ina (libauthin `auth_getkey` ajaa sen `-g`:llä
  avainta kysymään, kuten 9frontin bootfs:ssä).
- Osajoukko: `tools/subset/extra`an `/rc/bin/rcpu`, `/rc/bin/rconnect` ja
  `/amd64/bin/exportfs` perusteluineen; lähteet `subset/9front`iin
  9frontin gitistä - näihin polkuihin ei ole muutoksia julkaisun 11952
  jälkeen (ja jo osajoukossa olevat devtls.c, tlsclient.c, tlshand.c ja
  devpipe.c ovat gitissä tavu tavulta samat). derive.py:n `SRCMAP`:
  `/amd64/bin/test`in lähde on `test.c`, ei hakemisto `/sys/src/cmd/test/`
  (9frontin testisarja, joka oli osajoukossa väärin perustein).
  `subset/amd64/files`in rivit on lisätty käsin make.py:n muodossa:
  `tools/subset/derive` ja `make.py` VM:ssä vahvistavat ne ja tuovat
  rconnectin staattisen analyysin riippuvuudet (aan, tlssrv ...), ja
  `subset/amd64/proto` päivittyy samalla.
- Ydin, 9frontin tapaan: `/proc/n/args` kirjoitettava (rconnect kirjoittaa
  siihen isännän); devwsnet:n odotukset ottavat noten kuten `sleep`
  (`notepending` nollaksi, Eintr): ennen se jäi asetetuksi, ja mountin
  lukija tällä yhteydellä keskeytyi joka kerta uudelleen - devmnt lähetti
  tuhansia Tflusheja ja ydinkeko täyttyi (`no memory for allocb`).
- Sivu (platform.js): rfork(RFMEM)-procin note, joka tuli procin ollessa
  estettynä, jäi odottamaan seuraavan kutsun loppua - ja jos seuraava
  kutsu esti taas (exportfs:n orjat rendezvousissa), ei koskaan. Nyt se
  menee käsittelijälle seuraavan kutsun alussa. exportfs:n `fatal`
  (`kill` ryhmälle) lopettaa orjat, ja rcpu:n tulos voi kulkea putkessa.
  Testi `notekill`.
- Testit `rcpu` ja `rcpuask`: koneen oma rcpu (rconnect, tlsclient -a,
  exportfs) koneen sisäiselle rcpu-palvelimelle (`d4 -s`: tlssrv -a ja
  9frontin `tcp17019`:n skripti, oma noteryhmä ja nimiavaruus kuten
  aux/listenin palveluilla) `tools/test-authsrv`in releen kautta (`/17019` yhdistetään
  odottavaan `/17999`:een), liput auth-palvelimelta (glenda ja bootes).
  Etäkomento ajetaan `service=cpu`:lla, ja se näkee terminaalin
  nimiavaruuden: `/mnt/term/env/sysname`, `/mnt/term/boot`, terminaalin
  `/dev/draw/new`. `rcpuask`: avainta ei ole, factotum kysyy sen
  konsolilla (`!Adding key`, `user[glenda]:`, `password:`), kaiutonta
  salasanaa, ja avain jää factotumiin. exportfs sanoo lopussa `short write
  in reply`, kun palvelin on jo mennyt; testit jättävät sen pois.
- Testi `rcpuvm` (CPU-VM:n oikea rcpu: palvelu `cpu`, terminaalin draw
  näkyy) on kirjoitettu, ajamatta. D4:n "valmis kun" tarkistetaan
  käsin: selaimen koneen rion ikkunassa `rcpu -h cpu`, salasana
  kysyttäessä, ja VM:ssä `rio` (tai `clock`) piirtää ikkunaan.
- Vielä tekemättä: wss-polku `/rcpu` ilman TLS-PSK:ta (webterm `-s`)
  https-sivulle, jossa yhteys on jo salattu.
- Katselmus (4.10.): wasm32:n devproc käytti procia tarkistamatta sitä
  lukon alla - kirjoitettava `/proc/n/args` olisi voinut vapauttaa
  argsin lukijan alta tai kirjoittaa uuden procin rakenteeseen samassa
  paikassa. Nyt kuten 9frontissa: `proctab`, `p->debug` (eqlock), pid
  lukon alla, ja luku kopioidaan lukon alla; exec vaihtaa nimen ja
  nollaa argsin saman lukon alla (rconnectin isäntä ei jää seuraavalle
  ohjelmalle). Testi `procargs`. devwsnet:n `netwait`in 60 s:n raja ei
  lauennut koskaan (laskuri pysähtyi nollaan): nyt kellon takaraja;
  testi `nettimeout` (test-authsrv:n `/17998` ei vastaa koskaan).

D5 valmis (4.10.): wasm32 käynnistyy Plan2001 Boot ABI v1:llä kuten pc64
ja arm64 (`docs/boot-abi-wasm32.md`). Sivu (`platform.js`:n `firmware()`)
tekee BootInfo-blobin koneen muistiin: config (plan9.ini: initin argv
`init=`-rivinä ja sivun `?conf=`-rivit), muistikartta, epoch,
RNG-siemen, kehyspuskuri ja juuren arkisto. Kernel saa blobin osoitteen
`_start`in kautta `main`in argumenttina.
- Kernel käyttää forkin yhteistä koodia, `port/bootinfo.c`:tä ja
  `port/bootargs.c`:tä (`bootinfoinit`, `getconf`, `setconfenv`,
  `*bootscreen`). Oma osuus on `wasm32/bootarch.c` (`bootearlymap`,
  `halt`).
- `confinit` lukee muistin kartan Conventional-alueista. `screen.c`
  piirtää BootInfon kehyspuskuriin (`allocmemimaged`), ja `devrootfs.c`
  lukee arkiston paikallaan (`rdbase`/`rdlen`, lisätty headerin loppuun
  v1:ssä). RNG-siemen sekoitetaan `hwrandbuf`in kautta, ja kello
  asetetaan epochista.
- Poistuivat `platbootfs`, `platbootargs`, `platscreen` ja `platfb`.
  Ytimen 64 Mt on nyt kokonaan sen imagea ja kekoa: blob, arkisto ja
  kehyspuskuri ovat sen yläpuolella. Ennen arkisto kopioitiin kekoon.
- Testit: `bootinfo` (configin rivi ympäristössä, `*bootscreen` 640x480,
  kello), `badblob` (kernel pysähtyy ennen mitään, kun blob on väärän
  ISA:n tai kun arkisto tai framebuffer on jaettavassa muistissa) ja
  `oldheader` (vanhemman v1-loaderin lyhyempi header, jonka perässä on
  roskaa: se kelpaa, ja puuttuvat kentät ovat 0).
- Katselmus (4.10.): kernel vaati, että `headersize` on vähintään sen oma
  `sizeof(BootInfo)`. Silloin `rdbase`/`rdlen`in lisäys olisi pysäyttänyt
  uuden kernelin vanhalla v1-loaderilla. Nyt kernel kopioi headerin
  nollatäytettynä. Arkisto ja framebuffer tarkistetaan muistikarttaa
  vasten, ja wasm32:n `bootearlymap` tarkistaa muistin todellisen koon
  (`platmemsize`), ei 4 GiB:tä.
  Kaikki muut testit menevät läpi entisellään, koska initin argv kulkee
  nyt configin kautta.

D6 valmis (4.10.): koneella on levy, ja Plan 9:n tapaan sen päällä on
tiedostopalvelin.
- Levy on `#S/sdW0/{ctl,data}` (`devsdw.c`), nimetty kuten 9frontin sd.
  Se on tiedosto originin OPFS:ssä (`sdW0`, oletuksena 256 Mt,
  `?disk=MB`, `?disk=0` = ei levyä). OPFS:ää voi käyttää synkronisesti
  vain Workerista ja vain yksi kerrallaan, joten sivun levy-Worker
  (`platform.js`: `disk()`) omistaa sen. Toinen saman originin
  välilehti jää ilman levyä.
- Firmware kuvaa levyn BootInfossa kuten laitteen: configin rivi
  `*sdW0=regs tavut`, ja rekisterisivu on muistikartassa Reserved.
  Ydin kirjoittaa pyynnön rekistereihin (op, pituus, osoite, offset),
  ja levy-Worker odottaa niitä `Atomics.wait`illa. Kutakin pyyntöä
  kohti ei siis tarvita postMessagea. Worker lukee ytimen muistiin tai
  kirjoittaa sieltä tiedostoon. Kirjoitukset viedään tiedostoon
  (`flush`), kun levy on ollut hetken joutilaana, ja ctl:n `flush`illa.
- Tiedostojärjestelmä on 9frontin hjfs, käännetty 3c:llä (`auth.c`
  forkin oma). `/boot/disk`, jota `/boot/init` kutsuu, käynnistää
  hjfs:n (`/srv/disk`). Uusi levy reamataan, ja hjfs:n konsolilla
  luodaan `/usr/glenda`. Sitten `bind -bc /n/disk/usr/glenda
  /usr/glenda`: juuren arkiston tiedostot näkyvät edelleen, ja
  glendan uudet tiedostot menevät levylle.
- `test-wasmapp`: `THEN='[argv]'` lataa sivun uudelleen samassa
  selaimessa (sama OPFS), ja EXPECT on toisen latauksen tuloste.
  Testi `disk`: tiedosto kirjoitetaan, hjfs synkataan ja levy
  flushataan, sitten uudelleenlataus. Tiedosto on tallessa, eikä levyä
  reamata toista kertaa.
- Katselmus (4.10.): levy-Workerin joutilas-flush oli virheenkäsittelyn
  ulkopuolella. Jos OPFS heitti virheen, Worker kuoli, ja ydin odotti
  pyyntöään ikuisesti QLock hallussaan, jolloin myös hjfs ja
  `/usr/glenda` jumittuivat. Nyt Worker ottaa kaikki virheet kiinni ja
  merkitsee levyn kuolleeksi (rekisteri `Rstate`). Se päättää odotetun
  pyynnön tulokseen -1. Sivu tekee saman, jos Worker kuolee muuten
  (`onerror`). Ydin palauttaa `Eio`n, ja pyynnöllä on 30 sekunnin
  takaraja. Takaraja on turvallinen, koska Worker koskee vain levyn
  omaan bounce-puskuriin (64 KiB, ei koskaan vapautettu): myöhässä tuleva
  vastaus ei kirjoita muistiin, jota ydin käyttää muuhun. Lisäksi
  firmwaren `*sdW0=`-rivi on nyt configin viimeisenä, joten sivun
  `?conf=` ei voi korvata laitekuvausta. Testi `diskdead`: flush
  epäonnistuu, luku antaa i/o-virheen, levy on `dead` ja kone jatkaa.
  Väärennetty `*sdW0` ei vaikuta. Ilman korjausta luku jää odottamaan.
- Rajat (alkuperäiset): liitos oli vain kotihakemiston yläpuolella
  (bind -bc), ja hjfs synkkasi 10 sekunnin välein. Factotumin avaimet
  eivät tule levylle selväkielisinä: ne ovat secstoressa, ja levyllä on
  korkeintaan salattu välimuisti (päätös 4.10., "Avainten paikka").
- Viimeistely (4.10.): koko kotihakemisto on levyllä. `/boot/disk` bindaa
  levyn `/usr/glenda`n juuren kotihakemiston päälle, ja juuren
  `/usr/glenda` täydentää siitä puuttuvat tiedostot kuten newuser:
  uusi levy saa kaiken, vanha uudet tiedostot, eikä mitään käyttäjän
  omaa korvata. Kopiot ovat käyttäjän muokattavia (664, `bin`issä 775),
  koska juuren tiedostot ovat kaikki 0555. `/boot/disk` synkkaa hjfs:n
  2 sekunnin välein, joten suljettu sivu menettää enintään sen verran.
  Levy-Worker kirjoittaa tiedostoon neljännessekunnin sisällä
  viimeisestä kirjoituksesta. Testi `disk`: tiedosto `lib/`-hakemistossa,
  johon juurellakin on tiedostoja, ja muokattu profiili säilyvät
  uudelleenlatauksen yli, ja juuren tiedostot ovat mukana. Vanhalla
  skriptillä testi kaatuu.

D7 toteutettu (4.10.), VM:ssä ja pilvessä tarkistamatta: selaimessa on
vain wasm32-kone. Käyttäjä päätti, että rc-httpd tarjoaa sivut samassa
portissa ja että kaikki Monolithin JS poistetaan nyt.
- Palvelin: 9frontin rc-httpd tlssrv:n takana (17443) tarjoaa sivun
  Plan2001:n select-handlerilla (`plan2001/rc/bin/rc-httpd`). Staattinen
  käsittelijä lähettää COOP/COEP-otsakkeet, oikeat tyypit ja gzipin,
  ETag/304. `/app` kertoo originin sovelluksen. WebSocketit menevät
  `webterm -s -r`:lle, joka saa rc-httpd:n jo lukeman pyynnön
  (`$request`, `$reqlines`). Webterm ei enää tarjoa sivuja (`-w` ja
  `-n` poistettu, `POST /log` samoin). Policyn sana `cpu` sallii `/17019`:n
  eli rcpu:n sellaisenaan (`term`). Julkinen hiekkalaatikko on rc-httpd
  `$pagesonly`lla (`tools/cloud/sandbox.rc`). `monolith/tools/pages`
  kokoaa sivun, ja `deploy`, `tools/vm-cpu`, `tools/cpu-live.rc pages` ja
  `tools/cloud/sandbox` asentavat sen. rc-httpd on lisätty
  `tools/subset/extra`an (derive VM:ssä tekemättä).
- Pääte ja sovellukset: sama sivu joka originille
  (`docs/app-origins.md`, D7). Sivu kysyy sovelluksensa (`GET /app`) ja
  kirjoittaa BootInfoon `app=` ja `cpu=`. `term` ajaa rioa ja rcpu:ta
  `$cpu`:hun. Sovelluksen kone ajaa `/boot/app`: rcpu `aux/wsrcpu`:lla
  webtermin `/rcpu`-istuntoon. Sivun verkko hoitaa istunnon protokollan
  (kuittaukset, lähetetty tallessa, jatko katkoksen jälkeen).
  devwsnet:ssä `rcpuws` on polku `/rcpu`. Webtermin sovellusskriptin loppu
  on nyt rcpu:n palvelimen kaltainen (cpunote, tila, `rfailed`), koska
  asiakas on oikea rcpu.
- Testi `app`: `tools/test-authsrv`in `/rcpu` tekee webtermin istunnon
  koneen sisäisen palvelimen (`d4 -w`: p9any ja webtermin skripti)
  ympärille ja katkaisee sivun WebSocketin kerran. Sivu jatkaa
  istuntoa, ja 616512 tavun tiedosto kopioituu molempiin suuntiin.
  Ilman sivun istuntokerrosta auth epäonnistuu.
- Poistettu: `monolith/web` (index.html, monolith.js, cr.*),
  drawtermin `gui-web` ja `Make.emscripten`, `wasm32host` (host3),
  `wasmapp`, `third_party/9apps`:n wasmapp-osa (sen `clock.c` jää build-bin3:lle, kunnes osajoukko tuo sen), `plan2001/lib/app/compute` sekä
  testit ja työkalut `test-host3`, `test-headless`, `test-stress`,
  `test-apps`, `test-compute`, `test-wasmclang` ja `tools/term`.
  `third_party/drawterm` jää 9ptermiä varten: pilven hallinta (`cpu-live`,
  `cloud-image`, `kbuild9p`) käyttää sitä. `tools/build` rakentaa vain
  9ptermin ja natiivin drawtermin. `test-wasmapp` ajaa vain koneen sivua,
  ja sen käynnistysodotus ei enää odota olematonta tiedostoa, joten joka
  testi on noin 10 s nopeampi.
- Katselmus (4.10.), korjattu:
  - P0: kernel.html hyväksyi `?ws=`n. Sovelluksen `/rcpu`:ssa ei ole omaa
    TLS:ää (`aux/wsrcpu`: p9any, sitten raaka virta), joten sivun voi
    ohjata hyökkääjän WebSocketille, joka välittää authin oikealle
    webtermille ja näkee istunnon. Nyt verkko on aina sivun oma origin:
    https:llä `wss://` + host, http:llä vain loopbackilla seuraava portti
    (`tools/serve`, testit), muuten ei verkkoa. Webterm vaatii myös
    `Origin`in `/rcpu`:lle ja `/resume`lle.
  - P1: webtermin istunto varasi 8 Mt ja useita procseja ennen authia, eikä
    yhteydettömän istunnon ajastin koskenut kiinnitettyyn,
    autentikoimattomaan istuntoon. Nyt puskuri kasvaa vasta datan mukana
    (`Highwater` on yläraja), auth kertoo onnistumisestaan putkella, ja
    istunto päättyy, jos authia ei ole tullut 30 sekunnissa
    (`Authwait`) tai auth-lapsi kuolee. Istuntoja on yhtä aikaa
    enintään 32 (`Maxsessions`, laskettu `/srv`:n istuntomerkinnöistä
    ennen `wsaccept`ia).
  - P1: jatkossa webterm lähettää koko jonon yhtenä kehyksenä (enintään
    8 Mt), mutta sivu kirjoitti sen suoraan 64 KiB:n ytimen renkaaseen ja
    katkaisi istunnon, kun se ei mahtunut. Nyt sivulla on jono, josta
    tavut menevät renkaaseen sitä mukaa kuin ydin lukee. Istunto kuittaa
    vain renkaaseen menneen, joten webtermin `Highwater` rajaa myös sivun
    jonoa (`QMAX`, tavallisella yhteydellä `QMAXRAW` 1 Mt). Testi
    `appresume`: 16 rinnakkaista kopiota, ja test-authsrv pysäyttää sivun
    sekunniksi ennen katkaisua, joten jatko lähettää 98696 tavua yhtenä
    kehyksenä. Ilman korjausta istunto katkeaa.
  - P1: sivu piti kuittaamattomat lähetyksensä ilman rajaa. Nyt renkaassa
    on sana `sendq`, jossa on sivun hallussa olevat tavut (lähettämättä
    tai kuittaamatta). devwsnet:n kirjoittaja odottaa, kun sana ylittää
    `Sendhigh`in (1 Mt), kuten täyden putken kohdalla, eikä odota
    QLockin alla. Testi `sendq`: istunto, joka ei koskaan kuittaa
    (test-authsrv:n sink). Sivu pitää enintään noin 1 Mt, ja kirjoittaja
    odottaa. Ilman korjausta sivu pitää 3,7 Mt.
  - Webtermin korjaukset on käännetty natiivilla 6c:llä, mutta niitä ei
    ole ajettu oikeassa 9frontissa.
- Katselmus 2 (4.10.), korjattu:
  - P1: jatkossa vanhan liitoksen lukija saattoi kirjoittaa jo lukemansa
    kehyksen rcpu:hun sen jälkeen, kun uusi liitos oli sanonut `r N`. Sivu
    lähetti saman kehyksen uudelleen, ja tavut tulivat kahdesti 9P-virtaan.
    Nyt kehys menee rcpu:hun ja `rcvd`:hen yhdessä `uplk`:n alla, ja vain
    jos liitos on yhä istunnon (`gen`). sessionctl ottaa saman lukon ennen
    kuin lähettää `r rcvd`. Kehys on siis joko laskussa tai sivu lähettää
    sen uudelleen, ei koskaan molempia. Lukko on oma, koska `s->lk`:n alla
    estynyt kirjoitus rcpu:hun lukitsisi sessiondownin.
  - P1: istuntoraja oli laskenta ja myöhempi luonti, joten yhtä aikaa
    tulleet yhteydet näkivät kaikki tilaa. Nyt istunto varaa jonkin 32
    paikasta, `/srv/webterm.slot.N`. Varaus on atominen, koska devsrv:n
    create tarkistaa ja luo saman lukon alla (Eexist). Paikka häviää
    istunnon viimeisen procin mukana (ORCLOSE).
  - P1: `sendq` oli sivun jälkikäteen kirjoittama määrä. Nopea kirjoittaja
    ehti jonottaa viestejä ennen kuin määrä näkyi, ja yksi iso write meni
    yhtenä viestinä. Nyt ydin lähettää kirjoituksen enintään 64 KiB:n
    paloina ja varaa jokaisen palan atomisesti (`cmpswap`) ennen
    `platnetsend`iä. Se odottaa, jos varaus veisi määrän yli `Sendhigh`in.
    Sivu vähentää sen, mitä se ei enää pidä (session kuittaama tai
    WebSocketin `bufferedAmount`ista lähtenyt). Raja on Sendhigh plus yksi
    pala. Testi `sendq`: yksi 4 Mt:n write (`d4 -B`) kuittaamattomaan
    istuntoon, ja sivu pitää enintään 1 Mt + 64 KiB. Vanhalla koodilla
    testi kaatuu.
  - Webtermin kaksi korjausta on käännetty natiivilla 6c:llä, ajamatta
    oikeassa 9frontissa.
- Laskentapooli (crsrv, rcc) jää palvelimelle, mutta selaimen CR on poissa
  D8:aan asti. Tarkistamatta: rc-httpd, select-handler ja webterm `-r`
  oikeassa 9frontissa (webterm käännetty natiivilla 6c:llä vain
  syntaksin osalta), sovellusten origin oikeaa webtermiä vasten,
  `tools/cloud-image` ja hiekkalaatikko.

## Avainten paikka: secstore (päätös 4.10.2026)

Seuraava tavoite on Plan2001:n amd64-versio pilvessä (9Front-2001 +
`plan2001/`). Sen kautta jaetaan selaimiin kahdenlaisia wasm32-koneita:
- **päätteitä**, joiden cpu ja levy ovat palvelimella,
- **itsenäisiä koneita**, joilla on oma cpu ja OPFS-levy.

Myöhemmin myös näiden yhdistelmiä, joissa fs, auth, secstore ja cpu ovat
erillisiä koneita. Kaikissa malleissa avaimet ovat samoissa paikoissa.

| Avain | Paikka |
|---|---|
| käyttäjän salasana / dp9ik-avain | pilven auth-palvelin (keyfs, `/adm/keys`): tätä vastaan kaikki koneet tunnistautuvat |
| factotumin avainnippu (dp9ik muihin domaineihin, ssh, sovellusten salasanat) | **secstore**: käyttäjän tiedosto `factotum`, salattu secstore-salasanasta johdetulla avaimella; palvelin ei näe sitä auki |
| palvelinkoneen oma avain (hostowner: cpu, fs, auth, secstore) | kunkin amd64-koneen nvram, kuten 9frontissa |

Periaatteet:
- **Factotum pitää avaimet vain muistissa.** Selaimen kone kysyy bootissa
  secstore-salasanan ja hakee nipun (`auth/secstore -G factotum` →
  `/mnt/factotum/ctl`) wss:n yli webtermin kautta. Uudet avaimet viedään
  takaisin secstoreen (`secstore -p`), kuten 9frontissa.
- **Pääte (cpu pilvessä):** käyttäjän avaimet eivät jää pilven
  cpu-koneelle. rcpu-istunnossa cpu-puolen `/mnt/factotum` on päätteen
  factotum (`/mnt/term/mnt/factotum`). 9frontin `rcpu` ja
  `/lib/namespace` antavat istunnolle cpu-palvelimen oman factotumin, joten
  bindaus on Plan2001:n profiilin `case cpu` -kohdassa (`plan2001/`).
- **Itsenäinen kone:** sama haku secstoresta. Lisäksi OPFS-levylle voi
  tulla välimuisti offline-käyttöä varten. Se on secstoren tiedostomuodossa
  (salattu samalla salasanalla), joten levyn vienti ei paljasta avaimia.
  Verkon kanssa secstore on lähde, ja välimuisti päivitetään siitä.
- **Erilliset palvelimet:** auth ja secstore voivat aluksi olla samalla
  amd64-koneella ja erota myöhemmin. Asiakkaan puolella muuttuu vain
  osoite (`/net/cs`, `$auth`, `$secstore`). secstore saa webtermin
  politiikkaan oman sanansa (portti 5356), kuten `cpu` sallii `/17019`:n.
- **D8 / XCPU-pooli** (myöhemmin): auktoriteetti on auth-palvelin ja
  päätteen factotum, ei kunkin selaimen levy. Solmut saavat lippunsa
  sitä kautta.

Kirjautuminen plan2001.com:iin (WebAuthn, passkey ja salasana) rakentuu
tämän päälle: passkey avaa saman salasanan, jolla avaimet haetaan
secstoresta (suunnitelma: docs/webauthn.md).

Hylätty: avaimet pelkästään OPFS:ään. Ne olisivat silloin yhdessä
selaimessa: uusi laite tai tyhjennetty profiili kadottaisi ne, eivätkä
pilven pääte ja itsenäinen kone jakaisi samaa avainnippua.

Työjärjestys:
1. Tämä päätös (4.10.).
2. `auth/secstore` wasm32:n juureen (tehty 4.10.: `build-bin3`,
   `native-wasm32`). `secstored` pilven amd64-koneelle on tekemättä:
   VM ja osajoukon derive (`secureidcheck.c` puuttuu osajoukosta).
3. `/boot/init`: secstore-salasanan kysely ja nipun haku factotumiin
   (tehty 4.10.). `/boot/secstore` tekee saman kuin 9frontin bootrc:
   jos `$secstore` tai `$auth` (BootInfon config) nimeää palvelimen,
   `auth/secstore -G factotum` vie avaimet `/mnt/factotum/ctl`:iin, ja
   salasana kysytään konsolilla. Väärän salasanan jälkeen se kysytään
   uudelleen, ja tyhjällä rivillä jatketaan ilman avaimia. devwsnet
   tuntee palvelunimen `secstore` (5356). Webtermin politiikkasana
   `secstore` sallii `/5356`:n ja on `term`-sovelluksen politiikassa.
   Webterm yhdistää oman koneensa secstoreen, joten erillinen
   secstore-kone tarvitsee myöhemmin osoitteen. Sivu ei vielä anna
   `secstore=`-riviä; sen antaa pilven sivusto (kohta 6) tai
   `?conf=secstore=…`.
   Testi `secstore`: `tools/test-authsrv`in `/5356` on secstore-palvelin
   Pythonilla, kirjoitettu erikseen 9frontin C:stä (PAK, SConn:n RC4 ja
   SHA1-MAC). Koneen oikea 9front-asiakas ja se ovat yhtä mieltä
   (`secstore -p`, sitten `/boot/secstore`). Testi kaatuu ilman hakua.
4. Salattu OPFS-välimuisti itsenäiselle koneelle (tehty 4.10.).
   `aux/seckeys` (forkin, `plan2001/sys/src/cmd/aux/seckeys`) on
   `/boot/secstore`n työkalu, kun kone on käynnistetty levyn kanssa:
   - Se kysyy salasanan kerran (readcons, ei kaiutusta) ja antaa sen
     putkella `auth/secstore -i -G factotum`ille ja `auth/aescbc -i`:lle.
     Salasana ja selväkieliset avaimet eivät käy levyllä eivätkä
     ympäristössä.
   - Verkon kanssa avaimet tulevat secstoresta factotumiin, ja kopio
     (`aescbc -e`, sama salasana, aescbc:n HMAC) kirjoitetaan
     glendan kotiin `lib/factotum.aes` (levyllä, kokonaan uutena).
   - Ilman secstorea (yhteys ei aukea) avaimet luetaan kopiosta
     (`aescbc -d`). Väärä salasana kysytään uudelleen (3 kertaa), ja
     tyhjällä rivillä jatketaan ilman avaimia.
   - Ilman levyä kopiota ei ole, ja toiminta on kuten 9frontin bootrc:ssä.
   Testi `seckeys`: haku levyn kanssa, kopion alku on aescbc:n otsake
   eikä avaimia, sitten avaimet pois factotumista ja `/5399`:n kautta
   ilman secstorea kopiosta (ensin väärä salasana).
   Avoinna: kopio päivittyy vain bootissa, kun avaimet haetaan. Uuden
   avaimen vienti secstoreen (`secstore -p`) ja kopioon on käyttäjän
   tehtävä.
5. Profiilin `case cpu`: päätteen factotum cpu-istuntoon (tehty 4.10.).
   Plan2001:n glendan profiili (`plan2001/usr/glenda/lib/profile`)
   bindaa `/mnt/term/mnt/factotum`in `/mnt/factotum`iin, jos päätteellä on
   factotum. Sama koskee sovelluksen originin istuntoa, koska webtermin
   skripti ajaa `service=cpu rc -l`. Testi `rcpukeys`: koneen oma rcpu
   palvelimelle, jonka nimiavaruuden factotumissa on bootesin avain, ja
   istunnon `/mnt/factotum/ctl` näyttää päätteen avaimen (glendan).
   Ilman bindausta näkyy bootesin avain (tarkistettu).
6. Pilven amd64: auth, secstore ja cpu yhdellä koneella, ja selaimet sitä
   vastaan (koodi 4.10., VM-ajo kesken).
   - `tools/vm-cpu` tekee glendan secstore-tilin (`auth/secuser`;
     secstore-salasana `cpu.secpass` välimuistissa, eri kuin
     kirjautumissalasana). Se käynnistää `secstored`in portissa 5356
     (`/rc/bin/service.auth/tcp5356`) ja kirjoittaa sivustolle
     `secstore`-tiedoston. `--update` tekee saman olemassa olevalle
     `cpu.qcow2`:lle, jos tiliä ei ole.
   - Sivu (`kernel.html`) kysyy terminaalille `GET /secstore`. Jos tiedosto
     on olemassa, sivu antaa sen sisällön BootInfon configiin
     (`secstore=`), ja `/boot/secstore` hakee avaimet. Ilman tiedostoa
     salasanaa ei kysytä. Testit `pagesecstore` ja `pagesecstore0`
     (`tools/serve`: `MONOLITH_SECSTORE`).
   - webterm sallii `/5356`:n myös ilman `-s`:ää (portti 17080), kuten
     auth:n.
   - `tools/vm` toimii ilman KVM:ää (`VM_ACCEL=tcg`, hidas), ja
     `tools/9run`in aikarajat kertoo `VM_TIMEOUT_SCALE`.
   - VM-testi `secstorevm`:
     1. Kone vie glendan dp9ik-avaimen VM:n secstoreen (`secstore -p`).
     2. `/boot/secstore` hakee sen tyhjään factotumiin.
     3. rcpu VM:lle tunnistautuu sillä.
     4. Istunnon factotum on päätteen: siinä on vain glendan avain, ei
        VM:n omia avaimia, kuten TLS:n RSA-avainta.
   - Codexin VM-ajo (4.10., commit 058bfa1; QEMU TCG, 9front-11952):
     - Buildit menivät läpi.
     - VM-ketjussa oli kolme virhettä, jotka on nyt korjattu:
       1. `vm-cpu` kirjoitti `cpustart`iin ennen kuin `/cfg/cirno` oli
          olemassa.
       2. Yli 255 merkin komentorivit katkesivat Plan2001:n kbdfs:n
          rivirajaan, ja 9run jäi odottamaan aikarajaa. Nyt 9run vie
          pitkän komennon tiedostoon paloina ja ajaa sen.
       3. `secstored` varaa portin 5356 itse, joten aux/listenin
          `tcp5356` vei portin siltä. Nyt secstored käynnistyy
          `cpustart`ista.
     - Kiertoteiden jälkeen `secstorevm`, `rcpuvm`, `dp9ikvm`, `net`
       (kolme renkaan aloitusarvoa) ja `netloop` menivät läpi, eli 7/7,
       oikeaa 9frontin secstoredia ja auth-palvelinta vastaan.
     - `secstorevm`: avain tallentui VM:n secstoreen, latautui tyhjään
       factotumiin ja tunnisti rcpu-istunnon, ja istunnossa näkyi vain
       päätteen avain.
     - Korjauksia ei ole vielä ajettu VM:llä.
   - Ajo, jossa on KVM (Codex):
     ```
     tools/vm-setup                  # jos base.qcow2 puuttuu
     tools/build.sh                  # build/amd64/9pc64, bootx64.efi
     (cd monolith && tools/pages)    # sivu: build/pages
     tools/vm-cpu --update --web monolith/build/pages  # tai ilman cpu.qcow2:ta: tools/vm-cpu
     tools/vm start --disk ~/.cache/plan2001/cpu.qcow2 --net
     (cd monolith && tools/test-9wasm32 secstorevm rcpuvm dp9ikvm net)
     tools/vm stop
     ```

## D8 odottaa: laskentapooli suunnitellaan uudelleen (päätös 4.10.2026)

Laskentapooli toimi (crsrv, rcc, selaimen CR:t, `docs/cpu-server-design.md`:
21 käännöstyötä kolmelle CR:lle, `-v duplicate` ja `2of3`). Se on
Plan2001:n ydinarvo. D8 ei kuitenkaan jatka suoraan crsrv:n
protokollasta, vaan se suunnitellaan uudelleen, ja olemassa oleva
työ otetaan huomioon. Ennen D8:aa 9front + wasm32 -osaprojekti
viimeistellään ja testataan (alla). D8 on siihen asti odottamassa.

**Lähtökohta, mitä on nyt.** crsrv (`plan2001/sys/src/cmd/crsrv.c`) jakaa
töitä CR:ille creditien mukaan. Viestit ovat pituus, otsakerivi ja
tiedostot (`hello`, `include`, `job`, `result`). Käyttäjän näkymä on
tiedostojärjestelmä (`/global/compute`, `/compute/cc`, rcc). Selaimen CR
oli `cr.js` ja `6c.wasm` web workereissa, ja se poistettiin D7:ssä. Ajoitus
on kaksitasoinen: crsrv valitsee CR:n, ja CR valitsee workerin. Tulokset
voi varmentaa. Kadonneen CR:n työt palaavat jonoon.

**Huomioitava: XCPU ja sen jatkajat.** 9P:llä tehty prosessinhallinta
klustereissa. Lähteet ja yksityiskohdat tarkistetaan suunnittelun alussa.
- XCPU (Los Alamos, Latchesar Ionkov, Ron Minnich ym.). Jokainen solmu
  tarjoaa 9P-tiedostojärjestelmän (xcpufs). Istunto avataan `clone`lla,
  ja istunnon hakemistossa ovat `ctl`, `exec`, `argv`, `env`, `stdin`,
  `stdout`, `stderr` ja `wait` sekä `fs/`, johon ohjelma ja sen tiedostot
  kopioidaan. Solmuja hallitaan tiedostoilla, ja ohjelma ja sen
  ympäristö viedään solmulle eikä oleteta sieltä. XCPU2 toi
  nimiavaruudet: istunto näkee asiakkaan tiedostot.
- IBM Research ja Plan 9 / Inferno -klusterit (Eric Van Hensbergen ym.):
  Plan 9 Blue Genellä (HARE), PUSH (datavirtojen komentotulkki) ja XCPU3
  (Brasil-pohjainen töiden jakaminen ja tulosten kokoaminen
  hierarkkisesti).
- Kysymykset uudelle suunnitelmalle:
  - Onko CR:n rajapinta XCPU:n kaltainen tiedostopuu (session, exec, io,
    wait), jolloin wasm32-kone, natiivi solmu ja selain toteuttavat
    saman puun? Vai crsrv:n viestiprotokolla?
  - Ajetaanko selaimen wasm32-koneella työ sen omana prosessina (3c:llä
    käännetty 6c ja muut), ja tuoko työ nimiavaruutensa mukanaan kuten
    XCPU2?
  - Ajoitus ja aggregointi: crsrv:n kaksi tasoa, vai XCPU3:n tapaan
    hierarkkisesti?
  - Kuinka luottamus, varmennus ja suostumus säilyvät: epäluotettava CR,
    `duplicate`/`2of3`, "share spare compute" ja Web Lock?
  - Kyvyt originilla (`cr`, `compute`, `docs/app-origins.md`).

**Ennen D8:aa: 9front + wasm32 kuntoon ja testattuna.**
- VM-kierros: `rcpuvm` (D4: VM:n rio selaimen ruudulla) ja `dp9ikvm`.
  D7:n palvelinpää oikeassa 9frontissa: rc-httpd, select-handler, `webterm
  -r`, `/app`, sovelluksen origin oikeaa webtermiä vasten,
  `tools/vm-cpu`/`deploy`, hiekkalaatikko ja `cloud-image`. Lisäksi
  osajoukon derive rc-httpd:lle (`tools/subset/derive`, `check`, `test`).
- Natiivi käännös (`docs/plan9-fork.md`): wasm32:n kirjastot, ydin ja
  juuren 9front-ohjelmat kääntyvät 9frontin mkfileillä 9Front-2001:ssä
  (nyt `tools/tree`, `tools/native-wasm32`, 4.10.). Kokeiltu
  Linuxissa plan9portin mk:lla, ja `tools/test-native` ajaa sarjan
  natiivilla ytimellä ja ohjelmilla. Tekemättä: sama oikeassa 9frontissa
  (VM) ja juuren arkisto Plan 9 -työkalulla. wasm32 ei tule CPUS-listaan
  (`installall`), vaan se käännetään `objtype=wasm32 mk install`.
- Avoimet kohdat: factotumin avainnippu secstoresta (päätös 4.10.,
  "Avainten paikka" alla) ja wss:n `/rcpu`-polku päätteelle (D4). Koko kotihakemisto levylle ja 2 s:n
  synkkaus tehtiin 4.10. (D6, yllä).
- rc-skriptien jäsennys ilman 9frontia: `tools/rccheck` (plan9portin rc,
  `PLAN9=...`) jäsentää koneen `/boot`-skriptit, rc-httpd:n sivuston,
  sovellukset, asentimen ja `tools/*.rc`/`tools/cloud/*.rc`:n
  ajamatta niitä. Se löysi kaksi oikeaa virhettä: `sandbox.rc`:n
  `Plan2001's` (D7) ja `setup.rc`:n `(the pool's ...)`, joiden heittomerkki
  avasi lainauksen loppuskriptiin asti.
- Testisarja pysyy vihreänä (`tools/test-9wasm32`), ja jokainen korjaus
  saa testin, joka kaatuu ilman korjausta.

## Kone, ikkuna ja nimiavaruus selaimessa (1.10.2026)

Nimiavaruus on Plan 9:n tapaan prosessiryhmän, ei originin eikä koneen.
Ydin ylläpitää nimiavaruuksia (Pgrp), ja prosessit perivät, kopioivat tai
rakentavat ne (`rfork(RFNAMEG)`, `newns`). wasm32 ei muuta tätä. Se
määrittelee vain, mikä selaimessa on kone.

| Selain | Plan2001 |
|---|---|
| origin | kone eli ydin ja käyttöjärjestelmä: identiteetti (factotum), pysyvä tallennus (OPFS koneen levynä), luottamusraja |
| välilehti | ikkuna: oma prosessiryhmä ja nimiavaruus, johon on liitetty välilehden omat laitteet (näyttö, näppäimistö, hiiri) kuten rio antaa ikkunalleen /dev/draw:n ja /dev/cons:n |
| sovellus | prosessi tai prosessiryhmä koneessa |
| CPU-, tiedosto- ja auth-palvelimet | 9P:n kautta (rcpu, mount, import /net), WebSocket 9P:n kuljettimena |

Käyttäjän terminaali on yksi origin, jonka välilehdet ovat saman koneen
ikkunoita. Sovelluskohtaiset originit ovat edelleen mahdollisia omina
koneinaan, esimerkiksi epäluotetulle sovellukselle, joka ei saa nähdä
käyttäjän avaimia.

Selain sallii muistin jakamisen (SharedArrayBuffer) välilehden Workerien
kesken mutta ei välilehtien välillä. Siksi:

1. Ensin kone on välilehti: jokainen välilehti käynnistää oman ytimensä.
   Tämä riittää vaiheelle C.
2. Myöhemmin kone on origin: ydin on yhdessä paikassa (ensimmäinen
   välilehti tai SharedWorker), ja muut saman originin välilehdet
   liittyvät siihen ikkunoina kanavan yli. Näytön, konsolin ja hiiren
   protokolla kulkee kanavaa pitkin, kuten Plan 9:ssä näyttö on laite,
   johon liitytään protokollalla. Varmistettava ensin: muistin jakaminen
   ja sisäkkäiset Workerit SharedWorkerissa cross-origin-eristettynä.

## Vaihe C: wasm32-ydin (päätös 1.10.2026: procit Workereina)

Ydin on 9frontin `port/` ja `plan2001/sys/src/9/wasm32`, käännettynä 3c:llä
yhdeksi moduuliksi, jolla on tuotu jaettu muisti.

- Jokainen ytimen proc on oma Workerinsa. Worker on yksi suoritin, ja sen
  SP ja muut globaalit ovat suorittimen rekisterejä. `sleep`/`wakeup` ovat
  `Atomics.wait`- ja `Atomics.notify`-kutsuja, joten ydin ei vaihda
  pinoa. 9frontin `proc.c`:n ajastinosa korvataan; drawtermin malli.
- Proc, Worker ja Mach (`plan2001/sys/src/9/wasm32/proc.c`): proc on oma Workerinsa,
  jolla on oma ytimen instanssi, omat `m` ja `up` (3c:n extern-rekisterit)
  ja jaettu ytimen muisti. `sleep` odottaa `p->state`a (`Atomics.wait`),
  `ready` asettaa Readyn ja herättää (New-proc saa Workerin), `sched`
  odottaa Readya tai lopettaa Workerin (Moribund). Ajojonoa, prioriteetteja,
  rebalancea, preemptiota tai EDF:ää ei ole: suorittimet jakaa selain.
  Jokaisen Workerin `m` on oma Machinsa (spl-taso, sched-label, proc),
  mutta sen `machno` on 0, koska `port/` indeksoi suoritinkohtaiset
  taulunsa (ajastimet, intrcount) sillä. `port/`:lle koneessa on yksi
  suoritin, `MACHP(0)` eli boot-Workerin mach0, ja `conf.nmach` on 1.
- Kello (`clock.c`): boot-Worker on mainin jälkeen kellon keskeytys. Se
  odottaa seuraavaan ajastimeen (`timerset`, lukon alla mistä tahansa
  Workerista) ja ajaa `timerintr`in, jonka HZ-ajastin (hzclock) laskee
  tickit. Myöhästynyt kello (taustavälilehti) ajaa erääntyneet ajastimet,
  ja menetetyt tickit jäävät pois kuten raudalla.
- `waserror`/`nexterror`: setlabel ja gotolabel ovat 3l:n setjmp ja
  longjmp (WebAssemblyn poikkeukset).
- Käyttäjäprosesseilla on oma muisti kuten host3:ssa. Järjestelmäkutsun
  reunalla argumentit kopioidaan ytimeen ja tulokset takaisin, joten
  laitteet saavat aina ytimen osoittimia. `sysexec`, `sysrfork`, brk ja
  notes tehdään wasm32:lle omina versioinaan. Fork, RFMEM, preemptio ja
  kontekstit siirtyvät host3:sta.
- Alusta (JavaScript) on firmware: muisti, Workerit, odotus ja herätys,
  aika, sarjaportti, näyttö, syöte. Ydin kutsuu sitä kuten mitä tahansa
  C-funktiota: 3l tekee ytimen määrittelemättömistä funktioista tuonteja
  (`platform`), ja argumentit ovat muistissa kuten wasm32:n
  kutsukonventiossa.

- Järjestelmäkutsun raja (`trap.c`, C2b): ohjelma on oma moduulinsa omalla
  muistillaan procin Workerissa (platform.js, `platuser`). Sen
  `plan9.syscall(n, a)` kutsuu saman Workerin ytimen `syscall(n, a)`:ta.
  Kutsutaulu kertoo jokaisen argumenttisanan lajin: sana, vlong,
  merkkijono, luettava tai kirjoitettava puskuri tai pipen int[2].
  Merkkijonot ja puskurit kopioidaan ytimeen ennen 9frontin `sys*`-funktiota
  ja kirjoitettu osa takaisin sen jälkeen, joten laitteet näkevät vain
  ytimen osoitteita. Virheet kulkevat 9frontin errstr-vaihdon kautta.
- Ohjelmat (`sysproc.c`): exec lukee moduulin (ja `#!`-tulkin) ytimeen ja
  antaa sen alustalle, joka purkaa vanhan ohjelman kehykset ja käynnistää
  uuden. Fork (RFPROC ilman RFMEM:iä) tekee lapsen 9frontin tapaan, minkä
  jälkeen alusta purkaa ohjelman pinon muistiin, kopioi muistin lapsen
  Workerille ja kutsuu `ready`a, ja molemmat rakentavat pinonsa
  uudelleen. exits on 9frontin pexit, joka palaa `procstart`iin, ja Worker
  päättyy. Kuolema on kaksivaiheinen: Proc ja sen KSTACK (samaa lohkoa)
  ovat vapaita, mutta `newproc` antaa ne uudelle procille vasta, kun
  alusta on asettanut `workergone`-sanan eli Worker on poistunut ytimestä.
  Proc saa Workerinsa kuitattuna (`procspawn`): ready odottaa, kunnes
  Worker on käynnistynyt tai sivu on kertonut, ettei sitä syntynyt. Fork
  ilman Workeria perutaan (`procunmake`), ja vanhempi saa -1:n ja virheen.
- Notes (C3a): järjestelmäkutsun palatessa `popnote` antaa noten, ja alusta
  kutsuu ohjelman notify-käsittelijää (ureg, msg) sen pinossa. `noted(NCONT)`
  palaa kutsun jälkeen, `notejmp` hyppää ulos (alusta kertoo ytimelle, että
  käsittely päättyi) ja NDFLT tai puuttuva käsittelijä lopettaa procin.
  Odotus keskeytyy 9frontin tapaan (`interrupted`). Alarm-kproc kuten
  pc64:ssä. Rendezvous on 9frontin. 3l:n preemptiokohta (kutsu 105) on
  notesien tarkistuspaikka; selain preemptoi Workerit.
- rfork(RFMEM), libthread ja preemptio (C3b, host3:n malli ytimessä):
  saman muistin procien ohjelmat vuorottelevat muistin Workerissa
  (platform.js `runprog`), ja niiden pinot ovat Plan 9:n tapaan yksityisiä
  samoissa osoitteissa (alueen vaihto). Kun muistissa on useampi proc,
  jokaisella on apuWorker (`helper`), joka tekee sen estävät kutsut ja
  exitsin. Muistin Worker valmistelee kutsun (`sysprep`: kopiot sisään)
  ja tekee itse nopeat ja alustaan vaikuttavat kutsut (rfork, brk, noted
  ...). Muut se antaa apuWorkerille, jatkaa seuraavalla valmiilla procilla
  ja viimeistelee kutsun (`sysfin`: kopiot ulos), kun apuWorker on valmis.
  Up on kutsuvan procin sekä muistin Workerissa että apuWorkerissa. Ensin
  ohjelmaa ajaneen procin apuWorkerilla on oma Machinsa ja pinonsa, jotka
  säilyvät Procin mukana (`hmach`, `hstack`), sillä muistin Worker pitää
  KSTACKin. Procin `workers` laskee Workerit, jotka voivat vielä koskea
  Prociin. Ensimmäinen rfork(RFMEM) on transaktio (`rfmemstart`): ensin
  vanhemman apuWorker ja sitten lapsen Worker, ja vasta sen jälkeen vanhempi
  liittyy muistin ryhmään (`Umem`, viitelaskettu). Jos Workeria ei synny,
  lapsi puretaan, apuWorker lopetetaan (`helperquit`), ja vanhempi saa -1:n
  ja jatkaa kuten ennen. Note, joka lopettaa muistin procin, ja noted
  NDFLT viedään apuWorkerille, koska pexit ei kuulu muistin Workerille.
  Pexit vapauttaa kesken jääneen kutsun puskurit (`callabort`).
  Kontekstit (kutsut 100–103) ja preemptio (kutsu 105, 10 ms:n viipale)
  ovat alustan. Rajat: exec RFMEM-procista ei vielä toimi; saman muistin
  procit käyttävät yhtä suoritinta; ohjelmaa ajavalle procille tullut note
  toimitetaan sen seuraavassa kutsussa; preemptiokohdat ovat taaksepäin
  hyppäävien haarojen edessä, joten pitkä rekursio ilman silmukkaa voi
  pitää muistin Workerin.
- Juuri `#R` (`devrootfs.c`): sivun käynnistyksessä antamat tiedostot
  (`tools/build-bin3`:n root) ovat vain luettavia. Init sitoo `#c`:n ja
  `#t`:n `/dev`iin sekä `#e`:n, `#s`:n ja `#p`:n paikoilleen, avaa
  `#t/eia0`:n tiedostoiksi 0, 1 ja 2 ja ajaa sivun `?arg=`-argumenteista
  saadun ohjelman, ilman niitä `/boot/init`in (`plan2001/sys/src/9/wasm32/init`).
- Konsoli (C3c): `/boot/init` käynnistää 9frontin kbdfs:n sarjakonsolille
  (`aux/kbdfs -q -s cons /dev/eia0`, kuten 9frontin boot) ja
  interaktiivisen rc:n sen `/dev/cons`iin. kbdfs tekee rivinmuokkauksen ja
  kaiun, ja DEL on interrupt: kbdfs kirjoittaa sen `/proc/n/notepg`:hen.
  Sivu lähettää jokaisen näppäimen sellaisenaan. Merkit ovat selaimen
  (`e.key`), joten käyttöjärjestelmän näppäimistöasettelu on jo käytetty
  ja äåö tulevat UTF-8:na ilman kbmapia; scancodeja ei wasm32:ssa ole.
  `#p` (`devproc.c`) on wasm32:n oma pieni proc-laite: ctl (kill), note,
  notepg, status ja args, ei segmenttejä eikä rekisterejä. lib9p ja
  libauth käännetään 3c:llä. `newproc` ohittaa vapaat Procit, joihin
  jokin Worker vielä viittaa (esimerkiksi kbdfs:n ensimmäinen proc, kun
  sen muisti jatkaa), eikä jää odottamaan niitä.
- Ruutu, hiiri ja näppäimistö (C3d): 9frontin devdraw ja devmouse sekä
  libmemdraw ja libmemlayer ytimessä. `screen.c`: kuvapuskuri on XRGB32
  ytimen jaetussa muistissa (`?screen=LxK`, oletus 800x600), ja
  `flushmemscreen` kertoo sivulle muuttuneen alueen; sivu piirtää sen
  canvasille kerran ruudunpäivityksessä suoraan jaetusta muistista.
  Kursori on canvasin CSS-kursori Cursorin bittikartoista. Canvasin
  osoitin on hiiri: tapahtumat (x, y, painikkeet 1 2 4, rulla 8 16)
  rengaspuskurin kautta kprocille, joka kutsuu `absmousetrack`ia.
  Näppäimistö on `#b/kbd` (`devkbd.c`) kbdfs:lle: näppäin alas `r`, ylös
  `R` ja liitetty merkki `c`, kukin selaimen merkkinä. Init sitoo `#i`,
  `#m` ja `#b` `/dev`iin. Ytimen kellonaika asetetaan käynnistyksessä
  alustan kellosta (`todset`), ja `/boot/init` kopioi
  `/adm/timezone/local`in `/env/timezone`en. 9frontin clock piirtää
  ytimen päällä. Testiajo: `DRAWS`, `KEYS` (selaimen omat
  näppäintapahtumat; Enterin jälkeen odotetaan vastausta, sillä etukäteen
  kirjoitettu kaikuu heti kuten Plan 9:ssä), `MOUSE`, `EXPECTJS`,
  `SENDFILE` ja `MASKPIDS` (wait-viestien pid:t).
- C3 valmis (2.10.): ydin ajaa kaiken, mitä host3 drawtermin ytimen päällä
  ajoi, ja `tools/test-9wasm32` sisältää host3:n testit sellaisinaan
  (hello, dclock, fork, rfmem, threads, preempt, bytes, rc, interaktiivinen
  rc, clock), yhteensä 24 testiä. Ohjelmien ajaminen selaimessa ei siis
  enää tarvitse drawtermia. Drawtermin web-asiakas on yhä yhteys
  Plan2001-koneeseen (webterm, rcpu), ja se korvautuu vaiheessa D, kun
  ytimellä on verkko (WebSocket 9P:n kuljettimena).
  Sivun syötteet (initin argumentit, `#R`:n arkisto) tarkistetaan: koko
  kysytään ensin, jokaisella merkkijonolla on oltava 0 alueen sisällä, ja
  polussa ei saa olla alussa /:ta, tyhjää elementtiä eikä . tai ..
  Testit: `tools/test-9wasm32` (c2a, echo, yli 4096 tavun argumentti, fork,
  2000 forkia exitin, chdirin, execin ja waitin kanssa, rc -c,
  interaktiivinen rc, notes ja rendezvous, fork, jonka lapselle sivu ei
  tee Workeria: `?failfork=N`, sekä host3:n rfmem-, threads- ja
  preempt-testit, kymmenen kierrosta niitä rc:n alla, note, joka
  lopettaa RFMEM-procin, ja rfork(RFMEM), jonka vanhemman apuWorkerille
  tai lapselle sivu ei tee Workeria: `?failhelper=N`, `?failrfmem=N`,
  sekä `/boot/init`: kaiku, äåö ja DEL; C2a:n testi `?noinit=1`).

| | Sisältö | Valmis kun |
|---|---|---|
| C1 | 3l:n ydintila (tuotu jaettu muisti, passiivinen data ja `_init`, platform-tuonnit, setlabel/gotolabel), alustan käynnistys ja kproc Workerina, eia0 | 3c:llä käännetty ydin tulostaa #t/eia0:aan, kproc ja sleep/wakeup toimivat (tekstitesti) |
| C2 | 9frontin `port/`: chan, dev, qio, alloc, pgrp, devroot, devcons, devpipe, devenv, devdup, devmnt, devsrv, sysfile; järjestelmäkutsut prep- ja fin-vaiheiden kautta | ydin ajaa rc:n juurilevyltä, test-host3:n tekstitestit ilman drawtermia (C2a 1.10.: port/ toimii; C2b 1.10.: exec, fork, wait, putket ja interaktiivinen rc #t/eia0:ssa ilman drawtermia) |
| C3 | C3a (2.10.): Workerin luonnin kuittaus, notes, rendezvous; C3b (2.10.): RFMEM, libthread, kontekstit ja preemptio; C3c (2.10.): kbdfs, `#p`, /boot/init; C3d (2.10.): devdraw, hiiri ja näppäimistö alustan ajureina, clock piirtää; host3:n testit ytimellä | clock ja testisarja; drawterm poistuu selainpuolelta ohjelmien ajajana (valmis 2.10.) |
