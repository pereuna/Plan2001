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
8. **OCI:n käynnistyslevy on LUN 1:ssä.** QEMU/KVM vastaa LUN 0:ssa
   paikkamerkillä "QEMU TARGET", ja 9frontin `sdvirtio` käytti aina LUN 0:aa
   (`scsiverify`: `r->lun = 0; /* ??? */`): UEFI löysi levyn ja latasi
   kernelin, mutta kernel ei nähnyt osioita (`/dev/sd00/fscache: file does
   not exist`). Korjaus: `sys/src/9/pc/sdvirtio.c` etsii kunkin kohteen
   ensimmäisen LUNin (0–7), jonka INQUIRY sanoo laitteen olevan kytketty.
   Harjoitus: `tools/vm … --oci` laittaa levyn LUN 1:een (`OCI_LUN`).
9. **Loaderin ja kernelin on oltava pari.** CPU-palvelin oli 9frontin
   asentama (9frontin loader ja kernel); Plan2001:n kernel vaatii
   Plan2001:n loaderin (BootInfo): väärä pari käynnistyy silmukkaan.
   Pilvikuvassa on nyt Plan2001:n molemmat (`9fat`: `9pc64`,
   `EFI/BOOT/BOOTX64.EFI`; ESP-osio on tyhjä).
10. **Ensimmäinen siirto:** kuva kirjoitettiin Ubuntun päälle
    (`dd` ssh:n yli), ja Plan2001 käynnistyi OCI:ssa sarjakonsoliin asti,
    mutta ilman levyä (kohta 8). Ubuntu on poissa, joten uutta kuvaa ei voi
    kirjoittaa samalla tavalla: tarvitaan käynnistyslevyn vaihto Ubuntuun
    (OCI) ja uusi siirto.
7. **Pääsynvalvonta:** pilvessä suoja on pilven palomuuri (security list)
   ja TLS. (Plan2001:n ytimessä on nyt WireGuard, `#W`,
   `docs/wireguard.md`, mutta pilvipalvelimen ydin on vielä 29.9.:n,
   ilman sitä.) Laskentapoolia ei avata
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

**Pilvessä (29.9.):** kuva kirjoitettiin Ubuntun päälle
(`dd if=cpu-oci.raw | gzip -1 | ssh … 'gunzip | dd of=/dev/sda'`, noin
10 min; ensimmäinen yritys ilman LUN-korjausta, kohta 8, sitten OCI:n
käynnistyslevyn vaihto takaisin Ubuntuun ja uusi kirjoitus). Plan2001
käynnistyy OCI:ssa ilman käsityötä: `https://APP.82.70.55.84.nip.io:17443/`
vastaa, `term`-origin avaa rcpu:n muttei poolia ja `compute`-origin
päinvastoin; rcpu (17019), auth (567), 17080, 17010 ja 22 eivät näy
internetiin (security list: vain 17443). Laskentapooli: `crsrv -k`
(avain `~/.cache/plan2001/cr.key`, sivulle `#key=`).

## Seuraavaksi: asennus, ei levykuva

20 Gt:n levykuva (2 Gt dataa) on kiertotie. Tavoite: pieni asennusmedia
käynnistyy pilvessä ja asentaa Plan2001:n samalle levylle ilman
interaktiota, ja CPU-palvelimen asetukset (auth, käyttäjät, webterm,
crsrv, `/lib/app`, varmenne) ovat osa asennusta eivätkä sillin
`tools/vm-cpu`:ta. Esteet nyt: asennin asentaa asennusosajoukon, ei
CPU-palvelinta; pilvikoneessa on yksi levy (asennus omalle medialle on
auki); `remote`-etäasennus (portti 17010, lyhyt avain) on lähiverkkoa
varten, ei internetiin.

## Oma verkkotunnus: cpu.plan2001.com (29.9.)

`plan2001.com` on rekisteröity Cloudflaressa, joten sen nimipalvelu on
Cloudflarella. `cpu.plan2001.com` on delegoitu Plan2001-palvelimelle, joka
vastaa omasta vyöhykkeestään ja hakee varmenteensa itse:

- **Cloudflare:** `ns1 A 82.70.55.84` (DNS only) ja `cpu NS ns1.plan2001.com`
  (nimipalvelimen nimi vyöhykkeen ulkopuolella: Cloudflare ei salli
  tietueita delegoidun aliverkkotunnuksen sisällä).
