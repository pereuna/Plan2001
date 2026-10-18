# Pysyvä olioympäristö: määrittely (18.10.2026)

Python-ohjelman tila säilyy ilman `save`- ja `load`-kutsuja. Moduulit
löytyvät ympäristöstä eivätkä hakemistopolulta, ja tiedot ovat graafi.
Tämä dokumentti on toteutuksen ainoa määrittely. Se korvaa
suunnitteluaineiston `~/Codex`:ssa (ei repossa):

| Aineisto | Mikä |
|---|---|
| `olioiden_tallennus_keskustelu1.md` | Lähtökeskustelu: Space, Environment, Module, graafi |
| `Plan2001-ohjelmistomaarittely-v0.1.md` | Luonnos: omistajuus, `#x`-kanava, GEFS-aluehallinta, NVMe |
| `Plan2001-ohjelmistomaarittely-v0.2.md` | v0.1:n katselmointi (ei uusi määrittely) |

Tähän otettiin niistä se, mikä on toteutettavissa ja tarpeellista, ja
jokainen poisjätetty kohta on perusteltu (luvut 2–4). Sana *on*
tarkoittaa tässä päätettyä, ei toteutettua. Toteutuksen tila on luvussa
8 ja `docs/status.md`:ssä.

## 1. Mihin määrittely nojaa (tarkistettu koodista 18.10.2026)

v0.1 kirjoitettiin näkemättä repoa, ja osa sen oletuksista on väärin.

| Asia | v0.1:n oletus | Todellisuus |
|---|---|---|
| MicroPython | "integroitu kerneliin" | Käyttäjätilan prosessi. `/boot/mp` on kernelin kuvassa (devroot), mutta se ajetaan tavallisena prosessina (`docs/mpinit.md`). Kernelin muistiin sillä ei ole pääsyä. |
| gefs | käyttäjätilan palvelin, jolle tehdään "palvelinkutsu" | Ajetaan kernelin kprocina ja palvelee 9P:tä kernelin putken kautta (`docs/gefs.md`, `sys/src/9/gefs`). Kirjoittajia on yksi (mutaattori), lukijoita useita. |
| Pysyvyys | NVMe-flushia ei ole varmistettu | `sdnvme.c`:n `nvmeflush` muuntaa SCSI:n synchronize cachen NVMe:n Flushiksi. gefs kutsuu sitä (`kern.c`:n `gflushdev`) jokaisen syncin kirjoitusvaiheen jälkeen. |
| Atominen julkaisu | on rakennettava | gefs on copy-on-write: sync päättyy superlohkon vaihtoon, joten levyllä on kaatumisen jälkeen jonkin syncin kokonainen tila. |
| fsync | ei mainittu | Tyhjä wstat (`nulldir`) on gefs:ssä `AOsync` (`fs.c`) ja kaikkien käyttäjien käytettävissä. |
| Kirjoituksen yksinoikeus | uusi capability-mekanismi | gefs tukee `DMEXCL`:ää: tiedoston voi avata vain yksi kerrallaan (`fs.c`). |
| MicroPythonin säikeet | "VM-tilat ja keot erotettava" | Säikeitä ei ole (`mpconfigport.h`). Jokainen `mp`-prosessi on erillinen tulkki, jolla on oma keko (4 MiB). |
| Koodiartefaktit | "nimenomainen toteutus puuttuu" | `py/persistentcode.c` on puussa. Se lataa `.mpy`-tavukoodin, ja tallennus kytketään päälle `MICROPY_PERSISTENT_CODE_SAVE`:lla. |
| Laitekirjain `#x` | tarkistettava | Vapaa nykyisessä puussa, mutta sitä ei tarvita (luku 4). |

## 2. Mahdottomat

Näitä ei voi toteuttaa järkevällä työllä, ja niitä on turha yrittää.

| # | Ehdotus (lähde) | Miksi mahdoton |
|---|---|---|
| M1 | Kesken olevan suorituksen jatkaminen uudelleenkäynnistyksen jälkeen: pino, ohjelmalaskuri, estävät kutsut (v0.1 1.2 rajasi pois, keskustelu vihjasi) | Estävän kutsun tila on kernelissä, verkon toisessa päässä ja laitteessa. Prosessin pinon kuva ei palauta niitä. |
| M2 | Ulkoisten resurssien palautus sellaisenaan: fd:t, socketit, ajastimet, DMA (v0.1 6.3) | Kahva on kernelin tilaa, joka katoaa uudelleenkäynnistyksessä. Palautetaan vain looginen kuvaus, ja ohjelma avaa resurssin uudelleen (5.6). |
| M3 | Pythonin tavallinen viittaussemantiikka kahden eri omistajan olion välillä ja samalla kummallekin yksi kirjoittaja (v0.1 9.1, G-03, v0.2 kohta 3) | Omistajat ovat eri prosesseja, joilla on eri keot. Jaettu muuttuva olio vaatisi hajautetun jaetun muistin. Omistajan raja on siksi näkyvä (5.1). |
| M4 | Ulkoisen vaikutuksen täsmälleen yksi toteutuminen olioverkon palautuksella (v0.1 16.4) | Lähetettyä viestiä ei voi perua. Toistuminen estyy vain, jos vastaanottaja tunnistaa toiston. Rajataan pois. |
| M5 | `id()`:n säilyminen uudelleenkäynnistyksen yli | `id()` on RAM-osoite. Identiteetti on OID (5.4). |
| M6 | Jokainen sijoitus on heti levyllä ilman, että suoritus hidastuu (G-01:n suora tulkinta) | Levykirjoitus kestää millisekunteja, sijoitus nanosekunteja. Pysyvyys syntyy vahvistuspisteissä (5.3). |
| M7 | Neljä CPU:ta antaa nelinkertaisen levynopeuden (v0.1 3, itsekin kielsi) | Laitteen kaista on yhteinen. Ei tavoite. |

## 3. Ristiriidat ja niiden ratkaisu

