# WireGuard: #W

Plan2001:n ytimessä on WireGuard omana laitteenaan `#W`
(`plan2001/sys/src/9/ip/devwg.c`). Se ei ole Linuxin ajurin portti, vaan
WireGuard-protokollan (Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s) oma
toteutus 9frontin IP-pinon päälle. IP-pino näkee sen mediumina `wg`,
samaan tapaan kuin `ether`-mediumin. Krypto tulee libsecistä: `x25519`,
`blake2s`, `hmac_blake2s_256`, `mac_blake2s_128`, `ccpoly` ja `hchacha`
(XChaCha20-Poly1305 cookieille).

```
      /net/ip (ipifc, reitit)
           |  sisempi IPv4/IPv6-paketti
           v
          #W  -- vertaistaulu, AllowedIPs, Noise IK, avaimet, replay-ikkuna, cookiet, ajastimet
           |  WireGuard-viesti
           v
      UDP+IP-otsakkeet tehdään #W:ssä  ->  ipoput4/6 reitillä, joka ei mene wg-rajapinnasta  ->  #l
      (vastaanotto: /net/udp:n headers-tilan keskustelu)
```

## Käyttö

Jokainen ctl-rivi on kokonainen: rivien välillä ei säilytetä tilaa, joten
samaan rajapintaan voi kirjoittaa kaksi prosessia yhtä aikaa. Rivi
tarkistetaan kokonaan ennen kuin mitään tehdään, joten virheellinen rivi
ei muuta mitään.

```
echo 'private BASE64'   >'#W/wg0/ctl'   # oma yksityinen avain (wg genkey); uusi avain päättää vanhan istunnot
echo 'listen 51820'     >'#W/wg0/ctl'   # UDP-portti (1-65535) kirjoittajan /net/udp:ssa
echo 'peer BASE64 allowed 10.99.0.1/32 allowed fd99::1/128 endpoint 1.2.3.4!51820 keepalive 25' >'#W/wg0/ctl'
echo 'peer BASE64 psk BASE64' >'#W/wg0/ctl'   # uusi PSK päättää vertaisen istunnot
echo 'remove BASE64'    >'#W/wg0/ctl'
echo 'cookie always'    >'#W/wg0/ctl'   # cookie-vastaus jokaiseen kättelyyn (testi); oletus auto: vain kuormassa
cat '#W/wg0/status'

{ n=`{read}; echo bind wg wg0 >/net/ipifc/$n/ctl
  echo add 10.99.0.2 255.255.255.0 >/net/ipifc/$n/ctl
  echo add fd99::2 /64 >/net/ipifc/$n/ctl } <>/net/ipifc/clone
```

`peer`-rivillä voi olla nämä valinnat missä järjestyksessä tahansa:
- `psk KEY`
- `allowed ADDR/LEN`, niin monta kuin tarvitaan: IPv4:llä pituus 0–32,
  IPv6:lla 0–128
- `endpoint ADDR!PORT`, portti 1–65535
- `keepalive SECS`, 0–65535

Ensimmäinen `peer`-rivi uudelle avaimelle lisää vertaisen, ja myöhemmät
rivit muuttavat sitä.

**Etuliitteellä on yksi omistaja** (cryptokey routing). Jos toisella
vertaisella on jo täsmälleen sama etuliite, rivi on virhe. Saman
etuliitteen lisääminen uudelleen samalle vertaiselle ei tee mitään.

Rajapintoja on neljä: `wg0`–`wg3`. `status` näyttää ensin rajapinnan
rivin: julkinen avain, portti, ipifc ja laskurit (`loops`, `drops`,
`noroute`, `bad`, `replays`, `cookies in/out`). Sen jälkeen tulee rivi
kustakin vertaisesta: tunniste (`peer N`), päätepiste, sallitut
osoitteet, viimeisin kättely, avainten tila, siirretyt tavut ja
kättelyyritykset.

**Vertaisen tunniste** on pysyvä pieni kokonaisluku: yksi avain, yksi
tunniste. Myöhempi kerros (9P, käyttäjän tunnistus, nimiavaruus) voi
käyttää sitä. Silloin WireGuard-vertainen yhdistettynä dp9ik-käyttäjään
antaa istunnon ilman, että 9P salataan uudelleen.

## Ulommat paketit ja koko liikenne tunnelin kautta

Ulommat paketit (UDP-paketit vertaisille) eivät kulje UDP-keskustelun
kautta, koska silloin ne reititettäisiin kuten mikä tahansa muu paketti.
Siksi `#W` tekee UDP- ja IP-otsakkeet itse ja antaa paketin
`ipoput4`/`ipoput6`:lle reittivihjeen kanssa.

Reitin hakee `v4lookupskip`/`v6lookupskip`. Se on Plan2001:n lisäys
9frontin `ip/iproute.c`:hen (overlay `plan2001/sys/src/9/ip/iproute.c`): pisimmän
etuliitteen haku, joka ohittaa reitit, jotka menevät ulos wg-mediumin
rajapinnasta. Lähdeosoite on sen rajapinnan osoite, jonka reitti antaa.

