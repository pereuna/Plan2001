# gefs: kernelin pysyvä oliotallennin (17.10.2026)

Plan2001:n kernel omistaa yhden pysyvän tallentimen, gefs:n (9frontin
copy-on-write-tiedostojärjestelmä). Tiedostot ovat sen näkymä, eivät sen
perusta. ext4, FAT ja muut ovat käyttäjätilan palvelimia kuten
9frontissa.

## Miksi gefs

gefs on pohjimmiltaan oliotallennus. Sen Bε-puun avaimet ovat tavujonoja,
ja tiedostojärjestelmä on rakennettu avaintyypeillä:

```
Kdat  qid[8] off[8]   → lohko-osoitin    olion data (qid on olion tunniste)
Kent  pqid[8] nimi    → Xdir             hakemistokerros, erillään datasta
Kup   qid[8]          → vanhempi
Kconf nimi            → arvo             asetukset
Ksnap, Klabel, Kdlist                    nimetyt puut, snapshotit, vapautus
```

Levyformaatissa on kaksi superlohkoa (levyn alussa ja lopussa), kaksi
arenaotsaketta, ja lohko-osoitin `off, hash, gen` kantaa lapsen
tarkistussumman. Kirjoitus on copy-on-write: vanha versio säilyy, kunnes
superlohko osoittaa uuteen. Tämä on ZFS:n DMU:n ja Cephin BlueStoren
idea pienessä koossa, eikä sitä tarvitse kirjoittaa itse.

## Levy

```
ESP (FAT, UEFI lukee)   loader ja kernel (myöhemmin vain loader)
gefs (GPT-osio)         kernelin tallennin
  ├─ puu kernel          kernelin pysyvät oliot: asetukset, identiteetti
  ├─ puu secrets         avaimet (myöhemmin salattuna, kahvoina)
  └─ puu main            juuri: ohjelmat, /sys/src, git
muut osiot              ext4, FAT, ISO9660: käyttäjätilan palvelimet
```

Puut ovat gefs:n nimettyjä puita (labeleita), eivät GPT-osioita. Ne
jakavat saman tilan, ja jokaisella on omat snapshotinsa. Päivitys
(`git/pull`, `mk install`, uusi kernel) alkaa snapshotilla, ja palautus
on toisen puun mounttaus.

## Vaiheet

1. **gefs-juuri kernelissä.** gefs:n koodi (`sys/src/9/gefs`) ajetaan
   kernelin kproceina. `fs.c` palvelee 9P:tä kuten käyttäjätilassa,
   mutta kernelin putken kautta, ja kernel mounttaa putken toisen pään
   juureksi (`mntattach`). Boot-tiedostojärjestelmää ei tarvita
   (`docs/init.md`): `root=` löytää gefs-osion.
2. **Kernelin olio-API.** Suora rajapinta moottoriin (oma
   työntekijä-id, `mutlk` ja epoch, kuten `fs.c`:n `sync()`). Puu
   `kernel` ja ensimmäiset kernelin pysyvät oliot. Ehdotus:
   `docs/gefs-obj.md` (avaintyyppi `Kobj`, `obj.c`, `DMOBJ`-tiedosto
   käyttäjätilalle; ensimmäinen käyttäjä on MicroPythonin Space, V5).
3. **ext4 pois kernelistä** (tehty 17.10.2026): ext4 on käyttäjätilan
   palvelin (9frontin ext4srv, Monolith), ja kernelin ainoa juuri on gefs.
4. Myöhemmin: `secrets` ja salaus. Lisäksi ehkä loader, joka lukee
   kernelin gefs:stä.

## Vaihe 1: rakenne

```
prosessit ── 9P ──► devmnt ──► putki (#|) ──► gefs:n kprocit ──► levy (#S)
                     (juuri)                 fs.c: runfs, runmutate,
                                             runread, runsweep, runsync
```

gefs pysyy lähellä upstreamia: `fs.c`, `tree.c`, `blk.c`, `snap.c` jne.
muuttuvat vain siltä osin kuin kernel vaatii:
- I/O: tiedostokuvaajien sijaan Chanit (levy ja putki);
- procit: `rfork` → `kproc`, `semacquire`-kanavat → kernelin lukot;
- `Rendez` (`rsleep`/`rwakeup`) kernelin unella. Kernelin `Rendez`
  sallii vain yhden nukkujan, joten tarvitaan jono;
