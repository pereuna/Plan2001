# wasm32: WebAssembly Plan2001:n arkkitehtuurina (3c, 3l)

WebAssembly ei ole Plan2001:n web-portti vaan yksi sen
prosessoriarkkitehtuureista: `objtype=wasm32`, kääntäjä `3c`, linkkeri
`3l`, oliot `.3` (5c 6c 7c 8c ... ja 3c). Numero 3 valittiin, koska
9frontissa sitä ei ole käytössä (4c oli MIPS); tämä on tarkistettava
CPU-palvelimen `/sys/src/cmd`:stä ennen kuin nimi lyödään lukkoon.

Selaimessa (tai Nodessa, wasmtimessa) ajettava 6c on edelleen 6c:
isäntä wasm32, kohde amd64. 3c kääntää wasm32:lle, ajoi se missä tahansa.
clang.wasm (monolith/docs/wasm-apps.md) jää vertailutoteutukseksi.

    monolith/tools/build-cc native 3c|3l|3a   build/cc-native/3c, 3l, 3a (Linux)
    monolith/tools/build-libc3             build/wasm32/lib/libc.a
    monolith/tools/build-3c3               build/wasm32/bin/3c.wasm, 3l.wasm
    monolith/tools/test-3c                 testit, ks. alla
    node monolith/tools/run3.mjs P.wasm    wasm32-prosessi pienellä ytimellä

## Lähteet

- `sys/src/cmd/3c`: backend 9frontin cc-frontendin (`../cc`, pgen.c,
  pswt.c) päälle, 6c:n rakenteella: gc.h, txt.c, cgen.c, sgen.c, swt.c,
  list.c, enam.c, machcap.c; `3.out.h` on käskykanta ja oliomuoto.
- `sys/src/cmd/3l`: obj.c (oliot ja ar-kirjastot), wasm.c (asettelu ja
  WebAssembly-moduuli).
- `wasm32/include/u.h`, `ureg.h`, `wasm32/mkfile` (CC=3c LD=3l O=3 AS=3a).
- `sys/src/cmd/3a`: vielä vain stub, joka sanoo, ettei assembleria ole, ja
  palauttaa virheen: mitä muilla arkkitehtuureilla on assemblerina, on
  wasm32:lla C:tä.
- `sys/src/libc/wasm32`: mitä 386:lla on assemblerina (main9, tas, atom,
  getfcr, setjmp, notejmp) sekä järjestelmäkutsut (syscall.c, sys.h:sta).
  Muu libc on 9frontin port, 9sys ja fmt sellaisenaan.
- Linuxilla 3c ja 3l käännetään monolith/cc9:n kautta kuten 6c.

## Kone

Abstrakti kone, jolla on mielivaltainen määrä tyypitettyjä rekistereitä:
3l tekee niistä WebAssemblyn paikallismuuttujia, ja selaimen JIT varaa
oikeat rekisterit. Siksi 3c:ssä ei ole rekisterinvarausta eikä
Sethi-Ullmania.

- Luokat: `Kw` i32 (char, short, int, long, osoittimet), `Kv` i64 (vlong),
  `Kf` f32, `Kd` f64. Rekisteri on `(n<<2)|luokka`; n=0 on RET, n=1 SP.
- Käskyt ovat kolmiosoitteisia: `op from, from2, to` eli
  `to = from op from2`. Vain MOVit (B, BU, H, HU, W, V, F, D) koskevat
  muistia. CVT/CVTU muuntaa luokasta toiseen. CMPxx tuottaa 0 tai 1, BZ ja
  BNZ haarautuvat, JMP, CALL, RET ja COPY (memory.copy) ovat omia
  käskyjään.
- ILP32, little-endian. Struct-kentät ja automaattimuuttujat tasataan
  8:aan kuten 6c:ssä; argumentit 4 tavun lokeroissa kuten 8c:ssä, jotta
  Plan 9:n `va_arg` toimii sellaisenaan.
- Kutsukonventio: argumentit muistissa kohdasta SP+0 ylöspäin, paluuarvo
  globaaleissa RET.w, RET.v, RET.f, RET.d. Structin palauttavalle
  funktiolle välitetään kohteen osoite kohdassa 0(SP) ja argumentit
  kohdasta 4 alkaen. Jokaisen funktion wasm-tyyppi on `() -> ()`, joten
  funktio-osoitin on taulukon indeksi ja `call_indirect` toimii, castattiin
  osoitin miten tahansa.
