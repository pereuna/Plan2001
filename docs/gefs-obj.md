# gefs:n olio-API: ehdotus (18.10.2026)

Tämä on V5:n (`docs/object-model.md`) ja gefs:n vaiheen 2
(`docs/gefs.md`) suunnitteludokumentti. Se kirjoitettiin ennen koodia,
kuten V5:n kuvaus vaatii. Sana *on* tarkoittaa tässä ehdotettua, ei
toteutettua. Ensimmäinen versio (f986e4a) katselmoitiin samana päivänä,
ja tämä versio korjaa sen (luku 8).

## Miksi

V4:n mittaus (QEMU, noin 30 000 olion Space): koko kuvan koodaus kestää
2,3–3,6 s ja restore 6,6–7,0 s, mutta kirjoitus ja sync vain 0,14 s.
Jokainen REPL-lause koodaa koko Spacen, ja pelkkä avaus kirjoittaa
uuden version. V5:n ehto (commit yli 200 ms) täyttyy.

Ratkaisu vaatii kaksi asiaa, eikä kumpikaan riitä yksin:
- tallennin, jossa olio on oma avaimensa `(Space, OID)`, jotta
  kirjoitetaan vain muuttunut (luvut 1–3);
- muutosten seuranta MicroPythonissa, jotta myös *koodataan* vain
  muuttunut (luku 4). Tiivistevertailu yksin käy yhä koko graafin läpi.

## Mihin ehdotus nojaa (gefs:n koodi, `sys/src/9/gefs`)

| Asia | Missä |
|---|---|
| Avaimet ovat tavujonoja, ja ensimmäinen tavu on tyyppi (`Kdat`, `Kent`, `Kup`, `Klabel`, `Ksnap`, `Kdlist`, `Kconf`) | `gdat.h` |
| Arvo enintään `Inlmax` (512) tavua, avain enintään `Keymax` (256); 9P-viesti enintään `Max9p` (32 KiB) | `gdat.h` |
| Kaikki muutokset tehdään `fs->mutlk`:n alla (`runmutate`), lukijat (`runread`) ilman sitä, epochissa | `fs.c` |
| `btupsert` vaihtaa puun juuren (`t->bp`) **useita kertoja**, jos viestit eivät mahdu juuren puskuriin (`filledbuf`, `Bufspc` noin 8 KiB): `goto Again` ("short pull"). Yksi viesti on aina yhdessä vaihdossa. | `tree.c` `btupsert` |
| `btupsert`in virhe ei peru jo vaihdettua juurta, ja juuren haun virhe asettaa `fs->rdonly`:n | `tree.c` |
| Tyhjä wstat on `AOsync`: sweeper tekee syncin ja vastaa sen jälkeen | `fs.c` |
| Hakemiston luku pitää skannauskursoria fidissä (`f->scan`) ja hyväksyy vain offsetin 0 tai edellisen luvun jatkon (`Edscan`) | `fs.c` `readdir` |
| `read` kasvattaa kanavan offsetia luettujen tavujen määrällä | `port/sysfile.c` |
| Snapshot koskee koko nimettyä puuta, ja sen tekee vain `adm` (`/n/adm/ctl`) | `fs.c`, `ctl.c` |

Seuraus: gefs:n upsertit eivät ole transaktioita. Lukija voi nähdä
osan upserttisarjasta, ja virhe voi jättää osan voimaan. Atomisuus
rakennetaan siksi storen sisään versioilla (MVCC), ei lukolla.

## 1. Levymuoto: avaintyyppi `Kobj` ja versiot

```
Kobj  store[8]                         =>  otsake       julkaistu tila
Kobj  store[8] oid[8] ~ver[8] idx[4]   =>  pala[n]      n <= Objchunk (480)
```

- `store` on olio-tiedoston (luku 3) `qid.path`. Yksi Space on yksi store.
- `oid` (64 bittiä) on olion tunniste. gefs ei anna eikä tulkitse sitä,
  vaan OID:t antaa omistaja (luku 4).
- `ver` on Spacen versio, jossa olio kirjoitettiin. Se tallennetaan
  komplementtina (`~ver`), joten saman olion uusin versio tulee
  avainjärjestyksessä ensin.
