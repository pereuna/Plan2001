# Boot ja init: kernel mounttaa juuren ja ajaa sen initin (16.10.2026)

Plan2001:ssä ei ole boot-tiedostojärjestelmää: ei bootfs.paq:ia, paqfs:ää,
bootrc:tä eikä `/boot/boot`ia. Kuten OpenBSD:ssä (ei initramfs:ää)
kernel mounttaa juuren itse ja ajaa juuren initin. Tämä on kernelin ja
juuren välinen sopimus. Plan2001:n juuren `/init` on repon juuressa
(`init`, `docs/selfhost.md`).

```
UEFI → loader (bootx64.efi) → kernel → init0: rootmount → juuren init
```

## plan9.ini

```
bootfile=/9pc64                          (tai /EFI/plan2001/9pc64)
root=-w UUID=22bcb0cd-...                pakollinen
init=/init                               oletus: /amd64/init -t (cpu: -c)
user=glenda                              hostowner
console=0                                sarjakonsoli (initin kbdfs:lle)
```

`root=`: `[-w] LAITE | UUID=uuid | LABEL=nimi | PARTUUID=guid [!/hakemisto]`.
`-w` kirjoitettavaksi (muuten vain luku). UUID:n, labelin tai PARTUUID:n
kernel etsii `#S`:n levyiltä: koko levy ja GPT:n osiot luettuna levyn
`data`-tiedostosta, joten osioita ei tarvitse asettaa (ei `diskparts`ia).
`!/hakemisto` tekee hakemistosta juuren (Plan 9 -puu Linuxin ext4:ssä).
Juuri on ext4 (`#E`, `docs/ext4.md`) AHCI-, NVMe- tai virtio-levyllä;
USB-levyt (nusb) ovat käyttäjätilaa, eivät juureksi.

Loader lukee plan9.ini:n ensin omasta hakemistostaan, sitten ESP:n
juuresta: toisen järjestelmän ESP:ssä `EFI/plan2001/` riittää
(`tools/espinstall`, GRUB ketjuttaa).

Ilman `root=`:a tai jos juuri ei mounttaudu kernel pysähtyy:
`panic: no root file system: plan9.ini root=...`.

## Mitä kernel tekee ennen initiä

`init0` (`pc64/main.c`) → `rootmount` (`port/devext4.c`):

1. `user=` on hostowner (`eve`, `/dev/hostowner`) ja ensimmäisen prosessin
   käyttäjä, kuten factotum -u teki.
2. Juuri mounttautuu `#E/root`:ksi, ja se sidotaan `/`:n eteen (MBEFORE):
   juuren `/rc`, `/amd64` jne.; `#/` antaa vain ne liitoskohdat, joita
   juuressa ei ole (`/dev`, `/env`, `/proc`, `/srv`, ...).
3. Ympäristöön `rootdir=#E/root` (9frontin `/lib/namespace` sitoo sen:
   `bind -a $rootdir /`), `cputype`, `service` (terminal tai cpu),
   `terminal` sekä plan9.ini:n muuttujat.
4. `#E/ctl` julkaistaan `/srv/ext4.root.cmd`:nä: 9frontin `fshalt` ajaa
   juuren alas sen kautta (`halt`).

`initcode` sitoo `#c` → `/dev` (perään), `#d` → `/fd`, `#e` ja `#ec` →
`/env`, `#p` → `/proc`, `#s` → `/srv`, `#σ` → `/shr`, avaa fd:t 0, 1 ja 2
`/dev/cons`:iin (kernelin konsoli) ja ajaa `init=`:n (tai oletuksen).
`#!`-skripti käy initiksi.

## Mitä initin on tehtävä itse

Kernelin `#c/cons` on vain tuloste: lukeminen antaa virheen. Näppäimistö,
hiiri ja rivieditori ovat kbdfs:n. Entinen bootrc teki seuraavat; nyt ne
ovat initin työtä (repon `init` tekee ne):

- `/bin`: ensimmäinen komento absoluuttisella polulla, `/bin` on aluksi
  `#/`:n tyhjä hakemisto (`/amd64/bin/bind /amd64/bin /bin`,
  `bind -a /rc/bin /bin`).
- Laitteet `/dev`:iin: `#¤ #¶ #P #S #f #æ #t #b #m` (`bind -qa`), verkko
  `#I #l #a` → `/net` (9frontin `/lib/namespace` tekee nämä initille).
- kbdfs konsoliksi (`aux/kbdfs -q -s cons [/dev/eia$console]`), sitten
  init sen `/dev/cons`:sta (`exec /amd64/init -t </dev/cons >/dev/cons >[2=1]`).
- factotum, jos tunnistusta tarvitaan (bootrc käynnisti sen aina);
  ilman sitä `/lib/namespace`:n `mount /srv/factotum` epäonnistuu hiljaa.
- `mntgen` (`/n`, `/mnt`): 9frontin termrc tekee sen itse.
- nusb (USB-näppäimistö ja -levyt): 9frontin termrc ajaa `nusbrc`:n.
- Kello: kernel asettaa sen UEFI:n RTC:stä, `#r/rtc`:tä ei tarvita.

Repon `init` (laitteet, kbdfs, aikavyöhyke plan9.ini:n `tz=`:sta,
mntgen, verkko: ipconfig, cs, dns, webfs; `ip=none` ohittaa) ja
testijuuren `/init` (`tools/ext4root`) ovat esimerkit. Jälkimmäinen: laitteet, kbdfs,
`ktest` jos plan9.ini:ssä on `ktest=...`, sitten `/amd64/init -t`.

## Alasajo

`fshalt` (9frontin) kirjoittaa `halt` `/srv/ext4.root.cmd`:hen: välimuisti
levylle, levyn kirjoitusvälimuisti tyhjäksi, journal kiinni, superlohko
puhtaaksi; juuri on sen jälkeen vain luku. Reboot (`#c/reboot`) tekee
saman. Ilman niitä data on levyllä viimeisimmän 5 s välein tehdyn syncin
verran, ja seuraava mount toistaa journalin.

## Mitä poistui (bootfs.paq)

Kernelin `bootdir` (bootfs.paq, paqfs, factotum, `/boot/boot`) ja repon
boot-ympäristö (`sys/src/9/boot/`: bootrc, ext4.rc, net.rc, reimage.rc,
devsd.proto, devext4.proto; `sys/src/cmd/aux/reimage.c`) poistettiin;
ne ovat historiassa (commit ennen poistoa). Kernel pieneni 5 056 448:sta
1 933 141 tavuun (size).

Lakkasivat toimimasta tällä kernelillä, kunnes ne siirtyvät `root=`:aan
ja ext4-juureen (Monolith): Monolithin CPU-VM (`tools/vm-cpu`), pilvikuva,
asennin (`inst/bootsetup` kirjoittaa `bootargs=local!...`), tämän repon
`tools/wgtest.sh` ja `tools/vm-install` (cwfs-juuri bootrc:n kautta).
Käännös-VM (`tools/vm`, `tools/build.sh`) käyttää 9frontin omaa kerneliä
eikä muutu.
