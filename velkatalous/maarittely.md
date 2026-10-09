# Velkatalous – tekninen määrittely ja simulaatiosuunnitelma

Versio 0.1, 9.10.2026. **Tutkimusmalli, ei valmis talous- tai oikeusjärjestelmä.**

## 1. Lähtöperiaatteet

- Jokainen ihminen syntyy velkasaldolla `D=0`. Saldo ei voi olla negatiivinen; nolla on rikkain mahdollinen tila. Yrityksiä voidaan simuloida erillisinä toimijoina, mutta niiden vastuut on lopulta määriteltävä.
- Velka on siirrettävä, aineeton valuutta. **Myyjä luovuttaa ostajalle velkaa** tuotteen/palvelun yhteydessä. Velkaa voi luovuttaa myös ilman hyödykettä.
- Velan siirto ei muuta kokonaisvelkaa. Tavallinen kaupankäynti ei luo velkaa.
- Velka häviää ainoastaan velallisen kuollessa. Yrityksen lopettamisen vaikutusta ei ole vielä määritelty.
- Yksilö saa vastaanottaa velkaa vain ennustetun elinaikaisen velankantokykynsä rajoissa. Mallissa raja `L_i(t)` on parametrisoitu, ei väitetä että todellinen elinaikainen kyky olisi luotettavasti laskettavissa.
- Kokeiltava bootstrap-sääntö: velanluovutussitoumus voi erääntyessään luoda puuttuvan velan, joka kirjataan **vastaanottajan** saldolle hänen suostumuksellaan ja luottorajan puitteissa. Tämä on **simulaatio-oletus**, joka täsmentää keskustelun ideaa, ei jo sovittu lopullinen sääntö.

## 2. Tilamalli

- `D_i ≥ 0`: toimijan velkasaldo.
- `L_i ≥ 0`: toimijan velkaraja; `D_i ≤ L_i`.
- `O_j = (seller, buyer, amount, due, status)`: myyjän ostajalle antama määräaikainen velanluovutussitoumus. Ei ole sama asia kuin toteutunut velkasaldo.
- `M`: erääntyessä uutena syntyneen velan kumulatiivinen määrä.
- `X`: kuolemissa poistuneen velan kumulatiivinen määrä.
- `D_total = Σ D_i = M − X`, kun alkusaldo on nolla.

### 2.1 Siirto

`transfer(A,B,x)` on sallittu, kun `0 ≤ x ≤ D_A` ja `D_B+x ≤ L_B`.

`D_A ← D_A−x`, `D_B ← D_B+x`.

### 2.2 Lupaus

`sell(A,B,x,due)` kirjaa sitoumuksen, jonka mukaan A toimittaa B:lle x velkayksikköä viimeistään `due`-päivänä. Tavara/palvelu voidaan luovuttaa jo nyt. Sitoumuksen kirjaus **ei muuta** saldoja. Ostajan hyväksyntä vaaditaan.

### 2.3 Erääntyminen ja bootstrap (kokeilu 1)

Erääntyessä siirretään ensin `t=min(x,D_A)` yksikköä A:lta B:lle. Jos puuttuu `r=x−t`, luodaan B:lle `r` uutta velkayksikköä. Lopputulos: `D_A←D_A−t`, `D_B←D_B+x`, `M←M+r`. Tämä toteutetaan atomisesti **vain**, jos `D_B+x ≤ L_B`; muuten sitoumus merkitään estyneeksi eikä osittaista muutosta tehdä. Tässä versiossa sitoumusta ei voi täyttää ennen eräpäivää, mutta jatkoversiossa se kannattaa sallia.

**Tärkeä poikkeama normaalista luotonannosta:** uusi velka syntyy ostajan saldoon, vaikka luomisen laukaisi myyjän toimitusvaje. Ostajan on siksi hyväksyttävä sekä määrä että eräpäivä etukäteen. Myyjälle on syytä asettaa avoimien sitoumusten katto, jotta hän ei voi luvata rajattomasti muiden velkaa.

### 2.4 Kuolema

`death(i)`: `X←X+D_i`, `D_i←0`, toimija poistuu. Avoimet sitoumukset on tässä ensimmäisessä simulaatiossa peruutettava ja kirjattava; todellisessa mallissa niiden kohtalo on ratkaistava erikseen. **Kuoleman rekisteröinti ja henkilön yksikäsitteisyys ovat ulkoisia luottamusongelmia.**

## 3. Nollasta käynnistymisen esimerkki

Alussa A:lla ja B:llä on velka 0, rajat 200.

1. Päivä 1: A myy B:lle puuta ja lupaa toimittaa 100 velkayksikköä päivänä 5. Saldot (0,0).
2. Päivä 5: A ei omista velkaa, joten B:n saldoon luodaan 100. Saldot (0,100), kokonaisvelka 100.
3. Päivä 6: B myy A:lle laatikoita ja siirtää 40 velkayksikköä. Saldot (40,60). Kokonaisvelka edelleen 100.
4. B kuolee: B:n jäljellä oleva 60 poistuu; kokonaisvelka 40.

Tämä ratkaisee alkuhetken likviditeettipuutteen, mutta ei automaattisesti takaa arvonmuodostusta, oikeudenmukaisuutta eikä kannustinten vakautta.

## 4. Taloudelliset ja institutionaaliset avoimet kysymykset

