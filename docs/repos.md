# Plan2001, Monolith ja Plan9-wasm32 (päätös 16.10.2026)

## Jako

| Repo | Mikä |
|---|---|
| **Plan2001** (tämä) | 9frontin UEFI-fork kolmelle raudalle: AMD64, ARM64 ja RISC-V64. Muokattu uusi kernel ja UEFI-loader, kernelin kehitystyökalut (työkaluketju, testaus, profilointi, git) ja niiden käännös ja testaus (9front-VM, QEMU, oikea rauta). Repo on Plan 9 -juuri, joka kääntää itsensä (`docs/selfhost.md`, 17.10.2026) |
| **Monolith** (`~/Src/monolith`) | Plan2001:n sovellukset ja jakelu: 9frontin julkaisukopio (`subset/9front`) ja USB-asennin, rc-httpd:n sivusto, laskentapooli (crsrv, rcc), glendan profiili, CPU-VM (`tools/vm-cpu`), pilven työkalut, drawterm ja 9pterm sekä Plan2001:n entinen wasm32-kone ja selainpalvelut |
| **Plan9-wasm32** ([pereuna/plan9-wasm32](https://github.com/pereuna/plan9-wasm32)) | Plan 9 selaimessa, 9front-yhteensopiva |

Monolith sai siirretyt tiedostot git-historioineen. Viimeinen Plan2001:n
commit, jossa ne kaikki olivat mukana, on `f399452` (`main`, nyt
`pereuna/Plan2001-history`: Plan2001 aloitti uuden historian 17.10.2026).

Pilveen (Oracle) asennetaan toistaiseksi vakio-OpenBSD tai -Linux
(päätös 16.10.2026). Sen asennus- ja ylläpitotyökalut tehdään
Monolithiin, kuten Plan 9 -pilvipalvelimen vanhat työkalut
(`tools/cloud`, `cloud-image`, `cpu-live`), jotka ovat siellä
historiana ja sovellustasona. Plan2001:ssä ei ole pilven työkaluja.

## Mitä Plan2001:ssä on

- Repon juuri on Plan 9 -juuri (17.10.2026, `docs/selfhost.md`):
  `sys/src/lib*` ja `sys/src/cmd` ovat työkaluketju ja kehitystyökalut
  (`tools/9front.import`), `init` juuren init.
- `sys/src/9/`: kernel (`port`, `pc`, `pc64`, `arm64`, `ip`,
  `ext4`). Boot-ympäristöä (bootfs.paq, bootrc, reimage) ei ole
  16.10.2026 jälkeen: kernel mounttaa juuren ja ajaa sen initin
  (`docs/init.md`).
- `sys/src/boot/efi/`: UEFI-loader.
- `sys/include/bootinfo.h`: Boot ABI v1 (`docs/boot-abi.md`).
  `BootArchWasm32` jää ABI:in, koska Monolithin wasm32-ydin käyttää sitä.
- `aux/kbdfs` (rivieditori; `vga.c`:n konsoli nojaa siihen; juuren
  `/init` käynnistää sen) ja hjfs:n korjattu `auth.c` (`build/amd64/hjfs`
  Monolithin asentimelle). Monolith ottaa kernelin puun `tools/overlay -k`:lla.
- `tools/`: `build.sh`/`build.rc` (VM), `kbuild9p.rc` (CPU-palvelin),
  `test-qemu.sh`, `vm`, `vm-setup`, `vm-install`, `vm-target`, `9run`,
  `hw`, `targets/`, `overlay`, `rccheck`, `wgtest.sh` ja `9pterm`.

## Riippuvuudet repojen välillä

- **Monolith → Plan2001** (`PLAN2001`, oletus `~/Plan2001`):
  - VM-työkalut ja niiden `build/` (`vm`, `9run`, `target.sh`,
    `inst.dialog`, `hw`): CPU-VM, osajoukon johto ja USB-tikku;
  - kernel ja loader (`build/amd64`);
  - wasm32-ytimen BootInfo (`bootinfo.h`, `port/bootinfo.c`,
    `bootargs.c`) ja hjfs:n `auth.c`.
- **Plan2001 → Monolith** (`MONOLITH`, oletus `~/Src/monolith`):
  - `tools/9pterm` (`kbuild9p.rc`-istunnot, `wgtest.sh`);
  - `wgtest.sh`:n CPU-VM (Monolithin `tools/vm-cpu`).

## Siirrettävä Monolithiin

Haarassa `ccr-155e0997-7l7ykh` tehtyä työtä, joka kuuluu jaon jälkeen
Monolithiin. Se poistui Plan2001:stä haaran mergessä mainin kanssa, ja
Monolith ottaa sen Plan2001:n historiasta:

- `dba4f12`: `tools/cloud` (ylläpitokoneiden WireGuard-vertaiset
  `wg-peers`, `wg-admin init`, `peers`, `peers.rc`; plan2001.com
  Plan9-wasm32:n demona: `site`, `site.rc`, `rc-httpd/select-handler`)
  ja `docs/cloud.md`.
- `7fbfdfd`: SharedWorker-kokeilu (`docs/web-windows.md`,
  `monolith/tools/probe-windows`).
- `docs/legacy.md` kohdat 4–7 (userland) versiossa `3432a31`:
  Monolithin `docs/legacy-userland.md`.

## Historia

- 5.10.2026: wasm32-kone omaksi projektikseen Plan9-wasm32.
- 7.10.2026: Plan2001 ei ole 9front-yhteensopiva; puu yhdistettiin
  `plan2001/`:een.
- 16.10.2026: wasm32 ja selainpalvelut Monolithiin, sitten myös
  sovellukset, jakelu ja pilvi. Plan2001 on kernel-repo (tämä dokumentti).