- **OCI:n security list:** TCP 443, TCP ja UDP 53 (ja 17443).
- **Plan2001 (`/lib/ndb/local`):** `dom=cpu.plan2001.com soa=` (ns, mb,
  `caa=letsencrypt.org`) ja `APP.cpu.plan2001.com ip=82.70.55.84`
  sovelluksille. `/cfg/cirno/cpurc`: `ndb/dns -rsL` - vastaa julkisesti
  omasta vyöhykkeestään, rekursio vain paikalliselle verkolle (ulkopuolisen
  kysely `google.com`: REFUSED).
- **Varmenne:** `auth/acmed -t dns` (Let's Encrypt, DNS-haaste
  `/lib/ndb/dnschallenge`:n kautta): jokerivarmenne `*.cpu.plan2001.com`,
  voimassa 28.12.2026 asti. Tili `hostmaster@plan2001.com`
  (`/sys/lib/tls/acmed/`), TLS-avain `/sys/lib/tls/cpu.plan2001.com.key`
  (factotumiin `cpustart`issa).
- **Portti 443:** `/rc/bin/service/tcp443`: `tlssrv -c
  /sys/lib/tls/acmed/cpu.plan2001.com.crt /bin/webterm -s -w
  /sys/lib/monolith`. Osoitteet: `https://term.cpu.plan2001.com/`,
  `acme.`, `clock.`, `compute.` … ilman varmennevaroituksia.
- **Hallinta:** pilvipalvelimeen ei ole ssh:ta; `monolith/tools/term HOST
  'komento'` ajaa komennon term-sovelluksessa (headless Chromium) ja lukee
  tulosteen https:llä.

Kesken: varmenteen uusiminen ennen 28.12.2026 (`acmed` ajastettuna,
esim. `cron`), ja nämä asetukset osaksi asennusta (nyt tehty käsin
palvelimella, kirjattu tähän).

## Paikallinen CPU-palvelin (cirno) ja pilvi samaan tasoon (30.9.)

`tools/vm-cpu` tekee paikallisesta CPU-palvelimesta (cpu.qcow2) saman
järjestelmän kuin pilvessä. Tätä ennen cirnossa oli vielä 9frontin
asentama ydin ja loader, ja kummassakin koneessa oli 9frontin glenda-profiili,
joka käynnisti terminaalissa `rio -i riostart`:n (rio itse oli jo poistettu).

`vm-cpu` (sekä luonti että `--update`) tekee nyt nämä:
- kopioi Plan2001:n ytimen ja loaderin parina 9fat:iin
  (`9pc64`, `efi/boot/bootx64.efi`); ne tulevat `build/amd64`:sta
  (`tools/build.sh` tai `tools/kbuild9p.rc`);
- asentaa glenda-profiilin repon `usr/glenda/lib/profile`:sta, jossa ei
  ole rio:ta: terminaalissa on rc-kehote, ja ikkunat tulevat
  Monolithin kautta;
- poistaa `riostart`:in ja `rio`:n.

Pilveen tehtiin 30.9. sama profiilin vaihto 9pterm-istunnolla (vanha
tallessa `profile.9front`) ja `riostart` poistettiin. Pilven ydin on
yhä 29.9.:n.

Vain pilvessä, tarkoituksella:
- `cpurc`: `ndb/dns -rsL`, oma vyöhyke;
- `tcp443` ja Let's Encryptin varmenne;
- `crsrv -k` (avaintunnistus);
- `plan9.ini`:n `sd00`;
- `cpustart`:issa varmenteen avain factotumiin.

Paikallisessa koneessa on kehitys-CA ja portit 17443/17080.

base.qcow2 (build-VM) on 9frontin koskematon asennus ja cpu.qcow2:n
taustakuva, joten siihen ei kosketa. Sen 9front-profiili yrittää
terminaalissa rio:ta, mikä näkyy vain, kun base käynnistetään
terminaalina (build-VM, testit).

## Uusi pilvi-image: cpu-amd64-poc1 (1.10.)

Vanhan pilvipalvelimen auth meni sekaisin, kun salasanaa yritettiin
vaihtaa käsin. `changeuser` kysyy olemassa olevalta käyttäjältä ensin
"assign new Plan 9 password?", ja väärä vastaus jättää keyfs:ään vanhan
salasanan, vaikka nvram (`wrkey`) saa uuden. Palvelimelle ei saa enää
tehdä käsityötä, joten image tehdään kokonaan skriptillä.

**Nimet:**
- **Palvelu** on `cpu.plan2001.com` (sovellukset `APP.cpu.plan2001.com`).
  Se on osoite, ei kone.
