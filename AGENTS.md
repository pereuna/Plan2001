# Ohje koodiagenteille (Codex, Claude): testaus ennen pushia

Tämä repo on Plan2001: 9frontin UEFI-fork kolmelle raudalle (AMD64,
ARM64, RISC-V64), sen muokattu kernel ja kernelin kehitystyökalut.
Repon juuri on Plan 9 -juuri, ja asennettu juuri on sen klooni, joka
kääntää itsensä (`docs/selfhost.md`). Lue ensin `docs/architecture.md`,
`docs/selfhost.md` ja `docs/repos.md` (päätös 16.10.2026: sovellukset,
jakelu, pilvi ja wasm32 ovat Monolithissa).

## Säännöt

- **Vain kernel ja sen kehitystyökalut.** Kernel, loader, työkaluketju
  (kääntäjät, mk, rc, kirjastot), kernelin testaus ja profilointi sekä
  se, millä juuri hakee lähteensä (git, webfs, ipconfig, ndb). Muut
  käyttäjätason ohjelmat, 9frontin julkaisukopio, asennin ja pilvi
  kuuluvat Monolithiin (`MONOLITH`, oletus `~/Src/monolith`). Kaikki
  ylimääräinen pois kontekstista. Poikkeus: hjfs:n `auth.c`
  (Monolithin asennin).
- **Ei 9front-yhteensopiva.** Koodia, ideoita ja protokollia kopioidaan
  9frontista ja muokataan vapaasti. Legacy jää pois.
- **9frontin tiedostot** ovat puussa samalla polulla kuin 9frontissa, ja
  niitä muokataan suoraan. Uusi 9frontin tiedosto tai hakemisto:
  `tools/9front.import`-listaan ja `tools/import9front` (vain lisää).
  Poistettu tiedosto `!`-riviksi listaan, ettei import tuo sitä takaisin.
  Kerro commitissa lähde (repo ja commit tai julkaisu).
- Tee työ omassa haarassasi; älä pushaa toisen haaraan.

## Puu

- `sys/src/9/`: kernel (`port`, `pc`, `pc64`, `arm64`, `ip`, `gefs`).
- `sys/src/boot/efi/`: UEFI-loader.
- `sys/include/bootinfo.h`: Boot ABI.
- `sys/src/lib*`, `sys/src/cmd`: kirjastot ja komennot (`sys/src/mkfile`).
- `init`: juuren init (`docs/init.md`).
- `tools/overlay DIR` antaa repon Plan 9 -puun (ilman `tools/`:ia ja
  `docs/`:ia), jonka VM:n käännös (`tools/build.sh`) laittaa VM:n puun
  tilalle; `-k` vain kernelin puun (Monolith).
- `tools/targets/{amd64,arm64,riscv64}`: kohteet (`TARGET`, oletus amd64).

## Ympäristö

- Linux, Python 3, QEMU, OVMF ja KVM. 9front-VM:n tekee `tools/vm-setup`
  (`~/.cache/plan2001/base.qcow2`).
- Ilman VM:ää käännös ja testit ohitetaan; kerro silloin commitissa,
  mitä jäi ajamatta.

## Rakennus ja testit

```
tools/build.sh           # puu, kernel ja loader VM:ssä -> build/$TARGET/ (WHAT=kernel: nopea)
tools/selfroot DIR|-p DEV  # juuri: HEADin klooni + build/$TARGET/root:n binäärit
tools/test-selfroot.sh   # juuri QEMU:ssa: verkko, git/clone, mk install, kernel, fshalt
tools/test-qemu.sh       # kernel init0:aan asti QEMU:ssa UEFI:llä (ei root=)
tools/test-gefs.sh       # gefs-juuri QEMU:ssa: BUILD=1, CRASH=N, KTEST=all (docs/gefs.md)
tools/gefsroot DIR OUT   # juuripuu gefs-kuvaksi käännös-VM:ssä (gefsdisk: levy, gefsck: tarkistus)
tools/wgtest.sh          # WireGuard (#W) Linuxin wg:tä vastaan (Monolithin CPU-VM)
tools/rccheck            # rc-skriptien jäsennys (PLAN9: plan9portin rc)
```

- `tools/9pterm -S SOCK -T 900 -c 'rc /mnt/term$PWD/tools/kbuild9p.rc /mnt/term$PWD'`
  kääntää kernelin CPU-palvelimella. 9pterm on Monolithin, ja
  `tools/9pterm` kääntää sen tarvittaessa.
- `tools/rccheck` tarvitsee plan9portin rc:n (`PLAN9`). Aja se, kun
  muutat rc-skriptiä (`tools/*.rc`). Heittomerkki
  viestissä, kuten `Plan2001's`, avaa lainauksen.
- Monolith käyttää tämän repon VM-työkaluja, kerneliä (`build/amd64`) ja
  BootInfoa. Jos muutat `bootinfo.h`:ta, `port/bootinfo.c`:tä,
  `bootargs.c`:tä tai VM-työkalujen käyttöliittymää, aja Monolithin
  `monolith/tools/build-9wasm32` tai kerro commitissa, että se jäi
  ajamatta.

## Sudenkuopat

- Pysyvä VM-levy (`cpu.qcow2`) suljetaan `fshalt`illa. `tools/vm stop`
  tekee sen itse (`NOHALT=1` ohittaa). Pelkkä QEMU:n quit jätti cwfs:n
  kirjoittamatta, eikä levy enää mountannut.
- Älä vaihda nvramin avainta (`auth/wrkey`) ilman `auth/convkeys
  /adm/keys`:ia: keyfs ei silloin enää avaa käyttäjiä.
- Älä käytä `pkill -f`:ää komentorivillä, joka sisältää saman
  merkkijonon (tappaa oman kuoresi).
- rc-komento bashista (`tools/9run`): bashin `'...'` syö rc:n
  heittomerkit (`cat '#c/sysname'` → kommentti, cat jää lukemaan). Käytä
  bashin `"..."`.
- 9frontin git ei lue `packed-refs`iä eikä Linuxin indeksiä:
  `tools/selfroot` kirjoittaa irtoviitteet ja `.git/INDEX9`.

## Commit

- Viesti kertoo mitä ja miksi, repon tyyliin (`git log`).
- Kerro, mitkä testit ajoit ja mitkä jäivät ajamatta (VM, rauta).
- `docs/status.md` ajan tasalle, kun vaihe etenee.
