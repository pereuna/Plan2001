# gefs:n olio-API: ehdotus (18.10.2026)

Tämä on V5:n (`docs/object-model.md`) ja gefs:n vaiheen 2
(`docs/gefs.md`) suunnitteludokumentti. Se kirjoitettiin ennen koodia,
kuten V5:n kuvaus vaatii. Sana *on* tarkoittaa tässä ehdotettua, ei
toteutettua.

## Miksi

V4:n mittaus (QEMU, noin 30 000 olion Space): koko kuvan koodaus kestää
2,3–3,6 s ja restore 6,6–7,0 s, mutta kirjoitus ja sync vain 0,14 s.
Jokainen REPL-lause koodaa koko Spacen, ja pelkkä avaus kirjoittaa
uuden version. V5:n ehto (commit yli 200 ms) täyttyy. Ratkaisu on
kirjoittaa vain muuttuneet oliot, ja se vaatii tallentimen, jossa olio
on oma avaimensa: `(Space, OID)`.

## Mihin ehdotus nojaa (gefs:n koodi, `sys/src/9/gefs`)

| Asia | Missä |
|---|---|
| Avaimet ovat tavujonoja, ja ensimmäinen tavu on tyyppi (`Kdat`, `Kent`, `Kup`, `Klabel`, `Ksnap`, `Kdlist`, `Kconf`) | `gdat.h` |
| Arvo enintään `Inlmax` (512) tavua, avain enintään `Keymax` (256); 9P-viesti enintään `Max9p` (32 KiB) | `gdat.h` |
| Kaikki muutokset tehdään `fs->mutlk`:n alla (`runmutate`, `epochstart`/`epochend`) | `fs.c` |
| `sync()` ottaa `mutlk`:n passissa 0, joten yhden `mutlk`-jakson upsertit ovat aina samassa syncissä | `fs.c` |
| `upsert(mnt, msgs, n)` → `btupsert`; haku `btlookup`, skannaus `btnewscan`/`btenter`/`btnext` prefiksillä | `fs.c`, `tree.c` |
| Tyhjä wstat on `AOsync`: sweeper tekee syncin ja vastaa sen jälkeen | `fs.c` |
| Poistetun tiedoston lohkot vapauttaa sweeperin `AOclear` erissä | `fs.c` |
| `Kdat`-arvo on lohko-osoitin, ja `btupsert` vapauttaa vanhan lohkon ylikirjoituksessa | `tree.c` |

## 1. Levymuoto: avaintyyppi `Kobj`

```
Kobj  store[8] oid[8] idx[2]  =>  data[n]     n <= Objchunk (480)
```

- `store` on olio-tiedoston (3.) `qid.path`. Yksi Space on yksi store.
- `oid` on olion tunniste storessa (64 bittiä). gefs ei anna eikä
  tulkitse sitä: OID:t antaa omistaja (mp, 4.).
- Arvo on aina inline. Olio, joka on yli `Objchunk` tavua, jaetaan
  paloihin `idx` 0…n−1. Palan 0 alussa on olion pituus `len[4]`. Koska
  arvo ei ole lohko-osoitin, `tree.c`:n lohkojen vapautus ei muutu.
  Valtaosa Python-olioista (instanssi, pieni lista, dict) mahtuu yhteen
  palaan. Isot blobit lohko-osoittimina myöhemmin, jos mittaus vaatii.
- `Kobj` lisätään `gdat.h`:n enumin loppuun, joten vanhat avaimet eivät
  muutu. Muutos tehdään myös käyttäjätilan gefs:ään (`sys/src/cmd/gefs`),
  jotta `tools/gefsck` tarkistaa ja lukee saman osion. `check.c`
  ja `dump.c` tuntevat `Kobj`:n molemmissa.
- Tämä on ensimmäinen ero upstreamin levymuotoon. 9frontin gefs ei tunne
  avainta: osio, jossa on storeja, luetaan vain Plan2001:n gefs:llä.

## 2. Moottorin API (kernel, `sys/src/9/gefs/obj.c`)

Tämä on `docs/gefs.md`:n vaihe 2. Kernelin omat pysyvät oliot (puu
`kernel`) ja käyttäjätilan näkymä (3.) käyttävät samoja funktioita.

```c
Otx*	objbegin(Mount *mnt, vlong store);
void	objput(Otx *tx, vlong oid, char *data, int n);
void	objdel(Otx *tx, vlong oid);
void	objcommit(Otx *tx, int sync);	/* error() → tx hylätty */
void	objabort(Otx *tx);
int	objget(Tree *t, vlong store, vlong oid, char **data);	/* -1: ei ole */
void	objscan(Tree *t, vlong store, vlong from,
		int (*f)(vlong oid, int idx, char *data, int n, void *a), void *a);
```

- **Transaktio** kerää viestit muistiin. `objcommit` tekee niistä
  `upsert`it yhden `qlock(&fs->mutlk)`- ja `epochstart`-jakson aikana,
  tarvittaessa useana eränä. Koska `sync()` odottaa samaa lukkoa,
  transaktio on levyllä kokonaan tai ei lainkaan. Muut kirjoittajat
  odottavat commitin ajan, kuten ison `fswrite`n.
- `sync` 1: commitin jälkeen `AOsync` sweeperille, ja `objcommit` palaa
  vasta syncin jälkeen (kuten tyhjä wstat, `plan9.sync`).