| # | Ristiriita | Ratkaisu |
|---|---|---|
| R1 | G-02 ("ei tallennuskutsuja") vs. O-06 (commit vain ulkoisen tapahtuman lopussa): pitkään pyörivä silmukka ei tallennu koskaan (v0.2 kohta 2) | Vahvistuspisteet ovat REPL-lause, ohjelman loppu ja **lepopisteet** (estävä `plan9`-kutsu tai `sleep`, kun edellisestä commitista on kulunut väli). Ohjelma ei tarvitse kutsuja. Sovelluksen invariantin voi suojata vapaaehtoisella `with space.atomic():` -lohkolla (5.3). |
| R2 | v0.1 "Owner" voi olla yksi olio tai olioryhmä, ja `ObjectVersion` on "olion tai oliojoukon" versio (v0.2 kohta 3) | **Omistaja on Space, ja versio on koko Spacen commit.** Erillistä Owner-käsitettä ei ole. |
| R3 | G-08/X-07 (data-I/O ohittaa gefs:n) vs. F-01…F-07 (gefs:n täytyy tietää kaikista lohkoista) vs. päätös 17.10.2026 pitää gefs lähellä upstreamia | Data kulkee gefs:n kautta. Suora polku on ehdollinen vaihe V8, jonka edellytyksenä on mitattu pullonkaula (luku 4). |
| R4 | v0.2 ehdottaa ext4-tiedostoa prototyypin tallentimeksi vs. ext4 poistettiin kernelistä 17.10.2026 | Tavallinen tiedosto gefs-juuressa. Se on jo lopullinen tallennin, joten vaihetta "ensin ext4" ei tarvita. |
| R5 | v0.1 S-07/O-15 (kernelin MicroPythonin luottamusmalli) vs. todellisuus | MicroPython on käyttäjätilan prosessi. Suojausraja on Plan 9:n prosessiraja ja gefs:n oikeudet, eikä uutta mallia tarvita. |
| R6 | Keskustelu: graafihaku moduulien löytämiseen vs. M-02 (ei arvattua versiota) | Import ratkaistaan vain kiinnitetystä Environmentista. Graafi ei osallistu importtiin. |
| R7 | v0.1 "palautus ei aja `class`- ja `def`-lauseita" vs. v0.2 kohta 6 ("miten luokka syntyy?") | Funktio on tallennettu tavukoodi (`.mpy`:n raw code) ja luokka tallennettu rakenne: nimi, kantaluokat ja sanakirja. Palautus rakentaa ne suoraan (5.5). |
| R8 | v0.1 snapshotit omana GEFS-tietuetyyppinään vs. gefs:n omat snapshotit | Spacen vanhat versiot ovat sen versiotiedostoja (6.1). Koko puun snapshotit ovat gefs:n (`/n/adm/ctl snap`). |
| R9 | Space "pysyvä nimiavaruus" vs. Plan 9:n prosessin nimiavaruus | Plan 9:n nimiavaruus pysyy ennallaan eikä ole pysyvä. Space on Python-olioiden juuri, ei mount-taulu. |
| R10 | `__main__`: keskustelussa REPLin muuttujat säilyvät, v0.1:ssä vain moduuli-instanssit | REPLin `__main__` on Spacen moduuli-instanssi, ja sen globaalit säilyvät. |
| R11 | Tunniste `#0102939` vs. numerotagi `#2026` | Tagirivillä ensimmäinen tagi on aina tunniste ja loput ovat tageja. Haussa tunniste on tagi muiden joukossa, eikä sille ole omaa merkkiä (5.8). |

## 4. Ei toteuteta (nyt), ja miksi

| Ehdotus | Miksi ei | Milloin uudelleen |
|---|---|---|
| `#x`-kanava, ChannelCapability, kirjoitustiketit, receipt (v0.1 11) | MicroPython on yksisäikeinen tulkki, eikä pullonkaulaa ole mitattu. Mekanismi kopioisi gefs:n varaus-, tarkistussumma- ja CoW-logiikan (R3). | V8, jos mittaus osoittaa gefs:n mutaattorin pullonkaulaksi |
| GEFS-aluehallinta, arenat, extentit, uusi tietuetyyppi (v0.1 10) | Kuten yllä. Lisäksi se tarkoittaisi omaa tallenninmoottoria gefs:n viereen, ja siitä on päätetty olla tekemättä. | V8 |
| Prosessikohtaiset NVMe-jonot (v0.1 12.1) | Ajurin asia, ei ohjelmointimallin. | Ajurin suorituskykytyö |
| CommitID-kyselypalvelu (v0.1 14) | Commit on paikallinen tiedosto: versio näkyy tai ei näy. Epäselvää tilaa ei synny. | Kun Spacea muutetaan verkon yli |
| Usean omistajan atomiset tapahtumat (v0.1 15.4) | Laaja työ, eikä ensimmäinen käyttötapaus tarvitse sitä. | Erillinen määrittely |
| PyPI/pip-tuonti (keskustelu 7, v0.1 8.3) | Useimmat PyPI-paketit eivät toimi MicroPythonissa, ja jakelu kuuluu Monolithiin. | Monolith |
| Capability-oikeudet graafin yli (keskustelu 11) | Plan 9:n tiedosto-oikeudet riittävät, kun Space on käyttäjän tiedosto. | Kun Spaceja jaetaan käyttäjien kesken |
| Tagien tekstikenttä (GUI/TUI) | Sovellus kuuluu Monolithiin. Tähän repoon tulee `graph`-API (5.8) ja sen testit. | Monolith |
| Päivämäärätagien aikavälihaku ja kirjoitusasujen yhtenäistäminen (`#11.12.26` = `#11.12.2026`) | Ensin tavalliset tagit. | V4:n jälkeen |
| Graafin hakemistonäkymä 9P:nä | Käyttäjätason palvelin. Repo päätetään toteutettaessa (AGENTS.md: sovellukset Monolithiin). | V4:n jälkeen |
| Init ohjelmaoliona ilman juurimounttausta (`~/plan9mp/GEFS-MP-TAVOITE.md` vaihe 2) | Erillinen boot-hanke, ei riipu tästä. | Oma määrittely |
| `plan2001-architecture-lab`:n 15 CPython-testiä (v0.1 21.1) | Lähdekoodi ei ole saatavilla. Vastaesimerkit 12, 13 ja 15 ovat tässä testeinä T2.2, T7.2 ja T2.9. | – |
| `volatile`-avainsana (v0.1 6.2) | Pythonin syntaksia ei muuteta. Tilalle tulee `__transient__` (5.6). | – |

## 5. Ohjelmointimalli

### 5.1 Käsitteet

| Käsite | Määritelmä |
|---|---|
| **ModuleDefinition** | Moduulin lähdekoodi ja siitä käännetty `.mpy`. Tunniste on lähdekoodin SHA-256. Määritelmä on muuttumaton: samasta lähteestä tehdään `.mpy` kerran, eikä sitä käännetä uudelleen. |
| **Environment** | Muuttumaton lista `import-nimi → ModuleDefinition` sekä runtime-tunniste. Tunniste on tekstin SHA-256. |
| **Space** | Pysyvä olioverkko: moduuli-instanssit (myös `__main__`), niiden globaalit ja kaikki niistä saavutettava. Spacella on täsmälleen yksi Environment. Space on myös omistaja: sillä on yksi kirjoittaja kerrallaan. |
| **ModuleInstance** | ModuleDefinitionin tila yhdessä Spacessa (moduulin globaalit). Tunniste on `(Space, import-nimi)`. |
| **Execution** | `mp`-prosessi, joka on avannut Spacen. Kirjoittaja on prosessi, jolla on Spacen `lock`-tiedosto auki (`DMEXCL`). |
| **Version** | Spacen commit `N` (1, 2, 3, …): kokonainen ja muuttumaton kuva. |
| **OID** | Olion tunniste Spacen sisällä (5.4). |