Näin tunneli voi kantaa oletusreitin, ja sen omat paketit lähtevät silti
fyysisen rajapinnan kautta:

```
echo 'peer KEY allowed 0.0.0.0/0' >'#W/wg0/ctl'      # vertainen saa kantaa kaiken
echo add 0.0.0.0   128.0.0.0 10.99.0.1 >/net/iproute  # 0/1 ja 128/1 wg0:n kautta;
echo add 128.0.0.0 128.0.0.0 10.99.0.1 >/net/iproute  # vanha oletusreitti jää ulommille
```

Jos ulompi paketti silti päätyy wg-rajapintaan (esimerkiksi reitit
muuttuivat haun ja lähetyksen välissä), se tunnistetaan alkuperästä eikä
porteista. Lähettävä prosessi on merkitty ulomman paketin lähettäjäksi,
ja wg:n `bwrite` pudottaa paketin ja kasvattaa `loops`-laskuria.
Sisempi UDP-paketti, jonka lähdeportti on sama kuin WireGuardin, kulkee
normaalisti. Tunneli toisen wg-tunnelin sisällä ei ole tuettu.

## Mitä on toteutettu

- Kättely kumpaankin suuntaan:
  - aloittajana init → response ja avainten vahvistus keepalivella;
  - vastaajana avaimet otetaan käyttöön vasta aloittajan ensimmäisestä
    viestistä;
  - mac1 lähtevissä ja saapuvissa viesteissä;
  - TAI64N-aikaleima ja uusintasuoja kättelylle.
- Cookie-mekanismi (DoS-suoja):
  - Kuormassa (yli 20 kättelyviestiä sekunnissa, tai `cookie always`)
    kättelyviesti käsitellään vain, jos sen mac2 on oikea. Muuten
    lähetetään cookie-vastaus: cookie = MAC(salaisuus, osoite‖portti),
    salaisuus vaihtuu 2 min välein, ja cookie salataan XChaCha20-Poly1305:llä
    viestin mac1:een sidottuna.
  - Vertaisen cookie-vastaus otetaan vastaan, ja seuraavat kättelyviestit
    saavat mac2:n.
- Siirtoviestit:
  - ChaCha20-Poly1305 ja laskurinonce;
  - täyte 16 tavuun (MTU 1420);
  - replay-ikkuna 2048 viestiä, uusinnat lasketaan `replays`-laskuriin.
- IPv4 ja IPv6 sekä sisällä että ulkona. Purettu sisempi paketti menee
  version mukaan `ipiput4`:lle tai `ipiput6`:lle.
- AllowedIPs:
  - lähtevä paketti menee vertaiselle, jonka pisin etuliite osuu
    kohdeosoitteeseen;
  - saapuva paketti hyväksytään vain, jos sen lähdeosoite kuuluu samalle
    vertaiselle (muuten `bad`);
  - jokaisella etuliitteellä on yksi omistaja.
- Avainten vaihto:
  - uusi `private` poistaa jokaisen vertaisen istunnot (cur, prev, next),
    keskeneräisen kättelyn ja cookien;
  - uusi `psk` tekee saman sille vertaiselle;
  - uusi kättely alkaa seuraavasta paketista tai keepalivesta.
- Päätepisteen vaellus: todennettu paketti päivittää päätepisteen.
- Ajastimet:
  - kättely uudelleen 5 s:n välein, luovutus 90 s:n jälkeen;
  - uudelleenavainnus 120 s:n jälkeen lähetettäessä ja ennen 180 s:n
    rajaa vastaanotettaessa;
  - vanhat avaimet pois;
  - passiivinen keepalive 10 s ja pysyvä keepalive.
- Paketit odottavat kättelyä jonossa, enintään 32 vertaista kohden.

## Mitä puuttuu

- **Uudelleenavainnus viestimäärän mukaan** (2^60 viestiä): hylkäysraja
  on kuitenkin mukana.
- **Sallitun etuliitteen poisto**: tällä hetkellä vain vertainen
  poistetaan kokonaan (`remove`) ja lisätään uudelleen.
- **Nopeutus**: krypto ajetaan rajapinnan lukon alla, yksi viesti
  kerrallaan.
- **`wg`-yhteensopiva komento.**
- **Vertaisen tunnisteen käyttö ylemmissä kerroksissa**, eli
  connection → Wpeer → 9P-käyttäjä.

## Regressiotesti: tools/wgtest.sh

Jokaisen tarkistuksen on mentävä läpi. Testin paluuarvo on
epäonnistuneiden tarkistusten määrä, ja 100 tarkoittaa, ettei
testiympäristö noussut.