- Arvo on aina inline. Yli `Objchunk` tavun olio jaetaan paloihin
  `idx` 0…n−1, ja palan 0 alussa on olion pituus `len[4]`. Olion
  enimmäiskoko on transaktion raja (64 MiB). Koska arvo ei ole
  lohko-osoitin, `tree.c`:n lohkojen vapautus ei muutu.
- Poistettu olio on versio, jonka pala 0 on hautakivi (`len` = −1).
- **Otsake** (avain pelkkä `Kobj store`, joten se on storen ensimmäinen
  avain): julkaistu versio `N`, vanhin säilytetty versio `K`,
  OID-laskuri ja juuret (moduulien nimet → OID). gefs ei tulkitse
  otsaketta, ainoastaan sen avaimen.
- `Kobj` lisätään `gdat.h`:n enumin loppuun, joten vanhat avaimet eivät
  muutu. Sama muutos tehdään käyttäjätilan gefs:ään (`sys/src/cmd/gefs`),
  jotta `tools/gefsck` tarkistaa ja lukee saman osion. `check.c` ja
  `dump.c` tuntevat `Kobj`:n molemmissa.
- Tämä on ensimmäinen ero upstreamin levymuotoon. 9frontin gefs ei tunne
  avainta, joten osio, jossa on storeja, luetaan vain Plan2001:n gefs:llä.

## 2. Versiot: commit, julkaisu, lukija ja historia

**Näkyvä tila on julkaistu versio `N`:** jokaisesta oliosta sen uusin
versio `ver <= N`, ellei se ole hautakivi.

**Commit `N+1`:**
1. Muuttuneet oliot ja hautakivet kirjoitetaan versiolla `N+1`. Erät
   voivat olla missä tahansa määrässä upsertteja ja syncejä, ja niiden
   välillä `mutlk` vapautuu (muut kirjoittajat eivät odota koko
   commitia). Lukija ei näe niitä, koska `N+1 > N`.
2. Otsake (`N+1`, uusi `K`, laskuri, juuret) kirjoitetaan **yhtenä
   viestinä**. Yksi viesti on aina yhdessä juuren vaihdossa, joten
   julkaisu on atominen sekä lukijoille että levyllä.
3. Sync (`AOsync`), jos commit vaatii pysyvyyden (kuten nyt aina).

**Virhe tai kaatuminen ennen vaihetta 2:** julkaistu `N` ei muutu.
Kirjoittaja poistaa versiot `> N` heti, tai avaus poistaa ne
skannaamalla. Jos gefs meni virheen takia `rdonly`:ksi, store on silti
ehjä versiossa `N`, ja jatko vaatii gefs:n uudelleenkäynnistyksen (kuten
nyt T3.5:ssä). Kaatuminen vaiheen 2 jälkeen ennen synciä: levyllä on
jonkin syncin tila, eli joko `N` tai `N+1` kokonaisena.

**Historia:** säilytetään versiot `K…N` (oletus 3, kuten nyt `v<N>`,
`v<N-1>` ja `v<N-2>`). Kun olio kirjoitetaan versiolla `N+1`,
poistetaan sen versiot, jotka ovat vanhempia kuin sen uusin versio
`<= K`. Ne eivät näy missään säilytetyssä versiossa. Siivous käy vain
olion omat avaimet läpi, eli se on O(muutos). `space.open(nimi,
version=v)` avaa säilytetyn vanhan version, ja V7:n palautuspiste on
julkaistu versio. gefs:n snapshotit jäävät koko puun varmistuksiksi ja
ylläpidon palautuspisteiksi, eivät Spacen historiaksi.

**Lukija** (V6, toinen prosessi): lukee otsakkeen (`N`), sitten
jokaisesta oliosta uusimman version `<= N`. Lopuksi se lukee otsakkeen
uudelleen. Jos sen `K > N` (kirjoittaja ehti siivota version, jota
lukija luki), lukija aloittaa alusta. Lukija ei tarvitse lukkoa eikä
gefs-snapshotteja, ja sen näkymä on aina kokonainen versio.

## 3. Moottorin API (kernel) ja käyttäjätilan näkymä

### 3.1 Moottori (`sys/src/9/gefs/obj.c`)

