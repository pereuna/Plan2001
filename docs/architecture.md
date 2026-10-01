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
| A | 3c/3l-prosessit drawtermin ytimen alla: prosessi on Worker, syscall on drawtermin sysopen, sysread jne., copyin ja copyout alustassa | 3c:llä käännetty clock ja rc toimivat selaimessa |
| B | 3c/3l: poikkeukset (setjmp, waserror), atomics, rfork(RFMEM) eli säikeet samassa muistissa, libthread | libthreadia käyttävä ohjelma toimii |
| C | wasm32-ydin: 9frontin port/ ja `sys/src/9/wasm32` (alusta, ajurit) 3c:llä käännettynä, JavaScript vain alustaliimana | ydin käynnistää rc:n selaimessa ilman drawtermia |
| D | Boot ABI wasm32/selaimelle, terminal- ja cpu-roolit | Monolith, drawterm ja webterm poistettu |
