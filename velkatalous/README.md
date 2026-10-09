# Velkatalous v0.1: simulaattori

Toteutus määrittelylle [`maarittely.md`](maarittely.md) (v0.1, 9.10.2026):
vaihe A (kirjanpito), vaihe B (agenttipohjainen markkina) ja osa vaiheen C
rasitustesteistä. Python 3, ei riippuvuuksia. Tämä on tutkimusmalli, ei
talousjärjestelmä.

```
cd velkatalous
python3 -m unittest discover -s tests   # 15 testiä, ~1 s
python3 -m velka esimerkki              # luvun 3 nollasta käynnistys
python3 -m velka markkina [SKENAARIO...] [--n N] [--years Y] [--seed S] [--csv vuodet.csv]
python3 -m velka rasitus                # kaikki skenaariot, 1 000 agenttia x 400 v, ~70 s
```

## Vaihe A: kirjanpito (`velka/ledger.py`)

Luvun 2 säännöt kokonaislukuina: `create`, `set_limit`, `transfer`,
`sell` (sitoumus), `settle`/`settle_due` (erääntyminen ja bootstrap),
`death`. Luvun 6 tapahtumat kirjataan lokiin järjestysnumeroin, ja
`Ledger.replay(events)` rakentaa lokista saman tilan ja tarkistaa, että
erääntymiset antavat saman tuloksen (deterministinen sääntö).

Invariantit `D_i ≥ 0`, `D_i ≤ L_i`, kuolleen saldo 0 ja `ΣD = M − X`
tarkistetaan jokaisen tapahtuman jälkeen (`strict=True`).

Määrittelyn jättämät valinnat:
- Erääntyminen on atominen: jos `D_B + x > L_B`, sitoumus on `blocked`
  eikä mitään siirry (2.3).
- Raja ei voi laskea saldon alle (muuten `D ≤ L` rikkoutuisi).
- `seller_cap`: myyjän avoimien sitoumusten katto (2.3), oletuksena pois.
- `reserve=True`: `sell` vaatii, että ostajan saldo + avoimet ostot + uusi
  mahtuvat rajaan (luvun 7 "varaus"); oletuksena pois kirjanpidossa,
  päällä markkinassa.
- Kuolema perii avoimet sitoumukset molempiin suuntiin (`cancelled`).
- Itsensä kanssa ei voi käydä kauppaa eikä siirtää.

Testit (`tests/test_ledger.py`): nollasta käynnistys, ei velkaa
siirrettäväksi, täysi ja osittainen toimitus, ostajan raja, siirron raja,
kuolema, rajan lasku, sitoumuksen ehdot (katto, varaus, eräpäivä,
hyväksyntä) sekä 20 satunnaista 60 päivän ajoa, joissa invariantit
pitävät ja replay antaa saman tilan. Kun luonnin kirjaus (`M`) rikotaan,
12/15 testiä kaatuu.

## Vaihe B: markkina (`velka/market.py`)

Kierros on vuosi. Jokainen kauppa on kirjanpidon sitoumus, joka erääntyy
seuraavana vuonna; muuta velan liikettä ei ole. Oletukset (kaikki
`Config`issa):

| | |
|---|---|
| Väestö | 1 000; malliin 20-vuotiaana saldolla 0; eläke 65; elinikä N(78, 10). Kuolleiden tilalle syntyy sama määrä. |
| Tuotteet | perus (jokainen tarvitsee 1/v, hinta 10), panos (puu, 8), ylellisyys (15). Ketju: 2 perusyksikköä vaatii 1 panoksen. |
| Erikoistuminen | 35 % perus (kapasiteetti 4/v, ruokkii itsensä), 15 % panos (5/v), 50 % ylellisyys (2/v). |
| Työ | Työikäinen tuottaa kapasiteettinsa, jos hänellä on velkaa, jota ei ole jo luvattu pois, muuten todennäköisyydellä `work_ethic` = 0,1. |
| Kulutus | perus aina, jos raja sallii; ylellisyys strategian mukaan: `minimi` ei, `maksimi` 2/v rajan puitteissa, `tasapaino` (oletus) kun velka + avoimet ostot ≤ 0,5 L. |
| Raja | `L = 200 + 0,5 · hinta · kapasiteetti · jäljellä olevat työvuodet`, ei saldon alle. Eläkeläisellä 200. |
| Nälkä | Ilman perustuotetta jäänyt kuolee seuraavana vuonna todennäköisyydellä 0,5. |

