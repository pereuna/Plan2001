# 9pterm

drawterm tekstipäätteenä: ei grafiikkaa, ei ääntä. Plan 9/9frontin
tavallinen rcpu (dp9ik, TLS-PSK, 9P), ei vielä WSS:ää eikä 2001P:tä.
Tavoite: käyttökelpoinen pääte ihmiselle ja AI:lle - yksittäiset komennot
ja interaktiivinen istunto. Tausta: `docs/ai/2026-09-30/`.

## Rakenne

Build-muoto repon drawtermiin (`monolith/third_party/drawterm`, pin
64dcc24), ei kopio: upstreamin korjaukset tulevat kaikille muodoille.

- `Make.9pterm`: Linux, `GUI=none`, `AUDIO=none`, `-DNINEPTERM`, TARG=9pterm
- `gui-none/`: näyttöfunktiot tyhjinä, `gscreen = nil`
- `cpu.c`: `NINEPTERM` → aina `-G`
- `monolith/tools/build 9pterm` → `monolith/build/9pterm/9pterm`
  (riippuvuudet vain libc ja libm)

Käyttö pilveen (rcpu 17019 ja auth 567 auki OCI:ssa):

```
9pterm -h cpu.plan2001.com -a 'tcp!cpu.plan2001.com!567' -u glenda -P ~/.cache/plan2001/cpu.pass -c 'cat /dev/sysname'
```

Yhteys: 9pterm hakee tiketit auth-palvelimelta (567, dp9ik: salasana ei
kulje verkossa), todistaa niillä henkilöllisyytensä CPU-palvelimelle
(17019) ja salaa yhteyden TLS:llä; sen sisällä palvelin ajaa komennon ja
käyttää 9ptermin tiedostoja (`/dev/cons`, `/dev/stderr`, `/mnt/term/root`)
9P:llä. Ei selainta, WebSocketia eikä webtermiä.

Käyttö (sillin CPU-VM):

```
9pterm -h 127.0.0.1 -a 'tcp!127.0.0.1!5670' -u glenda -P ~/.cache/plan2001/cpu.pass -c 'cat /dev/sysname'
```

Valitsimet (drawtermin lisäksi):

| | |
|---|---|
| `-P tiedosto` | salasana tiedostosta (tai ympäristömuuttuja `PASS`); ei koskaan kaiuteta |
| `-W s` | yhteyden ja tunnistautumisen aikaraja, oletus 30 |
| `-T s` | komennon aikaraja, oletus ei rajaa |
| `-K` | interaktiivisessa istunnossa myös kursorinäppäimet (nuolet, Home, End, PgUp/PgDn) - vain palvelimelle, jonka kbdfs:ssä on rivieditori (Plan2001:n); 9frontin kbdfs lisää ne riville merkkeinä, joten oletuksena ne ohitetaan |

Paluuarvo: 0 = etäkomennon status tyhjä; 1 = status ei tyhjä (status
stderr:iin, esim. `rc 608: oops`); 2 = paikallinen virhe (yhteys,
tunnistautuminen, valitsimet); 124 = `-T` ylittyi.

stdout on etäkomennon tuloste, stderr sen fd 2 (`/mnt/term/dev/stderr`,
devconsin `NINEPTERM`-tiedosto) ja 9ptermin omat viestit. stdin menee
komennolle, ja EOF päättää sen syötteen.

Interaktiivinen istunto (ei `-c`:tä, stdin on pääte): 9pterm tarjoaa
`/dev/kbd`:n, joten palvelimen kbdfs hoitaa rivieditorin, kaiun ja
keskeytyksen kuten drawtermissa; paikallinen pääte on raw-tilassa
(`gui-none/tty.c`) ja palautuu lopuksi. Näppäimet: Backspace → ^H,
**Ctrl-C → Delete eli keskeytys**, Enter → rivinvaihto, ^D → EOF.
Putkella ilman `-c`:tä syöte menee rc:lle sellaisenaan.

Tiedostot:

```
9pterm … put PAIKALLINEN ETÄ
9pterm … get ETÄ PAIKALLINEN
```

Siirto kulkee 9ptermin nimiavaruuden kautta (`/mnt/term/root` on paikallinen
levy), MD5 tarkistetaan, ja tiedosto nimetään lopulliseksi vasta, kun se
on kokonainen ja täsmää (`NIMI.9ptmp` → `NIMI`): `put` tarkistaa etäpäässä
paikallisesti lasketun summan, `get` paikallisesti etäpään summan.
Paluuarvot kuten `-c`:ssä: 1 etäpään virheestä (esim. status `copy`,
`checksum`), 2 paikallisesta.

## Istunto AI:lle: tunnistaudu kerran

```
9pterm -h … -u glenda -P … [-I s] -M ~/.cache/9pterm.sock &   # istunto: tunnistautuu kerran
9pterm -S ~/.cache/9pterm.sock -T 20 -c 'komento'              # komento istunnon kautta
9pterm -S ~/.cache/9pterm.sock put PAIKALLINEN ETÄ             # tiedostot kuten ilman -S:ää
9pterm -S ~/.cache/9pterm.sock get ETÄ PAIKALLINEN
9pterm -S ~/.cache/9pterm.sock -Q                              # istunnon tila
9pterm -S ~/.cache/9pterm.sock -X                              # istunnon loppu
```

