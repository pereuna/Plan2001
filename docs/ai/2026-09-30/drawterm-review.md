# Drawterm: Plan2001-katselmointi ja versiohistoria

Päivä: 30.9.2026. Tarkastettu Plan2001 commit 807a12a.
Katselmointi, ei ohjelmistopäivitys. Pilvipalvelimeen ei tehty tämän työn aikana muutoksia.

## Johtopäätös

Plan2001:n drawterm on 9frontin aktiivisesti ylläpidetystä haarasta,
pinnattu commitiin 64dcc2432d85a0bdf64bb706be8787042beb8db6 (12.9.2026).
Se sisältää vanhaa Plan 9:n käyttäjätilaan tuotua kernel- ja kirjastokoodia,
mutta ei ole tunnistetusti Windows 95 -ohjelman portti. Win32 on yksi
alustatausta POSIX-, Cocoa-, Wayland-, X11- ja muiden taustojen rinnalla.

Uusin 9front-commit on 2840502ea9ec3d35d21ed22894addea7d3be3517 (26.9.2026).
Sitä ei pidä ottaa Plan2001:een sellaisenaan: siinä on tässä katselmoinnissa
toistettu parserin rajatarkistusvirhe. Repon VERSION kuvaa lisäksi sen
GUI-käynnistysregression, jota tämän työn Linux -G -kokeet eivät toistaneet.

9fans- ja 9front-haarat eivät ole yksi yhtenäinen laatuketju. 9fans on
protokollaltaan vanhempi, mutta siinä on eräitä korjauksia ja CI-näyttöä,
joita 9frontissa ei ole. Yhtä todistettavasti testatuinta cross-platform
versiota ei löytynyt.

## Alkuperä ja paikalliset muutokset

Vendoroidun puun kaikki upstream-tiedostot vastaavat byte-for-byte
64dcc24-committia. Paikallisesti lisätyt tiedostot ovat VERSION,
Make.emscripten ja kuusi gui-web-tiedostoa. Selainpuolen web/monolith.js,
web/index.html ja työkaluskriptit ovat tämän lisäksi Plan2001:n omia.
Tämä erottaa upstreamin viat paikallisen portin vioista.

Manuaali kuvaa Russ Coxin alkuperäisen drawtermin Plan 9:n 4. laitoksen
asiakkaaksi. Git-historiassa on elokuun 2005 tuontia; tämä ei todista
alkuperäisen kehitystyön tarkkaa aloituspäivää. Historiassa on myös
Mercurial/Tailor-tuonnin epäkronologisia aikaleimoja, joten git log
--reverse -järjestystä ei pidä tulkita suoraan aikajanaksi.

## Historia ja alustat

| Ajankohta | Muutos / näyttö |
|---|---|
| 2005 | Yhteisessä historiassa POSIX/X11 ja Win32-koodia. |
| 31.10.2005 | 5d6ebf7: Windows/MinGW-portin yhdistäminen. |
| 2005 | OS X -muutoksia vanhassa X11/POSIX-linjassa. Russ Coxin sivulla vanhat PowerPC- ja Intel OS X -binäärit. |
| 18.2.2016 | d6ca0cf: 9frontin DP9IK- ja rcpu-tuki sekä libauthsrv/libsec/libmp-päivitys. |
| 3.7.2018 | d951a96: Cocoa-portti 9front-haaraan. |
| 10.10.2021 | f3e0b45: Wayland-tausta. PipeWire-tuki seuraa samassa kuussa. |
| 7.5.2022 | e60c1d7: toimiva 64-bittinen Windows-build. |
| 14.8.2023 | 0085f65: Cocoa vaihtaa OpenGL:n Metaliin; commit ilmoittaa minimitasoksi OS X El Capitanin. |
| 9.7.2024 | 9fans korjaa reallocin jälkeiset osoitinlaskut vsmprint/runevsmprint-funktioissa. |
| 24.4.2026 | 43a9987: macOS HiDPI/scaling-tuki. |
| 15.8.2026 | 45ab4d2: Waylandin screen-muutosten korjaus. |
| 12.9.2026 | 64dcc24: X11 TARGETS-vastauksen elementti-/tavumäärän korjaus; Plan2001:n pin. |
| 26.9.2026 | 2840502: uusi drawunpack-parseri, upstream HEAD tarkastushetkellä. |

