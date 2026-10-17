# Plan2001

Moderni, UEFI boot-polku 9frontille (Plan 9 -jatkokehitys) x86-64:llä.
Tavoite: käyttää UEFI-palveluita mahdollisimman pitkään ennen `ExitBootServices`ia
ja poistaa BIOS-ajan legacy-koodi, joka ei ole enää tarpeen puhtaalla UEFI-koneella.

Tilanne ja suunnitelma: **`docs/status.md`**. Lue se ensin.

Plan2001 ei ole 9front-yhteensopiva, ja se on nyt kernel-repo: kernel,
UEFI-loader ja niiden käännös ja testaus (**`docs/architecture.md`**).
Sovellukset, jakelu (9frontin osajoukko ja USB-asennin), pilvi ja wasm32
siirrettiin 16.10.2026 Monolithiin (**`docs/repos.md`**). Alla oleva
UEFI-boot-kuvaus on sitä edeltävää historiaa.

AI-etäkäytön, 9P:n, 2001P:n ja drawtermin suunnittelukeskustelu ja
Codex CLI:n jatkomuistio ovat Monolithissa (`docs/ai/2026-09-30`).

## Miten tämä repo on rakennettu

17.10.2026 alkaen repo on Plan 9 -juuri (`sys/`, `rc/`, `lib/`, `amd64/`,
`init`), ja asennettu juuri on sen klooni, joka kääntää itsensä
(**`docs/selfhost.md`**). 9frontista otetut tiedostot (`tools/9front.import`)
ovat samoilla poluilla kuin 9frontissa, ja niitä muokataan suoraan. Repossa
on vain kernel ja sen kehitystyökalut; kaikki muu on Monolithin.

Käännöksen bootstrap on 9front-VM (`tools/build.sh`): sen kääntäjät
kääntävät repon puun, jonka omat työkalut kääntävät loput. Asennetussa
juuressa `cd /sys/src && mk install`.

Aiemmin (16.10.2026 asti) repossa olivat vain muutetut tiedostot
`plan2001/`:n alla, ja muu otettiin käännös-VM:n `/sys/src`:stä.

## Muutetut/uudet tiedostot