- `-T s` tai asiakkaan kuolema (SIGTERM, ^C) tappaa etäkomennon koko
  note-ryhmän: istunto huomaa asiakkaan lähdön, ja 9pjobd saa `K`-pyynnön
  (se kertoo jokaisen työn prosessin `p`-kehyksellä). Mitattu: `sleep` poissa
  palvelimelta alle 0,2 s:ssa. (rc-silmukassa ei keskeytystä.)
- `-I s`: istunto päättyy, kun yhtään työtä ei ole ollut s sekuntiin; käynnissä
  oleva työ pitää sen hengissä. Socket poistetaan istunnon päättyessä.
- `-Q`: istunnon kone ja käyttäjä, socket, ikä, töitä käynnissä / tehty,
  tyhjäkäynti.

Istunto (`-M`) tunnistautuu kerran ja pitää rcpu-yhteyden; palvelin ajaa
silmukkaa 9ptermin laitteen `#J` yli (`kern/devjobs.c`, palvelimelle
`/mnt/term/dev/jobs`): `new` antaa seuraavan työn numeron, `N/cmd` on
komento (rc-skripti), `N/out`, `N/err` ja `N/status` palaavat työn
antajalle. Työt tulevat Unix-socketista (tila 600); asiakas (`-S`,
`gui-none/jobclient.c`) ei tunnistaudu eikä käynnistä drawtermin kerneliä.
Paluuarvot kuten `-c`:ssä; 2 myös, jos istuntoa ei ole tai se päättyy.
Jokainen työ ajetaan omassa aliprosessissaan (`@{…} &`), joten työt voivat
olla rinnakkain.

Palvelimen puoli on `9pjobd` (`kern/9pjobd.c`): 9pterm kuljettaa sen
lähteen (`jobs/src`, upotettu buildissa), ja istunnon alussa palvelin
kääntää sen `$home/bin/$cputype`:iin, jos sitä ei ole tai lähde on
muuttunut. Se pitää yhden tiedoston auki (`jobs/ctl`): luku antaa kokonaisen
työn, ja tuloste ja status palaavat kirjoituksina samaan tiedostoon -
työtä kohden ei avata tiedostoja. Jos käännös ei onnistu, käytetään
rc-silmukkaa työtiedostojen yli.

Mitattu (30.9.), `cat /dev/sysname`:

| | pilvi (TCP-RTT ~30 ms) | sillin VM |
|---|---|---|
| suora `-c` (tunnistautuu joka kerta) | 1,33 s | 0,23 s |
| istunto, rc-silmukka | 0,50–0,56 s | 11–28 ms |
| **istunto, 9pjobd** | **73–112 ms** | **8–15 ms** |

20 rinnakkaista komentoa (`sleep 1; echo job N`) samassa istunnossa:
1,57 s, kaikki tulosteet oikeille kutsujille.

## Vaiheet

| # | Vaihe | Tila |
|---|---|---|
| 1 | Build ilman GUI:ta; `-c` toimii | tehty 30.9.: `echo hi` 0,23 s, väärä salasana 0,1 s |
| 2 | AI-ystävällinen `-c`: paluuarvot 0/1/2/124, stderr erikseen, aikarajat `-W` ja `-T`, `-P`, EOF stdinissä | tehty 30.9. (testattu sillin CPU-VM:ää vasten; natiivi drawterm kääntyy ennallaan) |
| 3 | Interaktiivinen istunto: raw-tila, `/dev/kbd` palvelimen kbdfs:lle, rivieditori ja Delete | tehty 30.9.: pty-testi - komento, Backspace, Ctrl-C keskeyttää `sleep 100`:n 0,03 s:ssa, `exit` → 0; `-K` kursorinäppäimille |
| 4 | Tiedostot: `put`/`get` (`/mnt/term/root`), tarkistussumma | tehty 30.9.: 5 Mt put 0,38 s, get 0,60 s, md5 sama; virheet 1/2, ei `.9ptmp`-jäänteitä |
| 5 | Pilvi: rcpu 17019 ja auth 567 OCI:ssa | tehty 30.9.: `-c` 1,3 s, put+get 5 Mt 17 s md5 sama, interaktiivinen istunto ja Ctrl-C toimivat. Korjaus: drawterm kokeilee aina secstorea (auth-palvelimen portti 5356), ja pilven palomuuri pudottaa paketit hiljaa, jolloin yhteys odotti TCP:n aikarajaan; 9pterm käyttää secstorea vain `-s`:llä |
| 6 | Windows (`Make.win64` + gui-none) | vanhentunut (obsolete) toistaiseksi: ei tehdä nyt |
| 7 | Istunto AI:lle (`-M`/`-S`), 9pjobd | tehty 30.9.: pilveen 73–112 ms/komento (1,33 s ilman), 20 rinnakkaista komentoa oikein |
| 8 | Istunnon keskeytys (`-T`, asiakkaan kuolema) | tehty 30.9.: etäkomento tapetaan alle 0,2 s:ssa |
| 9 | `put`/`get` istunnon kautta | tehty 30.9.: 5 Mt pilveen ja takaisin 14 s, md5 sama; sillin VM:ään 0,69 s |
| 10 | Elinkaari: `-I`, `-Q`, `-X`, socketin poisto | tehty 30.9. |