Spacen rajan yli ei ole muuttuvia viittauksia (M3). Toisen Spacen
olioita voi lukea sen julkaistusta versiosta (V6). Muutos toiseen
Spaceen on viesti sen kirjoittajalle, ja se määritellään myöhemmin.

### 5.2 Import

`mp -S nimi` avaa Spacen. Silloin `import x` ratkaistaan tässä
järjestyksessä:

1. Sisäänrakennetut moduulit (`sys`, `math`, `json`, `plan9`, …).
2. Spacen Environment: `x` → ModuleDefinition. Jos instanssi on jo
   Spacessa, se palautetaan. Muuten määritelmän ylätason koodi ajetaan
   uudessa instanssissa.
3. Muuten `ImportError`. `sys.path`ia ja tiedostopolkuja ei käytetä.

Ilman `-S`:ää `mp` toimii kuten nyt (`sys.path`, tiedostot), eikä mikään
ole pysyvää.

Ensimmäisessä versiossa vain litteät moduulit ovat tuettuja. Paketit
(`a.b`) ja suhteelliset importit antavat `ImportError: packages not
supported`. Epäonnistunut ylätason koodi poistaa instanssin, kuten
CPythonissa, eikä se päädy commitiin.

Ympäristön ylläpito on hallintaa, ei tietojen tallennusta (G-02
koskee tietoja): `space.define(polku) → tunniste`,
`space.mkenv({nimi: tunniste}) → tunniste` ja
`space.create(nimi, env)`.

### 5.3 Vahvistuspisteet ja kaatuminen

Commit (6.2) tehdään, kun Spacen tila on muuttunut edellisestä
commitista ja jokin seuraavista toteutuu:

1. REPL-lause (myös raw REPLin lohko) päättyy, myös poikkeukseen.
2. Ohjelma päättyy normaalisti tai `SystemExit`illä.
3. **Lepopiste:** estävä kutsu (`plan9.read`, `plan9.wait`,
   `plan9.sleep`, `time.sleep`), kun edellisestä commitista on kulunut vähintään `space.interval`
   sekuntia (oletus 1.0). Commit tehdään ennen kuin kutsu estää.

`with space.atomic():` lykkää kohtien 1 ja 3 commitit lohkon loppuun.
Lohkot voivat olla sisäkkäin, ja commit tehdään uloimman lopussa, jos
jokin piste osui lohkon sisälle.

Kaatuminen tarkoittaa prosessin kuolemaa, sähkökatkoa tai QEMU:n
tappamista. Sen jälkeen Space on viimeisimmän valmiin commitin tilassa.
Uusi `mp -S` jatkaa siitä. Ohjelma käynnistyy alusta, mutta jo
alustettujen moduulien ylätason koodia ei ajeta uudelleen.

Commit tehdään vain kirjoittajaprosessissa. `rfork(RFPROC)`:n lapsi
perii keon kopion ja lock-fd:n, mutta se ei commitoi: sen muutokset
eivät tallennu. Runtime tarkistaa tämän pid:llä. Lock-fd avataan
`OCEXEC`illä.

Jos commit epäonnistuu (tukematon tyyppi, levy täynnä, I/O-virhe),
edellinen versio säilyy, ja virhe näkyy:
- REPLissä tulostetaan `space: commit failed: ...`, ja REPL jatkaa;
- lepopisteessä estävä kutsu nostaa `space.CommitError`in ennen
  estämistä;
- ohjelman lopussa virhe tulostetaan, ja exit-status on virhe.

### 5.4 Identiteetti

Vaiheissa V2–V4 OID on olion indeksi versiokuvassa, eikä se ole vakaa
versioiden välillä. Vakaat 64-bittiset OID:t, joita ei koskaan
kierrätetä, tulevat vaiheessa V5, kun tallennus muuttuu
inkrementaaliseksi. Saman version sisällä jaettu viittaus palautuu
samaksi olioksi, ja syklit säilyvät.

Graafin `Node`n tunniste (`_id`, 5.8) on vakaa jo V4:ssä. Se on Noden
tavallinen attribuutti, joka tallentuu muiden mukana, eikä se riipu
V5:n OID:istä.

### 5.5 Tuetut tyypit

| Tyyppi | Tallennus |
|---|---|
| `None`, `bool`, `int` (myös mpz), `float`, `str`, `bytes`, `bytearray`, `tuple`, `list`, `dict`, `set`, `frozenset` | Arvo tai sisältö, joka viittaa muihin olioihin |
| Funktio, joka on määritelty Environmentin moduulissa | `(määritelmä, raw coden indeksi .mpy:ssä)`, globaalit (instanssi), oletusargumentit ja sulkeuman solut |
| Luokka, joka on määritelty Environmentin moduulissa | Nimi, kantaluokat ja luokan sanakirja |
| Tällaisen luokan instanssi | Luokka ja attribuuttisanakirja |
| Sidottu metodi, `staticmethod`, `classmethod` | Funktio ja `self`/luokka |
| Sisäänrakennettu moduuli tai funktio (`json`, `print`, `math.sqrt`) | Nimi |
| Environmentin moduuli-instanssi | `(Space, nimi)` |

Muut tyypit nostavat commitissa `CommitError: not persistent: <tyyppi>
at <polku>`. Tällaisia ovat esimerkiksi generaattori, tiedosto-olio,
`memoryview`, `sys.path`in moduulit ja natiiviolio. Polku kertoo, mistä
juuresta olioon päädyttiin.

Palautuksessa `dict` ja `set` täytetään vasta, kun kaikki muut oliot on
rakennettu, koska käyttäjän `__hash__` ja `__eq__` voivat tarvita niitä.
Ylätason koodia, `__init__`iä tai koristelijoita ei ajeta.

### 5.6 Haihtuvat nimet

Moduulin `__transient__ = ('sock', 'f')` jättää nämä globaalit pois
commitista. Palautuksessa ne ovat `None`. Ohjelma avaa resurssin
uudelleen esimerkiksi `if sock is None:`. Muilla tavoilla mikään ei
ohita tyyppitarkistusta hiljaa.

### 5.7 Graafi (V4)

Relaatio on luokan attribuutti. Se luetaan ja asetetaan tavallisella
sijoituksella, ja käänteinen puoli päivittyy itsestään:

```python
import graph

class Project(graph.Node):
    pass

class Photo(graph.Node):
    project = graph.link(Project, inverse='photos')     # moni yhteen
    people  = graph.links('Person', inverse='photos')   # moni moneen

photo.project = p          # p.photos sisältää photon
photo.people.add(matti)    # matti.photos sisältää photon
graph.find(Photo, project=p)    # käänteisjoukosta, ei läpikäyntiä
```

- `link` on yksi kohde tai `None`. `links` on joukko, jossa on `add`,
  `discard` ja iterointi. Käänteinen puoli on aina vain luku -joukko.
