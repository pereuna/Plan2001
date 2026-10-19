# Kaatumisvuoto: 200 MiB QEMU-tapossa (19.10.2026)

> **HUOM (19.10.2026, myöhempi mittaus: tämän tiedoston luku 2 ja
> luvun 6 vaihtoehto 1 eivät vastaa mitattua käyttäytymistä).**
> Uudelleenmitattu kaatokuva, jossa häntä oli 155.27 MiB, oli
> käyttäjätilan `gefs -c`:llä **puhdas**, ja 99.9 % hännän
> allokaatioista oli jo vapaassa listassa toiston jälkeen (93 % niistä
> ei ollut koskaan kirjoitettu levylle). "Häntä" on siis
> allokointimäärä, ei kadonnut tila. Mekanismi on tarkemmin:
> `loadarena` aloittaa vapaan tilan **tyhjänä** (`a->free` tyhjä,
> `a->used = a->size`, `load.c`), ja toisto täyttää sen meritkin
> edellä olevista `LogFree`-merkinnöistä. Hännän allokaatio on siksi
> ilmaista tilaa, paitsi jos jokin meritkin *edellä* otti lohkon pois
> vapaasta tilasta. (Aiempi muotoilu "aloittaa koko areenan vapaana" oli
> virheellinen, samoin `avlmax`-perustelu; ks. `docs/status.md`.)
> Mitattu 19.10.2026: 9924/9937 hännän allokaatiota on peitettyjä ja
> **13 (0.20 MiB) peittämättömiä**; jokaisessa peittämättömässä on
> hännässä `LogFree` ennen omaa `LogAlloc`iaan, joten naiivi hännän
> vapautus kaatuisi `grabrangen` `abort()`iin (lohko ei ole vapaassa
> tilassa, joten toisto ei ottanut sitä); 8 näistä oli lisäksi
> vapautettu jo meritkin edellä, joten ne kaatuisivat myös
> `freerange`n `already free` -virheeseen. Korjausta ei tarvita
> eikä toteutettu. Mittaukset: `docs/status.md`, "Kaatumisvuoto:
> mittaus kumoaa diagnoosin" ja sen tarkennus.
> Alla oleva teksti on säilytetty sellaisenaan historiana.

`docs/gefs-obj.md` (luku 7 "Avoimet") jätti avoimeksi havainnon:
`test-gefs.sh CRASH=3` näyttää `check`in vuotolohkolaskurin kasvavan
noin 200 MiB kaatumista kohden, "suunnilleen viimeisen syncin jälkeen
kirjoitettu määrä", ja vertailu `main`-haaran kerneliin oli tekemättä.
Selvitys tehtiin ennen kuin `space-v6` yhdistettiin `main`-haaraan
(merge `c8854e6`), joten kohdissa 1–4 `main` tarkoittaa sen aikaista
`main`ia ilman gefs:n olio-API:a (`obj.c`); yhteinen esi-isä oli
`b1119c4`. Havainto kirjattiin `main`-haaraan, koska vuoto koskee
gefs:n palautusta yleensä, ei vain olio-API:a.

Tämä selvitys tekee vertailun ja paikantaa syntyhetken. **Vuoto ei ole
regressio `space-v6`:ssa.**

## 1. Vertailu: `main` ja `space-v6`

Haarojen yhteinen esi-isä on `b1119c4`; `space-v6`:ssä on 17 committia.
Allokointilokin kannalta oleelliset funktiot ovat **tavulleen samat**
molemmissa haaroissa ja molemmissa gefs-kopioissa (kernel
`sys/src/9/gefs`, käyttäjätila `sys/src/cmd/gefs`):

| Funktio | `main` vs `space-v6` |
|---|---|
| `loadlog` | sama (md5 `a5d885a6…`, kaikki neljä kopiota) |
| `logappend` | sama |
| `flushlog` | sama |
| `compresslog` | sama |
| `logbarrier` | sama |
| `blkdealloc_lk` | sama |
| `finalize`, `syncblk`, `readblk` | samat |
| `blkalloc_lk` | **eroaa** |
| `blkalloc` | **eroaa** |

