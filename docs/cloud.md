# Plan2001 pilvessä

Tavoite: Plan2001 asentuu pilveen (ja mille tahansa uudelle raudalle) ilman
käsityötä. Ensimmäinen yritys (29.9.): sillin CPU-palvelimen levy Oracle
Cloudin koneelle. Tämä dokumentti kirjaa, mikä siinä ei toiminut
sellaisenaan, ja mitä Plan2001:n pitää siksi olla.

## Kohde: Oracle Cloud, VM.Standard.E2.1.Micro

- x86-64, 2 vCPU (1/8 OCPU), 952 Mt muistia, 46,6 Gt käynnistyslevy,
  eu-stockholm-1.
- UEFI. Paravirtualisoitu tila: levy virtio-scsi (1af4:1004), verkko
  legacy virtio-net (1af4:1000); lisäksi IDE-ohjain ilman levyä.
- Verkko: DHCP, 10.0.0.0/24, yhdyskäytävä 10.0.0.1, nimipalvelin
  169.254.169.254 (myös metadata), julkinen osoite NATin takana.
- Ei sisäkkäistä virtualisointia (KVM:ää): QEMU pilvikoneen sisällä olisi
  emuloitu ja hidas.
- Vianetsintä vain OCI:n sarjakonsolista; oma levykuva joko custom imagena
  (Object Storage → import) tai kirjoittamalla käynnistyslevy uudelleen.
- Harjoitus sillissä: `tools/vm start --disk D --net --oci` (1 Gt,
  virtio-scsi, legacy virtio-net, ei in.img/out.img).

## Mikä ei toiminut sellaisenaan

1. **Levyn nimi riippuu ohjaimesta.** QEMU:n IDE/AHCI: `sdE0`; virtio-scsi:
   `sd00`. Nimi on kovakoodattu kahteen paikkaan: `plan9.ini`
   (`bootargs=local!/dev/sdE0/fscache`, `nobootprompt`) ja **cwfs:n oma
   asetuslohko** (`filsys main c(/dev/sdE0/fscache)(/dev/sdE0/fsworm)`).
   Levy toiseen koneeseen → ei käynnisty ilman sarjakonsolia.
2. **cwfs:n asetustila vaatii `-C`:n.** `local!/dev/sd00/fscache -c` ja
   oikeat `filsys`-rivit kaatuvat: `checktag … expected Tdir/1`,
   `panic: FID1 attach to root`. Syy: ilman `-C`:tä cwfs lukee välimuistia
   vanhassa muodossa (printconf: `newcache`). Oikein:
   `local!/dev/sd00/fscache -C -c`. Ansa, jota ei ole dokumentoitu.
3. **Verkkoidentiteetti on MAC-osoite ja kiinteä IP.** `/lib/ndb/local`:
   `sys=cirno ether=525400123456 ip=10.0.2.15`; tunnistautumispalvelin
   (`auth=cirno authdom=plan2001`) tulee aliverkon `ipnet`-rivistä.
   Uudessa verkossa (OCI: toinen MAC, 10.0.0.0/24) kone ei tunne nimeään
   eikä tunnistautumispalvelintaan.
4. **cpurc asettaa osoitteen vain tunnetulle MAC:lle.** Jos koneen MAC ei
   ole `/lib/ndb`:ssä, `cpurc` ei aja edes DHCP:tä: ei verkkoa lainkaan.
   Korjaus (`tools/vm-cpu` → `cpustart`): ilman osoitetta
   `ip/ipconfig -h $sysname ether /net/ether0`. DHCP kirjoittaa `/net/ndb`:hen
   `sys=cirno` uudella osoitteella, joten webtermin `sysname!tcp!PORTTI`
   ratkeaa oikein. (Loopbackia 127.1 ei näissä koneissa ole asetettu, joten
   sitä ei voi käyttää sellaisenaan.)