1. **Hinnan muodostus:** mikä määrittää tuotteen velkayksiköt, jos velka on ostajalle rasite ja myyjä luopuu siitä? Mikä estää ilmaisen tai keinotekoisen kaupankäynnin?
2. **Ensimmäisen velan syntyminen:** määräaikainen toimituslupaus ja puuttuvan osuuden minttaus mahdollistavat nollasta käynnistyksen, mutta velan luominen vaatii ostajan nimenomaisen hyväksynnän.
3. **Moraalikato:** voiko A luvata velkaa ilman omaa saldoa ja sysätä syntyvän velan B:lle? Tarvitaan sitoumusraja, mahdollinen vakuus/maine ja auditointi.
4. **Luottoraja:** velan vastaanotto on rajoitettava ennen sopimusta ja uudelleen erääntyessä; muuten syntyy estyneitä sitoumuksia.
5. **Kuolema ja perintö:** velka poistuu kuollessa; omaisuus, keskeneräiset kaupat, yritysvastuut ja tahallinen väärä kuolemailmoitus vaativat säännöt.
6. **Kannustin:** myyjä haluaa siirtää velkaa pois, ostaja joutuu ottamaan sitä vastaan; kaupan molemminpuolinen hyväksyntä, niukkuus ja arvostus on testattava.
7. **Identiteetti:** yksi ihminen = yksi pysyvä identiteetti; muuten luottorajat voi kiertää uusilla tunnuksilla.
8. **Auktoriteetti:** lohkoketju voi varmentaa siirrot ja sopimukset, mutta ei yksin syntymää, kuolemaa, tuotteen toimitusta tai maksukykyä. Mallissa mainittu yhteiskunnallinen valvonta on erillinen hallintakerros.
9. **Yksityisyys ja sensuuri:** julkinen velkasaldo, henkilötiedot ja siirtohistoria eivät välttämättä sovi julkiseen lohkoketjuun.
10. **Velan niukkuus:** kuolemien aiheuttama poistuma ja erääntymisissä tapahtuva luonti vaikuttavat kokonaismäärään. Velkayksikön ostovoiman vakautta ei ole osoitettu.

## 5. Simulaation eteneminen

### Vaihe A: deterministinen kirjanpitotesti

- 2–10 toimijaa, kiinteät luottorajat.
- Alkusaldot nolla.
- Luo sitoumuksia, eräännytä ne, siirrä velkaa, simuloi kuolema.
- Tarkista jokaisen tapahtuman jälkeen invariantit: `D_i≥0`, `D_i≤L_i`, `ΣD=M−X`.
- Testaa: ei velkaa siirrettäväksi, täysi/osittainen toimitus, ostajan raja, kuolema, nollasta käynnistys.

### Vaihe B: agenttipohjainen markkinasimulaatio

- 100, 1 000 ja 10 000 agenttia, eri ikä, elinaika, tuotantokyky ja kulutustarve.
- 3 tuoteryhmää: perustarpeet, tuotantopanokset, ylellisyys; tuotantoketjut (puu → laatikko → kauppa).
- Jokaisella kierroksella agentit tarjoavat tuotantoa, valitsevat ostoksia, hyväksyvät tai hylkäävät velkasitoumuksia, erääntymiset käsitellään.
- Vertaile käyttäytymissääntöjä: minimivelka, maksimaalinen kulutus luottorajan puitteissa, tasapainoinen tuotanto/kulutus.
- Mittarit: kokonaisvelka per elossa oleva, uudet velat/kierros, kuolemissa poistunut velka, estyneet kaupat, velkarajalla olevien osuus, tuotannon ja kulutuksen määrät, toimettomuus, velan keskittyminen, toimitussitoumusten viiveet.

### Vaihe C: rasitustestit

- Kaikki aloittavat nollasta; ei vapaaehtoisia ostajia; yksi suuri myyjä; luksustuotteiden kysyntäpiikki; äkillinen kuolleisuus; ikääntyvä väestö; identiteettien monistus; koordinoitu keinotekoinen kaupankäynti; ketjun tilapäinen jakautuminen.
- Tarkista erityisesti, syntyykö velkaa liian nopeasti, jäätyykö vaihdanta tai mahdollistaako sääntö velan mielivaltaisen siirtämisen.

## 6. Lohkoketjun minimirajapinta

Tapahtumat: `IdentityCreated`, `LimitUpdated`, `PromiseCreated`, `PromiseSettled`, `PromiseBlocked`, `DebtTransferred`, `DeathRegistered`. Jokaisessa allekirjoitukset, monotoninen järjestysnumero, aikaleima/lohkokorkeus ja tapahtuman tunniste. Erääntymissääntö on deterministinen; henkilöllisyys, kuolema ja velkaraja vaativat erikseen määritellyn luottamusmekanismin.

**Suositus:** aloita keskitetyllä deterministisellä simulaattorilla, älä lohkoketjulla. Kun taloussäännöt kestävät testit, korvaa tapahtumaloki konsensusprotokollalla. Lohkoketju ei korjaa talousmallin virheellisiä kannustimia.

## 7. Vielä vahvistettavat päätökset

- Onko erääntyessä luotava velka varmasti **ostajan** eikä myyjän vastuulla? Tässä prototyypissä kyllä.
- Saako sopimuksen tehdä, jos ostajan tämänhetkinen saldo ja avoimet lupaukset yhdessä ylittävät hänen luottorajansa? Tässä prototyypissä sopimukset hyväksytään, mutta erääntyminen voidaan estää; jatkossa suositellaan varausta.
- Miten hinnoittelu ja vapaaehtoinen hyväksyntä toteutuvat? Tässä prototyypissä ne ovat ulkoisia päätöksiä.