Windows: nykyinen 9front-puu sisältää erilliset win32- ja win64-buildit
sekä gui-win32-taustan. screen.c määrittelee _WIN32_WINNT=0x0500,
eli Windows 2000 -API-tason. Win32/GDI:n käyttö ei yksin osoita Win95-alkuperää.
9fansin README kertoo nimenomaisesti Windows-binäärin ristikkäiskäännöstä
Linuxissa ja että kirjoittaja ei ole testannut Windows-buildia Windowsissa.
Tämä historiallinen README ei todista kaikkien muiden käyttäjien testauksen puuttumista.

Linux: 9front Make.linux käyttää nykyisin Waylandia ja PipeWirea.
Plan2001 tools/build native pakottaa X11:n, AUDIO=none ja omat CFLAGS/LDADD-arvot.
Siten upstreamin Linux-oletus ja projektin Linux-testipolku ovat erilaiset.
-G ei tarvitse graafista näyttöä ajon aikana, vaikka binääriin olisi linkitetty GUI-tausta.

macOS: Cocoa/Metal ja XQuartz/X11 ovat olemassa. Vanhaa 9fans-haaraa ei
pidä rinnastaa nykyiseen 9front Cocoa/Metal -toteutukseen. Tarkastettu
9fans-puu ei sisällä gui-cocoa-taustaa. Apple Silicon- ja Intel-runtimea
ei tässä työssä testattu, eikä nightly .app:n arkkitehtuuria vahvistettu.

## Versiot ja testausnäyttö

| Linja | Tarkastettu uusin / pin | Näyttö ja rajoitus |
|---|---|---|
| Plan2001 | 64dcc24, 12.9.2026 + paikallinen gui-web | Repon selain/VM-hyväksymistestit; aiemman keskustelun kirjautuminen ja Acme-käännös. Ei sama asia kuin Windows/macOS-matriisi. |
| 9front upstream | 2840502, 26.9.2026 | Aktiivinen alustakehitys; GitHub-mirrorissa ei workflowja eikä julkaisutageja/releaseja. Uusi parserivirhe toistettu. |
| 9fans upstream | 77d5a793956cd61a63252965fe577912583aafb1, 5.7.2026 | Linux/X11 build-CI ja Coverity-workflow. Vanha p9sk1/cpu-polku; ei DP9IK/rcpu-tukea tässä puussa. |
| Debian stable drawterm-9front | 0~git20220608.bee4db6-3 (+ arkkitehtuurikohtainen rebuild) | Distro pakkaa useille CPU-arkkitehtuureille. Lähdepohja vuodelta 2022, ei Plan2001:n vuoden 2026 ominaisuuksien hyväksymistesti. |
| 9front nightly binäärit | Windows amd64/i686 ja macOS .app | Julkaistu ladattavaksi, mutta sivu ei yksilöi kaikkien binäärien lähdecommittia tai runtime-testituloksia. |

9fansin 77d5a79-commitin C- ja Coverity-workflowt raportoivat success.
C-workflow sisältää dependencies, make clean ja make; se ei aja kirjautumis-,
katkos- tai käyttöliittymätestejä. Coverity-workflow'n onnistuminen ei yksin
osoita analyysin löydösten määrää tai niiden puuttumista. Myöhempi epäonnistunut
run 32719493911 oli pull_request-commitille 7f4d473, ei mainin HEADille.

## Löydökset

### F1 — P2: tunnetut reallocin jälkeiset osoitinlaskut edelleen pinnatussa haarassa

Nykyinen libc/vsmprint.c:25 ja libc/runevsmprint.c:25 käyttävät vanhan
allokaation osoittimia reallocin jälkeen offsetin laskemiseen. C:n mukaan
onnistunut realloc päättää vanhan allokaation eliniän. Offset on laskettava
ennen reallokia ja sovellettava uuteen osoittimeen.

GCC antaa -Wuse-after-free-varoitukset. 9fans korjasi samat kohdat
commiteissa 6f30fc8 ja ab7254a 9.7.2024. Korjaukset puuttuvat sekä
Plan2001:n pinistä että uusimmasta 9front HEADista.