- Pino on lineaarisessa muistissa ja kasvaa alaspäin globaalista SP:stä.
  Funktio vähentää siitä kehyksensä ja pitää kopion paikallismuuttujassa 0.

## Ohjausrakenne (3l)

pgen.c tuottaa hyppyjä kuten kaikille Ken-kääntäjille. 3l jakaa funktion
peruslohkoihin ja sijoittaa ne sisäkkäisiin `block`-rakenteisiin niin,
että lohkon j koodi tulee lohkon j `end`-käskyn jälkeen. Hyppy eteenpäin
on suora `br` ulos kohdelohkoon. Hyppy taaksepäin asettaa lohkonumeron
paikallismuuttujaan 1 ja palaa silmukan alkuun, jossa `br_table` valitsee
lohkon. Silmukka syntyy vain funktioihin, joissa on hyppyjä taaksepäin.
Myöhemmin varsinainen stackifier voi poistaa br_table-kierroksen.

## setjmp, longjmp ja atomics (3l)

`setjmp` tehdään kutsukohtaan: jmp_buf[0] saa kehyksen SP:n ja
jmp_buf[1] kutsun jälkeisen peruslohkon numeron, tulos on 0. `longjmp(buf,
v)` heittää WebAssembly-poikkeuksen (tag 0, (buf, v)). Funktio, joka kutsuu
setjmp:ia, on try-lohkossa. Sen catch tarkistaa, onko buf[0] sen oma SP.
Jos on, catch palauttaa globaalin SP:n (välistä purettujen kehysten
epilogit jäivät ajamatta), asettaa tuloksen (v, tai 1 jos v on 0) ja
palaa silmukan br_tablen kautta buf[1]:n lohkoon. Muuten se heittää
poikkeuksen eteenpäin (rethrow). Paikallismuuttujat säilyvät, koska ollaan
samassa funktiokutsussa. Käytössä ovat legacy-poikkeuskäskyt (try, catch,
throw, rethrow), jotka toimivat kaikissa nykyselaimissa ja Node 20:ssä.

`_tas`, `ainc`, `adec`, `cas`, `casp`, `casl` ja `coherence` ovat 3l:n
tekemiä funktioita WebAssemblyn atomic-käskyillä (xchg, add, sub, cmpxchg,
fence), kun ohjelma käyttää niitä eikä määrittele niitä itse.

## fork: pinon purku ja uudelleenrakennus (3l)

WebAssemblyssa käynnissä olevaa suoritusta ei voi kloonata, joten 3l tekee
pinon tallennettavaksi asyncifyn tapaan (päätös 1.10.2026: tämä tapa ja
optimointi myöhemmin):

- Kutsugraafista: funktio voi olla pinossa forkin aikana, jos se kutsuu
  `_trap`:ia, kutsuu osoittimen kautta tai kutsuu tällaista funktiota.
  Vain nämä muunnetaan (libc-hellossa 156/229 funktiota).
- Muunnetussa funktiossa jokainen tällainen kutsu on oma peruslohkonsa.
  Kutsun jälkeen: jos `asstate` on 1 (purku), funktio tallentaa
  lohkonumeron ja kaikki paikallismuuttujansa tallennuspinoon (`asptr`,
  256 kt bss:n jälkeen) ja palaa.
- Funktion alussa: jos `asstate` on 2 (palautus), funktio ottaa
  tallennuksensa pinosta, asettaa SP:n ja hyppää br_tablen kautta
  kutsulohkoon, joka kutsuu kutsuttavaa uudelleen.
- `_trap` päättää palautuksen: se asettaa tilaksi 0 ja palauttaa
  `asret`-arvon.

Fork: ydin asettaa `asstate`:ksi 1, ja pino purkautuu `_start`iin asti.
Ydin kopioi muistin lapselle, ja molemmat kutsuvat `_start`ia uudelleen
tilassa 2. Vanhemman `asret` on lapsen pid, lapsen 0. Hinta: muunnetut
ohjelmat kasvavat noin 1,4–2-kertaisiksi (3c.wasm 447 kt -> 723 kt), ja
muunnetut kutsut ovat hitaampia.