- pino: kernelin pino on 16 KiB, joten isot pinopuskurit
  (esim. `respond`:n 32 KiB, `fswstat`:n 64 KiB) siirtyvät kekoon;
- auth: kernelin putki ei näy kenellekään muulle, ja devmnt kertoo
  käyttäjän (`up->user`), joten gefs ajetaan ilman 9P-authia (kuten `-A`);
- konsoli (`cons.c`: halt, check, show) näkyy
  `/srv/gefs.root.cmd`:nä, jonka `fshalt` pysäyttää.

**GE-instanssi (20.10.2026).** Jokainen `gefsserve` tekee oman
`Gefs`-instanssin: fd-taulukko, konsoli, halt-tila, levyn raw-tiedosto,
synkronoijan ja siivoajan prosessit, oliotransaktiot, tarkistuksen tila
ja välimuisti ovat tilavuuden, eivät koko gefsin. Tilavuus kulkee
objektin mukana (`Limbo`/`Blk`/`Tree`/`Mount`/`Dlist`, `Fid`, `Conn`,
`Dent`, `Arena`, `Syncq`, `Scan`, `Otx`) tai ensimmäisenä parametrina;
globaalia `Gefs *fs`:ää ei ole. Yhden instanssin rajoitus on poistettu,
ja `gefscache=` on nyt kaikkien tilavuuksien yhteinen budjetti. Tämä on
`docs/ge-fs-o.md`:n vaihe 1; ks. `docs/ge-fs-o-impl.md` (mitä puuttuu:
toisen tilavuuden käynnistys, natiivi oliomuoto, `-O`/`-S`).

Muoto on täsmälleen upstreamin (vaihe 2:n `Kobj` on ensimmäinen ero,
`docs/gefs-obj.md`). Käyttäjätilan gefs (`sys/src/cmd/gefs`)
reamaa ja tarkistaa saman osion, ja testit vertaavat kernelin
kirjoittamaa siihen.

## Tila (17.10.2026): vaihe 1 toimii QEMU:ssa ja raudalla

`root=-w PARTUUID=...` gefs-osiolle: kernel lataa gefs:n, käynnistää sen
kprocit (mutaattori, lukijat, sweeper, syncer, tasks, konsoli ja 9P),
mounttaa puun `main` juureksi ja ajaa sen `/init`in. Juuressa kirjoitus,
32 Mt:n tiedosto, ohjelman käännös (`6c`, `6l`), adm-puun `status` ja
`ctl` (`check`, `snap`) sekä `fshalt` toimivat. Käyttäjätilan `gefs -c`
on puhdas jälkeenpäin, ja tiedostot luetaan sillä takaisin.

Raudalla (silli, NVMe, 17.10.2026): gefs-juuri p5:llä (15 GiB,
`root=-w PARTUUID=...`), osio rakennettu QEMU:ssa ja kirjoitettu
`dd`:llä. Boot, kirjoitus ja `fshalt` toimivat, ja osion kopio oli
`gefs -c`:llä puhdas jälkeenpäin.

```
/srv/gefs.root        9P (eve): mount /srv/gefs.root /n/adm adm
/srv/gefs.root.cmd    konsoli: halt, check, show; fshalt kirjoittaa halt
/n/adm/ctl            sync, snap VANHA UUSI, fork, delsnap, check, users
/n/adm/status         käyttö, puut ja snapshotit
plan9.ini gefscache=MB  välimuisti (oletus 1/8 muistista, 16–512 MiB)
plan9.ini gefsdebug=1   9P-viestit konsoliin (gefs:n debug)
```

Kernelin gefs (`sys/src/9/gefs`, 9front-11952:n `sys/src/cmd/gefs`)
eroaa upstreamista (`git diff e8f98ab -- sys/src/9/gefs`):
- `gefs.h` korvaa libc:n: kernelin otsakkeet, ja `kern.c` antaa atomit
  (lukoilla, koska libc:n `atomic.s`:ssä on kernelin `coherence`),
  monen nukkujan `Rendez`in, `error()`n muotoilulla ja `%r`:llä,
  fd-taulun kernelin Chaneille ja kprocit (main.c:n `launch`).
- Nimet, jotka ovat kernelin: `Chan`, `Mount`, `Path`, `Rendez`, `Lock`
  (qlockiksi: gefs nukkuu lukkojen pitäessä), `cachedel`, `%M` → `%N`
  ja `%Q` → `%J`, paikalliset muuttujat `up` → `pup`, kuusi
  virhemerkkijonoa.