- Kohdeluokan voi antaa nimenä, jos se määritellään myöhemmin samassa
  moduulissa.
- Jokaisen `Node`-luokan instanssit ovat Spacen luokkakohtaisessa
  joukossa. `graph.find(Luokka, **ehdot)` käyttää käänteisjoukkoja
  linkkiehtoihin ja käy muut ehdot läpi tällä joukolla.
- `graph.delete(n)` poistaa `n`:n relaatioista ja luokkajoukosta. Muut
  Python-viittaukset pitävät olion elossa tavallisena oliona.
- Linkki toisen Spacen olioon on `TypeError`.
- Toteutus käyttää deskriptoreita (`MICROPY_PY_DESCRIPTORS`, otetaan
  käyttöön).

### 5.8 Ihmisen näkymä: tunniste ja #tagit (V4)

Ihminen ei tallenna tiedostoa nimellä. Olio saa tunnisteen, ja käyttäjä
kirjoittaa sen perään omat tagit somen tapaan:

```
#0102939 #kaviot #mirkku #11.12.2026 #Liisa #Hanna #Puuilo
```

Olio löytyy myöhemmin millä tahansa näistä tageista. Tunniste on yksi
tageista: varaosamyyjä muistaa numeron, joku toinen nimen tai päivän.

- **Ihmiselle näkyvä olio on `graph.Node`.** Pelkkä `str` ei voi kantaa
  tunnistetta, joten teksti on `graph.Text(Node)` (`.text`). Vanha
  tiedosto (`koe.txt`) tuodaan `Text`-olioksi, ja tiedoston nimi tulee
  sen tagiksi (`#koe.txt`).
- **Tunniste:** Node saa luonnissa Spacen laskurista numeron (`n._id`).
  Numeroa ei kierrätetä poistonkaan jälkeen. Tunniste näytetään tagina:
  `#` ja vähintään 7 numeroa etunollilla (`#0102939`). Se on olion
  ensimmäinen tagi, eikä sitä voi poistaa. Tunniste on yksilöllinen
  Spacen sisällä. Spacejen välinen muoto (`space/#0102939`) tulee V6:ssa.
- **Tagirivi on paikkasidonnainen:** ensimmäinen tagi on aina tunniste,
  ja loput ovat tageja. `#0102939 #kaviot #2026` tarkoittaa tunnistetta
  102939 sekä tageja `kaviot` ja `2026`. Uudelle oliolle järjestelmä
  kirjoittaa ensimmäisen tagin, ja käyttäjä kirjoittaa loput.
- **Haussa tagit ovat samanarvoisia:** `#x` osuu jokaiseen olioon, jolla
  on tagi `x`, oli se tunniste tai käyttäjän tagi. Numeroilla, nimillä ja
  päivämäärillä ei ole omia tyyppejä. Jos käyttäjä tagaa olion B
  tagilla `#0102939`, haku palauttaa sekä olion 102939 että B:n.
  GUI/TUI voi näyttää tunnisteosuman ensin.
- **Tagin muoto:** `#` ja merkkijono seuraavaan välilyöntiin asti
  (UTF-8). Pisteet, viivat ja numerot ovat sallittuja. Vertailu ei
  riipu kirjainkoosta (`#Mirkku` = `#mirkku`), ja tämä koskee myös
  å:ta, ä:tä ja ö:tä. MicroPythonin `str.lower` taittaa vain ASCII:n,
  joten `graph`illa on oma taittotaulu (aluksi Latin-1-täydennys ja
  Latin Extended-A). Näytössä säilyy ensimmäinen kirjoitusasu. Numerot
  verrataan merkkijonoina: `#102939` ≠ `#0102939`.
- **Tagi on Spacen olio** (`graph.Tag`: nimi ja taitettu avain), ja
  olion tagit ovat `links(Tag, inverse='objects')`. Haku käyttää
  käänteisjoukkoja (5.7) eikä käy olioita läpi.
- **Päivämäärät** ovat tavallisia tageja (`#11.12.2026`). Aikavälihaku
  tulee myöhemmin (luku 4).
- **rc:** `#` aloittaa rc:ssä kommentin, joten komentorivillä tagit
  lainataan (`'#kaviot'`). Tämä ei koske UI:ta eikä Pythonia.

Rajapinta on sama AI:lle ja käyttöliittymälle:

| Kutsu | Tulos |
|---|---|
| `graph.tag(n, '#kaviot #mirkku')` | Lisää tagit. Puuttuva `Tag` luodaan. |
| `graph.untag(n, '#mirkku')` | Poistaa tagin. Tunnistetta ei voi poistaa (`ValueError`). |
| `graph.tags(n)` | Tagit listana, tunniste ensin |
| `graph.line(n)` | Tagirivi (`#0102939 #kaviot ...`) |
| `graph.parse(rivi)` | `(tunniste, [tagit])`. Jos ensimmäinen ei ole numero tai oliota ei ole, tulee `ValueError`. Käänteinen `line`lle. |
| `graph.search('#kaviot #mirkku')` | Oliot, joilla on kaikki tagit (AND). Tyhjä haku on `ValueError`. |
| `graph.byid(102939)` | Node tai `None` |

## 6. Tallennus

### 6.1 Polut

```
/usr/$user/lib/mod/<sha256>.py     ModuleDefinitionin lähde
/usr/$user/lib/mod/<sha256>.mpy    sen tavukoodi (kerran käännetty)
/usr/$user/lib/env/<sha256>        Environment (teksti)
/usr/$user/space/<nimi>/lock       DMEXCL, kirjoittaja pitää auki
/usr/$user/space/<nimi>/v<N>       version N kuva (N 16 numerona)
```

Environmentin muoto on rivit `runtime <tunniste>` ja `<nimi> <sha256>`.
Runtime-tunniste on `mp-<versio>-mpy<mpy-versio>`. Jos se ei vastaa
ajettavaa `mp`:tä, Space ei aukea.

Kuvan muoto: otsake (`P2SP`, muodon versio, `N`, Environmentin
tunniste), oliotaulu ja juuret, ja lopuksi pituus ja SHA-256 kaikesta
sitä edeltävästä. Tarkka tavumuoto on `p9space.c`:ssä (otsake) ja `p9image.c`:ssä (runko), ja muodon versio
kasvaa jokaisessa muutoksessa.

### 6.2 Commit

1. Kuva koodataan muistiin. Jos koodaus epäonnistuu, mitään ei
   kirjoiteta.
2. `t<N+1>` luodaan, kirjoitetaan ja suljetaan.
3. `t<N+1>` nimetään `v<N+1>`:ksi (wstat).
4. Tyhjä wstat käynnistää gefs:n syncin. Commit on valmis, kun se palaa.
5. Säilytetään kaksi edellistä versiota (`v<N>`, `v<N-1>`), ja vanhemmat
   poistetaan.