Myös `load.c` (`loadarena`, `loadfs`), `fs.c`:n `sync()`,
`promote`/`wrbarrier`/`wrwait` ja `packsb`/`unpacksb` ovat samat.
Ainoa lohkoallokointia muuttava ero on `space-v6`:n täyden levyn korjaus
(`docs/bugs_to_9front.md` gefs 6): `blkalloc` ei anna `Tdat`-lohkoja kun
vapaana on enää `2*reserve`, `blkalloc_lk` palauttaa `-1` tyhjästä
arenasta `Estuffed`in sijaan, sync- ja sweeper-prosessit saavat käyttää
reservin, ja uusi `dataroom()` kysyy tilaa olioiden arvoille. Nämä
rajoittavat *milloin* lohkoja saa varata, eivät *mitä lokiin kirjoitetaan*
eivätkä miten loki palautetaan.

`loadlog`in paluu on siis identtinen. Ero ei ole koodissa vaan
kuormituksessa: `space-v6`n olio-API kirjoittaa sync-ikkunan sisällä
paljon enemmän kuin `main`-haaran tiedostokirjoitukset (V5:n commit on
enintään 64 MiB, `Objtxmax`, ja `space-v6`n testit tekevät kymmeniä
committeja per sync-ikkuna), joten sama mekanismi näkyy siinä selvemmin.

## 2. Vuodon syntyhetki

Vuoto syntyy **siinä hetkessä kun kone kaatuu**, ei synkissä eikä
palautuksessa. Ketju:

1. `blkalloc` (blk.c) ottaa lohkon ja kirjoittaa `logappend`illa
   `LogAlloc`-merkinnän. **Loki kirjoitetaan ennen puuta** — se on
   tarkoituksellista: loki on jälkikäteen toistettava varausloki.
2. `sync()` (fs.c) kirjoittaa jokaiselle areenalle `logbarrier(a, qgen)`
   eli `LogSync`-merkin, ja vasta sen jälkeen puun, arenaotsakkeet ja
   superlohkon. Sync-ikkunan sisällä syntyneet puulohkot ja niiden
   `LogAlloc`-merkinnät ovat levyllä, mutta mikään puu ei vielä viittaa
   niihin.
3. Kaatuminen (QEMU `kill -9`) jättää levyn tähän tilaan: loki sisältää
   enemmän kuin puu tuntee.
4. `loadarena` (load.c) alustaa vapaan tilan **tyhjäksi**
   (`a->free = avlcreate(...)`, `a->used = a->size`), ja `loadlog` on
   ainoa, joka lisää siihen alueita (`grabrange`/`freerange`).
5. `loadlog` toistaa lokia kunnes tulee ensimmäinen `LogSync`, jonka
   sukupolvi on yhä voimassa (`gen >= fs->qgen`), ja **palauttaa
   silloin heti** (`if(a->logtl == nil){ … return; }`). Ketjun loppuosa
   jää lukematta.

Seuraus: jokainen lohko, jonka `LogAlloc` on merkin jälkeen, on levyllä,
**ei missään vapaassa listassa** (toisto ei ehtinyt siihen) ja **ei
missään puussa** (transaktiota ei koskaan synkattu). Sitä ei voi enää
vapauttaa, koska kukaan ei tiedä siitä. Se on vuoto. Määrä on
suunnilleen se, mitä viimeisen syncin jälkeen kirjoitettiin — juuri
`docs/gefs-obj.md`: n havaitsema 200 MiB.

Sama mekanismi on `main`-haarassa ja upstreamin 9frontissa; ks. luku 4.

## 3. Mittari: `tools/gefslog`

Vuodon syntyhetken näkee ilman QEMU:a lukemalla kuvan lokiketjun. Uusi
työkalu `tools/gefslog` tekee sen **vain lukien** (ei kirjoita kuvaan,
ei ota lukkoja):

```
tools/gefslog IMG                 # areenakohtainen ketju ja häntä
tools/gefslog IMG --json          # sama JSONina
tools/gefslog IMG --census        # myös lohkotyyppien laskenta
```

Se lukee superlohkon (ensisijainen, tarvittaessa varaslohko), jokaisen
areenan otsakkeen (`h0`/`h1`), ja kävelee `loghd`-ketjun
(`logp.addr`, `logp.hash`). Se ilmoittaa jokaisesta areenasta
`LogSync`-merkit, toiston pysähtymiskohdan ja **merkin jälkeisen hännän
lohkoina ja MiBEinä**. Tarkistussummat se hyväksyy `blkhash`ina
(`finalize`) ja `bufhash`ina (lokin omat tavut), ja kertoo kumpi
täsmäsi.

Kaatumista muistuttava kuva (200 MiB / 16 KiB lohkoa merkin jälkeen):

