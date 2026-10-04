# WebAuthn-kirjautuminen plan2001.com:iin (suunnitelma 4.10.2026)

Tavoite: plan2001.com:n pääsivulla käyttäjä joko luo tunnuksen
(käyttäjänimi, salasana ja passkey) tai kirjautuu aiemmin tehdyillä
tunnuksilla. Kirjautumisen jälkeen hänen wasm32-koneensa saa avaimensa ja
on pääte pilven CPU-palvelimelle.

Päätökset (4.10.):
- Yksi salasana sekä dp9ik:lle että secstorelle.
- Kirjautumisvalikko on koneessa (Plan 9 -ohjelma), ja sivulla on vain
  WebAuthn-painike.
- plan2001.com ja `cpu.plan2001.com` ovat sama kone, ja sovellukset ovat
  muodossa `APP.plan2001.com`.

## Periaate: WebAuthn avaa avaimet, Plan 9 tunnistaa

Plan 9:n tunnistus pysyy sellaisenaan: dp9ik auth-palvelinta vastaan,
factotum ja secstore (docs/architecture.md, "Avainten paikka"). WebAuthn
ei korvaa sitä, vaan se on tapa avata avaimet ilman salasanan
kirjoittamista:

- Käyttäjällä on **yksi salasana P**. Siitä tehdään hänen dp9ik-avaimensa
  (keyfs) ja secstoren PAK-todentaja (Hi). Secstoren `factotum`-tiedostossa
  on dp9ik-avain ja myöhemmin muut avaimet.
- **Passkey** käyttää WebAuthnin PRF-laajennusta. Autentikaattori antaa
  32 tavun salaisuuden, joka on sidottu passkeyhin ja suolaan, eikä sitä
  näe kukaan muu. Salaisuudella salataan P (*kääre*), ja kääre on
  palvelimella passkeyn tunnisteen alla.
- **Kirjautuminen passkeylla:** selain kysyy passkeyn (sormenjälki, PIN
  tai puhelin) ja saa PRF-salaisuuden. Palvelin antaa kääreen, ja kone
  avaa sen ja saa P:n. Sen jälkeen kirjautuminen jatkuu kuten nyt:
  `/boot/secstore` hakee avaimet, ja rcpu tunnistautuu dp9ik:llä.
- **Kirjautuminen salasanalla:** käyttäjä kirjoittaa P:n. Tämä on sama
  polku kuin nyt, ja se toimii aina, myös selaimessa, jossa ei ole PRF:ää.

P ei koskaan lähde selaimen koneesta selväkielisenä. Palvelin näkee vain
johdetut avaimet (dp9ik:n AES-avain, Hi) ja kääreet, joita se ei pysty
avaamaan. Palvelimelle murtautuja saa saman kuin nyt: keyfs:n ja
secstoren todentajat. Kääreistä ei ole hänelle hyötyä ilman autentikaattoria.

## Käyttäjän polut

1. **Uusi tunnus.**
   1. Käyttäjä antaa nimen (Plan 9 -nimi: `[a-z][a-z0-9]{1,27}`, ei
      varattu, kuten glenda, bootes, adm, sys, none, upas) ja salasanan
      P kahdesti (vähintään 10 merkkiä).
   2. Selain luo passkeyn (`navigator.credentials.create`, rpId
      `plan2001.com`, ES256, PRF päälle, resident key).
   3. Kone laskee johdetut avaimet ja PRF-kääreen ja lähettää ne
      palvelimelle.
   4. Palvelin luo käyttäjän:
      - keyfs ja dp9ik,
      - secstore-tili ja `factotum`-tiedosto, jossa on dp9ik-avain (kone
        vie sen `secstore -p`:llä),
      - kotihakemisto (cwfs `newuser`, glendan luuranko),
      - passkeyn julkinen avain ja kääre (`/adm/webauthn/NIMI/`).
2. **Kirjautuminen passkeylla.** Yksi napautus: selain tarjoaa passkeyn
   ilman nimeä (discoverable credential, userHandle = nimi). Sen jälkeen
   avaimet ja rcpu kuten yllä. Ei kirjoitettavaa.
3. **Kirjautuminen salasanalla.** Nimi ja P, sitten secstore ja rcpu.
4. **Uusi laite.** Passkey synkronoituu (iCloud Keychain, Google Password
   Manager) tai on puhelimessa (hybrid, QR). Muuten käyttäjä kirjautuu
   salasanalla ja voi lisätä laitteen passkeyn (polku 6).