**Miksi tämä on atominen:** gefs:n sync vaihtaa superlohkon, joten
kaatumisen jälkeen levyllä on jonkin syncin tila. Vaiheiden 2 ja 3
muutokset joko ovat samassa syncissä tai `t`-tiedosto on ilman
`v`-nimeä. gefs:n automaattinen sync (5 s) voi osua niiden väliin.
Puolikasta `v`-tiedostoa ei siis voi syntyä. Jos superlohko on ehjä,
mutta data ei, gefs:n lohkotarkistussummat ja kuvan SHA-256 paljastavat
vian.

**Avaus:** `t*` poistetaan. Käyttöön otetaan suurin `v<N>`, jonka pituus
ja SHA-256 täsmäävät. Rikkinäinen uudempi versio ilmoitetaan, ja se
siirretään nimelle `bad<N>`.

Toisen prosessin lukija (V6) voi nähdä `v<N+1>`:n ennen vaiheen 4
syncin paluuta. Silloin kaatuminen voi poistaa version, jonka lukija jo
näki. Lukija, jolle tämä ei käy, tekee itse syncin.

### 6.3 Toteutuksen paikat

| Mitä | Missä |
|---|---|
| Kuvan koodaus ja purku | `sys/src/cmd/micropython/p9image.c` (C, koska tarvitaan `mp_obj`-sisuksia) |
| Commit, avaus, vahvistuspisteet | `sys/src/cmd/micropython/p9space.c` |
| `space`-moduuli (`atomic`, `interval`, `define`, `mkenv`, `create`, `CommitError`) | `p9space.c` |
| Import-koukku | `main.c`:n `mp_import_stat` ja `mp_lexer_new_from_file`, ja `.mpy`:n lataus `persistentcode.c`:llä |
| `graph` | `sys/lib/python/graph.py` (Pythonia, deskriptorit) |
| Konfiguraatio | `mpconfigport.h`: `MICROPY_PERSISTENT_CODE_SAVE`, `MICROPY_PY_DESCRIPTORS`; `genhdr/` uudelleen Linuxissa |
| Testit | `tools/test-space.sh` (6.4), askeleet `sys/lib/python/test/space/` |

### 6.4 Testiympäristö

`tools/test-space.sh` rakentuu `tools/test-mpinit.sh`:n `ROOT=1`:n
päälle (gefsdisk, `root=-w`, raw REPL sarjakonsolin kautta) ja ottaa
käyttöön `tools/test-gefs.sh`:n `CRASH=N`:n:

- `PHASE=n` ajaa vaiheen n testit ja kaikkien aiempien vaiheiden testit.
- Uudelleenkäynnistys on QEMU:n uusi käynnistys samalla levyllä. Kun
  testi vaatii kaatumisen, QEMU tapetaan ilman `fshalt`ia.
- Lopuksi `tools/gefsck` tarkistaa osion käyttäjätilan gefs:llä.
- Jokaisella testillä on aikaraja, ja skripti kertoo sen etukäteen.
  `CRASH=N` on oletuksena 3. Pidemmät ajot tehdään erikseen.

## 7. Toteutusvaiheet

Jokainen vaihe on oma haaransa ja `--no-ff`-mergensä. Vaihe on valmis,
kun sen testit ja aiempien vaiheiden testit menevät läpi QEMU:ssa.
Raudalla (silli) ajetaan vaiheiden V0, V3 ja V8 testit.

**Testipaketti raudalle (18.10.2026): `spacehw`.**
`/sys/lib/python/test/space/hw.py` sisältää samat V0–V3:n testit kuin
`tools/test-space.sh` (T3.6 mukaan lukien), mutta kaatuminen on
käsin tehty virrankatkaisu. Käynnistys konsolista kerran: `spacehw`
(`/rc/bin/spacehw`). Skripti lisää itsensä `$home/lib/profile`en ja
jatkaa jokaisessa bootissa tilatiedoston (`$home/spacehw/state`)
vaiheesta. Kolme kertaa se pyytää katkaisemaan virran. Kukin katkaisu
kattaa yhtä aikaa V0:n (T0.1), V2:n (T2.1–T2.4) ja V3:n (T3.1–T3.2,
viimeisellä myös T3.3) kaatumisvaiheet. Jokainen tulos kirjataan
synkattuna riviksi tiedostoon `$home/spacehw/log`. Lopuksi skripti
poistaa koukun ja ajaa `fshalt`in. `spacehw reset` aloittaa alusta.
Kuivaharjoitus QEMU:ssa: `HW=1 tools/test-space.sh` (QEMU tapetaan,
kun skripti pyytää katkaisemaan virran), 0 FAIL.

Sillillä: `tools/build.sh`, `tools/selfroot -p /dev/nvme0n1p5` (p5:n
sisältö korvataan, PARTUUID säilyy) ja `sudo tools/espinstall /boot/efi
'-w PARTUUID=…'` (kernel, jossa `/boot/mp`). Sitten boot GRUBin
Plan2001-valinnalla ja `spacehw`. Linuxissa jälkeenpäin: `sudo dd
if=/dev/nvme0n1p5 of=build/p5.img bs=4M conv=sparse` ja `tools/gefsck
build/p5.img /usr/glenda/spacehw/log`.

**Tulos raudalla (silli, Dell Latitude 7480, NVMe, 18.10.2026):** 0 FAIL.
Kaikki V0–V3:n testit menivät läpi kolmen käsin tehdyn virrankatkaisun
yli, ja p5:n `gefs -c` oli puhdas jälkeenpäin. `plan9.sync`in mediaani
oli pienellä muutoksella 3 ms ja 1 MiB:n muutoksella 4 ms (QEMU:ssa
59 ja 106 ms). T3.1/T3.6:ssa `n` kasvoi 41 → 82 → 113. T3.2:ssa
`a == b` oli tosi jokaisen katkaisun jälkeen (71, 137, 198). T3.3:ssa
puoliksi kirjoitettu `t2` poistui, ja käyttöön tuli `v1`. T3.5 antoi
`OSError: file system full`, ja edellinen versio säilyi. Synkkaamaton
tiedosto säilyi kaikissa kolmessa katkaisussa (3/3), koska käyttäjän
reagointiaika ylittää gefs:n 5 sekunnin automaattisen syncin.

### V0. Pohja: sync ja yksinoikeus

- `plan9.sync(polku)`: tyhjä wstat.
- `plan9.create`/`open` hyväksyy `DMEXCL`:n ja `OCEXEC`in (tarkistetaan,
  että ne menevät läpi).

| Testi | Hyväksymisehto |
|---|---|
| T0.1 | Kirjoitus, `plan9.sync`, QEMU tapetaan heti paluun jälkeen. Bootin jälkeen tiedosto on ehjä, ja `gefsck` on puhdas. Tämä todentaa, että `Rwstat` palaa vasta syncin jälkeen. Jos ei palaa, korjataan gefs (ja kirjataan `docs/bugs_to_9front.md`:hen). |
| T0.2 | `DMEXCL`-tiedoston toinen avaus epäonnistuu. Ensimmäisen sulkemisen jälkeen avaus onnistuu. Prosessin kuolema vapauttaa sen. |
| T0.3 | Syncin kesto mitataan (pieni muutos, 1 MiB:n muutos) QEMU:ssa ja sillillä. Tulos kirjataan tähän dokumenttiin. |