## Stressitesti pilveen (30.9.)

Pilvipalvelin (1/8 OCPU) käänsi ohjelmia 9pterm-yhteyksien kautta; pooli:
käyttäjän selainvälilehdet (`compute.cpu.plan2001.com`), 23 + 3 workeria.
Skriptit viety `put`illa, ajettu `-c`:llä (`/tmp/build.rc`, `/tmp/stress.rc`).

| Kierros | Tulos |
|---|---|
| peräkkäin, oma 6c vs. pooli | acme 9 s → **1 s**; sam < 1 s molemmin; kernel pc64 (174 obj.) 23 s → 24 s. Objektit samat (kernelissä eroavat vain päiväykselliset `pc64.6`, `pc64.root.6`). 211 käännöstyötä, jako 125 / 86 |
| 3 yhteyttä yhtä aikaa: acme, sam, kernel | kaikki paluuarvo 0; 3,4 s, 4,5 s, 24,8 s; objektit samat kuin omalla 6c:llä |
| 3 yhteyttä yhtä aikaa: 3 kernelin kopiota | kaikki paluuarvo 0, 174 objektia ja `9pc64` kussakin; yhteensä 68 s. Pienempi välilehti poistui kesken, pooli jatkoi yhdellä (754 työtä) |

Havainnot:
- 9pterm kesti rinnakkaiset yhteydet ja minuutin istunnot; tuloste tuli
  reaaliajassa.
- Kernel ei nopeudu: kolme kerneliä rinnakkain vie saman ajan kuin
  peräkkäin (3 × 23 s). Pullonkaula on pilvipalvelin: jokaiselle
  käännökselle `rcc` kokoaa otsikoiden sulkeuman (kymmeniä tiedostoja) ja
  lähettää sen selaimeen. Seuraava parannus: CR:n otsikkovälimuisti
  (työ lähettää tunnisteet, CR hakee puuttuvat).
- Ajoittaja antaa työn ensimmäiselle vapaalle workerille, ei kapasiteetin
  suhteessa.
- Yksi yhdistetty vertailukomento jäi kerran kiinni (ei toistunut);
  AI:n komennoissa aina `-T`.

## Katselmointi ja korjaukset (30.9.)

Ulkoisen katselmoinnin löydökset istuntotilasta ja niiden korjaukset:

| Löydös | Korjaus | Testi |
|---|---|---|
| Socketin `write`/`send` voi kirjoittaa osan: kehystys hajoaa kuormassa | `sendfull`/`writefull` molempiin suuntiin, EINTR huomioitu; asiakas ohittaa SIGPIPEn | 5 Mt:n tuloste istunnon kautta, md5 sama |
| Job-rakenteet eivät vapaudu: pitkä istunto vuotaa | viitelaskuri (lista, vahti, jokainen käyttö); lisäksi drawtermin kproc-säikeet irrotetaan (`pthread_detach`, NINEPTERM), koska kukaan ei liitä niitä - tämä oli varsinainen vuoto (~8 kt/työ) | 4 000 työtä: RSS 3,2 → 3,7 Mt (ennen 3,9 → 37 Mt), säikeitä 6 |
| `jobs/ctl` ei kanna 9P-viestiä suurempaa komentoa | tietue luetaan tarvittaessa usealla lukukerralla, ja `9pjobd` lukee otsakkeen ja komennon `readn`:llä; kirjoitettujen kehysten palat kootaan | 129 kt:n komento (ennen raja ~8 kt) |
| Plan 9:n `exec` ottaa ~64 kt argumentteja | yli 32 kt:n komento rc:lle väliaikaistiedostona (poistetaan työn jälkeen) | 94 kt ja 129 kt oikein, ei jäänteitä |
| ESC-sekvenssi rikkoutuu lukurajalla; UTF-8 hyväksyy virheellisiä tavuja | pysyvä tilakone (ESC, CSI, UTF-8); yksinäinen ESC odottaa 50 ms; virheellinen tavu → U+FFFD | pty: `ESC [` + `D` eri lukukerroilla, `ä` kahdessa osassa, 0xFF |
| `get` poisti vanhan tiedoston ennen uuden nimeämistä | vanha → `NIMI.9pold`, uusi → `NIMI`, vanha pois vasta sitten; epäonnistuessa vanha takaisin (drawtermin rename ei korvaa) | `get` olemassa olevan päälle, ei jäänteitä |
| `-S get` otti talteen vain viimeisen tulostekehyksen | kaikki kehykset kootaan | |
| (oma) vahti ja `endjob` sulkivat saman socketin: numero voi mennä uudelle yhteydelle | vain vahti sulkee; `endjob` tekee `shutdown`in | |