Tämä on `docs/gefs.md`:n vaihe 2. Kernelin omat pysyvät oliot (puu
`kernel`) ja käyttäjätilan näkymä käyttävät samoja funktioita.

```c
Otx*	objbegin(Mount *mnt, vlong store);	/* lukee otsakkeen: N */
void	objput(Otx *tx, uvlong oid, char *data, vlong n);
void	objdel(Otx *tx, uvlong oid);
void	objhdr(Otx *tx, char *hdr, int n);	/* uusi otsake (ilman N:ää ja K:ta) */
void	objcommit(Otx *tx, int sync);		/* error(): N ennallaan, versiot > N pois */
void	objabort(Otx *tx);
vlong	objget(Tree *t, vlong store, uvlong oid, uvlong ver, char **data);
void	objscan(Tree *t, vlong store, uvlong from, uvlong ver,
		int (*f)(uvlong oid, char *data, vlong n, void *a), void *a);
```

- `objput` ja `objdel` voivat kirjoittaa eriä jo ennen commitia (versio
  `N+1` on näkymätön), joten iso transaktio ei vaadi kaikkea muistiin.
  Transaktion raja on 64 MiB.
- `objcommit` kirjoittaa otsakkeen yhtenä viestinä, siivoaa historian
  (luku 2) ja tekee tarvittaessa syncin `AOsync`illa.
- Kirjoittajia on yksi storea kohden (Spacen `lock`). gefs ei sitä
  valvo; kaksi samanaikaista transaktiota samaan storeen on `Ebusy`.
- Storen poisto (`fsremove`): `AOclear` skannaa prefiksin `Kobj store`
  ja poistaa avaimet erissä, kuten `Kdat`-silmukka.

### 3.2 Käyttäjätila: olio-tiedosto 9P:nä

Uusia järjestelmäkutsuja ei tule. Store on gefs:n tiedosto.

- **Luonti:** uusi mode-bitti `DMOBJ` (`0x02000000`, qid-tyyppi `QTOBJ`
  `0x02`, molemmat vapaita `sys/include/libc.h`:ssa).
  `create("obj", ORDWR, DMOBJ|0600)` tekee storen. MicroPythonissa
  `plan9.DMOBJ`.
- **Kirjoitus on tietuevirta.** Fid pitää transaktiota, ja tietue voi
  jatkua seuraavaan Twriteen. Offsetilla ei ole merkitystä.

  | Tietue | Merkitys |
  |---|---|
  | `'p' oid[8] len[8] data[len]` | put |
  | `'d' oid[8]` | delete (hautakivi) |
  | `'h' len[4] data[len]` | otsakkeen sisältö (juuret, laskuri) |
  | `'c' flags[1]` | commit, `flags & 1` = sync |
  | `'a'` | abort |
  | `'v' ver[8]` | lukuversio (oletus: julkaistu `N`) |
  | `'s' oid[8]` | lukukursori olioon `oid` |

  Twrite, jossa on `c`, palaa vasta, kun julkaisu (ja sync) on tehty.
  Virhe hylkää transaktion, ja julkaistu versio säilyy. Fidin clunk
  ilman `c`:tä on abort.
- **Luku on virta, jonka kursori on fidissä**, kuten gefs:n hakemiston
  luvussa (`readdir`, `f->scan`): offset 0 aloittaa alusta (otsake,
  sitten oliot OID-järjestyksessä), muu offset on oltava edellisen luvun
  jatko. Tavallinen `read` toimii siis sellaisenaan. Tietueet:
  `'H' N[8] K[8] len[4] data` (otsake) ja `'o' oid[8] ver[8] len[8]`,
  jota seuraa olion data. Iso olio jatkuu seuraavaan Treadiin. Kun fid
  on aloittanut luvun, se näkee saman version `N` loppuun asti (luku 2).
  Jos kirjoittaja ehti siivota sen (`K > N`), luku antaa virheen, ja
  lukija aloittaa alusta offsetista 0.
  `'s'` ja `'v'` ennen lukua valitsevat yksittäisen olion ja version.
- **stat:** `length` on 0. `mtime` ja `muid` päivittyvät commitissa.
- **Kirjoittajan yksinoikeus** pysyy Spacen `lock`-tiedostolla
  (`DMEXCL`), koska lukijat (V6) avaavat saman storen.

