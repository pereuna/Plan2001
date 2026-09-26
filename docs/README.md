# Plan2001

Moderni, UEFI boot-polku 9frontille (Plan 9 -jatkokehitys) x86-64:llä.
Tavoite: käyttää UEFI-palveluita mahdollisimman pitkään ennen `ExitBootServices`ia
ja poistaa BIOS-ajan legacy-koodi, joka ei ole enää tarpeen puhtaalla UEFI-koneella.

Tilanne ja suunnitelma: **`docs/status.md`**. Lue se ensin.

## Miten tämä repo on rakennettu

`sys/`-hakemistossa on **vain ne tiedostot, joita tämä projekti on oikeasti
kirjoittanut tai muokannut**, samoissa suhteellisissa poluissa kuin 9frontin omassa
lähdepuussa (`sys/src/9/pc64/l.s` vastaa upstreamin `/sys/src/9/pc64/l.s`:ää).
Kaikki muu — kääntäjä, kirjastot, ja jokainen tiedosto, jota emme ole koskeneet —
haetaan tuoreena käännöskoneen omasta `/sys/src`-puusta joka käännöksellä
(`tools/build.rc`). Tämä on tarkoituksellinen valinta: aiempi versio piti kopiota
koko boot-polun 120+ tiedostosta repossa, mikä vanheni hiljaa käännöskoneen
todellisesta tilasta ja teki koodihausta epäluotettavaa. Nyt jokainen `git grep`
tässä repossa löytää vain oikeasti relevanttia koodia.

Seurauksena: **tämä repo ei käänny yksinään.** Kääntäminen tehdään aina
9front-VM:ssä (ks. alla), jonka `/sys/src` toimii pohjana.

`subset/` on eri asia: **koskematon kopio** 9front-11952:n asennusaikaisesta
osajoukosta (`subset/9front/`: asennusohjelma, sen ohjelmien lähteet,
kirjastot, kernel ja UEFI-loader, ei BIOS/ISO-legacyä), sen proto-tiedosto,
josta USB-asennustikku rakennetaan, ja tiedostolista. Kopio on
johdettu koneellisesti, ja sitä verrataan VM:n puuhun md5:llä
(`tools/subset/check`). Sitä ei muokata, vaan omat muutokset tehdään `sys/`-hakemistoon. Ks.
**`docs/install-subset.md`**.

Historiallinen dokumentti `docs/upstream-scope-manifest.md` kuvaa alkuperäisen
rajauksen (mitkä tiedostot boot-polku koskettaa ja miksi) siltä ajalta, kun repo
vielä peilasi koko tiedostojoukkoa. Osa siinä mainituista funktioista on jo
poistettu — ks. `docs/status.md`.

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
tools/build.sh           # kääntää 9pc64 + bootx64.efi -> build/amd64/
tools/test-qemu.sh       # käynnistää tuloksen QEMU:ssa, PASS/FAIL sarjalokista
```

**Kohteet:** jokainen työkalu ottaa `TARGET`-muuttujan (oletus `amd64`). Kohteet
kuvataan tiedostoissa `tools/targets/{amd64,arm64,riscv64}`: kernelin hakemisto,
konfiguraatio ja nimi, lähdehakemistot, loader ja sen ESP-nimi, QEMU ja firmware.
Kuvaukset ovat samaa syntaksia bashille, rc:lle ja Pythonille. Vain `amd64` on
tuettu. `arm64` on suunniteltu (vaihe 4) ja `riscv64` tuleva, ja työkalut
kieltäytyvät niistä selvällä viestillä. Tulokset menevät hakemistoon
`build/$TARGET/` ja osajoukko hakemistoon `subset/$TARGET/`, ja `subset/9front/`
on kaikkien kohteiden yhteinen kopio.

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
  9frontin ISOa (käännös-VM), `tools/subset/test` Plan2001:n USB-tikkua.
- `tools/subset/mkusb` rakentaa Plan2001:n **USB-asennustikun** (vain UEFI,
  Plan2001:n loader ja kernel): `build/subset/amd64/plan2001-inst.img`, joka
  kirjoitetaan tikulle `dd`:llä. Ks. `docs/install-subset.md`.
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
                      └─ exec("/boot/boot")
```

## AI/agentille

- `docs/status.md`: mitä on tehty, mitä seuraavaksi.
- `docs/plan-linux-env.md`: kehitysympäristön suunnitelma. Vaiheet 1 (Debian 13,
  sarjakonsoliohjaus) ja 2 (asennusaikainen osajoukko, `docs/install-subset.md`)
  on tehty. Seuraavaksi voidaan alkaa muokata asennusohjelmaa.
- `git log --oneline`: jokainen commit on itsenäinen, testattu askel.
- Älä oleta paikallista lähdepuun kopiota olevan täydellinen — se EI ole,
  tarkoituksella (ks. yllä). Muita tiedostoja luetaan VM:stä:
  `tools/vm start; tools/9run 'cat /sys/src/...'; tools/vm stop`.