5. **Itsenäinen kone ilman verkkoa.** Kääre on myös OPFS-levyllä
   (`aux/seckeys`in kopion vieressä). Passkey ja PRF avaavat sen, ja P
   avaa levyn salatun avainkopion. Passkey toimii siis myös offline.
6. **Passkeyn lisäys ja poisto.** Kirjautunut käyttäjä lisää passkeyn:
   kone tuntee P:n, joten se tekee uuden kääreen. Poisto poistaa kääreen
   ja julkisen avaimen.
7. **Salasanan vaihto.** Uusi P:
   - keyfs ja Hi päivitetään,
   - secstoren tiedostot salataan uudelleen (`secstore -c`:n tapaan),
   - kääreet tehdään uudelleen niillä passkeyillä, jotka ovat käsillä,
   - muut kääreet poistetaan, ja käyttäjälle kerrotaan siitä.
8. **Passkey kadonnut.** Salasana toimii yhä. **Salasana unohtunut:**
   passkey avaa P:n, ja sen jälkeen voi vaihtaa salasanan (polku 7). Jos
   molemmat ovat poissa, tunnus ja secstoren sisältö menetetään. Tämä on
   tietoinen valinta: palautusavainta ei ole palvelimella. Käyttäjä voi
   tulostaa palautuskoodin, joka on oma kääreensä.

## Arkkitehtuuri

```
selain: plan2001.com
  sivu (firmware: kernel.html, platform.js)
    WebAuthn: navigator.credentials (create/get + PRF), vain sivulla
    devwebauthn  <- #W/webauthn: koneen pyynnöt sivulle, vastaukset takaisin
  wasm32-kone
    /boot/login  kirjautumisvalikko konsolilla tai riossa
    auth/signup  uusi tunnus: johdetut avaimet, kääre, secstore -p
    auth/passkey avaa kääreen (PRF), antaa P:n seckeysille
    aux/seckeys  P -> secstore -> factotum (nyt: -i, putkella)
      | wss (webterm, politiikkasanat signup ja login)
pilvi: CPU- ja auth-palvelin (9Front-2001 + plan2001/)
  aux/signupd  /17040 (signup): luo käyttäjän, rajoittaa luontitahtia
  aux/passkeyd /17041 (login): kääre passkeyn tunnisteella, tarkistus (vaihe 2)
  keyfs, secstored, cwfs:n newuser
  /adm/webauthn/NIMI/ID  julkinen avain (COSE ES256), signCount, kääre
```

### Sivu: WebAuthn-laite (plan9/, wasm32:n firmware)

WebAuthn-rajapinta on vain sivun JavaScriptissä, joten kone käyttää sitä
laitteen kautta, kuten näppäimistöä ja OPFS-levyä. `devwebauthn.c`
(`#W`):

- `#W/webauthn`: kone kirjoittaa pyynnön ja lukee vastauksen. Pyyntö on
  rivimuotoinen: `get rpid salt-b64 [credid-b64...]` tai `create rpid
  user-b64 name salt-b64`. Vastaus on `ok credid-b64 prf-b64 [pubkey,
  authdata, signature, clientdata]` tai `error syy`.
- Sivu vaatii käyttäjän eleen: koneen pyyntö näyttää painikkeen
  ("Kirjaudu passkeylla" tai "Luo passkey"), ja painallus kutsuu
  rajapintaa. Selaimet eivät salli WebAuthnia ilman elettä, ja ele kertoo
  käyttäjälle, mitä on tapahtumassa.
- rpId on sivun oma rekisteröitävä verkkotunnus (`plan2001.com`). Se
  kelpaa myös alidomaineille (`cpu.`, `APP.`), joten sama passkey toimii
  sovellusten origineissa. Sivu ei päästä konetta valitsemaan muuta
  rpId:tä kuin oman originsa suffiksin, joten vieras sivusto ei saa
  plan2001.com:n passkeyta.
- Laite on 9front-yhteensopiva lisäys wasm32-alustaan: plan9/.

### Kone: kirjautuminen (plan2001/)

- `/boot/login` korvaa `/boot/secstore`n valikon, kun sivusto tarjoaa
  kirjautumisen (`GET /login`, kuten `GET /secstore` nyt): passkey,
  salasana, uusi tunnus tai kokeilu ilman tunnusta (nykyinen sandbox).
- `auth/passkey`:
  1. `get` laitteelta saa PRF-salaisuuden ja credid:n.
  2. passkeyd antaa kääreen credid:llä.
  3. P avataan, ja se annetaan `aux/seckeys -i`:lle putkella.
- Kääreen avain on `hkdf(PRF, "plan2001 passkey wrap v1")`, ja kääre
  salataan AES-GCM:llä. Kääreen sisältö on P sekä käyttäjänimi
  (eheyden takia).
