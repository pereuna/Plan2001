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
root=-w PARTUUID=6698a508-...            pakollinen
init=/init                               oletus: /amd64/init -t (cpu: -c)
user=glenda                              hostowner
console=0                                sarjakonsoli (initin kbdfs:lle)
```

`root=`: `[-w] PARTUUID=guid | LAITE [!puu]`. `-w` kirjoitettavaksi
(muuten vain luku). Juuri on kernelin gefs (`docs/gefs.md`): osio, jonka
superlohkossa on `gefs9.00`, AHCI-, NVMe- tai virtio-levyllä. PARTUUID:n
kernel etsii `#S`:n levyiltä, eli jokaisen levyn GPT:n luettuna sen
`data`-tiedostosta, joten osioita ei tarvitse asettaa (ei `diskparts`ia).
`!puu` valitsee gefs:n puun (oletus `main`): `root=-w PARTUUID=guid` tai
`root=-w #S/sdE0/data!adm`. USB-levyt (nusb) ovat käyttäjätilaa, eivät
juureksi. ext4 ei ole enää kernelissä (17.10.2026).

Loader lukee plan9.ini:n ensin omasta hakemistostaan, sitten ESP:n
juuresta: toisen järjestelmän ESP:ssä `EFI/plan2001/` riittää
(`tools/espinstall`, GRUB ketjuttaa).

Ilman `root=`:a tai jos juuri ei mounttaudu kernel pysähtyy:
`panic: no root file system: plan9.ini root=...`.

## Mitä kernel tekee ennen initiä

`init0` (`pc64/main.c`) → `rootmount` (`port/devgefs.c`):

1. `user=` on hostowner (`eve`, `/dev/hostowner`) ja ensimmäisen prosessin
   käyttäjä, kuten factotum -u teki.
2. gefs käynnistyy kernelin kproceina osiolla, ja sen 9P-putken toinen pää
   mounttautuu (`mntattach`, käyttäjänä hostowner) `/`:n eteen (MBEFORE):
   juuren `/rc`, `/amd64` jne.; `#/` antaa vain ne liitoskohdat, joita
   juuressa ei ole (`/dev`, `/env`, `/proc`, `/srv`, ...).
3. Ympäristöön `cputype`, `service` (terminal tai cpu), `terminal` sekä
   plan9.ini:n muuttujat. `rootdir`ia ei ole: juuri on mount, ei polku.
4. `/srv/gefs.root` on juuren 9P (eve:n; muut puut: `mount /srv/gefs.root
   /n/adm adm`) ja `/srv/gefs.root.cmd` gefs:n konsoli, jonka kautta
   `fshalt` ajaa juuren alas (`halt`).

Tiedostopalvelin tarkistaa oikeudet attachin käyttäjän mukaan, ja juuri
on attachattu kerran hostownerina: kaikki sen nimiavaruuden perivät
prosessit näkevät juuren hostownerin oikeuksin (kuten 9frontin
terminaalissa). Toisen käyttäjän attach: `mount /srv/gefs.root ...`
(hostownerin avaama fd).

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
`ktest` (plan9.ini `ktest=...`) on esimerkki.

## Alasajo

`fshalt` kirjoittaa `halt` `/srv/gefs.root.cmd`:hen: gefs synkkaa
(arenaotsakkeet, superlohkot, levyn kirjoitusvälimuisti tyhjäksi) ja jää
vain luku -tilaan. Reboot (`#c/reboot`) tekee saman. Ilman niitä data on
levyllä viimeisimmän 5 s välein tehdyn syncin verran, ja gefs palautuu
siihen (`docs/gefs.md`).

## Mitä poistui (bootfs.paq)

Kernelin `bootdir` (bootfs.paq, paqfs, factotum, `/boot/boot`) ja repon
boot-ympäristö (`sys/src/9/boot/`: bootrc, ext4.rc, net.rc, reimage.rc,
devsd.proto, devext4.proto; `sys/src/cmd/aux/reimage.c`) poistettiin;
ne ovat historiassa (commit ennen poistoa). Kernel pieneni 5 056 448:sta
1 933 141 tavuun (size).

Lakkasivat toimimasta tällä kernelillä, kunnes ne siirtyvät `root=`:aan
ja gefs-juureen (Monolith): Monolithin CPU-VM (`tools/vm-cpu`), pilvikuva,
asennin (`inst/bootsetup` kirjoittaa `bootargs=local!...`), tämän repon
`tools/wgtest.sh` ja `tools/vm-install` (cwfs-juuri bootrc:n kautta).
Käännös-VM (`tools/vm`, `tools/build.sh`) käyttää 9frontin omaa kerneliä
eikä muutu.