Testi rakentaa ympäristön näin:
1. Paikallisen CPU-palvelimen (cirno) levystä otetaan hetkikopio sen
   QEMU:n `drive_backup`-komennolla, joten cirno saa jatkaa käyntiään.
2. Kopio käynnistetään Plan2001-loaderilla ja testattavalla ytimellä
   CPU-palvelimena.
3. Konetta ohjataan 9pterm-istunnolla.
4. Linuxiin tehdään sudolla rajapinta `wgp9` (10.99.0.1 ja fd99::1).
   Plan2001:ssä on `wg0` (10.99.0.2 ja fd99::2).

`LONG=0` jättää pois rekey-tarkistuksen, joka kestää 130 s.

| Tarkistus | Vaatimus |
|---|---|
| kättely Linuxin kanssa | onnistuu |
| IPv4 ping molempiin suuntiin | onnistuu |
| IPv6 ping molempiin suuntiin | onnistuu |
| TCP 16 Mt molempiin suuntiin | md5 täsmää |
| sisempi UDP-paketti, jonka lähdeportti on 51820 | menee perille |
| ping lähdeosoitteesta, jota vertainen ei saa käyttää | pudotetaan, `bad` kasvaa |
| kaapatun siirtoviestin uusinta (3 kertaa) | pudotetaan, `replays` +3, tunneli pysyy ylhäällä |
| toisen vertaisen etuliite, /-1, /33, /129, portti 70000, keepalive -1, listen 0 | ctl-virhe |
| virheellinen rivi | ei muuta mitään (vertaista ei synny) |
| `cookie always`: Linux aloittaa kättelyn | saa cookien ja pääsee läpi sen kanssa (Linux hyväksyy XChaCha-cookien) |
| wg1 vastaa wg0:lle cookiella | wg0 ottaa sen, ja kättely onnistuu mac2:n kanssa |
| full tunnel: 0/1 ja 128/1 wg0:n kautta | ping Linuxin LAN-osoitteeseen kulkee wg0:n kautta, `loops` 0, tunneli pysyy ylhäällä |
| yksityisen avaimen vaihto | istunnot pois, vanha istunto kuollut, palaa vanhalla avaimella |
| PSK:n vaihto | istunnot pois, vanha istunto kuollut, palaa kun PSK on molemmilla |
| rekey: 130 s pingiä | ei hukkaa, kättelyitä tulee lisää |

## Käännös 9pterm-istunnolla

Ydin käännetään CPU-palvelimella suoraan repon lähteistä `/mnt/term`in
kautta (`tools/kbuild9p.rc`). Menetelmä on sama overlay kuin
`tools/build.rc`:ssä, mutta tar-levyjä ja build-VM:n käynnistystä ei
tarvita:

```
9pterm -S ~/.cache/9pterm-local.sock -T 900 \
  -c 'rc /mnt/term/home/lapere/Plan2001/tools/kbuild9p.rc /mnt/term/home/lapere/Plan2001'
```

Koko pc64-ydin kääntyy cirnossa noin 7 sekunnissa, ja tulos kirjoitetaan
suoraan tiedostoon `build/amd64/9pc64`.

### Ratkaistu: terminaalin konsoli kbuild9p-ytimellä (30.9.)

Kun `service=terminal` ja käytössä oli pelkkä sarjakonsoli, kbuild9p:llä
käännetyn ytimen rc sai konsolilta heti EOF:n (`init: rc exit status:
false` silmukassa).

**Syy:** bootfs.paq ottaa tiedostojen ja hakemistojen tilat siitä, mistä
se tehdään. `amd64/bin/aux` on union, jonka ensimmäinen osa on kbuild9p:n
oma `$d/auxbin`. Se oli `/tmp`:ssä, ja cpu-istunnon `/tmp` antaa uusille
tiedostoille tilan 0750 (glenda). bootrc:n `test -x /bin/aux/kbdfs`
tehdään ennen kuin isäntäomistaja on asetettu, joten se epäonnistui:
kbdfs ei käynnistynyt, `/srv/cons` puuttui ja `/dev/cons` oli ytimen
oma `#c`, joka antaa pelkän EOF:n. build.rc tekee saman hakemiston
kotihakemistoon (0775), joten sen ydin toimi.

**Näin syy löytyi:**
1. Sama lähde käännettiin build.rc:llä erillisessä base-VM:ssä, ja
   tulos toimi, joten syy oli käännösympäristössä.
2. Ytimien koodi ja data olivat samat bootfs.paq:ta lukuun ottamatta.
3. Kaatuvan käynnistyksen profiilissa kirjoitettu diagnostiikkaloki
   (luettu toimivalla ytimellä samalta levyltä) näytti, ettei kbdfs ollut
   käynnissä.

**Korjaus:** `chmod 775 $d/auxbin $d/auxbin/kbdfs`. Lisäksi kbuild9p
kaatuu nyt, jos bootfs:n `bin`, `bin/aux` tai `kbdfs` ei ole kaikkien
suoritettavissa.