## 4. MicroPython: OID:t ja muutosten seuranta

Tavoite on, että commitin työ on O(muutos), myös CPU:lla.

- **OID-taulu** (`p9image.c`) C-keossa GC:n ulkopuolella: avoin
  hajautustaulu (lineaarinen haku, poisto siirtämällä), jossa osoite
  → OID, edellisen commitin FNV-tiiviste sekä `dirty`-, `isnew`- ja
  `mark`-liput. Viitteet ovat heikkoja, eli GC ei näe taulua. Uusi OID
  tulee otsakkeen laskurista (`next`). OID:itä ei kierrätetä. Juuritietue
  on OID 1, oliot alkavat 2:sta. (Suunnitelman bittikartat GC-lohkoille
  jäivät pois: taulua tarvitaan OID:n takia joka tapauksessa, eikä
  `hasoid`-tarkistus maksa barrierissa, kun Spacea ei ole auki:
  `p9_space_noid`.)
- **Kirjoitusbarrierit** `P9_DIRTY(obj)` (`mpconfigport.h`;
  `py/mpconfig.h`:ssa tyhjä) C-poluissa, jotka muuttavat pysyviä
  tyyppejä (5.5): `py/map.c` (`mp_map_lookup` lisäyksellä ja
  poistolla, `mp_map_clear`, `mp_set_lookup`, `mp_set_remove_first`,
  `mp_set_clear`: dict, set, instanssin attribuutit, moduulin
  globaalit, luokan dict), `objdict.c` (`popitem`), `objlist.c`:n
  muuttajat, `objarray.c` (append, extend, sijoitus ja kirjoitettava
  puskuri: `readinto`, `plan9.read`) ja solun sijoitus (`obj.h`).
  Barrier kirjaa olion likaiseksi vain, jos sillä on OID (uudet löytyvät
  viitteistä). Jos likaisten lista täyttyy, commit käy taulun läpi
  (`dirtylost`).
- **Commit** koodaa juuret (muutama), likaiset ja niistä löytyvät uudet
  oliot (rekursio vain uusiin). Tietue kirjoitetaan vain, jos olio on
  uusi tai sen tiiviste muuttui. Juuritietue kirjoitetaan, jos sen
  tiiviste muuttui. Virheessä uudet OID:t perutaan, ja likaiset jäävät
  likaisiksi seuraavaan commitiin.
- **Kanoninen koodaus:** dictin, setin ja instanssin attribuuttien
  kentät koodataan tavujärjestykseen lajiteltuina. MicroPythonin
  hajautustaulun järjestys riippuu varauksesta ja lisäyshistoriasta,
  eikä restore säilytä sitä. Ilman lajittelua jokainen palautettu
  dict näytti muuttuneelta (`$spacecheck` löysi tämän V2:ssa).
- **Poisto:** GC:n sweep ja `gc_free` kutsuvat `P9_FREED`ia. OID-taulun
  rivi poistuu, ja aiemmin kirjoitetun olion OID siirtyy poistolistalle.
  Seuraava commit kirjoittaa sille hautakiven (`'d'`). Osoitteen
  uudelleenkäyttö ei siis peri OID:tä. Olio, joka on yhä RAMissa mutta
  jota ei saavuteta juurista (esim. vain `__transient__`-nimestä), jää
  storeen (avoin, luku 7).
- **Varmistus:** `$spacecheck` tekee jokaisessa commitissa myös täyden
  koodauksen ja vertaa. Testit ajetaan sen kanssa, joten unohtunut
  barrier näkyy testissä eikä hiljaisena tietojen menetyksenä.
- **Avaus** palauttaa yhä koko graafin (OID-tietueista, `build()`
  jaettu vanhan muodon kanssa) ja täyttää OID-taulun tiivisteineen.
- **Otsakkeen käyttäjäosa** (`'h'`, 72 tavua): Environmentin tiiviste
  (64 heksamerkkiä) ja OID-laskuri `next[8]`.
