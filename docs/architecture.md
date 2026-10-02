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
| C | wasm32-ydin: 9frontin port/ ja `sys/src/9/wasm32` (alusta, ajurit) 3c:llä käännettynä, JavaScript vain alustaliimana | ydin käynnistää rc:n selaimessa ilman drawtermia |
| D | Boot ABI wasm32/selaimelle, terminal- ja cpu-roolit | Monolith, drawterm ja host3 poistettu; webtermistä jää WebSocket-kuljetin |

## Vaihe D: wasm32-kone Plan2001:n terminalina (suunnitelma 2.10.2026)

Selaimen wasm32-kone on Plan2001:n terminal ja myöhemmin cpu. Monolith,
drawterm ja host3 poistuvat. webterm ei katoa kokonaan: selain ei avaa
TCP-yhteyksiä, joten palvelimen puolella jokin ottaa WebSocketin vastaan,
ja webterm tekee sen jo (`GET /17019`, `/567`, `/rcpu`). Siitä jää
kuljetin, ja sivujen tarjoilu ja Monolithin erityispolut poistuvat.

| | Sisältö | Valmis kun |
|---|---|---|
| D1 | paikallinen terminal: rio (libframe, libplumb) 3c:llä ytimen päälle; exec RFMEM-procista (rion ikkunat); `/boot/init` voi käynnistää rion; ramfs `/tmp`:ksi | rio, ikkunat, rc ikkunassa ja clock ikkunassa selaimessa ilman verkkoa (valmis 2.10.) |
| D2 | verkko: wasm32:n oma `/net` (tcp: clone, ctl, data, local, remote, status); `dial tcp!kone!17019` avaa WebSocketin webtermin samaan polkuun kuin drawtermin wsock.c; pieni `/net/cs`; samat sallitut palvelut kuin webtermillä | `dial` webtermin kautta: auth (567) ja rcpu (17019) vastaavat |
| D3 | tunnistus: libmp, libsec, libauthsrv 3c:llä; factotum selaimen koneeseen, salasana ensin kysymällä, myöhemmin OPFS:ään | factotum hoitaa dp9ik:n VM:n auth-palvelimelle |
| D4 | rcpu: 9frontin `rcpu`, `tlsclient` ja `exportfs`; terminal vie ruutunsa, näppäimistönsä ja hiirensä cpu-palvelimelle kuten drawterm; wss-polulla `/rcpu` ilman TLS-PSK:ta kuten Monolithissa | VM:n rio näkyy selaimen wasm32-ytimen ruudulla rcpu:n kautta |
| D5 | Boot ABI: `plat*`-käynnistyskutsujen tilalle BootInfo (config, kehyspuskuri, RNG, RTC, juuren arkisto) docs/boot-abi.md:n data-ABI:na; wasm32:n entry-ABI on `_start` ja BootInfon osoite; `getconf` configista | sama BootInfo-data kuin amd64:llä ja arm64:llä; sivu on firmware |
| D6 | tallennus: OPFS koneen levynä, pysyvä ja kirjoitettava juuri tai `/usr/$user` | tiedosto säilyy sivun uudelleenlatauksen yli |
| D7 | siirtymä: index.html (Monolith) korvataan wasm32-terminalilla; app-originit (docs/app-origins.md) wasm32-koneina; host3, third_party/drawterm ja Monolithin JS poistetaan; webtermistä poistetaan sivujen tarjoilu | pilvi ja sovellukset toimivat ilman drawtermia |
| D8 | cpu-rooli: selain compute poolissa (crsrv, `/17030`) wasm32-koneena | compute pool -työ ajetaan wasm32-ytimen prosessina |

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

Ydin on 9frontin `port/` ja `sys/src/9/wasm32`, käännettynä 3c:llä
yhdeksi moduuliksi, jolla on tuotu jaettu muisti.

- Jokainen ytimen proc on oma Workerinsa. Worker on yksi suoritin, ja sen
  SP ja muut globaalit ovat suorittimen rekisterejä. `sleep`/`wakeup` ovat
  `Atomics.wait`- ja `Atomics.notify`-kutsuja, joten ydin ei vaihda
  pinoa. 9frontin `proc.c`:n ajastinosa korvataan; drawtermin malli.
- Proc, Worker ja Mach (`sys/src/9/wasm32/proc.c`): proc on oma Workerinsa,
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
  saadun ohjelman, ilman niitä `/boot/init`in (`sys/src/9/wasm32/init`).
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