5. **Levyn kopiointi vaatii hallitun pysäytyksen** (`fshalt`), muuten
   cwfs:n välimuisti voi olla kesken.
6. **Tiedostojen siirto koneelle:** `in.img`/`out.img` eivät ole pilvessä;
   harjoituksessa siirto kulki http:llä (`hget` → sillin 10.0.2.2) ja vaati
   ensin `webfs`:n.
7. **Pääsynvalvonta:** Plan2001:ssä ei ole WireGuardia; pilvessä suoja on
   pilven palomuuri (security list) ja TLS. Laskentapoolia ei avata
   julkisesti ennen CR:ien tunnistautumista (`docs/app-origins.md`, kohta 11).

## Mitä Plan2001:n pitää olla

- **Levyt roolin, ei polun mukaan.** Käynnistys löytää Plan2001:n
  tiedostojärjestelmän osion tunnisteen (tyyppi/label/UUID) perusteella,
  ei ohjaimen nimestä; tiedostojärjestelmän asetukset viittaavat rooleihin
  (`fs`, `cache`, `worm`), ei `/dev/sdXX`-polkuihin.
- **Vakio levyrakenne ja vakio asennus.** Yksi layout (ESP, Plan2001:n fs,
  swap/nvram tarvittaessa) ja yksi asennusmenettely, joka tuottaa saman
  lopputuloksen USB-tikulle, levylle ja pilven levykuvaksi (raw/qcow2), ja
  jonka build voi tehdä ilman interaktiota.
- **Yksiselitteinen tiedostojärjestelmäkerros.** Ajatus: gefs käännetään
  kerneliin nopeuden vuoksi (juuren tiedostojärjestelmä ilman
  käyttäjätilan 9P-kierrosta), ja muut tiedostojärjestelmät (cwfs, hjfs,
  dossrv, …) ovat palvelimia. Juuren fs:n valinta ja asetukset eivät ole
  käynnistyksen `-c`-dialogi.
- **Verkko DHCP:llä, identiteetti ei MAC:sta.** Oletuksena DHCP; koneen
  nimi ja tunnistautumisalue tulevat asetuksesta (plan9.ini / pilven
  metadata), eivät MAC-osoitteesta. Itsenäinen cpu+auth-palvelin käyttää
  tunnistautumiseen itseään (loopback).
- **Palvelut itseensä ilman verkkoriippuvuutta** (loopback asetettuna ja
  käytössä, tai yhteys paikalliseen palveluun ilman nimenratkaisua).
- **Ensimmäinen käynnistys pilvessä:** nimi, julkiset avaimet ja
  varmenne metadatasta tai asetustiedostosta; sarjakonsoli oletuksena.
- **Varmenne:** 9frontin `auth/acmed` (Let's Encrypt) julkiselle nimelle.

## Tila (29.9.)

Harjoitus sillissä (`tools/vm start --disk build/oci/cpu-oci.qcow2 --net
--oci`: 1 Gt, virtio-scsi, legacy virtio-net, OCI:n MAC ja 10.0.0.0/24):

- levykuva: sillin CPU-palvelimen kopio `fshalt`in jälkeen, cwfs:n
  asetukset `sd00`-poluille (`-C -c`), `plan9.ini`: `sd00` ja
  `nobootprompt`, `cpustart`: DHCP ilman tunnettua MAC:ia;
- käynnistyy ilman käsityötä, saa osoitteen 10.0.0.223 DHCP:llä;
- `monolith/tools/test-apps` PASS tätä konetta vasten (term, clock,
  originit, policyt, jatko); laskentapooliin liittyi kolme CR:ää
  (`test-compute`in käännösosa siirtää skriptinsä `out.img`:llä, jota
  OCI-tilassa ei ole: testin rajoitus).

Seuraavaksi siirtotavan valinta: custom image (Object Storage → import)
vai käynnistyslevyn uudelleenkirjoitus Ubuntusta, ja pilven palomuuri.
