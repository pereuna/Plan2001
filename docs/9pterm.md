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
9pterm -h … -u glenda -P … -M ~/.cache/9pterm.sock &     # istunto: tunnistautuu kerran
9pterm -S ~/.cache/9pterm.sock -T 20 -c 'komento'        # komento istunnon kautta
```

Istunto (`-M`) tunnistautuu kerran ja pitää rcpu-yhteyden; palvelin ajaa
silmukkaa 9ptermin laitteen `#J` yli (`kern/devjobs.c`, palvelimelle
`/mnt/term/dev/jobs`): `new` antaa seuraavan työn numeron, `N/cmd` on
komento (rc-skripti), `N/out`, `N/err` ja `N/status` palaavat työn
antajalle. Työt tulevat Unix-socketista (tila 600); asiakas (`-S`,
`gui-none/jobclient.c`) ei tunnistaudu eikä käynnistä drawtermin kerneliä.
Paluuarvot kuten `-c`:ssä; 2 myös, jos istuntoa ei ole tai se päättyy.
Jokainen työ ajetaan omassa aliprosessissaan (`@{…} &`), joten työt voivat
olla rinnakkain.

Mitattu (30.9.), `cat /dev/sysname`:

| | pilvi | sillin VM |
|---|---|---|
| suora `-c` (tunnistautuu joka kerta) | 1,33 s | 0,23 s |
| istunnon kautta | 0,50–0,56 s | 11–28 ms |

Pilvessä aika menee 9P:n edestakaisiin matkoihin: työ avaa etäpäässä neljä
tiedostoa ja käynnistää rc:n ja catin. Seuraavaksi: pieni apuohjelma
palvelimelle, joka pitää yhden kanavan auki (työ ja tuloste samassa
virrassa), jolloin työ maksaa noin yhden edestakaisen matkan.

## Vaiheet

| # | Vaihe | Tila |
|---|---|---|
| 1 | Build ilman GUI:ta; `-c` toimii | tehty 30.9.: `echo hi` 0,23 s, väärä salasana 0,1 s |
| 2 | AI-ystävällinen `-c`: paluuarvot 0/1/2/124, stderr erikseen, aikarajat `-W` ja `-T`, `-P`, EOF stdinissä | tehty 30.9. (testattu sillin CPU-VM:ää vasten; natiivi drawterm kääntyy ennallaan) |
| 3 | Interaktiivinen istunto: raw-tila, `/dev/kbd` palvelimen kbdfs:lle, rivieditori ja Delete | tehty 30.9.: pty-testi - komento, Backspace, Ctrl-C keskeyttää `sleep 100`:n 0,03 s:ssa, `exit` → 0; `-K` kursorinäppäimille |
| 4 | Tiedostot: `put`/`get` (`/mnt/term/root`), tarkistussumma | tehty 30.9.: 5 Mt put 0,38 s, get 0,60 s, md5 sama; virheet 1/2, ei `.9ptmp`-jäänteitä |
| 5 | Pilvi: rcpu 17019 ja auth 567 OCI:ssa | tehty 30.9.: `-c` 1,3 s, put+get 5 Mt 17 s md5 sama, interaktiivinen istunto ja Ctrl-C toimivat. Korjaus: drawterm kokeilee aina secstorea (auth-palvelimen portti 5356), ja pilven palomuuri pudottaa paketit hiljaa, jolloin yhteys odotti TCP:n aikarajaan; 9pterm käyttää secstorea vain `-s`:llä |
| 6 | Windows (`Make.win64` + gui-none) | vanhentunut (obsolete) toistaiseksi: ei tehdä nyt |