Myyjän ainoa taloudellinen hyöty myynnistä on oman velan väheneminen.
Nollasaldoinen ei hyödy: hänen myyntinsä luovat velan ostajalle. Tämä on
mallin keskeinen piirre, ei toteutuksen valinta: se seuraa säännöistä
`D ≥ 0` ja "nolla on rikkain tila".

Mittarit (luvun 5 lista): velka/hlö, luotu ja kuolemissa poistunut velka,
kaupan arvo, uuden velan osuus kaupasta, nälkä (tarjonnan puute vs. raja),
nälkäkuolemat, toimettomat työikäiset, rajalla olevat (eivät pysty
ostamaan perustuotetta), nollasaldoiset, gini, ylimmän 10 %:n osuus
velasta, estyneet sitoumukset.

## Tulokset (siemen 1, keskiarvot vuosilta 201–400)

```
                         perus  ei-etiikkaa  etiikka-0.5       minimi      maksimi         seka  ei-varausta    ikaantyva  kuolleisuus luksuspiikki  kuolinvuode   myyjakatto
nälkä v. 1-5               31%          51%           6%          39%          31%          28%          31%          28%          31%          31%          31%          48%
väestö                    1000         1000         1000         1000         1000         1000         1000          225         1000         1000         1000         1000
velka/hlö                   75            0           60           12           51           57           56           72           74           71           69            3
vaihtelu (CV)             0.26         0.00         0.08         0.21         0.22         0.18         0.15         0.23         0.29         0.29         0.28         0.10
luotu/hlö/v                2.8          0.0          2.9          1.7          3.0          2.7          2.7          2.7          2.8          3.0          2.9          0.1
poistui/hlö/v              2.8          0.0          2.9          1.7          3.1          2.7          2.7          3.1          2.8          2.9          2.9          0.1
kauppa/hlö/v              19.2          0.0         22.0          3.2         20.2         18.5         20.4         18.3         19.4         19.3         19.3          0.2
luodun osuus               15%           0%          13%          55%          15%          14%          13%          15%          15%          16%          15%          65%
nälkä                     3.4%        10.8%         1.6%         8.5%         3.0%         3.1%         2.0%         4.0%         3.6%         3.4%         3.6%        10.9%
  ei tarjontaa            1.9%        10.8%         0.0%         8.5%         0.8%         1.7%         0.6%         2.5%         2.3%         1.8%         2.0%        10.9%
  raja                    1.5%         0.0%         1.6%         0.0%         2.2%         1.3%         1.4%         1.5%         1.4%         1.6%         1.5%         0.0%
nälkäkuolemat/v          1.62%        5.40%        0.74%        4.18%        1.43%        1.48%        0.93%        1.93%        1.74%        1.66%        1.61%        5.41%
toimettomat                15%         100%           8%          73%          19%          26%          16%          16%          15%          16%          16%          82%
saldo 0                    14%         100%          13%          77%          18%          26%          15%          14%          14%          15%          15%          91%
gini                      0.55         0.00         0.52         0.88         0.59         0.62         0.54         0.54         0.55         0.57         0.56         0.92
kuolinvuode/hlö/v          0.0          0.0          0.0          0.0          0.0          0.0          0.0          0.0          0.0          0.0          0.4          0.0
```

(Osa riveistä pois; `python3 -m velka rasitus` tulostaa kaikki ja
shokkien vertailun.) 10 000 agentilla `perus` antaa samat tasot (velka/hlö
72, nälkä 2,3 %, gini 0,55; 200 v, 45 s). Siemenillä 2 ja 3 velka/hlö on
61–62 ja trendin etumerkki vaihtelee: kokonaisvelka ei karkaa eikä
sammu, vaan luonti ja kuolemissa poistuminen ovat tasapainossa (≈ 2,8
yksikköä/hlö/v).

## Havainnot

Hypoteesi oli: syntyykö itsestään tasapainoinen talous, jossa tuottaminen
edellyttää kuluttamista, vai syntyykö velan kiertoon rakenteellisia
pullonkauloja. Tässä mallissa vastaus on molemmat:

1. **Velan määrä tasapainottuu.** Velka/hlö pysyy noin 60–75:ssä, ja
   kaupasta 13–16 % on uutta velkaa. Luonti ja kuolemissa poistuminen
   kumoavat toisensa ilman ohjausta.