Tulokset (18.10.2026, QEMU/KVM, virtio-blk, `PHASE=0 CRASH=3`, 2,5 min):
kaikki läpi. T0.1: jokainen synkattu 1 MiB:n tiedosto säilyi kolmessa
tapossa, ja `gefsck` on puhdas. Vertailuksi 0,5 s ennen tappoa
kirjoitetuista synkkaamattomista tiedostoista säilyi 0/3, joten sync
todella ratkaisee pysyvyyden. T0.3: mediaani pieni 61 ms ja 1 MiB 106 ms.
Pienen syncin 61 ms on yläraja commitin viiveelle REPL-lauseessa.
Sillillä (NVMe) mediaani on 3 ms pienelle ja 4 ms 1 MiB:n muutokselle
(`spacehw`, V3:n jälkeen).

### V1. Koodin identiteetti ja Environment

- `mpconfigport.h`: `MICROPY_PERSISTENT_CODE_SAVE`.
- `space.define`, `space.mkenv`, `mp -E env` (import Environmentista,
  ilman Spacea ja ilman pysyvyyttä).

| Testi | Hyväksymisehto |
|---|---|
| T1.1 | Sama lähde antaa saman tunnisteen. `.mpy` tehdään kerran, eikä toinen `define` muuta sitä. |
| T1.2 | Kahdessa Environmentissa on `calc`-moduulin eri versiot: sama asiakaskoodi antaa kummassakin oman tuloksensa. |
| T1.3 | Työhakemistossa oleva samanniminen `calc.py` ei vaikuta tulokseen (`sys.path` ei ole käytössä). |
| T1.4 | Tuntematon nimi antaa `ImportError`in. `import a.b` antaa `ImportError: packages not supported`. |
| T1.5 | Väärä runtime-tunniste Environmentissa: avaus hylätään selvällä virheellä. |

Toteutus (18.10.2026, `p9space.c`): `space.define(polku)`,
`space.mkenv({nimi: määritelmä})`, `space.hash(data)`, `space.runtime`
(`mp-1.27.0-mpy6.3`), `space.useenv(env)` ja `space.env()`. `mp -E env`
kutsuu `useenv`ia ennen ohjelmaa. Import kulkee `p9_import`in kautta
(`mpconfigport.h`: `mp_builtin___import__`). Ilman Environmentia se on
MicroPythonin oma. `.mpy` ladataan muistista (`mp_raw_code_load_mem`),
ja moduulin `__file__` on sen lähde `lib/mod/<sha256>.py`. Environment
tarkistetaan avattaessa: tekstin SHA-256 on sen nimi, muuten
`corrupt`. Tulokset: QEMU, `PHASE=1` (V0+V1, 3 min), kaikki läpi.
`tools/test-mpinit.sh` (molemmat tilat) läpi.

### V2. Space ja palautus (koko kuva)

- `p9space.c`: koodaus, purku, commit (6.2), avaus.
- `mp -S nimi`. Vahvistuspisteet 1 ja 2 (REPL ja ohjelman loppu).
- Tuetut tyypit (5.5) ja `__transient__` (5.6).

| Testi | Hyväksymisehto |
|---|---|
| T2.1 | `counter.increment()` kolmessa bootissa antaa 1, 2 ja 3. REPLin `a = 10` säilyy bootin yli. |
| T2.2 | Moduulin ylätason `print('init')` tulostuu vain ensimmäisessä bootissa (labin vastaesimerkki 12). |
| T2.3 | `a = []; b = {'x': a, 'y': a}; a.append(b)`: bootin jälkeen `b['x'] is b['y']` ja `a[0] is b`. |
| T2.4 | Luokka, instanssi, metodi, sulkeuma, oletusargumentti ja `staticmethod` toimivat bootin jälkeen. Luokan määrittelyä ei ajeta (luokan ylätason `print` ei tulostu). |
| T2.5 | Generaattori globaalissa: commit epäonnistuu viestillä, jossa on tyyppi ja polku. Edellinen versio säilyy. |
| T2.6 | `__transient__`-nimi on bootin jälkeen `None`. Muut nimet säilyvät. |
| T2.7 | Ylätasolla epäonnistuva import ei jätä instanssia (T2.1:n Space ennallaan). |
| T2.8 | Kaksi Spacea samalla Environmentilla: laskurit ovat erillisiä. |
| T2.9 | Toinen `mp -S` samaan Spaceen antaa virheen `space locked` (labin vastaesimerkki 15). `rfork`-lapsen muutokset eivät tallennu. |
| T2.10 | `dict`, jonka avaimena on käyttäjäluokan instanssi omalla `__hash__`illa, palautuu oikein. |

Toteutus (18.10.2026): `p9image.c` koodaa ja purkaa kuvan, ja
`p9space.c` hoitaa Spacen (`space.create(nimi, env)`, `space.open`,
`space.info()`, `space.CommitError`) ja commitin. `mp -S nimi` avaa
Spacen ennen ohjelmaa. REPL-lauseen commit tehdään
`MICROPY_BOARD_AFTER_PYTHON_EXEC`-koukussa, joten pyexec.c ei muutu.
Kuvan runko koostuu oliotaulusta (lajit: tuple, list, dict, set,
frozenset, bytearray, solu, luokka, instanssi, funktio, sulkeuma,
static-/classmethod, sidottu metodi) ja juurista (`__main__` ja ne
Environmentin moduulit, jotka ovat yhä `sys.modules`issa). Arvot
(None, bool, int, mpz desimaalina, float, str, bytes, sisäänrakennettu
nimellä, Spacen moduuli nimellä) ovat rivissä. Funktio on (moduuli,
polku `.mpy`:n raw code -puussa, oletusarvot). Palautus rakentaa
ensin kuoret: luokka kutsumalla `type(nimi, kannat, tyhjä dict)`,
instanssi ilman `__init__`iä ja funktio raw codesta. Sen jälkeen se
täyttää sisällöt; dictit ja setit täytetään viimeisinä, ja ne, joiden
avaimet ovat viitteitä, aivan lopuksi. `MICROPY_PY_BUILTINS_FROZENSET`
on päällä.

V2:n rajat (5.5:n mukaan, ei vikoja): `__main__`issa (REPL tai `-c`)
määritelty funktio tai luokka estää commitin (`not persistent: a
function defined in __main__`), joten funktiot kuuluvat moduuleihin.
`property` ja natiivikantaiset luokat eivät tallennu. Builtin-olion
haku on lineaarinen, ja suurten kuvien nopeus mitataan V3:ssa.