Tässä ei toistettu merkkijonomuotoilun kaatumista; löydös perustuu suoraan
koodiin, kääntäjän varoitukseen ja vastaavaan upstream-korjaukseen.

### F2 — P1 AI-komentokanavassa: syötetapahtumia katoaa hiljaisesti

gui-web/events.c:35 hyväksyy vain 1024 jonossa olevaa tapahtumaa. Täysi
jono pudottaa lisätapahtumat. mo_input palauttaa void, joten asiakas ei
saa virhettä tai uudelleenyrityksen mahdollisuutta. Näppäimen painallus
ja vapautus kuluttavat kaksi tapahtumaa.

Koe oikealla events.c-koodilla ilman kuluttajaa: 2000 lähetettyä tapahtumaa,
1024 jonossa, 976 hiljaisesti pudotettua. Testi osoittaa jonon semantiikan;
se ei väitä tavallisen 30 ms näppäilyn aina täyttävän juuri tätä jonoa.
Myös kern/devcons.c:296 jättää qproduce-paluuarvon tarkistamatta. Useampi
jonokerros voi siis kadottaa syötettä. Pitkien AI-komentojen siirto
näppäintapahtumina on epäluotettava työkalurajapinta.

### F3 — P2: WSS-yhteyden muodostuksella ei ole omaa aikarajaa

gui-web/wsock.c:204 odottaa pthread_cond_waitilla, kun tila on Connecting.
WSS Link merkitsee yhteyden Open vasta saatuaan palvelimen r-kuittauksen.
Jos WebSocket pysyy auki mutta sovelluskuittaus ei tule, odotus ei pääty
omasta aikarajasta. Linkin 10 minuutin Giveup koskee katkeamisen jälkeistä
palautumista, ei tätä avoimena pysyvää alkukättelyä.

Todettu koodikatselmoinnissa, ei viallisen palvelimen end-to-end-kokeella.
Yhteyden, tunnistautumisen ja tehtävän suoritusajan rajat tulee erottaa.

### F4 — P2: inkrementaalinen build voi käyttää vanhoja objektitiedostoja

Ylätason Makefile menee alihakemistoon vain puuttuvan .a:n vuoksi.
Plan2001 tools/build poistaa arkistot mutta jättää .o:t. Säännöissä ei
ole kattavia header- eikä compiler-flags-riippuvuuksia. Headerin tai
CFLAGS:n muutokset eivät siten takaa objektien uudelleenkäännöstä.

Koe käännetyssä upstream-työkopiossa: include/draw.h:n aikaleiman muutos
ja make -n tuotti "Nothing to be done for 'all'". tools/buildin .a-poisto
pakottaa arkistot uudelleen, mutta ei lisää puuttuvia .o/header-riippuvuuksia.
cp -rpu voi lisäksi jättää päivittämättä uudelleentuodun tiedoston, jos sen
mtime on kohteessa olevaa vanhempi. Versioiden vertailuun tarvitaan puhdas build.

### F5 — P1 ehdokkaana päivitettävässä HEADissa: drawunpack lukee rajojen ulkopuolelta

Vain 2840502:ssa, ei nykyisessä 64dcc24-pinissä.
kern/devdraw.c:1391 käyttää z-kentän pituustavua a[0] ennen kuin on
varmistettu, että a < e. Trunkattu n-piirtoviesti, joka sisältää vain
komentotavun ja 4-tavuisen tunnuksen, vie _lz-parserin tilanteeseen a==e.

Vdrawunpack ja drawunpack irrotettiin sellaisinaan lähteestä pieneen
testiharnessiin. ASan tunnisti yhden tavun heap-buffer-overflow-lukuoperaation
viiden tavun puskurin jälkeen. Tämä on rajatarkistusvirheen toisto, ei
todiste tuotantopalvelimeen tehdystä hyökkäyksestä tai koko GUI:n kaatumisesta.

## Tehdyt kokeet ja rajat

- Git-vertailu: vendoroitu upstream-puu identtinen 64dcc24:n kanssa.
- 64dcc24 ja 2840502: puhtaat Linux-käännökset CONF=fbdev AUDIO=none LDADD=-lm.
- Molemmat: -G -h 127.0.0.1 -a 127.0.0.1 -u glenda -c 'echo REVIEW'.
  Odotettu Connection refused, ei startup-assertia. Palvelinta ei ollut paikallisessa portissa.