```
$ tools/gefslog /tmp/crash200.img
/tmp/crash200.img: primary superblock, 1 arena(s), qgen 9, snap 0x4000
  arena 0 @0x4000: 256.0 of 256.0 MiB used, log chain 7 block(s)
    barriers: 9*   (replayed to gen 9; * = loadlog's replay point)
    past the replay point: 12799 entr(ies), 12799 alloc and 0 free; 199.98 MiB allocated, 0.00 MiB freed: net +199.98 MiB
  total: 12799 block(s), 199.98 MiB allocated past the last durable barrier
```

Luku 199.98 MiB on sama suuruusluokka kuin `docs/gefs-obj.md`: n 200 MiB,
ja se syntyy yhdestä asiasta: merkin jälkeisistä `LogAlloc`-merkinnöistä.

Testit (17 kpl, ei vaadi Plan 9 -työkaluja eikä QEMU:a):

```
python3 tools/test-gefslog.py
```

Ne rakentavat kuvat lohko kerrallaan gefs:n formaatissa (`gdat.h`,
`pack.c`, `blk.c`) ja tarkistavat: siisti loki ei tuota häntää; merkin
jälkeinen häntä lasketaan oikein; vanhentunut merkki (`gen < qgen`) ei
pysäytä toistoa; kaksiosaiset `LogAlloc`/`LogFree` luetaan 16 tavun
merkintöinä; usean lohkon ketju lasketaan kokonaan; areenat summataan;
varasuperlohko löytyy; ketjun silmukka on virhe; ja että ajo **ei
muuta kuvaa** (sha256 ja mtime ennallaan). Lisäksi `metrohash64_1`:n
kultatestivektorit on laskettu repon omasta `sys/src/cmd/gefs/hash.c`:stä
kääntämällä ja ajamalla.

## 4. Miksi tämä ei ole `space-v6`:n regressio

`loadlog`in paluu on sama kuin `main`-haarassa (luku 1), joten vuoto
syntyy samalla tavalla molemmissa. Ero on siinä, kuinka paljon
synkkaamatonta tavaraa ehtii kertyä ennen tappoa. `main`-haaran
`CRASH=N`-testi kirjoittaa 8 × 16 KiB:n tiedostoja 100 000 kertaa ja
poistaa niitä, eli noin 128 KiB per tiedosto; `space-v6`:n oliotesti
tekee 30 000 olion committeja, joissa on enemmän tavaraa per
sync-ikkuna. Siksi sama vuoto näkyy `space-v6`:ssä 200 MiB:nä.

Upstreamin gefs:ssä on lisäksi oma, erillinen vikansa
(`docs/bugs_to_9front.md` gefs 3): ennen korjausta `loadlog` katkaisi
häntälohkon vain muistiin, jolloin levyn ketju jatkui merkin jälkeen
oleviin, nyt vapaisiin lohkoihin, ja `check`/`compresslog` saattoivat
vapauttaa jo vapaan lohkon. **Se on jo korjattu** (`finalize` +
`syncblk` `loadlog`issa, `blk.c`): katkaistu häntä kirjoitetaan levylle
heti. Jäljelle jää tämän selvityksen vuoto, joka ei ole
uudelleenvapautus vaan hukkaan jäänyt varaus.

## 5. Miksi allokointilokin rakennetta ei muuteta (vielä)

Pyyntö oli: älä muuta allokointilokin rakennetta ennen kuin vuodon
syntyhetki tunnetaan. Syntyhetki on nyt tiedossa (luku 2), ja se on
**kaatumishetki**, ei formaatti. Vuoto ei johdu siitä, että loki olisi
väärän muotoinen, vaan siitä, että kaatumishetkellä levyllä on
synkkaamatonta allokointia, jota palautus ei ota haltuunsa. Siksi tässä
työssä:

- **ei** muuteta lokimerkintöjen muotoa, `LogSync`in semantiikkaa,
  `Arenasz`:ia eikä `loadlog`in paluuta;
- lisätään vain **lukutyökalu** ja sen testit, jotka mittaavat vuodon
  suuruuden suoraan kuvasta.

Näin muutos on turvallinen: mikään olemassa oleva kuva ei muutu, eikä
palautuspolkuun tule uutta käyttäytymistä. Jos vuoto halutaan korjata,
se on erillinen päätös, ja tämä mittari antaa sille hyväksymisehdon
(hännän pitää pudota nollaan ilman että siisti loki muuttuu). Vaihtoehdot
ja niiden riskit ovat luvussa 6.