## Prosessi

Yksi 3l:n tuottama moduuli on yksi prosessi, jolla on oma muisti eli oma
osoiteavaruus:

- muisti: [0, 4096) tyhjä, sitten pino (oletus 1 Mt, `3l -s`), data, bss,
  `end`; keko kasvaa `end`-kohdasta ylöspäin (brk kasvattaa muistia);
- viennit: `memory`, `sp`, `_start` (kutsuu -E:n, oletus `_main`);
- tuonti: yksi, `plan9.syscall(numero i32, argumentit i32) -> i64`.

Järjestelmäkutsu on kuin oikea trappi: libc:n stubi on
`_trap(NUMERO, &ensimmäinen_parametri)`, ja ydin lukee argumentit
prosessin muistista (sys.h:n numerot, 9frontin järjestyksessä). `_trap`
on 3l:n tekemä funktio, joka kutsuu tuontia ja asettaa tuloksen RET.v- ja
RET.w-globaaleihin.

Ydin kirjoittaa argc:n ja argv[0] ... nil:n pinon huipulle, asettaa `sp`:n
ja kutsuu `_start`:ia. `_main(int argc, char *arg0)` saa ne suoraan
parametreikseen, joten `&arg0` on argv, samoin kuin Plan 9:ssä.

## Testit (monolith/tools/test-3c), 1.10.2026

| Testi | Mitä |
|---|---|
| t1, t2 | ilman libc:tä; tuloste sama kuin gcc:n samasta lähteestä: rekursio, switch, funktio-osoittimet, structien palautus ja välitys arvona, vlong, double, bittikentät, unionit, goto, staattiset muuttujat, Plan 9:n varargs, sekatyyppiset `op=`-sijoitukset |
| t3 | MOVit (3l:n emov): jokainen leveys ladattuna ja tallennettuna, etumerkki- ja nollalaajennus, tallennuksen katkaisu, siirtymät osoittimen kautta (myös yli 2^16 ja negatiiviset), tasaamattomat osoitteet, rekisteristä rekisteriin kaventaminen, structien kopiot; tuloste sama kuin gcc:n |
| t4 | setjmp ja longjmp (WebAssemblyn poikkeukset): syvältä takaisin, longjmp(.., 0) antaa 1, 20 000 longjmp:ia ilman pinon vuotoa, ytimen waserror/nexterror-pino, rekisterit säilyvät; atomics (_tas, ainc, adec, cas) |
| libc/fork | fork rekursion pohjalta: rekisterit, pino ja keko säilyvät, lapsen ja vanhemman muistit erillään, sisäkkäinen fork, wait ja exits-viestit (run3.mjs: lapsi on worker-säie, await odottaa Atomics.waitilla) |
| libc/hello | 9frontin libc: print-muotoilut, smprint, malloc, qsort, strtol, atof, sqrt, pow, tokenize, rune-funktiot, cleanname |
| libc/sysabi | järjestelmäkutsujen ABI: jokainen argumentti siellä, mistä ydin sen lukee, myös vlongit (pread, pwrite, seek); odotettu tuloste kirjoitettu käsin kutsujen merkityksestä |
| self | 3c.wasm kääntää 3c:n omat 25 lähdettä samoiksi tavuiksi kuin natiivi 3c, ja 3l.wasm linkittää ne samaksi 3c.wasm:ksi (447 kt) |

Itseisäännöstys todistaa, että 3c ja 3l toimivat samoin natiivina ja
wasm32:na, mutta ei sitä, että 3l:n tuottama koodi on semanttisesti
oikein: virhe, joka on molemmissa, tuottaisi samat tavut. Semantiikan
testaavat t1–t3 (gcc vertailukohtana) ja libc-testit (odotettu tuloste).

Itseisäännöstys on silti koko kääntäjän laajin koe: cc-frontendin
yacc-jäsennin, makroprosessori, tyyppijärjestelmä ja 3c:n backend
kääntyvät 3c:llä ja tuottavat saman tuloksen. Natiivi 3c kääntää t1.c:n
ja 3c.wasm Nodessa 0,3 sekunnissa.

## Selaimessa drawtermin ytimen alla (vaihe A, 1.10.2026)