- Syötejonon ylivuoto oikealla events.c-koodilla.
- HEAD-parserin ASan-harness.
- Header-riippuvuuksien dry-run.

Ei ajettu Windowsia, macOS:ää, X11/Wayland GUI:ta eikä selain/WASM-buildia
tämän katselmoinnin aikana. Emscripten ja graafiset kehitysriippuvuudet eivät
olleet valmiina. VERSIONin startup-regressio säilyy dokumentoituna
projektihavaintona, ei tässä työssä itsenäisesti vahvistettuna GUI-regressiona.
Tämä on kohdennettu katselmointi, ei koko kryptografian/kernelin auditointi.

## Suositus Plan2001:lle

1. Säilytä toistaiseksi 64dcc24-pin. Älä vaihda uusimpaan nightlyyn automaattisesti.
2. Tuo tunnetut vsmprint/runevsmprint-korjaukset arvioitavaksi pieninä patcheina.
3. Tee puhtaat buildit ja versioidut, toistettavat Linux/Windows/macOS-koepolut.
4. Irrota AI:n komento- ja tiedostokanava näppäintapahtumista ja GUI:sta.
5. Käytä 9P/auth/rcpu-koodia yhteensopivuusreferenssinä, mutta määrittele natiiville
   asiakkaalle rajattu rajapinta ja käyttöoikeudet. Pelkkä drawtermin uudelleennimeäminen ei poista kern/libmemdraw/libmemlayer-riippuvuuksia.
6. Toteuta 2001P:n pysyvä tehtäväpalvelu erillisenä elinkaarena. Drawtermin
   istunnon jatkaminen ei ole sama lupaus kuin pysyvästi hyväksytty tehtävä.

## Lähteet ja testiaineisto

- [Plan2001 VERSION](https://github.com/pereuna/Plan2001/blob/main/monolith/third_party/drawterm/VERSION)
- [9front drawterm ja binäärien ohjeet](https://drawterm.9front.org/)
- [9front GitHub-mirror](https://github.com/9front/drawterm)
- [9fans alkuperäinen linja](https://github.com/9fans/drawterm)
- [Russ Coxin sivu, vanhat binäärit](https://swtch.com/drawterm/)
- [DP9IK/rcpu-tuonti 2016](https://github.com/9front/drawterm/commit/d6ca0cfb326c9ac13c1796de917cc0547d421626)
- [Windows MinGW -yhdistäminen 2005](https://github.com/9fans/drawterm/commit/5d6ebf7)
- [64-bittinen Windows 2022](https://github.com/9front/drawterm/commit/e60c1d7)
- [Cocoa 2018](https://github.com/9front/drawterm/commit/d951a96)
- [Metal 2023](https://github.com/9front/drawterm/commit/0085f65)
- [macOS HiDPI 2026](https://github.com/9front/drawterm/commit/43a9987)
- [vsmprint-korjaus](https://github.com/9fans/drawterm/commit/6f30fc8502ade6fd510d6ef8280686ada7b60964)
- [runevsmprint-korjaus](https://github.com/9fans/drawterm/commit/ab7254aef709060ac412cbb4481fa17a49edf380)
- [9fans mainin onnistunut Linux-build](https://github.com/9fans/drawterm/actions/runs/28750428284)
- [9fans Coverity-workflow](https://github.com/9fans/drawterm/actions/runs/28750428272)
- [Debian stable -paketti](https://packages.debian.org/stable/drawterm-9front)
- [Upstream HEAD-parseri](https://github.com/9front/drawterm/blob/2840502ea9ec3d35d21ed22894addea7d3be3517/kern/devdraw.c#L1390)
- [Nightly Windows/macOS -lataukset](https://iso.only9fans.com/drawterm/)

Kokeet tehtiin keskustelun väliaikaisessa Linux-työympäristössä. Alkuperäiset
paikalliset build-logit ja harnessit eivät kuulu tähän dokumentaatiocommittiin.
Niiden keskeiset tulokset ja kokeiden rajaukset on kirjattu yllä. Paikalliset
`/workspace/scratch`-polut eivät ole toisessa Codex-istunnossa käytettävissä.