- Pino (16 KiB): `respond`in 32 KiB:n puskuri Conniin, `fswstat`in
  64 KiB:n merkkijonot staattisiksi (vain mutaattori), `fsstat`in
  `STATMAX` nimien kokoiseksi. Kernelin `NERR` on 64 (gefs:n
  `Estacksz`).
- Ei 9P-authia (kernelin putki), ei automaattisia snapshotteja
  (`retain` tarvitsee libc:n ajat) eikä liukulukuja (`status`).
  Trace-puskuri vain `gefstrace=MiB`:llä.
- Levyn välimuisti tyhjennetään (SCSI synchronize cache, kuten
  ext4:n aikanaan) jokaisen `wrwait`in jälkeen: arenaotsakkeet, superlohkot ja
  arenoiden loput menevät levylle tässä järjestyksessä.
- Halt pysäyttää kirjoituksen (kprocit jäävät, vain luku). Kernelin
  shutdown (reboot) haltaa sweeperin kautta (ei vielä testattu).

Upstreamin vikoja, jotka löytyivät (kaikki kootaan 9frontille `docs/bugs_to_9front.md`:hen; korjattu vain kernelin kopiossa):
- Mountin viimeisen fidin sulkeminen (`clunkmount`) pudotti puun
  listasta ennen synciä, joten sen kirjoitukset viimeisen syncin
  jälkeen katosivat (unmount ja halt alle 5 s:ssa: koko kopio pois).
  Nyt kirjoitettu puu pysyy listassa, kunnes sync on tallentanut sen.
- `user.c`:n `slurp` jätti lohkon pitoon, ja `readblk`in virheviesti
  antoi `%llx`:lle Bptr-rakenteen.

**Palautus kaatumisesta (korjattu 17.10.2026, kumpaankin kopioon).**
Upstreamin `loadlog` katkaisee allokointilokin viimeisen kelvollisen
sync-merkin kohdalle vain muistissa. Levyllä häntälohko osoitti yhä
seuraavaan lokilohkoon, jonka varaus oli merkin jälkeen (eli nyt vapaa).
`check` luki ketjun levyltä välimuistiin, ja ensimmäinen sync, joka
tiivistää lokin, vapautti vanhan ketjun sen kautta: jo vapaa lohko
vapautettiin uudelleen (kernelissä `freerange`n assert, käyttäjätilassa
myöhemmin `getblk`in `b->bp.gen == bp.gen`). Nyt katkaistu häntä
kirjoitetaan levylle heti palautuksessa (ei vain luku -mountissa).
Kernelin `check` on tiukempi kuin upstreamin: jokainen viitattu lohko
(myös vanhemmat sukupolvet, viimeinen pivotin lapsi ja lokiketjut)
tarkistetaan vapaiden listaa vasten, ja `gefstrace=MiB` tallentaa
gefs:n trace-puskurin, jonka assert tulostaa lohkon osalta.

**Kaatumisvuoto (selvitetty 19.10.2026).** Kaatumisen jälkeen levyyn
jää hukkaan noin viimeisen syncin jälkeisen kirjoituksen verran
(`space-v6`:n oliotestissä ~200 MiB; `main`-haaran mittauksessa 155 MiB).
Syy on allokointiloki, joka kirjoitetaan ennen
puuta: `loadlog` palauttaa heti ensimmäisen voimassa olevan
`LogSync`-merkin kohdalla, joten merkin jälkeen varatut lohkot eivät
tule vapaisiin listoihin eivätkä mihinkään puuhun. Vuoto syntyy
kaatumishetkellä; `loadlog` on sama sekä `space-v6`:ssä että sitä
edeltäneessä `main`-haarassa.