## 6. Korjausvaihtoehdot (ei toteutettu)

Kaikki ovat muutoksia **palautukseen**, eivät lokin muotoon, joten
`tools/gefslog` toimii niiden mittarina sellaisenaan.

1. **Hännän vapautus palautuksessa.** `loadlog` jatkaisi merkin jälkeen
   ketjun loppuun ja `freerange`isi jokaisen `LogAlloc`in ja
   `grabrange`isi jokaisen `LogFree`n, mutta **vasta kun loppuosa on
   luettu** — ei siis vuoron perään, koska toisto alkaa tyhjästä puusta.
   Laajennus on pieni ja paikallinen. Riski: jos merkin jälkeinen
   transaktio ehti osittain puuhun (sync kaatui keskeltä), osa hännästä
   on oikeasti käytössä ja sen vapauttaminen olisi tuplavaraus. Siksi
   tämä vaatii `check`in kaltaisen saavutettavuustarkistuksen tai
   `Ksnap`-sukupolven vertailun ennen vapautusta.
2. **Hännän mitätöinti kirjoittamalla.** `loadlog` kirjoittaisi jo
   nykyisen katkaisun (`finalize` + `syncblk`) lisäksi hännän lohkot
   nolliksi. Ei vapauta tilaa, mutta estää vanhan datan näkymisen.
   Ei ratkaise vuotoa.
3. **Sync useammin / pienempi ikkuna.** Ei korjaa vuotoa, pienentää
   sitä. `space-v6`:n olio-API voisi synkata commitin välein; tämä on
   konfiguraatio, ei formaatti.

Suositus: vaihtoehto 1 yhdessä `check`in saavutettavuuslaskennan kanssa,
ja hyväksymisehtona `tools/gefslog`: n hännän pieneneminen nollaan
kuvassa, jossa kaato tehtiin kirjoituksen aikana — sekä
`tools/test-gefslog.py`: n testien läpäisy. Oikea kuva kaatuneesta
QEMU:sta on nyt mitattu (luku 8), joten hyväksymisehdon lähtötaso
tunnetaan.

## 7. Toistaminen

```
# kuvasta (käyttäjän gefs:n syncin jälkeen, tai kaatuneen QEMU:n levy)
tools/gefslog build/<target>/gefspart.img
tools/gefslog build/<target>/gefspart.img --census --json

# kaato QEMU:ssa (vaatii käännös-VM:n: tools/build.sh); CRASHCOPY kopioi
# osion juuri kill -9:n jälkeen, ennen kuin seuraava boot toistaa lokin
CRASHCOPY=/tmp/crash NOROOT=1 CRASH=3 tools/test-gefs.sh
tools/gefslog /tmp/crash/crash1.img
```

`CRASHCOPY` on tarpeen, koska `CRASH=N` ilman sitä käynnistää koneen
uudelleen heti tapon jälkeen: palautus toistaa lokin ja katkaisee
hännän levyltä, joten tarkasteltava tila katoaa. Pelkkä `KEEP=1` ei
auta — se jättää QEMUn käyntiin vain *virheen* jälkeen.

**`check` ei näyttänyt tätä lukua `main`-haarassa (tilanne 19.10.2026,
korjattu).** Tarkistus mitattiin kaatuneesta kuvasta (luku 8):
käyttäjätilan `gefs -c` sanoo `ok`, vaikka häntää on 155 MiB. Sen
aikaisen `main`-haaran `checkfs` ei laskenut vuotoa lainkaan: se
tarkisti, ettei mikään vapaa lohko ole käytössä (`checkdata`: `isfree`),
mutta ei sitä, että jokainen käytetty lohko olisi saavutettu. Laskuri
`checkleak` (`nreach`/`reach[]`, lohkot jotka eivät ole vapaita eikä
saavutettuja) oli silloin vain `space-v6`-haarassa, ja `space-v6`:n
mergen (`c8854e6`) jälkeenkin vain kernelin `check`issä: käyttäjätilan
`check.c` (jota `gefs -c` ajaa) ei sitä tuntenut.