`monolith/wasm32host`: wasm32-ohjelma on prosessi drawtermin ytimen alla.
Prosessi on kproc eli Worker, joka ajaa ohjelman moduulia.
`plan9.syscall` tulee host3.c:hen, joka kutsuu drawtermin sys*-funktioita.
Ohjelman muistiin päästään vain host3js.c:n copyin- ja copyout-kutsuilla,
kuin DMA:lla.

- fork (rfork RFPROC): pino puretaan (3l), muisti kopioidaan ytimen
  kekoon, lapsi on uusi kproc, ja molemmat rakentavat pinon uudelleen.
  RFFDG/RFCFDG kopioivat tai tyhjentävät tiedostokuvaajat, RFREND
  rendezvous-ryhmän. RFNAMEG ja RFENVG jakavat vielä nimiavaruuden ja
  ympäristön, ja RFMEM puuttuu.
- exec ytimen nimiavaruudesta: WebAssembly-moduuli tai `#!`-skripti.
- await/wait: lapsen exits-viesti Plan 9:n muodossa.
- Prosessin päättyessä sen tiedostot suljetaan, jotta putken toinen pää
  näkee lopun (drawtermin closefgrp ei sulkenut kanavia; korjattu).
- Juuri: `tools/build-bin3` kääntää 9frontin rc:n ja komennot (cat, ls,
  wc, date, cp, mv, rm, mkdir, sort, xd ...) ja kokoaa `build/wasm32/root`
  -hakemiston (bin/, rc/lib/rcmain). Sivu lataa sen #U:hun, ja host3
  liittää sen polkuihin / ja /bin. `/env/timezone` tulee selaimen
  aikavyöhykkeestä.

    monolith/tools/build host3 && monolith/tools/build-bin3
    ?wasm=host3&prog=rc&arg=-c&arg=...          rc /bin:stä; &debug=1..3 jäljittää
    PROG=rc ARGS='["-c", "echo a b c | wc"]' tools/test-wasmapp host3   PASS
    PROG=hello / PROG=dclock DRAWS=1 tools/test-wasmapp host3            PASS
    PROG=clock DRAWS=1 RUNS=1 tools/test-wasmapp host3                   PASS

rfork(RFMEM) (päätös 1.10.: vaihtoehto 1): saman muistin procit vuorottelevat
prosessin Workerissa kuten yhden suorittimen koneessa. Ajastin (host3js.c)
vaihtaa procia purkamalla toisen ja rakentamalla toisen. Järjestelmäkutsu on
prep (argumentit sisään Workerissa), doreq (ytimen osa: Workerissa tai procin
apu-kprocissa) ja fin (tulokset ulos). Kun procceja on useita, estävä kutsu
menee apu-kprocille, ja seuraava valmis proc jatkaa.

Pino on Plan 9:n tapaan procin yksityinen samoissa osoitteissa: lapsi
jakaa vanhempansa kontekstin alueen, eli pinon [SP, huippu) ja
tallennukset [base, asptr). Muistissa on aina yhden version sisältö, ja
muiden versiot ajastin pitää tallessa. Kun proc jatkaa ja alueella on
toinen versio, sen version käytetty osa tallennetaan ja procin oma
palautetaan. Kopioitavaa on vain käytetty osa, ja vaihtoja tulee vain
estävissä kutsuissa. Myös valmistuneen kutsun tulokset kopioidaan vasta,
kun procin oma pino on paikallaan. Osoitteita ei siirretä, joten pinon
osoite, joka on tallennettu globaaliin ennen rforkia, osoittaa lapsessa
lapsen omaan kopioon kuten Plan 9:ssä (testi host3/rfmem). Tämä korvasi
1.10. aiemman ratkaisun, jossa pino siirrettiin säiepaikkaan ja
pino-osoitteilta näyttävät arvot korjattiin heuristisesti. 3l:n kiinteä
säiealue (16 paikkaa) poistui samalla. Raja: exec RFMEM-procista ei vielä
toimi.

    tools/test-host3        selaintestit: hello, dclock, fork, rfmem, threads, rc, clock

