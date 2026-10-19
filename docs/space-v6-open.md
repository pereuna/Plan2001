# V6-haaran avoimet ennen V7:tä (19.10.2026)

Haara `space-v6` (`2863f82`) on nyt yhdistetty `main`-haaraan (merge
`c8854e6`, 19.10.2026), joten myös sen suunnitteludokumentit
(`docs/object-model.md`, `docs/gefs-obj.md`) ovat `main`issa. Merge ei
kuitenkaan sulkenut alla olevia asioita: ne olivat avoimia haaran
HEADissa ja ovat sitä edelleen. Tämä tiedosto kokoaa ne asiat, jotka
dokumenttien mukaan ovat kesken ennen vaihetta V7, ja erottaa ne, jotka
on jo korjattu.

Lähteet on tarkistettu koodista `space-v6`:n HEADista (`2863f82`).
Viittaukset `obj.c`, `p9space.c`, `p9image.c` ja
`tools/test-space.sh` ovat siihen versioon.

## 1. "200 MiB" ei ole osio vaan kaatumisvuodon laskuri

Muistikirjauksissa kulkenut "200 MiB:n osio" on itse asiassa
`gefs`-osion **vuotolohkolaskuri**, ei osion koko eikä varaus.

> **HUOM (19.10.2026):** alla oleva kohta on mitattu uudelleen, eikä
> vuotoa ole. Kuvassa, jossa häntä oli 155.27 MiB, käyttäjätilan
> `gefs -c` oli puhdas, 99.9 % hännän allokaatioista oli jo vapaassa
> listassa toiston jälkeen ja 93 % niistä ei ollut koskaan kirjoitettu
> levylle: luku on allokointimäärä, ei kadonnut tila. Tarkemmin:
> `loadarena` aloittaa vapaan tilan tyhjänä ja toisto täyttää sen
> meritkin edellä olevista `LogFree`-merkinnöistä; 13 allokaatiota
> (0.20 MiB) ei ole peitettyjä, ja niissä on hännässä `LogFree` ennen
> omaa `LogAlloc`iaan. Korjausta ei tarvita. Mittaukset:
> `docs/status.md`, "Kaatumisvuoto: mittaus kumoaa diagnoosin" ja sen
> tarkennus.

> **HUOM (19.10.2026, korjattu):** alla oleva väite, että laskuri olisi
> vain kernelissä, ei enää pidä paikkaansa: `checkleak` on nyt myös
> käyttäjätilan `check`issä (`sys/src/cmd/gefs/check.c`), joten
> `gefs -c` tulostaa `leaked blocks:`-luvun. Se ei edelleenkään kaada
> tarkistusta (diagnostinen).

- Luku tulee `check`in saavutettavuuslaskennasta: `checkleak`
  (`sys/src/9/gefs/check.c:391`) laskee lohkot, jotka eivät ole
  vapaita eikä saavutettuja. `tools/test-gefs.sh CRASH=3` näyttää sen
  kasvavan noin 200 MiB kaatumista kohden, "suunnilleen viimeisen
  syncin jälkeen kirjoitettu määrä" (`docs/gefs-obj.md` luku 7,
  haara `space-v6`).
- Syntyhetki on **kaatumishetki**, ei sync eikä palautus: `blkalloc`
  kirjoittaa `LogAlloc`-merkinnän lokiin ennen puuta, ja `loadlog`
  palauttaa heti ensimmäisen voimassa olevan `LogSync`-merkin kohdalla.
  Merkin jälkeen varatut lohkot eivät siis päädy vapaisiin listoihin
  eivätkä mihinkään puuhun.
- `docs/gefs-crashleak.md` on jo tehnyt vertailun:
  **vuoto ei ole `space-v6`:n regressio.** `loadlog`, `logappend`,
  `flushlog`, `compresslog`, `logbarrier` ja `blkdealloc_lk` ovat
  tavulleen samat `main`-haarassa, ja oikeasta kaatuneesta `main`-kuvasta
  mitattiin 155.52 MiB.
- **Auki jää kaksi asiaa.** (a) Itse vuotoa ei ole korjattu: luvun 6
  vaihtoehdoista 1 (hännän vapautus palautuksessa) vaatii `check`in
  kaltaisen saavutettavuustarkistuksen, eikä sitä ole toteutettu.
  (b) `checkleak` vain tulostaa, ei kaada tarkistusta — koodin
  kommentti: "reported, not failed: in-flight frees look leaked".
  Ennen mergeä `main`-haaran `check` ei laskenut vuotoa lainkaan;
  mergen jälkeen laskuri on myös `main`issa, mutta se ei edelleenkään
  vaikuta tulokseen, joten `gefs -c` sanoo `ok`, vaikka häntää on
  155 MiB. Luotettavin mittari on `tools/gefslog`.