**Korjaus (19.10.2026):** saavutettavuuslaskenta on nyt myös
käyttäjätilan `check`issä (`sys/src/cmd/gefs/check.c`, sama `mark`/
`checkleak` kuin kernelissä), joten `gefs -c` tulostaa saman
`leaked blocks:`-luvun. Se ei edelleenkään kaada tarkistusta —
kommentti koodissa: "reported, not failed: in-flight frees look leaked"
— koska myös puhtaalla levyllä luku on nollasta poikkeava (mitattu:
puhdas juuri 17 lohkoa, kaatunut kuva 73 lohkoa).

`tools/gefslog` on siis edelleen kätevin mittari, koska se lukee luvun
suoraan lokiketjusta ilman QEMU:a ja erottaa peitetyt lohkot
peittämättömistä.
(Huom: sama varaus kuin `checkleak`illa — aja kuvasta, joka on levossa:
kesken oleva kirjoitus näyttää vuodolta.)

## 8. Mitattu oikeasta kaatuneesta kuvasta (19.10.2026)

Edellisen version avoin kohta oli, että työkalua ei ollut ajettu oikealla
kaatuneen QEMU:n levyllä. Se on nyt ajettu: QEMU 10.0.13, OVMF, KVM,
`tools/vm-setup` → `tools/build.sh` → `CRASHCOPY=build/crashimg NOROOT=1
CRASH=1 tools/test-gefs.sh`. Ajo päättyi `PASS: gefs root`, eli
palautuminen onnistui ja `gefsck` oli puhdas.

Kolme erillistä kuvaa samasta ajosta:

| Kuva | Tila | Häntä merkin jälkeen |
|---|---|---|
| `build/baseline/clean.img` | juuri tehty juuri, ei koskaan bootattu | **0.00 MiB** (0 lohkoa) |
| `build/crashimg/crash1.img` | juuri `kill -9`:n jälkeen | **155.52 MiB** (9953 lohkoa) |
| `build/amd64/gefsdisk.img`, kaadon jälkeen | seuraava boot on toistanut lokin | **0.00 MiB** (0 lohkoa) |

Kaatunut kuva (8 areenaa, qgen 48; jokaisella areenalla yksi lokilohko):

```
$ tools/gefslog build/crashimg/crash1.img
  arena 0 @0x4000: 12.1 of 517.5 MiB used, log chain 1 block(s)
    barriers: 48*   (replayed to gen 48; * = loadlog's replay point)
    past the replay point: 1704 entr(ies), 1703 alloc and 1 free; 26.61 MiB allocated, 0.02 MiB freed: net +26.59 MiB
  ...
  total: 9953 block(s), 155.52 MiB allocated past the last durable barrier
```

Tämä vahvistaa luvun 2 mallin kolmella tapaa:

1. **Vuoto on olemassa ja mitattavissa kuvasta.** 155.52 MiB on samaa
   suuruusluokkaa kuin `docs/gefs-obj.md`: n ~200 MiB (eri ajo, eri
   kuormitus: tässä `CRASH` kirjoittaa 8 × 16 KiB:n tiedostoja, oliotesti
   committeja). Luku ei ole vakio vaan viimeisen syncin jälkeisen
   kirjoituksen määrä, kuten luku 2 väittää.
2. **Vuoto syntyy kaatumishetkellä, ei palautuksessa.** Siisti kuva antaa
   nollan, kaatunut 155 MiB ja palautunut taas nollan: `loadlog`in
   paluu ei siis jätä häntää, vaan hännän olemassaolo on täsmälleen
   "loki kirjoitettiin, puuta ei ehditty syncata". Sama ajo myös näyttää,
   että palautus siivoaa hännän levyltä (luku 4, `finalize` + `syncblk`).
3. **`check` ei tätä nähnyt.** `tools/gefsck build/crashimg/crash1.img`
   tulostaa `ok` sekä 155 MiB:n hännällä että palautuksen jälkeen —
   sen aikainen `checkfs` ei laskenut saavutettamattomia lohkoja (luku 7).
   Vuoto ei siis ole eheysvirhe vaan hukattua tilaa, ja siksi se on
   aiemmin jäänyt huomaamatta `check`in läpäisseissä ajoissa. (Nykytila:
   käyttäjätilan `check` laskee ne ja tulostaa `leaked blocks:`-luvun,
   mutta ei kaada tarkistusta; ks. luku 7 ja `docs/status.md`.)

Työkalu ei muuta kuvaa: `tools/test-gefslog.py` tarkistaa sen (sha256 ja
mtime), ja sama pätee tähän ajoon — `gefslog` avaa kuvan vain lukien.
