# WireGuard: #W

Plan2001:n ytimessä on WireGuard omana laitteenaan `#W`
(`sys/src/9/ip/devwg.c`). Se ei ole Linuxin ajurin portti, vaan
WireGuard-protokollan (Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s) oma
toteutus 9frontin IP-pinon päälle. IP-pino näkee sen mediumina `wg`,
samaan tapaan kuin `ether`-mediumin. Krypto tulee libsecistä: `x25519`,
`blake2s`, `hmac_blake2s_256`, `mac_blake2s_128` ja `ccpoly`.

```
      /net/ip (ipifc, reitit)
           |  sisempi IPv4/IPv6-paketti
           v
          #W  -- vertaistaulu, AllowedIPs, Noise IK, avaimet, replay-ikkuna, ajastimet
           |  WireGuard-viesti
           v
      /net/udp (headers-tila)  ->  IP  ->  #l
```

## Käyttö

```
echo 'private BASE64'          >'#W/wg0/ctl'   # oma yksityinen avain (wg genkey)
echo 'listen 51820'            >'#W/wg0/ctl'   # UDP-portti kirjoittajan /net/udp:ssa
echo 'peer BASE64'             >'#W/wg0/ctl'   # lisää/valitse vertainen julkisella avaimella
echo 'psk BASE64'              >'#W/wg0/ctl'   # (valinnainen) valitun vertaisen PSK
echo 'allowed 10.99.0.1/32'    >'#W/wg0/ctl'   # vertaisen sallitut osoitteet (v4 tai v6)
echo 'endpoint 1.2.3.4!51820'  >'#W/wg0/ctl'   # missä vertainen on (vaeltaa: viimeisin todennettu paketti)
echo 'keepalive 25'            >'#W/wg0/ctl'   # pysyvä keepalive, s
echo 'remove BASE64'           >'#W/wg0/ctl'
cat '#W/wg0/status'

{ n=`{read}; echo bind wg wg0 >/net/ipifc/$n/ctl
  echo add 10.99.0.2 255.255.255.0 >/net/ipifc/$n/ctl } <>/net/ipifc/clone
```

Rajapintoja on neljä: `wg0`–`wg3`. Kun samalla rivillä on useita
komentoja, ne erotetaan rivinvaihdoin. `status` näyttää rajapinnan
julkisen avaimen ja jokaisen vertaisen tiedot: tunniste (`peer N`),
päätepiste, sallitut osoitteet, viimeisin kättely, avainten tila,
siirretyt tavut ja kättelyyritykset.

**Vertaisen tunniste** on pysyvä pieni kokonaisluku: yksi avain, yksi
tunniste. Se on ensimmäisen luokan tieto, jota myöhempi kerros (9P,
käyttäjän tunnistus, nimiavaruus) voi käyttää. Silloin WireGuard-vertainen
yhdistettynä dp9ik-käyttäjään antaa istunnon ilman, että 9P salataan
uudelleen.

## Mitä on toteutettu

- Kättely kumpaankin suuntaan:
  - aloittajana init → response ja avainten vahvistus keepalivella;
  - vastaajana avaimet otetaan käyttöön vasta aloittajan ensimmäisestä
    viestistä.
- mac1 lähtevissä ja saapuvissa viesteissä; TAI64N-aikaleima ja
  uusintasuoja kättelylle.
- Siirtoviestit:
  - ChaCha20-Poly1305 ja laskurinonce;
  - täyte 16 tavuun (MTU 1420);
  - replay-ikkuna 2048 viestiä.
- AllowedIPs:
  - lähtevä paketti menee vertaiselle, jonka pisin etuliite osuu
    kohdeosoitteeseen;
  - saapuva paketti hyväksytään vain, jos sen lähdeosoite kuuluu samalle
    vertaiselle.
- Päätepisteen vaellus: todennettu paketti päivittää päätepisteen.
- Ajastimet:
  - kättely uudelleen 5 s:n välein, luovutus 90 s:n jälkeen;
  - uudelleenavainnus 120 s:n jälkeen lähetettäessä ja ennen 180 s:n
    rajaa vastaanotettaessa;
  - vanhat avaimet pois;
  - passiivinen keepalive 10 s ja pysyvä keepalive.
- Paketit odottavat kättelyä jonossa, enintään 32 vertaista kohden.
- Silmukkasuoja: jos oma UDP-portti yritetään reitittää tunneliin
  itseensä, paketti pudotetaan ja se lasketaan (`loops`).

## Mitä puuttuu

- **Cookie reply** (tyyppi 3): WireGuard lähettää niitä vain kuormassa.
  Vastaanotettu cookie ohitetaan, ja kättely yritetään uudelleen.
- **Uudelleenavainnus viestimäärän mukaan** (2^60 viestiä): hylkäysraja
  on kuitenkin mukana.
- Vertaisten dynaaminen hallinta ja `wg`-yhteensopiva komento.
- Nopeutus: krypto ajetaan rajapinnan lukon alla, yksi viesti
  kerrallaan.
- Vertaisen tunnisteen käyttö ylemmissä kerroksissa, eli
  connection → Wpeer → 9P-käyttäjä.

## Testi Linuxin WireGuardia vastaan (30.9.)

`tools/wgtest.sh` ajaa testin seuraavasti:

1. Paikallisen CPU-palvelimen (cirno) levystä otetaan hetkikopio sen
   QEMU:n `drive_backup`-komennolla, joten cirno saa jatkaa käyntiään.
2. Kopio käynnistetään Plan2001-loaderilla ja testattavalla ytimellä
   CPU-palvelimena.
3. Konetta ohjataan 9pterm-istunnolla.
4. Linuxiin tehdään sudolla rajapinta `wgp9` (10.99.0.1, UDP 51999).
   Plan2001:ssä on `#W/wg0` (10.99.0.2), jonka päätepiste on
   10.0.2.2!51999 ja keepalive 5 s. Kättely lähtee QEMU:n NATin sisältä.

| Testi | Tulos |
|---|---|
| Kättely Linuxin kanssa | onnistui ensimmäisellä yrityksellä, `latest handshake` molemmilla puolilla |
| ping Plan2001 → Linux ja Linux → Plan2001 | 0 % hukka, ~1 ms |
| TCP Linux → Plan2001, 16 Mt | md5 sama; 2,3–13 Mt/s |
| TCP Plan2001 → Linux, 16 Mt | md5 sama; 14,7–15,8 Mt/s |
| Tunneli auki 280 s, ping 1/s | 0/250 hukattu; 3 kättelyä (alku ja kaksi uudelleenavainnusta) |

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

### Avoin havainto: terminaalitila sarjakonsolilla

Kun `service=terminal` ja käytössä on pelkkä sarjakonsoli (ei näyttöä),
kbuild9p:llä käännetyn ytimen rc saa konsolilta heti EOF:n, ja init
käynnistää sen yhä uudelleen. Tämä näkyi, kun base.qcow2 käynnistettiin
testiytimellä.

- 26.9. `build.sh`:lla käännetty ydin toimi samassa tilanteessa.
- Saman 26.9. lähteen kbuild9p-käännös ei toiminut, joten syy on
  käännösympäristössä, ei lähteessä.
- bootfs.paq:n kbdfs on oikea ja suoritettava, ja
  boot-lähteet ovat samat.
- CPU-palvelintila toimii.

Selvittämättä. Siksi wgtest käyttää CPU-palvelintilaa.