Sivulöytö: konsolin `\n` päättää REPL-rivin myös putkesta
(`p9console.c`). Ennen tätä rivi jäi ajamatta, kun `/dev/consctl`ia
ei ollut.

Tulokset: QEMU `ONLY=2` (4 boottia, joista kolme tapettu, 5 min) ja
`PHASE=2` (V0–V2, gefsck puhdas): kaikki läpi. `tools/test-mpinit.sh`
(molemmat tilat) läpi.

### V3. Lepopisteet ja kaatuminen

- Vahvistuspiste 3, `space.interval` ja `space.atomic`.

| Testi | Hyväksymisehto |
|---|---|
| T3.1 | `while True: n += 1; plan9.sleep(100)` ja `CRASH=N`: jokaisen bootin jälkeen `n` on vähintään edellisen bootin arvo, ja se kasvaa. `gefsck` on puhdas. |
| T3.2 | `with space.atomic(): a += 1; plan9.sleep(50); b += 1` ja kaatumiset: bootin jälkeen aina `a == b`. |
| T3.3 | Kaatuminen kesken `t<N+1>`:n kirjoituksen (testi hidastaa kirjoitusta): avaus käyttää `v<N>`:ää ja poistaa `t`:n. |
| T3.4 | `v<N>`:n tavu muutetaan käsin: avaus käyttää `v<N-1>`:tä, kertoo vian ja nimeää tiedoston `bad<N>`:ksi. |
| T3.5 | Levy täynnä (pieni gefs-osio): commit epäonnistuu `CommitError`illa, ja edellinen versio säilyy. |
| T3.6 | Sillillä (NVMe): T3.1 ja virran katkaisu käsin kerran. Tulos kirjataan. |

Toteutus (18.10.2026, `p9space.c`): `plan9.read`, `plan9.wait` ja
`plan9.sleep` kutsuvat `p9_space_rest`iä ennen estämistä. Se tekee
commitin, jos edellisestä on kulunut `space.interval` sekuntia
(moduuliattribuutti, `MP_REGISTER_MODULE_DELEGATION`; oletus 1.0, ≥ 0).
Epäonnistuminen nostaa `CommitError`in ennen estämistä, ja muut virheet
(esim. `OSError: file system full`) kääritään siihen. `space.atomic()`
on kontekstinhallitsija, joka siirtää lohkon sisällä osuneet REPL- ja
lepopisteiden commitit uloimman lohkon loppuun. Ohjelman loppu tekee
commitin myös lohkon sisällä (`SystemExit`). Testikoukku: kun
`$spaceslow` on asetettu, `t<N>`:n kirjoitus pitää puolivälissä tauon,
jonka pituus on sen arvo millisekunteina (T3.3). T3.5:n pieni gefs on
userspace-gefs tiedostossa (gefs vaatii vähintään 128 MiB), ja se
täytetään `dd`:llä. Täysi gefs siirtyy read-only-tilaan, minkä
jälkeen myös `t`:n luonti epäonnistuu. Edellinen versio säilyy, ja
avaus toimii.

Tulokset: QEMU `ONLY=3` (5 boottia, joista neljä tapettu) ja `PHASE=3`
(V0–V3, 116 tarkistusta, gefsck puhdas): kaikki läpi. T3.1:n `n`
kasvoi bootista toiseen 21 → 42 → 63, ja T3.2:ssa `a == b` oli tosi joka
bootissa. `tools/test-mpinit.sh` (molemmat tilat) läpi.

### V4. Graafi

- `graph.py` (5.7, 5.8) ja `MICROPY_PY_DESCRIPTORS`.

| Testi | Hyväksymisehto |
|---|---|
| T4.1 | `photo.project = p` → `photo in p.photos`. Kun projekti vaihdetaan, kuva poistuu vanhasta. |
| T4.2 | Moni moneen: `add` ja `discard` päivittävät molemmat puolet. Käänteiseen joukkoon sijoittaminen on virhe. |
| T4.3 | `graph.find(Photo, project=p)` toimii, ja se pysyy nopeana 10 000 kuvalla (aika kirjataan). |
| T4.4 | Relaatiot ja käänteisjoukot säilyvät bootin yli (T2:n mekanismi). |
| T4.5 | `graph.delete` poistaa olion molemmilta puolilta ja luokkajoukosta. Linkki toiseen Spaceen antaa `TypeError`in. |
| T4.6 | Uusi Node saa seuraavan tunnisteen. Tunnisteet eivät toistu poiston ja bootin jälkeen. |
| T4.7 | `graph.parse` antaa 5.8:n esimerkkiriviltä tunnisteen ja 6 tagia. `#0102939 #2026` antaa tunnisteen ja tagin `2026`. `parse(line(n))` antaa `n`:n tunnisteen ja tagit. |
| T4.8 | `search('#Mirkku #kaviot')` == `search('#mirkku #KAVIOT')`, ja `#Äiti` == `#äiti`. |
| T4.9 | `search('#0102939')` palauttaa olion 102939 ja lisäksi olion, jolla on käyttäjän tagi `#0102939`. `untag` ei poista tunnistetta. |
| T4.10 | `koe.txt` tuodaan `Text`-olioksi. `search('#koe.txt')` löytää sen, ja sisältö säilyy bootin yli. |
| T4.11 | 10 000 oliota ja kolmen tagin haku: aika kirjataan. |

Toteutus (18.10.2026): `sys/lib/python/graph.py` on Environmentin
moduuli, joten sen tila tallentuu Spaceen samalla mekanismilla kuin
muidenkin moduulien instanssit (V2). Tila koostuu tunnistelaskurista,
`_byid`:stä, luokkien instanssijoukoista, `_descs`:stä ja `_tags`:stä.
`link` ja `links` ovat deskriptoreita, jotka rekisteröityvät
`__set_name__`:lla (`MICROPY_PY_DESCRIPTORS` päällä). Käänteinen puoli
on kohdeluokan `_Inverse`-deskriptori, ja sen joukko on kohteen
attribuutissa `_i_<nimi>`. Nimellä annettu kohdeluokka haetaan
`sys.modules`ista ensimmäisellä käytöllä tai `Node.__getattr__`issa.
`Node.__new__` antaa tunnisteen ja lisää olion luokkajoukkoon, joten
aliluokan `__init__`in ei tarvitse kutsua `super`ia. `Tag` ei ole
`Node`, eikä se siksi kuluta tunnisteita. Taittotaulu kattaa ASCII:n,
Latin-1:n ja Latin Extended-A:n. Tiedoston tuonti on
`graph.importfile(polku)`.

Linkki toisen Spacen olioon (T4.5): V4:ssä prosessissa on vain yksi
Space, joten toisen Spacen olioita ei ole. T4.5 tarkistaa, että muu
kuin kohdeluokan olio antaa `TypeError`in. Sama tarkistus koskee V6:n
viittauksia.