- `auth/signup`:
  1. Laskee keyfs:n avaimen (`passtokey`) ja secstoren Hi:n (`PAK_Hi`).
  2. Luo passkeyn laitteelta ja kääreen.
  3. Lähettää ne signupd:lle.
  4. Vie dp9ik-avaimen secstoreen (`secstore -p`) ja kirjautuu.

### Palvelin (plan2001/)

- **signupd** (webterm `/17040`, politiikkasana `signup`) ajaa
  hostownerina:
  - Tarkistaa nimen (muoto, varatut, ei ennestään).
  - Kirjoittaa keyfs:ään (`/mnt/keys/NIMI`), secstoren `who`-tiedoston
    (Hi) ja `/adm/webauthn/NIMI/`.
  - Luo kotihakemiston (`newuser` cwfs:n konsoliin, luuranko).
  - Rajoittaa luontitahtia: osoitetta kohden (webterm antaa etäosoitteen
    ympäristössä) ja koko palvelimella. POC-vaiheessa luonti vaatii
    kutsukoodin.
- **passkeyd** (webterm `/17041`, politiikkasana `login`):
  - Vaihe 1: antaa kääreen credid:llä. Kääre on hyödytön ilman
    autentikaattoria. Credid on satunnainen (vähintään 16 tavua), eikä
    sitä voi arvata.
  - Vaihe 2: tarkistaa WebAuthn-allekirjoituksen ennen kääreen
    antamista. Challenge tulee passkeyd:ltä, ES256 tarkistetaan libsecin
    `ecdsaverify`llä ja P-256:lla, ja lisäksi tarkistetaan
    `clientDataJSON`in origin ja type, authDatan rpIdHash ja UP/UV-liput
    sekä signCount. Tämä on syvyyspuolustus: se estää kääreiden
    keräämisen ja antaa palvelimelle tiedon kirjautumisista.
- Tiedostot: `/adm/webauthn/NIMI/ID` sisältää rivit `pubkey`, `count`,
  `wrap`, `created` ja `name` (laitteen nimi käyttäjälle). Tiedostot
  kuuluvat hostownerille (0600).
- webterm: uudet politiikkasanat `signup` ja `login` term-sovellukselle
  (plan2001.com:n origin).

### plan2001.com ja CPU-palvelin

- plan2001.com:n sivu on nykyään diskless-sandbox (`webterm -n`, vain
  sivut). Siitä tulee kirjautumissivu, joka käynnistää koneen;
  kirjautumaton käyttäjä saa yhä sandboxin.
- Yksinkertaisinta on, että plan2001.com osoittaa samaan pilvikoneeseen
  kuin `cpu.plan2001.com`. Silloin origin on yksi, ja webtermin
  Origin-tarkistus sallii sen sellaisenaan. Sovellusten originit
  siirtyvät muotoon `APP.plan2001.com` (rpId kattaa ne).
- Jokainen käyttäjä on oikea Plan 9 -käyttäjä CPU-palvelimella. Siksi
  avoin rekisteröinti avaa palvelimen kenelle tahansa, ja kohta
  "Väärinkäyttö" on pakollinen ennen kuin kutsukoodista luovutaan.

## Turvallisuus

- **Phishing:** passkey on sidottu rpId:hen, eikä vieras sivu saa sitä.
  Salasana P on yhä kalastettavissa. Siksi salasanakirjautuminen voidaan
  myöhemmin rajata niille, joilla ei ole passkeyta.
- **Palvelimen vuoto:** keyfs:n AES-avaimet, secstoren Hi:t ja kääreet.
  Avaimiin ja Hi:hin on mahdollinen salasanan sanakirjahyökkäys, kuten
  nykyisessä Plan 9:ssä; dp9ik ja PAK hidastavat. Kääreisiin ei voi
  hyökätä ilman autentikaattoria.
- **Selaimen kone:** P ja avaimet ovat koneen muistissa (factotum) kuten
  nyt. Sivun JavaScript näkee PRF-salaisuuden, joten sivun eheys
  (CSP, SRI ja oma origin) on tärkeä.
- **PRF puuttuu:** autentikaattori ilman PRF:ää käy vain vaiheen 2
  tunnistukseen. P on silloin kirjoitettava. Sivu kertoo, kun
  passkeylla ei voi avata avaimia.
- **Palautus:** valinnainen palautuskoodi (satunnainen, tulostettava) on
  oma kääreensä. Palvelin ei voi palauttaa tunnusta.