| Tiedosto | Mikä |
|---|---|
| `sys/include/bootinfo.h` | **Plan2001 Boot ABI v1, data-osa**: BootInfo-blob, sama kaikille ISA:ille (uusi, ks. `docs/boot-abi.md`; AMD64-entry `docs/boot-abi-amd64.md`) |
| `sys/src/9/port/bootinfo.c` | Kernel, ISA-riippumaton: blobin validointi, muistikartan luokat, RNG/kello-kytkennät (uusi) |
| `sys/src/9/pc64/bootarch.c` | Kernel, AMD64:n hookit: `bootearlymap()` (blob `BOOTMAPVA`:han) ja `fbmap()` (PAT WC) (uusi) |
| `sys/src/9/port/bootfb.c` | Kernel, ISA-riippumaton: boot-vaiheiden merkit UEFI-framebufferiin, lokin toisto (uusi) |
| `sys/src/9/port/bootargs.c` | ISA-riippumaton plan9.ini-jäsennys blobin config-osiosta; `*acpi`/`*bootscreen` `BootInfo`sta (siirretty pc/:stä) |
| `sys/src/9/pc/memory.c` | Muistikartta blobista, blobin varaus, BootServices-muisti vapaaksi |
| `sys/src/9/pc/screen.c` | GOP-framebufferin tarkka osoite, näkyvä leveys ja stride erillään |
| `sys/src/9/pc/vga.c` | Konsoli: ei splash-laatikkoa, kolme saraketta scrollauksen sijaan, toistaa loaderin tekstin; ESC[2J vaihtaa interaktiiviseen tilaan (yksi sarake, ohjepalkki), ESC[nC/nD/K kursorinsiirto |
| `sys/src/cmd/aux/kbdfs/kbdfs.c` | Rivieditori `/dev/cons`iin: nuolet, Home/End, Del, Shift+Home/End/←/→ leikkaus, ^W, ^V liitä, historia |
| `sys/src/9/port/devcons.c` | Ainoa muutos: ESC-sekvenssit eivät mene kmesgiin |
| `sys/src/9/pc64/l.s` | `_efi64`-sisäänmeno (ent. `_protected`+Multiboot+32-bit) |
| `sys/src/9/pc64/main.c` | `bootmark`/`bootinfo*`-kutsut boot-järjestyksessä |
| `sys/src/9/pc64/trap.c` | boot-merkki paniikista ja ensimmäisestä `exec`istä |
| `sys/src/9/pc64/mem.h` | kiinteät boot-osoitteet (`CONFADDR`, `BOOTINFO`) poistettu |
| `sys/src/9/pc64/fns.h` | uusien funktioiden prototyypit |
| `sys/src/9/pc64/pc64` | kernelin konfiguraatio (`bootinfo`, `bootfb` mukaan) |
| `sys/src/boot/efi/*` | loader: yhteinen osa (`efi.c`, `sub.c`: BootInfo-blob, config, loki, muistikartta, RNG/RTC) ja AMD64-osa `archx64.c` (TSC, entry-tarkistukset, hyppy RDI = blob) |

## Käännös ja testaus

Isäntä on Debian 13 (x86-64, KVM). Kerran:

```
sudo apt install qemu-system-x86 qemu-utils ovmf mtools curl
sudo adduser $USER kvm      # ja uudelleenkirjautuminen
tools/vm-setup              # ISO välimuistiin + automaattinen asennus base.qcow2:een
```

Joka kerta:

```
tools/build.sh           # puu, 9pc64 ja bootx64.efi -> build/amd64/ (WHAT=kernel: nopea)
tools/test-selfroot.sh   # itseään kääntävä juuri QEMU:ssa (docs/selfhost.md)
tools/test-qemu.sh       # käynnistää tuloksen QEMU:ssa, PASS/FAIL sarjalokista
tools/9pterm -S SOCK -T 900 -c 'rc /mnt/term$PWD/tools/kbuild9p.rc /mnt/term$PWD'   # ydin CPU-palvelimella 9pterm-istunnolla (~7 s)
tools/wgtest.sh          # WireGuard (#W) Linuxin wg:tä vastaan, docs/wireguard.md
```

**Kohteet:** jokainen työkalu ottaa `TARGET`-muuttujan (oletus `amd64`). Kohteet
kuvataan tiedostoissa `tools/targets/{amd64,arm64,riscv64}`: kernelin hakemisto,
konfiguraatio ja nimi, lähdehakemistot, loader ja sen ESP-nimi, QEMU ja firmware.
Kuvaukset ovat samaa syntaksia bashille, rc:lle ja Pythonille. Vain `amd64` on
tuettu. `arm64` on suunniteltu (vaihe 4) ja `riscv64` tuleva, ja työkalut
kieltäytyvät niistä selvällä viestillä. Tulokset menevät hakemistoon
`build/$TARGET/`.

`test-qemu.sh` jättää ESP:n FAT-imageksi `build/amd64/esp.img` (suoraan `dd`:llä
USB-tikulle). `DISPLAY_QEMU=1` näyttää ruudun GTK-ikkunassa.

### VM

- Kaikki ohjaus kulkee **sarjakonsolin tekstinä** (`console=0`); ei näyttöä,
  ei verkkoa eikä etäyhteyksiä. Ruutukaappauksia ei käytetä.
- Välimuisti `${PLAN2001_CACHE:-~/.cache/plan2001}`: 9front-ISO
  (`tools/9front.release`: versio + sha256) ja `base.qcow2`. `vm-setup` ei lataa
  eikä asenna uudelleen, jos ne ovat jo olemassa.
- `tools/vm start|stop|status`: käynnistää kertakäyttöisen overlayn
  `base.qcow2`:n päälle (`KEEP=1 tools/vm stop` säilyttää sen). Levyt:
  `sdE0` järjestelmä, `sdE1` = `build/in.img` (raaka tar, lähteet sisään),
  `sdE2` = `build/out.img` (raaka tar, tulokset ulos). FATia ei käytetä, koska
  se ei hyväksy hakemistoa `aux` (varattu DOS-laitenimi).
- `tools/vm-install [--usb] MEDIA LEVY` asentaa 9frontin levylle ilman käsin
  tehtyjä askelia (vastaukset: `tools/inst.dialog`). `vm-setup` käyttää
  9frontin ISOa (käännös-VM), Monolithin `tools/subset/test` Plan2001:n
  USB-tikkua (Monolithin `tools/subset/mkusb`).
- `tools/9run 'rc-komento'` ajaa komennon VM:ssä ja palauttaa sen `$status`in
  (0/1, aikakatkaisu 124). `tools/9run --dialog` vastaa kehotteisiin
  sääntötiedoston mukaan (asennus ja boot, ks. `tools/vm-setup`).
- Lokit: `build/vm/serial.log` (koko sarjaistunto), `build/amd64/session.log`
  (build.rc:n tuloste), `build/amd64/{kernel,loader,kbdfs}.log`.
- Käyttäjä `glenda`, ei salasanaa. Jos `/dev/kvm` ei ole käytettävissä,
  skriptit pysähtyvät virheeseen (ei hidasta TCG-varapolkua).

## Ohjelmisto-arkkitehtuuri

```
UEFI firmware
  └─ bootx64.efi (sys/src/boot/efi/)
       AllocateAnyPages → BootInfo-blob: header | plan9.ini | loki | muistikartta
       └─ ExitBootServices
            └─ _efi64 (sys/src/9/pc64/l.s), RDI = blob   ← Plan2001 Boot ABI v1 (docs/boot-abi.md)
                 └─ main() (pc64/main.c)                    ei kiinteitä boot-osoitteita
                      bootinfoinit (mappaa VMAP+pa) → ... → bootinforandinit → ... → bootinfoclock
                      └─ init0: rootmount (plan9.ini root=, devgefs) → exec juuren init (docs/init.md)
```

## Monolith ja 9pterm (erotettu 16.10.2026)

Sovellukset, jakelu, pilvi, wasm32-kone ja drawterm/9pterm ovat
Monolithissa (`~/Src/monolith`, `docs/repos.md`). Tämän repon työkalut
ajavat 9ptermiä `tools/9pterm`-kääreellä.

## AI/agentille

- `docs/status.md`: mitä on tehty, mitä seuraavaksi.
- `docs/plan-linux-env.md`: kehitysympäristön suunnitelma. Vaiheet 1 (Debian 13,
  sarjakonsoliohjaus) ja 2 (asennusaikainen osajoukko, Monolithin `docs/install-subset.md`)
  on tehty. Seuraavaksi voidaan alkaa muokata asennusohjelmaa.
- `git log --oneline`: jokainen commit on itsenäinen, testattu askel.
- Repon puu on täydellinen: se kääntää itsensä (`docs/selfhost.md`).
  Mitä 9frontista puuttuu tarkoituksella, on `tools/9front.import`issa.