2. **Pelkkä velan hyöty ei käynnistä taloutta.** Ilman muuta työn
   motiivia (`ei-etiikkaa`) kukaan ei myy koskaan: nollasaldoinen ei
   hyödy myymisestä, ja velkaa syntyy vain myynnistä. Tila "kaikilla 0"
   on loukku, josta ei pääse pois. Bootstrap-sääntö ratkaisee
   likviditeetin mutta ei kannustinta. "Tuottaminen edellyttää
   kuluttamista" pitää kirjaimellisesti: ihminen tekee työtä vasta, kun
   hän on ensin ottanut velkaa. Käynnistyksessä 31 % näkee nälkää
   (`work_ethic` 0,1); 0,5:llä 6 %.

3. **Sukupolvisyklit.** Oletusmallissa velka/hlö vaihtelee noin 50 vuoden
   jaksoissa (CV 0,26), ja jaksoihin kuuluu nälänhätiä. Kuolemat poistavat
   velan, uudet tulevat malliin nollasaldolla ja ovat toimettomia, ja
   tarjonta romahtaa. Vahvempi työn motiivi (`etiikka-0.5`) vaimentaa
   syklin (CV 0,08).

4. **Vanhuus.** Eläkeläisen raja on 200 eikä hän voi enää tehdä työtä.
   Kun raja täyttyy, hän ei voi ostaa ruokaa: noin 1,5 % väestöstä on
   nälässä rajan takia, ja tarkistetussa otoksessa 33/34 heistä oli
   eläkeläisiä. Jos raja nostetaan, kasvaa vaihe C:n kuolinvuodeongelma.
   `kuolinvuode`-skenaariossa kuolevat ottavat vastaan muiden velkaa niin
   paljon kuin raja sallii (0,4 yksikköä/hlö/v, velka/hlö 62 → 58). Nämä
   kaksi vetävät eri suuntiin samasta parametrista.

5. **Kulutus kantaa tuotantoa.** `minimi`-strategialla (vain perustarpeet)
   73 % työikäisistä on toimettomia, kauppa putoaa kuudesosaan ja
   perustuotteesta on pulaa (8,5 % nälässä). Velka keskittyy (gini 0,88).
   `maksimi` ei kasvata velkaa, koska ylellisyyden tarjonta on rajallinen.

6. **Myyjän katto voi pysäyttää talouden.** Kun katto (20) on pienempi kuin
   vuoden tuotannon arvo, myyjät eivät pääse velastaan ja talous
   hiipuu lähes jäätyneeseen tilaan (91 % nollasaldoisia). Katon on
   oltava kapasiteetin mittainen.

7. **Shokit** (vuodet 300–319 verrattuna samaan siemeneen ilman shokkia):
   20 %:n kertakuolleisuus poistaa velkaa (62 → 57), mutta talous palautuu.
   Kaikkien siirtyminen maksimikulutukseen lisää vähän nälkää (1,4 → 1,8 %).
   Kumpikaan ei jätä pysyvää jälkeä.

8. **Varaus.** Ilman varausta (`ei-varausta`) estyneitä sitoumuksia on
   harvoin, koska ostaja tarkistaa rajansa ennen kauppaa ja sitoumukset
   erääntyvät vuodessa. Pidemmillä eräajoilla ero kasvaisi.

## Rajoitukset ja seuraavat askeleet

- Hinnat ovat kiinteät; hinnanmuodostus (luku 4.1) on tärkein puute.
  Seuraavaksi: myyjä nostaa hintaa, kun varasto myydään loppuun, ja laskee
  sitä, kun jää myymättä.
- Työn motiivi `work_ethic` on mallin ulkopuolinen oletus, ja tulokset
  riippuvat siitä eniten. Kohta 2 kannattaa viedä määrittelyyn: mikä saa
  nollasaldoisen tuottamaan? Kokeiltavia vaihtoehtoja: ennakkotoimitus
  (sitoumuksen täyttö ennen eräpäivää), rajan kasvu tuotannosta tai
  luodun velan kirjaus myyjälle (luku 7). Viimeinen pahentaisi
  kannustinta, koska nollasaldoinen myyjä saisi silloin velkaa.
- Yksi siemen ja yksi parametrijoukko; herkkyysanalyysi puuttuu.
- Vaiheesta C puuttuvat yksi suuri myyjä, identiteettien monistus,
  koordinoitu keinokauppa ja ketjun jakautuminen.
- Lapsuus puuttuu (malliin tullaan 20-vuotiaana), samoin perintö ja
  yritykset.