- **Korvaus ja poisto:** vanha palojen määrä luetaan palan 0 pituudesta
  (`btlookup`), ja ylimääräiset palat poistetaan (`Odelete`).
- Transaktion koko on rajattu (64 MiB): ylitys on virhe, ja tx hylätään.
- **Storen poisto** (`fsremove`): `AOclear` skannaa prefiksin
  `Kobj store` ja poistaa avaimet erissä, kuten `Kdat`-silmukka.
- Lukijat (`objget`, `objscan`) toimivat epochissa ilman `mutlk`:ta,
  kuten `runread`.

## 3. Käyttäjätilan näkymä: olio-tiedosto 9P:nä

Uusia järjestelmäkutsuja ei tule. Store on gefs:n tiedosto, jota luetaan
ja kirjoitetaan 9P:llä.

- **Luonti:** uusi mode-bitti `DMOBJ` (`0x02000000`, qid-tyyppi `QTOBJ`
  `0x02`, molemmat vapaita `sys/include/libc.h`:ssa).
  `create("obj", OWRITE, DMOBJ|0600)` tekee storen. MicroPythonissa
  `plan9.DMOBJ`.
- **Kirjoitus on tietuevirta.** Fid pitää transaktiota, ja tietue voi
  jatkua seuraavaan Twriteen (offset ei merkitse):

  | Tietue | Merkitys |
  |---|---|
  | `'p' oid[8] len[4] data[len]` | put: olio korvataan |
  | `'d' oid[8]` | delete |
  | `'c' flags[1]` | commit, `flags & 1` = sync |
  | `'a'` | abort |

  Twrite, jossa on `c`, palaa vasta, kun commit (ja sync) on tehty.
  Virhe hylkää koko transaktion. Fidin clunk ilman `c`:tä on abort.
- **Luku on avainskannaus.** Offset on avain `oid<<16 | idx`. Tread
  palauttaa avainjärjestyksessä kokonaisia palatietueita
  `oid[8] idx[2] n[2] data[n]` offsetista alkaen. Tyhjä vastaus on
  loppu. Yksittäinen olio luetaan offsetista `oid<<16`, koko store
  offsetista 0. Seuraava luku jatkaa viimeisen palan avaimesta + 1.
- **stat:** `length` on 0. `mtime` ja `muid` päivittyvät commitissa.
- **Kirjoittajan yksinoikeus** on edelleen Spacen `lock`-tiedostolla
  (`DMEXCL`), koska lukijat (V6) avaavat saman storen.
- **Lukijan ehjä näkymä** usean Treadin yli (V6): OID 0 on Spacen
  otsake (versio `N` ja juuret). Lukija lukee sen alussa ja lopussa, ja
  jos `N` muuttui, se lukee uudelleen. gefs:ltä tämä ei vaadi mitään.

## 4. MicroPythonin käyttö (V5)

- **OID:t:** sivutaulu osoite → OID, koska MicroPythonin oliossa ei ole
  vapaata kenttää. GC:n sweep poistaa vapautetut osoitteet. Uusi OID
  tulee laskurista, joka on otsakkeessa (OID 0), eikä OID:itä kierrätetä.
- **Commit:** oliot koodataan kuten nyt (`p9image.c`, mutta viitteet
  ovat OID:itä), ja jokaisen olion koodauksen tiiviste verrataan
  edelliseen. Vain muuttuneet kirjoitetaan (`p`). Oliot, joihin juurista
  ei enää pääse, poistetaan (`d`). Sitten tulee otsake ja `c` 1.
  Kirjoitusbarrierit vain, jos mittaus vaatii (object-model.md V5).
- **Avaus:** koko store luetaan ja rakennetaan kuten nyt. Laiska restore
  vain, jos mittaus vaatii.
- **Polut:** `/usr/$user/space/<nimi>/obj` (`DMOBJ`) korvaa
  `v<N>`-tiedostot. Vanhat versiot ovat gefs:n snapshotteja
  (`/n/adm/ctl snap`), eivät `v<N-1>`-tiedostoja. Kuvan SHA-256:ta ei
  tarvita, koska gefs tarkistaa lohkojen tiivisteet.

## 5. Testit

V2–V4:n testit ennallaan uudella tallentimella. Lisäksi:

| Testi | Hyväksymisehto |
|---|---|
| T5.1 | Yhden olion muutos kirjoittaa O(muutos) tavua (mitataan tietueiden tavut). |
| T5.2 | OID säilyy samana versioiden ja bootin yli. |
| T5.3 | Kaatuminen kesken transaktion (`CRASH=N`): bootin jälkeen joko vanha tai uusi kokonaisuus, `gefsck` puhdas. |
| T5.4 | Storen poisto vapauttaa sen avaimet (`gefsck` puhdas, tila palautuu). |
| T5.5 | V4:n noin 30 000 olion Spacessa REPL-lauseen commit alle 200 ms QEMU:ssa. |
| ktest `objtx` | Moottorin API: put, korvaus lyhyemmällä, delete, skannaus, abort. |

## 6. Toteutusjärjestys

1. `Kobj`, `obj.c` ja ktest `objtx`; `check.c` ja `dump.c` molemmissa
   gefs-kopioissa.
2. `DMOBJ`-tiedosto 9P:nä ja sen testi (`tools/test-space.sh`).
3. MicroPython: OID-sivutaulu ja inkrementaalinen commit.
4. V2–V4:n testit uudella tallentimella; mittaukset QEMU:ssa ja sillillä.