- **Polut:** `/usr/$user/space/<nimi>/obj` (`DMOBJ`) korvaa
  `v<N>`-tiedostot. Kuvan SHA-256:ta ei tarvita, koska gefs tarkistaa
  lohkojen tiivisteet.
- **Siirtymä V2–V4:stä:** jos Spacessa on `v<N>` mutta ei `obj`:ta,
  avaus palauttaa kuvan, ensimmäinen synkattu commit kirjoittaa kaiken
  storeen, ja vasta sen jälkeen `v*` poistetaan.

## 5. Testit

V2–V4:n testit ennallaan uudella tallentimella ja `$spacecheck`illa.
Lisäksi:

| Testi | Hyväksymisehto |
|---|---|
| T5.1 | Yhden olion muutos koodaa ja kirjoittaa O(muutos) tavua (mitataan koodatut oliot ja tietueiden tavut). |
| T5.2 | OID säilyy samana versioiden ja bootin yli. |
| T5.3 | Kaatuminen kesken commitin (`CRASH=N`): bootin jälkeen joko vanha tai uusi versio kokonaan, `gefsck` puhdas. |
| T5.4 | Storen poisto vapauttaa sen avaimet (`gefsck` puhdas, tila palautuu). |
| T5.5 | V4:n noin 30 000 olion Spacessa REPL-lauseen commit kestää alle 200 ms QEMU:ssa. |
| T5.6 | Toinen prosessi lukee storea, kun kirjoittaja tekee 100 committia: jokainen luettu näkymä on kokonainen versio. |
| T5.7 | Virhe kesken erien (testikoukku `$spacefail=k`): julkaistu versio ennallaan, versiot `> N` poistettu, ja seuraava commit onnistuu. |
| T5.8 | Olion enimmäiskoko: 64 MiB:n raja antaa `CommitError`in, ja tätä pienempi iso olio (esim. 1 MiB `bytes`) palautuu. |
| T5.9 | Levy täynnä: commit epäonnistuu, edellinen versio säilyy, ja uusi yritys onnistuu tilan vapauduttua ja gefs:n uudelleenkäynnistyksen jälkeen. |
| T5.10 | `space.open(nimi, version=v)` avaa säilytetyn vanhan version, ja liian vanha versio antaa virheen. |
| T5.11 | Siirtymä: V4:n `v<N>`-Space avautuu, ja ensimmäisen commitin jälkeen `v*` on poissa. |
| ktest `fsobj` | Moottori: put, korvaus lyhyemmällä, delete, skannaus, abort, versiot ja historian siivous. |

## 6. Toteutusjärjestys

1. `Kobj`, `obj.c` ja ktest `fsobj`; `check.c` ja `dump.c` molemmissa
   gefs-kopioissa.
2. `DMOBJ`-tiedosto 9P:nä ja sen testit (`tools/test-space.sh`: T5.6,
   T5.7).
3. MicroPython: bittikartat, barrierit, OID-sivutaulu,
   `$spacecheck` ja inkrementaalinen commit.
4. V2–V4:n testit uudella tallentimella; mittaukset QEMU:ssa ja sillillä.

## 7. Avoimet

- Avaus: V4:n noin 30 000 olion Space avautuu QEMU:ssa noin 8,3 s:ssa.
  Siitä dictien ja settien täyttö (`build()`) vie 5,5–6,4 s ja muu
  restore 1,5–2,8 s. Laiska restore
  tai nopeampi täyttö on seuraava mittauskohde.
- RAMissa elävät, juurista saavuttamattomat oliot (esim. vain
  `__transient__`-nimestä) jäävät storeen: täysi merkintä puuttuu.
- T3.5 ja T5.9 (levy täynnä, korjattu 19.10.2026) ajetaan juuren
  gefs:llä: `dd` täyttää juuren. Täysi levy vei gefs:n read-only-tilaan
  (`bugs_to_9front.md` gefs 6). Nyt data ja olioiden arvot (`dataroom`
  putissa ja jokaisessa arvoja sisältävässä erässä) pysähtyvät kahteen
  reserviin. Puun päivitykset pysähtyvät reserviin, ja sync sekä
  sweeper saavat käyttää reservin (`syncproc`, `sweepproc`). Tyhjä
  arena siirtää varauksen seuraavaan arenaan (ei `Estuffed`). Ison
  tiedoston poisto synkkaa 256 käytetyn lohkon välein, koska
  vapautetut lohkot palaavat vasta syncissä.