## 2. `docs/gefs-obj.md` luku 7 "Avoimet": mikä on auki

Luku 7 on V5/V6:n auktoritatiivinen avoin lista. Tila 19.10.2026:

| Asia | Tila |
|---|---|
| Avaus ~8,3 s 30 000 oliolla (dictien ja settien täyttö `build()` 5,5–6,4 s) | **auki**; laiska restore tai nopeampi täyttö on seuraava mittauskohde |
| RAMissa elävät, juurista saavuttamattomat oliot (esim. vain `__transient__`-nimestä) jäävät storeen: täysi merkintä puuttuu | **auki** |
| Yksi commit on enintään 64 MiB (`Objtxmax`, `obj.c:34`), joten iso `v<N>`-Space ei siirry storeen yhdellä commitilla | **auki**; koskettaa suoraan V7:n migraatiota |
| `check.c` ei vielä tarkista `Kobj`-avaimia erikseen (gefsck on puhdas) | **auki** |
| `check`in vuotolaskuri ~200 MiB kaatumista kohden | analyysi valmis (`docs/gefs-crashleak.md`), **korjaus auki**, ks. luku 1 |
| T3.5 ja T5.9 (levy täynnä) | korjattu 19.10.2026 (`dataroom`, `syncproc`, `sweepproc`, `blkalloc_lk`) |
| T5.4 (storen poisto kasvatti puuta ~20 MiB/kierros) | korjattu 19.10.2026 (`btdrain`) |
| Poisto ja kaatuminen | korjattu 19.10.2026 (`objresume`, T5.13) |
| Julkaisu ja abort | korjattu 19.10.2026 (T5.12, konsolin `objfail`) |
| Storen poisto jumitti gefs:n | korjattu 19.10.2026 (`Objyield`) |
| Barrierin kattavuus natiiveissa moduuleissa | suljettu: ne nostavat jo `CommitError`in |

`docs/object-model.md`:n tilataulukko tiivistää saman: "Avoimet: avaus
~8,3 s (dictien täyttö), `v<N>`:n siirto yli 64 MiB:n".

## 3. Mitä V6 lupasi mutta ei toimittanut

1. **T6.6:llä ei ole testiä.** `docs/object-model.md` luettelee T6.6:n
   ("`$spacecheck`: näkymän ottaminen ja käyttö ei muuta oman Spacen
   commitia"), mutta `tools/test-space.sh` sisältää vain T6.1–T6.5.
   Dokumentti kuittaa asian "katettu: `$spacecheck` on päällä kaikissa
   V6:n ajoissa", eli kyse on epäsuorasta kattavuudesta, ei testistä.
2. **Lukijan uudelleenaloitus ei toteudu.** `docs/gefs-obj.md` luku 2
   lupaa: "Jos sen `K > N` … lukija aloittaa alusta." Koodissa
   `objread` (`obj.c:786`) nostaa tässä tilanteessa virheen
   `object version not kept`, eikä `space_view` (`p9space.c:902`)
   yritä uudelleen: näkymän ottaminen epäonnistuu sen sijaan että
   aloittaisi alusta.
3. **Spacejen välinen tagimuoto puuttuu.** `docs/object-model.md` 5.8
   sanoo: "Spacejen välinen muoto (`space/#0102939`) tulee V6:ssa."
   Merkkijonoa ei ole `sys/lib/python/graph.py`:ssä eikä
   `p9space.c`:ssä — se esiintyy vain dokumentissa. Lupaus on siis
   joko siirretty tai unohdettu, eikä V6:n oma luku mainitse sitä.
4. **Näkymä luetaan kokonaan muistiin.** `storeread` (`p9space.c:548`)
   lukee koko storen `vstr`:ään ennen palautusta. Dokumentoitu kustannus
   "koko version restore" (~8,3 s) on siis lisäksi koko storen kokoinen
   muistivaraus.

## 4. Mitä V7 vaatii ja mitä ei vielä ole

`docs/object-model.md`:n V7 on "koodin päivitys ja migraatio".
Yksikään sen vaatimista mekanismeista ei ole koodissa (`git grep`
`space-v6`:stä: ei `upgrade`ia, ei `__schema__`ia, ei
`__migrate__`ia).

Varsinaiset esteet, eivät vain puuttuva työ:

1. **Funktion identiteettiä ei tallenneta nimellä.** V7 sitoo
   funktiot ja luokat uuteen määritelmään "nimellä (moduuli +
   `__qualname__`)". Storessa funktio on kuitenkin `putfun`
   (`p9image.c:594`) mukaan `(moduulin nimi, syvyys, indeksipolku
   moduulin raw code -puussa, oletusargumentit, sulkeuman solut)`, ja
   luokka (`O_CLASS`, `p9image.c:807`) vain nimi, kantaluokat ja
   sanakirja. `__qualname__` ei esiinny kuvassa lainkaan. Nykyisellä
   tallennusmuodolla V7:n sidontasääntöä ei voi toteuttaa: pitää joko
   lisätä nimi (`__qualname__`) kuvamuotoon (`FORMAT`-nousu) tai
   kirjoittaa otsakkeeseen nimi→olio-kartta.
