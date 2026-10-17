# ext4 kernelissä: juuri ilman tiedostopalvelinta (16.10.2026)

> **Poistettu kernelistä 17.10.2026.** Kernelin juuri on gefs
> (`docs/gefs.md`), ja ext4 on käyttäjätilan palvelin (9frontin ext4srv,
> Monolith). Tämä dokumentti on historiaa; sen lwext4-korjaukset (alla)
> siirretään Monolithin ext4srv:hen. Koodi: commit ennen poistoa
> (`git log -- sys/src/9/port/devext4.c`).

Plan2001:n juuritiedostojärjestelmä voi olla ext4, jonka kernel lukee
itse (`#E`, `sys/src/9/port/devext4.c`). Juuren ja ohjelmien
välissä ei ole käyttäjätilan 9P-palvelinta. Muut tiedostojärjestelmät
(FAT, ISO9660, hjfs, gefs, ext4srv) pysyvät käyttäjätilan palvelimina.
Uutta VFS-kerrosta ei tarvita: ajuri on tavallinen `Dev`.

Vaihe 1 (vain luku) ja vaihe 2 (kirjoitus: `mount -w`, plan9.ini
`root=-w UUID=...`) ovat valmiit ja testattu QEMU:ssa ja raudalla
(silli: juuri NVMe:n osiolla, Linuxin e2fsck puhdas jälkeenpäin).

## Kirjoitus (vaihe 2)

Kuten ext4srv: lwext4:n journal (toistetaan mountissa, `ext4_recover`)
ja lohkovälimuisti write-back-tilassa. Likaiset lohkot menevät levylle
`sync`issä (`echo sync >'#E/ctl'`), `ext4sync`-kproc:n toimesta 5 s
välein, umountissa ja kernelin shutdownissa (reboot). Sync kirjoittaa myös
superlohkon, jonka vapaiden lohkojen ja inodejen määrät lwext4 pitää
muistissa. create, write (append-only, kasvatus nollilla), remove (vain
tyhjä hakemisto), wstat (pituus, tila, mtime, ryhmä, nimi samassa
hakemistossa), ORCLOSE ja OTRUNC. Uuden tiedoston omistaja on luojan id
`etc/group`:sta tai hakemiston omistaja, ryhmä hakemiston; oikeudet kuten
Plan 9:ssä (hakemiston oikeuksilla rajattu). qid.vers on mtime plus
kirjoituslaskuri (inodeittain), joten exec näkee muuttuneen binäärin.

Korjattu lwext4:stä kirjoitusta varten:
- rw-mount merkitsi superlohkoon `ERROR_FS`:n ("mounted"); nyt kuten
  Linux: `VALID_FS` pois mountin ajaksi ja takaisin umountissa.

Katselmoinnin (16.10.2026) jälkeen:
- Levyn välimuisti: jokaisen syncin ja haltin lopuksi SCSI:n
  synchronize cache levyn `raw`-tiedoston kautta (AHCI: ATA flush cache,
  virtio: flush, NVMe: Flush; 9frontin sdnvme kuittasi sen tekemättä,
  `port/sdnvme.c`). `#E/ctl` näyttää flushien määrän. Journalin ja
  kotipaikkojen kirjoitusjärjestystä yhden syncin sisällä ei vielä
  pakoteta (flush on syncin lopussa, ei journalin commitin jälkeen).
- Poistettu inode: tiedostojärjestelmän kaikki Xfilet ovat listassa
  inodeineen; poisto merkitsee muut saman inoden Xfilet vanhentuneiksi
  ("ext4: file removed") ennen kuin inode voi olla toisen tiedoston.
- Oikeudet: OTRUNC vaatii kirjoitusoikeuden (devpermcheck katsoo vain
  omode&3), ORCLOSE hakemiston kirjoitusoikeuden jo avattaessa.
- Sama tiedostojärjestelmä (UUID superlohkosta ennen kuin lwext4
  kirjoittaa) ei mounttaudu toista kertaa, jos jompikumpi on kirjoittava.