- T5.4 (korjattu 19.10.2026): 40 MiB:n storen luonti ja poisto jätti
  noin 20 MiB käyttöä joka kierroksella. Vuotaneita lohkoja ei ollut
  (`check`in saavutettavuuslaskenta: 19 vanhaa ennen testiä), vaan
  puu kasvoi: poiston clobberit jäivät pivotien puskureihin, ja
  lehdet pitivät poistetut arvot, koska aluetta ei enää kirjoiteta.
  Nyt `objsweep` ajaa poiston jälkeen `btdrain`in alueen lehti
  kerrallaan (avainohjattu flush lehteen asti). Kolmella kierroksella
  puu pysyy 912 KiB:ssä ja käyttö palaa ennalleen.
- Poisto ja kaatuminen (19.10.2026): `remove` kirjoittaa samaan
  upserttiin pysyvän merkin `Kobj ~0 qpath`. Siivous poistaa sen
  viimeisenä, ja sweeper jatkaa kesken jääneet poistot käynnistyksen
  ensimmäisessä syncissä (`objresume`, T5.13).
- Julkaisu ja abort (korjattu 19.10.2026): virhe historian siivouksessa
  julkaisun jälkeen ei enää peru julkaistua versiota (`commit` nollaa
  `tx->ver`in otsakkeen jälkeen, T5.12, konsolin `objfail`).
- Storen poisto jumitti gefs:n (korjattu 19.10.2026): `objsweep` piti
  `mutlk`:ta ja epochia koko siivouksen ajan, eikä limbo päässyt
  vapauttamaan. Nyt `flushtx` päästää ne `Objyield` erän välein
  (kuten AOclear). Konsolin `show locks` näyttää pidetyt lukot.
- Yksi commit on enintään 64 MiB (`Objtxmax`), joten iso
  `v<N>`-Space ei siirry storeen yhdellä commitilla.
- `check.c` ei vielä tarkista `Kobj`-avaimia erikseen (gefsck on puhdas).
- Barrierin kattavuus natiiveissa moduuleissa, joiden oliot eivät ole
  pysyviä tyyppejä (5.5): ne nostavat jo `CommitError`in.

## 8. Katselmointi (18.10.2026)

Ulkoinen katselmointi ensimmäisestä versiosta (f986e4a). Kaikki viisi
pääkohtaa pitivät koodin mukaan paikkansa.

| # | Huomio | Koodissa | Ratkaisu |
|---|---|---|---|
| 1 | Tiivistevertailu ei poista koodauksen CPU-kustannusta | Koodaus käy koko graafin läpi | Muutosten seuranta: bittikartat ja barrierit (luku 4) |
| 2 | Yksi `mutlk`-jakso ei anna lukijalle atomista näkymää | Pahempi kuin arvioitiin: jo yksi `btupsert` vaihtaa juuren monta kertaa (`goto Again`) | Versioidut avaimet ja julkaisu yhdellä otsakeviestillä (luku 2) |
| 3 | Virhe kesken upserttien jättää osan voimaan | `btupsert`illa ei ole perumista | Versiot `> N` ovat näkymättömiä ja poistetaan (luku 2) |
| 4 | Offset `oid<<16` ei vastaa `read`in semantiikkaa ja rajaa OID:t 48 bittiin | `sysfile.c` kasvattaa offsetia tavuilla | Virta, kursori fidissä kuten `readdir` (3.2) |
| 5 | Vanhojen versioiden säilytys määrittelemättä | gefs:n snapshot on koko puun, ja vain `adm` tekee sen | Historia storen versioina (luku 2), V6 ja V7 päivitetty |

Pienemmät: olion enimmäiskoko (`idx[4]`, raja 64 MiB, T5.8),
OID-sivutaulun sääntö (luku 4), siirtymä V2–V4:stä (T5.11),
`object-model.md`:n 6.2 (V5-menettely lisätty), testit samanaikaiselle
lukijalle (T5.6), virheelle kesken erien (T5.7) ja epäonnistuneen
syncin jälkeiselle commitille (T5.9).