2. **Säilytysikkuna ei taivu palautuspisteeseen.** V7 vaatii: "Vanha
   versio säilyy päivityksen jälkeen palautuspisteenä … kunnes seuraava
   päivitys tehdään; säilytysväli `K…N` ei siivoa sitä ennen sitä."
   Koodissa säilytys on kiinteä: `Objkeep = 3` (`obj.c:33`) ja
   `h.k = h.n - Objkeep + 1` (`obj.c:467`), eli vanhin säilytetty on
   `N-2`. Kiinnitysmekanismia ei ole (`objpin` ei ole olemassa), joten
   V7:n takaamaa palautuspistettä **ei voi toteuttaa nykyisellä
   säilytyksellä** nostamatta `Objkeep`ia tai lisäämättä kiinnitystietuetta.
3. **Migraatio ei mahdu yhteen committiin.** `docs/gefs-obj.md`:n luvun 7 avoin kohta:
   `Objtxmax` on 64 MiB, joten iso V4:n `v<N>`-Space ei siirry storeen
   yhdellä commitilla. T5.11 todentaa siirron vain pienellä kuvalla.
   V7:n "päivitys on yksi commit" on siis saman rajan alla.
4. **Testit puuttuvat.** T7.1–T7.4 (tila säilyy, vanha funktioviite
   käyttää uutta koodia, `__schema__` ilman `__migrate__`ia hylätään,
   kaatuminen kesken päivityksen) eivät ole `tools/test-space.sh`:ssä.

## 5. Testit ajamatta ja haaran tila

- **V6:n commit `c029240`** kertoo itse: "Ajamatta: ONLY=3 ja ONLY=4
  (`p9image.c`:n muutos koskee vain vieraita merkintöjä); Silli
  (rauta)." Ajetut: `ONLY=6` sekä regressiot `ONLY=2` ja `ONLY=5`.
- **`spacehw` (`2863f82`)**: "Ajamatta: korjattu `hw.py` raudalla."
  Sitä edeltävä sillin ajo (`p5`, `9f12eda`) antoi kolme FAILia ja
  skripti kaatui T3.4:ään; loppuosa T3.4:stä eteenpäin ja `fshalt`
  jäivät ajamatta. Rauta-ajo on siis vielä kesken V5/V6:n osalta.
- **Haara on nyt mergattu.** `git merge-base --is-ancestor 2863f82
  main` on tosi ja `git branch --contains 2863f82` näyttää `main`in.
  Merge toi `main`iin V0–V6-jakson `docs/status.md`:hen sekä
  `docs/object-model.md`- ja `docs/gefs-obj.md`-tiedostot. Tämä
  sulkee vain sijaintikysymyksen: luvun 7 avoimet asiat ja luvun 5
  testauspuutteet ovat edelleen voimassa.

## 6. Ehdotettu järjestys ennen V7:tä

1. **Päätä 200 MiB:n vuodosta.** Joko toteuta luvun 6 vaihtoehto 1
   `check`in saavutettavuuslaskennalla (hyväksymisehto:
   `tools/gefslog`:n häntä nollaan kuvassa, jossa kaato tehtiin
   kirjoituksen aikana) tai kirjaa vuoto hyväksytyksi ja tee
   `checkleak`ista kaatava — nyt se on hiljainen.
2. **Sulje V6:n lupaukset.** T6.6:lle oma testi; joko lukijan
   uudelleenaloitus `K > N`:ssä tai dokumentin korjaus; päätä
   `space/#…`-muodon kohtalo (V6 vai myöhempi).
3. **Tee V7:n muototyö ennen V7:ää.** `__qualname__` tai nimi→olio-kartta
   kuvamuotoon, kiinnitys palautuspisteelle, ja yli 64 MiB:n migraatio
   (erissä tai vaiheistettuna).
4. **Aja puuttuvat testit** (`ONLY=3`, `ONLY=4`, `spacehw` sillillä).
   Ne jäivät ajamatta ennen mergeä, joten ne ovat nyt mergeä seuraavaa
   työtä.