**Vuotoa ei ole (mitattu 19.10.2026, ks. `docs/status.md`):** 155.27
MiB:n häntä oli käyttäjätilan `gefs -c`:llä puhdas, ja 99.9 % hännän
allokaatioista oli jo vapaassa listassa. Luku on allokointimäärä, ei
kadonnut tila; korjausta ei tarvita. Tarkemmin: `loadarena` aloittaa
vapaan tilan tyhjänä ja toisto täyttää sen meritkin edellä olevista
`LogFree`-merkinnöistä, joten hännän allokaatio on ilmaista tilaa,
paitsi jos jokin meritkin *edellä* otti lohkon pois vapaasta tilasta.
Mitattu 19.10.2026: 9924/9937 peitettyjä, **13 (0.20 MiB)
peittämättömiä**, ja jokaisessa peittämättömässä on hännässä `LogFree`
ennen omaa `LogAlloc`iaan — siksi hännän vapautus palautuksessa olisi
vaarallinen, ei hyödyllinen. Analyysi, vertailu ja korjausvaihtoehdot:
`docs/gefs-crashleak.md`; hännän lukee ja luokittelee kuvasta
`tools/gefslog` (testit: `tools/test-gefslog.py`). Mitattu oikeasta
kaadosta 19.10.2026: siisti levy 0 MiB, juuri tapettu 155.52 MiB,
palautuksen jälkeen taas 0 MiB — ja `gefs -c` sanoo silti `ok`, koska
vuotoluku on diagnostinen eikä kaada tarkistusta. Saavutettavuuslaskenta
(`checkleak`) on nyt sekä kernelin että käyttäjätilan `check`issä, joten
`gefs -c` tulostaa saman `leaked blocks:`-luvun. Kaatuneen levyn taltioi
`CRASHCOPY=DIR tools/test-gefs.sh` ennen seuraavaa boottia.
`mk install`, kernelin ja loaderin käännös gefs-juuressa toimivat
(`BUILD=1`).

Avoinna: arm64-kernelin konfiguraatio, gefs:n luonti ilman
käyttäjätilan gefs:ää (Linuxissa ei ole gefs:ää: juuri kopioidaan
Plan2001:ssä), `rootdir` (Monolithin `/lib/namespace`), automaattiset
snapshotit ja rebootin shutdown-halt.

**Lyhennys (korjattu 17.10.2026, kumpaankin kopioon).** Upstreamin
`AOclear` aloitti vapautuksen uudesta pituudesta (`d.length`) eikä
seuraavasta lohkorajasta, joten yksikään lohkoavain ei osunut: lohkoja ei
vapautettu, ja kasvatus näytti vanhan datan (ktest `fstrunc`). Lisäksi
viimeisen osittaisen lohkon loppu jäi ennalleen. Nyt vapautus alkaa
seuraavasta lohkosta, ja lyhennys nollaa viimeisen lohkon lopun (uusi
lohko samassa upsertissa).

**Nimeäminen (korjattu 17.10.2026, kumpaankin kopioon).** `fswstat`
jätti nimeämisen jälkeen välimuistin Dentin `name`n osoittamaan wstat-
viestin merkkijonoihin (`n.name = d.name`; `cpkey` päivitti vain
avaimen). Jos sama Dent säilyi seuraavaan nimeämiseen (esim. fid auki
tai kernelin viivästetty clunk), seuraava viesti ylikirjoitti puskurin.
Silloin `strcmp(d.name, de->name)` vertasi uutta nimeä itseensä, ja gefs
kuittasi nimeämisen tekemättä sitä: ktest `fsrename` kadotti nimiä
ajoituksesta riippuen. Nyt `de->name` osoittaa aina Dentin omaan
avaimeen (`de->buf + 11`, kuten `getdent`issä). Sama vika on upstreamin
HEADissa (`a411fb63c`, siellä puskuri on pinossa). ktest `fsrenopen`
pitää tiedoston auki 100 edestakaisen nimeämisen ajan, joten Dent pysyy
varmasti; ilman korjausta se kaatuu toisessa nimeämisessä.

**Oikeudet:** gefs tarkistaa ne attachin käyttäjän mukaan (Plan 9:n
tapa). Juuri on attachattu hostownerina, joten `none`ksi vaihtava
prosessi käyttää yhä hostownerin mounttia; toinen käyttäjä mounttaa
`/srv/gefs.root`:n itse (ktest `fsperm`). devext4 katsoi `up->user`:ia.

## Testit

`tools/test-gefs.sh`: `tools/gefsroot` tekee juurikuvan käännös-VM:ssä ja
`tools/gefsdisk` levyn, joka bootataan QEMU:ssa: kirjoitus, käännös,
check, snapshot, fshalt; lopuksi `tools/gefsck` tarkistaa osion VM:ssä
käyttäjätilan gefs:llä ja lukee bootin tiedostot takaisin. `BUILD=1`
kääntää gefs-juuressa puun, kernelin ja loaderin, `CRASH=N` tappaa
QEMU:n N kertaa kesken kirjoituksen ja `KTEST=all` ajaa kernelin testit
(`sys/src/cmd/ktest`, `-u` muulle tiedostopalvelimelle) bootatussa
järjestelmässä konsolin kautta (raudalla: `ktest all` kehotteessa). Tulossa:
snapshotista käynnistys.