Löytyi: kencc ei hyväksy nimettyä alustusta (`{ .next = NULL }`) unionin
nimettömän structin jäsenelle (`py/objtype.c`:n `__set_name__`-lista).
Kopio sijoittaa arvon alustuksen sijaan, ja vika on kirjattu
`docs/bugs_to_9front.md`:hen.

Tulokset (QEMU): `ONLY=4` (kaksi bootia, joista toinen tapettu) ja
`PHASE=4` (V0–V4, 137 tarkistusta, gefsck puhdas) läpi.
`tools/test-mpinit.sh` (molemmat tilat) läpi.

| Mittaus | Aika |
|---|---|
| T4.3: 10 000 kuvan luonti ja linkitys | 3,3–3,6 s |
| `find(project=p)` (1 osuma) | 0 ms |
| `find(project=q)` (10 000 osumaa) | 0,56 s |
| `find(n=5)` (läpikäynti) | 1,0–1,2 s |
| T4.11: kolmen tagin haku 10 000 oliosta (334 osumaa) | 0,33–0,38 s |
| Noin 30 000 olion Spacen restore | 6,6–7,0 s |
| Saman Spacen koodaus | 2,3–3,6 s |
| Saman Spacen kirjoitus ja sync | 0,14 s |

Aikojen erittely saadaan `$spacetime`:lla (fd 2). Restoren kuoret
vievät 0,3 s, muiden täyttö 1,9 s ja dictien ja settien täyttö 4,6 s,
eli aika kuluu MicroPythonin omaan työhön: instanssin hajautus hakee
`__hash__`ia luokkaketjusta, ja koodi on 6c:n optimoimatonta
käännöstä. Gefs ja kuvan muoto eivät ole hitaita. GC:n merkintäpino
kasvatettiin 64:stä 4096:een (`MICROPY_ALLOC_GC_STACK_SIZE`), koska
ylivuoto käy keon läpi uudelleen. Koodaus nopeutui noin 1 s.

Johtopäätös: V5:n ehto (commit yli 200 ms) täyttyy, kun graafissa on
kymmeniä tuhansia olioita. Koko kuvan koodaus kestää sekunteja jokaisen
REPL-lauseen ja ohjelman lopussa. Lisäksi pelkkä avaus kirjoittaa
uuden version, koska identiteetillä hajautettujen joukkojen järjestys
muuttuu restoressa. V5:n inkrementaalinen tallennus ratkaisee
molemmat.

### V5. Inkrementaalinen tallennus (ehdollinen)

**Aloitetaan, kun** T0.3:n ja V2:n mittausten mukaan 4 MiB:n keon
commit kestää sillillä yli 200 ms, tai kun V6 tarvitsee vakaat OID:t.

- Vakaat OID:t: MicroPythonin oliossa ei ole vapaata kenttää, joten
  tarvitaan sivutaulu osoite → OID, jonka GC:n sweep siivoaa.
- Commitissa kirjoitetaan vain muuttuneet oliot. Ensin muutos
  tunnistetaan vertaamalla olion koodattua tiivistettä, ja
  kirjoitusbarrierit tehdään vain, jos mittaus vaatii.
- Tallennin on gefs:n kernelin olio-API (`docs/gefs.md` vaihe 2), jossa
  avain on `(Space, OID)`. Sille tehdään oma suunnitteludokumentti ennen
  koodia.

Testit: V2–V4 ennallaan. Lisäksi T5.1: yhden olion muutos kirjoittaa
`O(muutos)` tavua (mitataan), ja T5.2: OID säilyy samana versioiden yli.

### V6. Toisen Spacen lukeminen

- `space.view(nimi)` lataa toisen Spacen viimeisimmän `v<N>`:n erilliseksi
  olioverkoksi. Oliot ovat tavallisia, mutta niitä ei commitoida:
  tällaisen olion sijoittaminen omaan Spaceen on `CommitError: foreign
  object`.

| Testi | Hyväksymisehto |
|---|---|
| T6.1 | Lukija näkee ehjän version, vaikka kirjoittaja commitoi samaan aikaan (100 committia lukemisen aikana). |
| T6.2 | Gefs-oikeudet: toisen käyttäjän Space ilman lukuoikeutta antaa virheen. |
| T6.3 | Vierasta oliota ei voi tallentaa omaan Spaceen. |

### V7. Koodin päivitys ja migraatio

- `space.upgrade(nimi, uusi_env)`. Funktiot ja luokat sidotaan uuteen
  määritelmään nimellä (moduuli + `__qualname__`). Jos nimeä ei löydy,
  päivitys hylätään.
- Luokan `__schema__` (int, oletus 0): jos se muuttuu,
  `__migrate__(self, vanha)` kutsutaan jokaiselle instanssille. Jos
  metodi puuttuu, päivitys hylätään.
- Päivitys on yksi commit. Vanha versio säilyy päivityksen jälkeen
  palautuspisteenä (`v<N>`), kunnes seuraava päivitys tehdään.

| Testi | Hyväksymisehto |
|---|---|
| T7.1 | `calc` v1 → v2: tila säilyy, ja uusi koodi on käytössä. |
| T7.2 | Globaaliin talletettu vanha funktioviite käyttää päivityksen jälkeen uutta koodia (labin vastaesimerkki 13). |
| T7.3 | `__schema__` muuttuu ilman `__migrate__`ia: päivitys hylätään, ja Space on ennallaan. |
| T7.4 | Kaatuminen kesken päivityksen: bootin jälkeen on joko vanha tai uusi kokonaisuus. |

### V8. Suora datapolku (ehdollinen)

**Aloitetaan vain, jos** V5:n jälkeen mittaus sillillä osoittaa, että
kahden tai useamman rinnakkain kirjoittavan Spacen commit-ajasta yli
puolet kuluu gefs:n mutaattoria odottaessa. Silloin kirjoitetaan
erillinen määrittely v0.1:n luvun 10–12 pohjalta (`#x`, alueet). Siihen
asti v0.1:n luvut 10–14 eivät ole voimassa.

## 8. Tila

| Vaihe | Tila |
|---|---|
| V0 | QEMU ja silli: valmis 18.10.2026 (`plan9.sync`, `plan9.DMEXCL`, `tools/test-space.sh`; sync NVMe:llä 3–4 ms) |
| V1 | QEMU ja silli: valmis 18.10.2026 (`space`-moduuli, `mp -E`) |
| V2 | QEMU ja silli: valmis 18.10.2026 (`mp -S`, kuva, commit, palautus) |
| V3 | QEMU ja silli: valmis 18.10.2026 (lepopisteet, `space.interval`, `space.atomic`, T3.1–T3.6; `spacehw`) |
| V4 | QEMU: valmis 18.10.2026 (`graph.py`: relaatiot, tunniste, #tagit). V5:n ehto täyttyy suurella graafilla (koko kuvan commit sekunteja) |
| V5 | ehdollinen |
| V6 | – |
| V7 | – |
| V8 | ehdollinen |