- **Kone** on `cpu-amd64-poc1`: rooli, arkkitehtuuri ja vaihe. Paikallinen
  on `cpu-amd64-dev1`. Nimi tulee plan9.ini:n `sysname=`-rivistä, ei
  imagesta tai MAC-osoitteesta (`/rc/bin/cpurc.local`).
- **Hostowner** on `admin`, ja tavallinen käyttäjä on `glenda`. Konsolin
  kehote on `cpu-amd64-poc1#`.

**Tekeminen:** `tools/cloud-image` tuottaa tiedoston
`build/oci/cpu-amd64-poc1.qcow2`. Se ajetaan QEMU:ssa pilven raudalla
(`tools/vm --oci`) pohjasta `build/oci/cpu-oci.qcow2`. Mitään ei
kirjoiteta käsin:
- **Salaisuudet** (salasanat ja WireGuard-avaimet) syntyvät
  hakemistoon `~/.cache/plan2001/cloud`, eivät repoon.
- **Konsolilla** `tools/9run` hoitaa:
  - käyttäjän `admin` ja ryhmät cwfs:ään;
  - `changeuser`:n käyttäjille `admin` ja `glenda` (dialogi
    `tools/cloud/changeuser.dialog.in`, joka kattaa kaikki
    kysymysvariantit);
  - `convkeys`:in, joka purkaa salauksen nvramin vanhalla avaimella;
  - `wrkey`:n, joka kirjoittaa nvramiin `admin`/`plan2001`;
  - skriptin `tools/cloud/rename.rc`.
- **Glendana** `tools/cloud/handover.rc`: glendan aiemmin asentamat
  järjestelmätiedostot siirtyvät admin-ryhmälle.
- **Adminina** 9pterm-istunnossa `tools/cloud/setup.rc`:
  - Plan2001:n ydin (`#W`) ja loader;
  - profiilit ilman rio:ta;
  - compute-pino (`cpu-live`);
  - DNS-vyöhyke;
  - TLS ja sen uusinta;
  - porttien vartija;
  - WireGuard.
- **Lopuksi** image käynnistetään ja tarkistetaan: `tools/cloud/check.rc`
  ja `monolith/tools/test-apps`. Sen jälkeen syntyy pakattu qcow2.

**Mikä on julkista:**

| Portti | Mitä | Kenelle |
|---|---|---|
| 443 | https/wss (webterm, sovellukset), Let's Encrypt | kaikille |
| 53 tcp/udp | `ndb/dns -rsL`, oma vyöhyke | kaikille (rekursio vain paikalliselle verkolle) |
| 51820 udp | WireGuard, ylläpitotunneli | vain avaimella |
| 567, 17019, 17020, 17080, 17443 | auth, rcpu, exportfs, webterm ilman TLS:ää, https kehitys-CA:n varmenteella | vain paikallinen verkko ja WireGuard (`/rc/bin/localonly`, hylkäykset lokiin `/sys/log/localonly`) |

**Ylläpito:**
- `tools/cloud/wg-admin up` nostaa tällä koneella rajapinnan `wgp2001`
  (10.201.0.2 ↔ 10.201.0.1).
- Sen jälkeen `9pterm -h 10.201.0.1 -a 'tcp!10.201.0.1!567' -u admin
  -P ~/.cache/plan2001/cloud/admin.pass`.
- `tools/cpu-live SOCK` toimii saman tunnelin yli.
- Vanhaa 9p-skriptiä, joka käyttää julkisia portteja ja tiedostoa
  `cpu.pass`, ei enää tarvita.

**Varmenne:**
- `/rc/bin/certrenew` hakee Let's Encrypt -varmenteen
  `*.cpu.plan2001.com` DNS-haasteella oman ndb/dns:n kautta minuutti
  käynnistyksen jälkeen ja sitten vuorokauden välein. Se uusii varmenteen,
  kun se on yli 60 päivää vanha, ja kirjoittaa lokiin
  `/sys/log/certrenew`.
- Siihen asti `tcp443` käyttää kehitys-CA:n varmennetta.
- TLS-avain ja ACME-tili syntyvät palvelimella, joten niitä ei tarvitse
  siirtää.

**Tuonti Oracle Cloudiin:**
1. Lataa `build/oci/cpu-amd64-poc1.qcow2` Object Storageen.
2. Compute → Custom images → Import: QCOW2, paravirtualisoitu, UEFI.
3. Vaihda instanssin käynnistyslevy tähän imageen. Julkinen IP säilyy.
4. Security list:
   - auki TCP 443, TCP/UDP 53 ja UDP 51820;
   - kiinni 567, 17019 ja 17443, sillä palvelin ei niitä internetistä
     hyväksy muutenkaan.