## Väärinkäyttö (ennen avointa rekisteröintiä)

- Luontitahti: osoitetta ja päivää kohden, sekä kutsukoodit POC-vaiheessa.
- Käyttäjän nimiavaruus CPU-palvelimella: `/lib/namespace` ilman ulospäin
  avautuvaa `/net`iä (kirjautuminen, ei postia tai skannausta) ja ilman
  laskentapoolia, ellei käyttäjällä ole siihen oikeutta.
- Resurssit: Plan 9:ssä ei ole kiintiöitä. Siksi käyttäjäkohtaiset rajat
  (procit, muisti) ovat oma työnsä, ja levylle tarvitaan cwfs:n
  käyttöseuranta.
- Lopetus: tunnuksen poisto poistaa keyfs:n, secstoren, kääreet ja
  kotihakemiston.

## Kerrokset

| Osa | Kerros |
|---|---|
| `devwebauthn.c`, platform.js:n WebAuthn | plan9/ (wasm32:n firmware ja laite) |
| `auth/passkey`, `auth/signup`, `/boot/login` | plan2001/ |
| `aux/signupd`, `aux/passkeyd`, `/adm/webauthn`, webtermin sanat | plan2001/ |
| kääreen muoto, protokollat | tämä dokumentti |

## Vaiheet ja testit

1. **Laite:** `devwebauthn`, platform.js ja painike (tehty 4.10.).
   - `#W/webauthn`: kone kirjoittaa pyynnön, ja kirjoitus odottaa sivun
     vastausta. Note keskeyttää odotuksen, ja silloin sivun painike
     poistuu. Vastaus luetaan samasta fd:stä alusta alkaen.
   - Sivu tarkistaa, että rp on sen oma host tai sen yläpuolinen
     verkkotunnus.
   - Passkey on ES256 ja resident key. PRF:n tulos tulee sekä luonnissa
     (`prfok`) että kirjautumisessa (`prf=`).
   - Vastaus sisältää palvelimen tarkistusta varten myös `pubkey`
     (SPKI), `attest`, `auth`, `client` ja `sig`. Muoto on base64url
     ilman täytettä.
   - Testi `webauthn` (`test/d5.c`): Chromiumin virtuaalinen
     autentikaattori (DevTools Protocol `WebAuthn.addVirtualAuthenticator`,
     PRF päällä), sivu localhostissa ja painikkeiden klikkaus
     `test-wasmapp`illa (`WEBAUTHN`, `PAGEHOST`). Testi:
     - luo passkeyn ja kirjautuu sillä,
     - tarkistaa, että PRF on 32 tavua, sama samalla suolalla ja eri
       toisella,
     - tarkistaa, että user handle tulee ilman tunnistetta,
     - peruu pyynnön sivulla,
     - tarkistaa, että toisen verkkotunnuksen ja vääränlaisen pyynnön
       laite tai sivu hylkää.
2. **Kääre ja salasana:**
   - `auth/passkey` ja `auth/signup` koneella.
   - `tools/test-authsrv`iin signupd:n ja passkeyd:n Python-vastineet.
   - Testit: uusi tunnus, kirjautuminen passkeylla, salasanalla ja
     offline-levyllä.
3. **Palvelin:** signupd ja passkeyd 9frontissa, `tools/vm-cpu`
   asentaa ne. VM-testi: tunnus luodaan selaimesta, ja rcpu toimii.
4. **Tarkistus (passkeyd vaihe 2):** ES256, challenge ja signCount.
   Testi: väärä origin, väärä allekirjoitus ja toistettu challenge
   hylätään.
5. **plan2001.com:**
   - Sivu kirjautumisineen, kutsukoodit ja nimiavaruusrajat.
   - Sandbox jää kirjautumattomille.
   - Käyttöönotto `tools/cloud`in kautta.

## Avoimet kysymykset

- Onko yksi salasana sekä dp9ik:lle että secstorelle oikea valinta?
  Plan 9 sallii kaksi, mutta yksi on helpompi käyttäjälle, ja passkey
  tekee salasanasta harvinaisen.
- Kääre palvelimella vai vain laitteella? Palvelimella oleva kääre tekee
  synkronoituvista passkeyistä käyttökelpoisia uusilla laitteilla.
- Kirjautumisvalikko konsolilla, riossa vai sivulla HTML:nä? Suunnitelma
  pitää sen koneessa (Plan 9 -ohjelmana), ja sivulla on vain
  WebAuthn-painike.
- Käyttäjäkohtaiset resurssirajat CPU-palvelimella: miten ne tehdään
  9frontissa ilman kiintiöitä?
