# Plan2001 ja Plan9-wasm32: rajanveto (7.10.2026)

wasm32-kone siirrettiin omaksi projektikseen 5.10.2026:
[pereuna/plan9-wasm32](https://github.com/pereuna/plan9-wasm32)
(tuotu tämän repon commitista `59ee476`). Se on todettu toimivaksi:
live-image ja asennin toimivat osoitteessa https://plan2001.com/plan9-wasm32/.
Tämä dokumentti kertoo, mikä kuuluu kummallekin, miten Plan2001 käyttää
Plan9-wasm32:ta ja missä järjestyksessä päällekkäinen koodi poistetaan
täältä.

## Periaate

Plan9-wasm32 on **alusta**: Plan 9 wasm32-arkkitehtuurille, jossa selain
on kone. Se ei tiedä Plan2001:stä mitään.

Plan2001 on **järjestelmä alustojen päällä**: CPU-palvelin, tunnukset,
sivusto, sovellusoriginit ja laskentapooli. Selaimen wasm32-kone on sille
yksi pääte ja (D8:ssa) yksi laskentaresurssi, ja se tulee
Plan9-wasm32:sta kuten pc64-ydin tulee 9frontista.

Sääntö uudelle koodille: jos se on hyödyllinen kenelle tahansa, joka ajaa
Plan 9:ää selaimessa, se kuuluu Plan9-wasm32:een (ja tehdään siellä).
Jos se on Plan2001:n palvelu tai käytäntö, se kuuluu tänne.

## Kummalle mikin kuuluu

| Osa | Plan9-wasm32 | Plan2001 |
|---|---|---|
| ydin `sys/src/9/wasm32`, sivu (`platform.js`, `index.html`), laitteet `#S #R #I #ω` | ✓ | |
| 3a, 3c, 3l; libc, libthread, libmp, libsec wasm32:lle; `/wasm32/include` | ✓ | |
| 9frontin paikat wasm32:lle (`patches/9front`), natiivi käännös 9frontin mkfileillä | ✓ | |
| bootfs, live-image, jakelu (ISO), `inst/start`, boot levyltä | ✓ | |
| `/boot/secstore`, `aux/seckeys` (avaimet secstoresta ja levyltä) | ✓ | |
| alustan testit (`tools/test/run`, `wasmapp`, `authsrv`) | ✓ | |
| CPU-palvelimen palvelut: `webterm`, `signupd`, `passkeyd`, `weblimitd`, `websession`, `crsrv`, `rcc` | | ✓ |
| sivusto: rc-httpd:n `select-handler`, `plan2001-static`, `plan2001-app` | | ✓ |
| koneen Plan2001-osat: `/boot/login`, `/boot/app`, `rconnect.app`, `auth/passkey`, `aux/wsrcpu` | | ✓ |
| sovelluspohjat `lib/app/*`, glendan profiili | | ✓ |
| pilvi ja VM: `tools/cloud`, `tools/vm*`, `tools/cpu-live`, 9pterm | | ✓ |
| laskentapooli (D8) | | ✓ |
| pc64/UEFI-boot ja BootInfo amd64:lle (`plan9/sys/src/9/pc*`, `boot/efi`), arm64 | | ✓ (toistaiseksi, ks. avoimet) |

BootInfo on yhteinen data-ABI. `bootinfo.h` ja `port/bootinfo.c` ovat
jo eriytyneet, joten wasm32:n osalta Plan9-wasm32:n versio on se oikea.

## Miten Plan2001 käyttää Plan9-wasm32:ta

1. **Kiinnitys.** `tools/plan9-wasm32.conf` nimeää Plan9-wasm32:n
   commitin, kuten Plan9-wasm32:n `release.conf` kiinnittää 9frontin
   julkaisun. Päivitys on yksi rivi ja oma commit.
2. **Rakennus.** Plan9-wasm32 rakennetaan sellaisenaan (`tools/build`:
   julkaisu, puu, hostcc, natiivi käännös, bootfs).
3. **Plan2001:n kerros.** Plan2001:n wasm32-ohjelmat (`auth/passkey`,
   `aux/wsrcpu`) käännetään Plan9-wasm32:n puussa sen 3c:llä ja
   mkfileillä (`TREE=... tools/native`), ja ne sekä `/boot/login`,
   `/boot/app` ja `rconnect.app` lisätään bootfs:ään omalla protolla
   (`tools/bootfs TREE OUT bootfs.proto plan2001.proto`).
4. **Sivuston asetukset.** Plan9-wasm32:n sivu hakee `plan9.ini`:n
   palvelimelta. Plan2001:n sivusto antaa siinä nykyisten erillisten
   hakujen (`GET /login`, `/secstore`, `/app`) tiedot: `login=1`,
   `secstore=...`, `passkeyrp=...` ja `app=...`.
5. **Testit.** Plan2001:n omat koneen testit (tunnukset, passkeyt,
   kirjautuminen, sovellusoriginit, rajat) ajetaan Plan9-wasm32:n
   `wasmapp`-valjailla. Palvelinpään sijaisena on Plan2001:n
   `test-authsrv` (signupd, passkeyd ja webterm-rele), koska
   Plan9-wasm32:n `authsrv` ei tunne Plan2001:n palveluja.

## Yhteensopivuus: mitä Plan2001:ssä pitää muuttaa

- `auth/passkey` avaa `#W/webauthn`. Plan9-wasm32:ssa laite on `#ω`
  (`#W` on pc64:llä WireGuard, `plan2001/sys/src/9/ip/devwg.c`).
- `auth/passkey` soittaa palveluihin nimillä `signup` ja `passkey`.
  Plan9-wasm32:n `devwsnet` tuntee nimistä vain `/lib/ndb/common`in
  omat (rcpu, ticket, exportfs, secstore), mutta numerot käyvät.
  Siksi käytetään numeroita 17040 ja 17041.
- **Kirjautumiskonsoli.** Plan2001:n sivu näyttää sarjakonsolin, kunnes
  ohjelma piirtää ruudulle (`loginconsole`, `front.drawn`). Tämä on
  yleinen ominaisuus, joka ehdotetaan Plan9-wasm32:een (esim.
  `plan9.ini`:n `console=untildraw`). Siihen asti `/boot/login` toimii
  myös `?console=1`:llä.
- Sovellusoriginien sivu (`app`) ja `aux/wsrcpu`: tarkistetaan, mitä
  Plan9-wasm32:n sivu jo tukee. Puuttuva yleinen osa ehdotetaan sinne,
  ja Plan2001:n osa jää tänne.

## Mikä poistuu täältä

Poistetaan vasta, kun vaiheet 1–5 yllä toimivat ja Plan2001:n testit
menevät läpi Plan9-wasm32:n päällä:

- `plan9/sys/src/9/wasm32`, `plan9/sys/src/cmd/{3a,3c,3l}`,
  `plan9/sys/src/lib*/wasm32`, `plan9/wasm32/`, wasm32:n
  `plan9/patches/9front`-diffit ja niiden kopiot, `plan9/sys/src/cmd/aux/seckeys`.
- `monolith/tools/`: `build-libc3`, `build-9wasm32`, `build-bin3`,
  `build-3c3`, `build-cc`, `test-9wasm32`, `test-3c`, `test-cc`,
  `test-wasmapp`, `run3.mjs`; `monolith/cc9`, `monolith/third_party/9cc`.
- `tools/9front-2001`, `tools/native-wasm32`, `tools/test-native`.

Jää: `monolith/tools/build` (9pterm ja natiivi drawterm),
`third_party/drawterm` niiltä osin kuin 9pterm sitä tarvitsee, `pages`
(kirjoitetaan uudelleen Plan9-wasm32:n rakennuksen päälle), `serve`,
`deploy`, `relay`, `spki` ja `test-authsrv`.

## Vaiheet

1. Rajanveto (tämä dokumentti).
2. `tools/plan9-wasm32`: kiinnitetyn commitin haku ja rakennus.
3. Plan2001:n kerros: mkfile wasm32-ohjelmille, `plan2001.proto`,
   `#ω` ja palvelunumerot `auth/passkey`iin.
4. Plan2001:n testiajo Plan9-wasm32:n valjailla; testit siirretään
   `test-9wasm32`:sta. Jokainen siirretty testi ajetaan ennen
   vanhan poistoa.
5. Sivusto: `pages` ja `cpu-live pages` tekevät plan2001.comin sivun
   Plan9-wasm32:n rakennuksesta ja Plan2001:n kerroksesta, ja
   `plan9.ini` korvaa erilliset haut.
6. Päällekkäisen koodin poisto (yllä), README, `docs/status.md`,
   `docs/architecture.md` ja AGENTS.md ajan tasalle.

## Avoimet

- pc64/UEFI-boot (`plan9/sys/src/9/pc*`, `boot/efi`, `subset/`):
  jääkö se Plan2001:een vai tuleeko siitäkin oma "Plan 9 -fork" -repo
  (`docs/plan9-fork.md`:n alkuperäinen ajatus)?
- Tuoko Plan2001 Plan9-wasm32:n jakelun (koko 9front wasm32:lle,
  levylle asennettava) vai vain bootfs:n kuten nyt? Kirjautunut
  käyttäjä hyötyisi asennetusta levystä.