Sarjaportti #t/eia0 (monolith/wasm32host/devuart3.c) on wasm32-alustan
ensimmäinen ajuri: tavuputki sivulle. Ytimen osa on Dev, jossa ovat eia0,
eia0ctl ja eia0status. Alustan osa (host3js.c) kirjoittaa sivulle
(`window.monolith.eia0out()`) ja ottaa sivulta vastaan
(`window.monolith.eia0in(teksti)`) ytimen rengaspuskurin kautta.
`?console=eia0` antaa ohjelmille konsoliksi sarjaportin ruudun sijaan.
Testit lukevat ja kirjoittavat tekstiä kuten koneen sarjakonsolia
(`SERIAL=1 [SEND=...] [EXPECT=tiedosto] tools/test-wasmapp host3`), myös
interaktiivista rc:tä. Kuvakaappaus jää piirtäville ohjelmille. Mukana on
myös #d (devdup, 9frontin), josta rcmain lukee `#d/0`:n. Alustan osa
siirtyy sellaisenaan `sys/src/9/wasm32`:een.

Procikohtainen data: Plan 9:ssä `_tos` (getpid) ja privallocin taulukko
ovat jokaisen prosessin omia samassa virtuaaliosoitteessa. wasm32:n libc
kokoaa ne `_perproc`-alueeseen. 3l vie alueen osoitteen ja koon, ja
ajastin tallentaa ja palauttaa sen procia vaihdettaessa kuin
rekisterijoukon. Se kirjoittaa alueelle myös procin pid:n.

Kontekstit ja libthread: proc voi vaihtaa itse pinostaan toiseen alustan
kutsuilla `_ctxnew(fn, arg, stk, n)`, `_ctxswitch(id)`, `_ctxself` ja
`_ctxfree` (järjestelmäkutsut 100–103, libc/wasm32/ctx.c). Vaihdossa
nykyinen konteksti puretaan, ja kohde rakennetaan uudelleen tai
käynnistetään. Alusta käynnistää uuden kontekstin kutsumalla funktiota
3l:n viemän taulun kautta. Kontekstit kuuluvat muistille: aloittamaton
siirtyy sille procille, joka siihen ensimmäisenä vaihtaa.
`sys/src/libthread/wasm32` on 9frontin libthread, jossa sched.c:n
setjmp/longjmp-parit ovat kontekstinvaihtoja, main.c:stä puuttuu `mainjmp`
ja wasm32.c:n `_threadinitstack` luo kontekstin. Säikeet ovat procin
konteksteja, ja procit (proccreate) ovat rfork(RFMEM)-procceja.

9frontin muuttamaton clock.c toimii: sen event-kirjasto käynnistää ajastimen,
hiiren ja näppäimistön apuprosessit `rfork(RFPROC)`:lla eikä RFMEM:llä,
joten ne ovat tavallisia forkattuja prosesseja, jotka puhuvat putken kautta.

## Kesken

Suunnitelma ja vaiheet A–D: docs/architecture.md.


- rfork(RFMEM) ja libthread: säikeet ovat samaa jaettua muistia käyttäviä
  Workereita, joilla on oma pinonsa ja oma SP-globaalinsa; `_tas` ja atom
  tarvitsevat WebAssemblyn atomic-käskyt.
- Ohjelmat libthreadin päällä: rio, acme ... (libframe, libplumb,
  9P-palvelimet).
- exec RFMEM-procista.
- RFNAMEG (nimiavaruuden kopio: pgrpcpy puuttuu drawtermista), RFENVG,
  notes.
- exec: wasm32-moduulin tunnistus ja käynnistys ytimen tavallisena
  exec-polkuna (nyt run3.mjs käynnistää moduulin itse).
- 3a: assembleri samalle käskykannalle (nyt stub).
- Käännös: Linuxilla monolith/cc9:n kautta; kanoninen Plan 9 -malli
  (`/sys/src/cmd/cc`, `3c`, `3l` mkfileineen) on olemassa mutta
  kokeilematta.
- Optimointi: skalaarimuuttujat, joiden osoitetta ei oteta, wasm-
  paikallismuuttujiksi muistin sijaan, sekä stackifier.
- Plan 9:n mkfilet ovat olemassa (`sys/src/cmd/3c/mkfile`, `3l/mkfile`),
  mutta 3c:tä ei ole vielä käännetty Plan 9:llä.