- Kasvatus nollilla 1 Mt kerrallaan lukko välillä vapaana; note
  keskeyttää (`up->nnote`: ajurin sleep kuluttaa notependingin).
- lwext4: hakemistonimen pituus rajattu (`de.name`; revision 0:n
  16-bittinen pituus olisi ylittänyt sen); inodejen, ryhmäkuvaajien ja
  hakemistolehtien tarkistussummavirheet ovat virheitä, kun lohkoon ei
  ole muita viittauksia (lwext4 laskee summan vasta put:ssa);
  `ext4_fwrite` ei merkinnyt inodea likaiseksi, kun vain mtime muuttui
  (summa jäi vanhaksi, myös levylle).
- Kernelpino: `#E/ctl`:n `kstack` on suurin käyttö levyajurin kutsussa;
  ktestissä 3332 / 16384 tavua.
- Itseään kääntävä juuri (17.10.2026, `docs/selfhost.md`): yli 64 KiB:n
  (`Maxread`) write kirjoittaa kaiken paloissa. Ennen se palautti lyhyen
  määrän, ja `ar` (iso libc:n jäsen kerralla) tulkitsi sen virheeksi;
  ktest `fsbigio`.

Rajoitukset: tiedostot ovat polun mukaan (lwext4:n rajapinta), joten
kanava, jonka polku on sittemmin toinen inode, antaa virheen ("file
renamed or removed"); uudelleennimeäminen vain samassa hakemistossa;
lwext4 ei päivitä hakemiston mtime:a (ajuri tekee sen create/remove/
rename-kohdissa).

## Osat

- `sys/src/9/ext4/`: lwext4 9frontin `ext4srv`:stä
  (9front-11952, `/sys/src/cmd/ext4srv`) ilman 9P-palvelinta
  (`ext4srv.c`), levyliitäntää (`part.c`), ryhmiä ja mkfs:ää.
  `ext4_config.h` on kernelin oma (kernelin otsakkeet ja malloc), ja
  `ext4glue.c` antaa `werrstr`in, jossa `%r` on edellinen virhe
  (libc:n `%r` tekisi järjestelmäkutsun). Muutokset lwext4:ään:
  - `ext4_fread`: reiät (sparse-tiedosto, unwritten extent) luetaan
    nollina. lwext4 luki niiden kohdalta laitteen lohkon 0
    (monilohkopolussa ja viimeisessä lohkossa), joten esimerkiksi
    `mkfs.ext4 -d`:n tekemät binäärit (nollalohkot reikinä) luettiin
    väärin. Sama vika on 9frontin ext4srv:ssä.
  - `metadata_csum_seed` (Debianin mkfs.ext4:n oletus): tarkistussummien
    siemen on superlohkon `s_checksum_seed`, ei UUID:stä laskettu.
  - Kernelin `Eio`, `Enomem` ja `Eperm` korvaavat lwext4:n omat.
- `port/devext4.c`: `#E/ctl` ja yksi hakemisto kullekin liitetylle
  tiedostojärjestelmälle (`#E/root`; myös `#Eroot`). Laite avataan
  kirjoittajan nimiavaruudessa, joten se voi olla `#S`:n osio tai
  prosessin palvelema tiedosto (`nusb/disk`). Symboliset linkit
  seurataan (absoluuttinen tiedostojärjestelmän juuresta, enintään 8),
  muut erikoistiedostot eivät näy. Omistajat ovat nimiä `etc/group`:sta
  (kuten ext4srv), muuten numeroita. lwext4 ei ole reentrantti: jokainen
  kutsu on tiedostojärjestelmän QLockin alla, ja luku käyttäjän
  puskuriin tehdään lukon ulkopuolella (sivuvirhe voisi lukea samaa
  tiedostojärjestelmää).
- `pc64/mem.h`: `KSTACK` 8 → 16 KiB (lwext4 ja sen alla levyajuri tai
  devmnt, USB:llä); `MACHSIZE` pysyy 16 KiB:ssä.
- Boot: kernel mounttaa juuren plan9.ini:n `root=`:sta ja ajaa sen
  initin (`docs/init.md`); boot-tiedostojärjestelmää (bootfs.paq,
  bootrc ja sen `ext4!`-menetelmä) ei enää ole.

```
#E/ctl:
	mount NAME LAITE
	mount NAME UUID=uuid|LABEL=nimi EHDOKAS...
	umount NAME          (vain kun ei käytössä)
cat '#E/ctl':  root /dev/sdE5/linuxdata ro uuid ... label 'plan2001'
```

`LAITE!/hakemisto` (myös `UUID=uuid!/hakemisto`) tekee hakemistosta
juuren: `..` ja absoluuttiset linkit pysyvät sen sisällä, ja `etc/group`
luetaan sieltä. Plan 9 -puu voi siis olla Linuxin tiedostojärjestelmässä.

plan9.ini: `root=UUID=3c49...`, sillillä `root=-w UUID=22bcb0cd-...`
(NVMe:n p5). Boot ja init: `docs/init.md`.

## Juuri ilman boot-ympäristöä

Kernel mounttaa juuren (plan9.ini `root=`) ja ajaa sen initin; sopimus
ja initin tehtävät `docs/init.md`:ssä. `init0` (pc64/main.c) kutsuu
`rootmount`ia (devext4.c): UUID:n, labelin tai PARTUUID:n kernel etsii
itse `#S`:n levyiltä, koko levy ja jokainen GPT-osio luettuna levyn
`data`-tiedostosta (lwext4:n `part_offset`), joten osioita (`diskparts`)
ei tarvita. Juuri sidotaan `/`:n eteen (MBEFORE): sen `/rc` eikä `#/`:n
tyhjä liitoskohta; `#/` antaa ne, joita juuressa ei ole (`/dev`, `/env`,
...). `$rootdir` on `#E/root` initin newns:lle. `initcode` ajaa
`argv[1]`:n, jonka `init0` valitsi (initin `_main` täyttää `argv0`:n
paikan rekisteristä). Ilman `root=`:a kernel pysähtyy.

Alasajo: `halt` (ctl) tyhjentää välimuistin, pysäyttää journalin ja
merkitsee superlohkon puhtaaksi (kuten Linuxin umount); tiedostojärjestelmä
on sen jälkeen vain luku. Juuren ctl on myös `/srv/ext4.root.cmd`, joten
9frontin `fshalt` (kirjoittaa `halt` `/srv/ext4*cmd`:hen ja odottaa sen
katoavan) ajaa sen alas sellaisenaan; reboot (kernelin shutdown) samoin.

Toisen järjestelmän ESP:stä GRUBin kautta: loader lukee `plan9.ini`:n
ensin omasta hakemistostaan (`LoadedImage`n tiedostopolku), sitten
juuresta. `tools/espinstall -g /boot/efi '-w UUID=...'` kopioi loaderin,
kernelin ja plan9.ini:n `EFI/plan2001`:een ja lisää GRUB-valikkoon
merkinnän (`/etc/grub.d/42_plan2001`).

Kernelin `#c/cons` ei anna syötettä: näppäimistö ja rivieditori ovat
kbdfs:n. Testijuuren `/init` (rc) sitoo `/bin`:n ja laitteet, käynnistää
kbdfs:n (tämän repon, `tools/build.sh` vie sen ulos) ja ajaa
`/amd64/init`:n. Itseään kääntävän juuren `/init` on repon juuressa
(`docs/selfhost.md`). USB-levyt (nusb) eivät ole
kernelissä, joten juuri on AHCI-, NVMe- tai virtio-levyllä.

## Testit

- `tools/ext4root`: ext4-juuri VM:n julkaisu-ISO:sta (`$objtype/bin`,
  `rc`, `lib`, ...; tilat asetetaan, koska ISO:n tilat katoavat),
  `etc/group`, testitiedostot (`/plan2001test`: 12 Mt, 600 tiedoston
  htree-hakemisto, linkit, 200 merkin nimi) ja glendan profiili, joka
  tarkistaa ne ja antaa rc:n.
- `tools/ext4disk`: GPT, ESP (loader, kernel, plan9.ini) ja juuri; sama
  kuva USB-tikulle (`CONSOLE=0` sarjakonsolille). `ROOT=UUID=...!/dir`:
  vain ESP, juuri koneen omalla levyllä.
- `tools/test-ext4.sh` (`DISK=ahci|virtio|nvme|usb`, `SUBDIR=`): QEMU,
  19 tarkistusta, muun muassa kaikkien `/amd64/bin`:n tiedostojen md5
  hostin arvoja vastaan ja kuusi rinnakkaista lukijaa. `RW=1`: kaksi
  kirjoittavaa bootia (16 kirjoitustarkistusta: luonti, append, truncate,
  rename, 300 tiedoston htree, poisto, nollatäyttö, exec, sync; toinen
  boot löytää ensimmäisen kirjoitukset ja poistaa koko puun), kummankin
  jälkeen Linuxin `e2fsck -fn` ja `debugfs`. `CRASH=1`: QEMU tapetaan
  kesken kirjoitusten; `e2fsck -fy` saa vain toistaa journalin, ja
  seuraava boot (lwext4:n toisto) kirjoittaa ja synkkaa puhtaaksi.

- `sys/src/cmd/ktest/ktest.c` (kääntyy `tools/build.sh`:ssa, `tools/ext4root`
  asentaa sen; `/init` ajaa sen, kun plan9.ini:ssä on `ktest=...`,
  `tools/test-ext4.sh KTEST=all`): jokainen testi omassa prosessissaan
  aikarajalla. fs: 64 Mt satunnaisilla uudelleenkirjoituksilla, 3000
  tiedoston hakemisto, 150 tason puu yhdellä walkilla, wstat-nimeämiset,
  12 rinnakkaista kirjoittajaa ja lukijaa, reiät, append-only neljältä
  prosessilta, truncate ja kasvatus, wstat, ORCLOSE, exec samaan aikaan
  uudelleenkirjoitetusta binääristä (qid.vers), sync, `fill` (levy täyteen,
  sitten taas käytössä). mem: puolet vapaasta (enintään 1,5 Gt), 4×48
  prosessia, pinon raja (12 Mt toimii, 40 Mt saa noten), jaettu segmentti,
  copy-on-write. proc: 128 Mt putken läpi, notet, 500 forkia. mix: kaikki
  yhtä aikaa. k (Plan2001:n muutokset): UEFI:n kello ja RNG, ajastin,
  `$rootdir` ja hostowner, juuri `#/`:n edessä, `#E/ctl`,
  `/srv/ext4.root.cmd`, 600 nimen walk ja 30 kerroksen unioni.

Testisarja löysi devext4:stä: hakemiston luku jatkui uudelleen
kysytystä (lukupuskuriin mahtumattomasta) tietueesta laskemalla alusta,
joten välillä poistetut tietueet siirsivät laskua ja olemassa olevia jäi
näkemättä (rm -r ei tyhjentänyt hakemistoa). Nyt se tuotetaan nimeltä.

Raudalla juuren on oltava levyllä, jonka ajuri on kernelissä (AHCI,
NVMe, virtio): USB-levyn lohkot kulkevat `nusb/disk`in (käyttäjätilan
9P-palvelin) kautta. Tikulla on silloin vain ESP, jonka firmware lukee.

Huom: luku-vain-juurella termrc:n `/tmp`-testi (`syscall create`) ei
huomaa, ettei juuri ole kirjoitettava, joten `/tmp` ei ole ramfs.
Testiprofiili käynnistää ramfs:n itse; vaihe 2 poistaa asian.
