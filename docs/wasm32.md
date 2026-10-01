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

## Kesken

Suunnitelma ja vaiheet A–D: docs/architecture.md.


- Ydin: wasm32-prosessit drawtermin ytimen alle selaimessa niin, että
  `plan9.syscall` on drawtermin sysopen, sysread ja niin edelleen. Prosessi
  on oma Workerinsa jaetulla muistilla, ja järjestelmäkutsu odottaa
  ytimen vastausta `Atomics.wait`:lla.
- rfork(RFMEM) ja libthread: säikeet ovat samaa jaettua muistia käyttäviä
  Workereita, joilla on oma pinonsa ja oma SP-globaalinsa; `_tas` ja atom
  tarvitsevat WebAssemblyn atomic-käskyt.
- setjmp ja longjmp: WebAssemblyn poikkeuksilla (nyt longjmp lopettaa
  prosessin).
- `_tas` ja atom eivät ole atomisia: riittää yhdelle säikeelle, ei
  rfork(RFMEM):lle eikä libthreadille.
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
