# Itseään kääntävä Plan2001 (17.10.2026)

Plan2001 ei ole pelkkä kernel ja mkfilet vaan kernel ja sen
kehitystyökalut, kuten OpenBSD ja Plan 9: juuri hakee lähteensä gitillä
ja kääntää itsensä. Kaikki muu on Monolithin jakelua. Tavoite on
tekoälylle sopiva järjestelmä, jonka kontekstissa ei ole mitään
ylimääräistä: kernel kolmelle arkkitehtuurille (AMD64, ARM64, RISC-V64),
sen käännöstyökalut sekä kernelin testaus ja profilointi.

## Repo on juuri

Repon juuri on Plan 9 -juuri, kuten 9frontin omassa repossa:

```
sys/src/9/{port,pc,pc64,ip,ext4,arm64}   kernel
sys/src/boot/efi                         UEFI-loader
sys/src/lib*                             kirjastot (kernelin, työkaluketjun, komentojen)
sys/src/cmd                              kääntäjät, mk, rc, git, verkko, testit
sys/include, amd64/, arm64/              otsakkeet ja mk-määrittelyt
rc/, lib/ndb, sys/lib                    rc:n kirjasto ja skriptit, ndb, yaccpar, acid, git, kbmap
init, etc/group                          juuren init (docs/init.md) ja omistajien nimet
docs/, tools/                            Linux-isännän puoli (käännös-VM, QEMU, testit)
```

Asennettu juuri on tämän repon klooni (origin GitHub) ja käännetyt
binäärit (`amd64/bin`, `amd64/lib`; `.gitignore`). `root=` ilman
alihakemistoa.

## Mitä 9frontista on otettu

`tools/9front.import` luettelee sen (9front-11952, `tools/9front.release`),
ja `tools/import9front` lisää puuttuvat tiedostot ISOsta ja
`sys/include`:n otsakkeet, joihin lähteet viittaavat (ja poistaa ne,
joihin ei). Plan2001:n omia tai muokkaamia tiedostoja se ei kosketa.

- Kirjastot: libc, libbio, libString, libregexp, libmach (vain amd64 ja
  arm64), libmp, libsec, libflate, libauth(srv), libndb, libip,
  libthread, lib9p sekä kernelin libdraw, libmemdraw, libmemlayer,
  libaml ja libfis.
- Työkaluketju: cc, 6a/6c/6l (amd64), 7a/7c/7l (arm64), yacc, mpc, ar,
  nm, size, strip, mk, rc, awk, sed, grep, diff, patch ja peruskomennot.
  RISC-V64:lle 9frontissa ei ole kääntäjää.
- Kernelin testaus ja profilointi: ktest, acid, kprof, tprof, ktrace,
  syscall.
- Lähteet verkosta: git, webfs (https), ip/ipconfig, ip/ping, ndb/cs,
  ndb/dns, hget.
- Konsoli: aux/kbdfs (Plan2001:n rivieditori), aux/aout2efi (loader).

Kernelin lähteistä karsittiin se, mitä pc64 (AMD64) tai arm64:n qemu
eivät käännä: ISA- ja vanhat PCI-verkkokortit, levyke, PCMCIA,
äänikortit, VGA-ohjaimet (vain vgasoft ja GOP), 32-bittisen pc-kernelin
tiedostot, dtracy. Loaderista PXE ja ISO9660 sekä ia32.

## Käännös

Bootstrap on 9front-VM:n kääntäjät (`tools/build.sh`): repon puu
korvaa VM:n `/sys/src`:n, `/sys/include`:n ja mk-määrittelyt, ja
`mk install` (`sys/src/mkfile`: kirjastot, sitten komennot) asentaa
kopioon VM:n `/amd64/bin`:stä. Ensin käännetyt kirjastot, kääntäjät, mk
ja rc kääntävät loput. Lopuksi kernel ja loader. Tulokset:
`build/amd64/root/amd64/{bin,lib}`, `9pc64`, `bootx64.efi`.
`PASSES=2` kääntää puun toiseen kertaan omilla työkaluillaan ja vertaa
ohjelmia. `WHAT=kernel` on nopea kernelin käännös VM:n kirjastoilla.

Asennetussa juuressa:

```
cd / && git/pull
cd /sys/src && mk install          kirjastot, kääntäjät, komennot
cd /sys/src/9/pc64 && mk           kernel
cd /sys/src/boot/efi && mk bootx64.efi
```

## Juuren kokoaminen ja testi

- `tools/selfroot DIR`: HEADin klooni (origin GitHub), 9frontin gitin
  `.git/INDEX9` ja irtoviitteet (9frontin git ei lue `packed-refs`iä),
  binäärit `build/amd64/root`:sta, `/tmp` ja `/usr/glenda`.
- `tools/selfroot -p /dev/nvme0n1p5`: sama osiolle (sudo); vanha sisältö
  pois, `lost+found`, label ja UUID säilyvät.
- `tools/test-selfroot.sh`: juuri ext4-levynä QEMU:ssa virtio-verkolla
  (QEMU:n user-verkko): DHCP, DNS, `hget https://...`, `git/clone`
  GitHubista, `mk install` omilla työkaluilla, kernel ja loader, `fshalt`
  ja Linuxin `e2fsck`.

## /init

Repon `/init` (docs/init.md): laitteet, kbdfs konsoliksi, mntgen (`/n`,
`/mnt`), verkko, jos on `ether0` (`ip/ipconfig` DHCP:llä tai plan9.ini
`ip=...`, ndb/cs, ndb/dns -r, webfs; `ip=none` ohittaa), `ktest=...`,
sitten rc (`$home/lib/profile`, jos on).

## Seuraavaksi

- Silli: verkko. Langallinen I219-LM (ether82563) toimii DHCP:llä, jos
  kaapeli on kiinni; WLAN (Intel 8265, etheriwl) tarvitsee firmwaren ja
  aux/wpa:n.
- Kernelin asennus juuresta: ESP on FAT (dossrv ja `EFI/plan2001`).
- Etäkäyttö AI:lle (rcpu/exportfs tai oma), man-sivut kernelin osalta.
- arm64-kernel `root=`-malliin (bootmkfile ja boot-ympäristö poistuivat).
