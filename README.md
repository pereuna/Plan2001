# Plan2001

Moderni, UEFI-vetoinen boot-polku 9frontille (Plan 9 -jatkokehitys) x86-64:llä.
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

Seurauksena: **tämä repo ei käänny yksinään.** Kääntäminen vaatii aina yhteyden
oikeaan 9front-koneeseen (ks. alla), jonka `/sys/src` toimii pohjana.

Historiallinen dokumentti `docs/upstream-scope-manifest.md` kuvaa alkuperäisen
rajauksen (mitkä tiedostot boot-polku koskettaa ja miksi) siltä ajalta, kun repo
vielä peilasi koko tiedostojoukkoa. Osa siinä mainituista funktioista on jo
poistettu — ks. `docs/status.md`.

## Muutetut/uudet tiedostot

| Tiedosto | Mikä |
|---|---|
| `sys/include/bootinfo.h` | Loaderin kernelille välittämä rakenne (uusi) |
| `sys/src/9/pc/bootinfo.c` | Kernel: `BootInfo`n luku, RNG/kello-kytkennät (uusi) |
| `sys/src/9/pc/bootfb.c` | Kernel: boot-vaiheiden merkit UEFI-framebufferiin (uusi) |
| `sys/src/9/pc/bootargs.c` | plan9.ini-jäsennys; `*acpi`/`*bootscreen` `BootInfo`sta |
| `sys/src/9/pc/memory.c` | Muistikartta `BootInfo`sta, BootServices-muisti vapaaksi |
| `sys/src/9/pc64/l.s` | `_efi64`-sisäänmeno (ent. `_protected`+Multiboot+32-bit) |
| `sys/src/9/pc64/main.c` | `bootmark`/`bootinfo*`-kutsut boot-järjestyksessä |
| `sys/src/9/pc64/trap.c` | boot-merkki paniikista ja ensimmäisestä `exec`istä |
| `sys/src/9/pc64/mem.h` | `BOOTINFO`-osoite |
| `sys/src/9/pc64/fns.h` | uusien funktioiden prototyypit |
| `sys/src/9/pc64/pc64` | kernelin konfiguraatio (`bootinfo`, `bootfb` mukaan) |
| `sys/src/boot/efi/*` | loader: `_efi64`-hyppy, `BootInfo`n kokoaminen, RNG/TSC/RTC |

## Käännös ja testaus

Tarvitset ajossa olevan 9front-VM:n cpu-palvelimena (ks. alla) ja `drawterm`in
(`~/.local/bin/drawterm`, salasana `~/.9front-pass`).

```
tools/build.sh          # kääntää 9pc64 + bootx64.efi -> build/
tools/test-qemu.sh       # käynnistää tuloksen QEMU:ssa, tarkistaa bootfb-merkit
```

`tools/build.sh` löytää käännöskoneen automaattisesti (`ip route`, Windows-VM
oletuksena). `NINE_HOST=127.0.0.1` pakottaa WSL:n oman QEMU:n
(`tools/vm/wsl-run.sh`), jota tarvitaan vain silloin kun debuggaus vaatii
`-d int,cpu_reset`-tyylistä QEMU-jäljitystä — sitä WHPX (Windows) ei tue.

### VM

- **Ensisijainen: Windows-QEMU (WHPX).** `tools/vm/run.cmd` käynnistää levyn
  `C:\VM\9front\9front.qcow2`. Nopea (laitteistokiihdytetty).
- **Varalla: WSL-QEMU (TCG).** `tools/vm/wsl-run.sh` ajaa `~/vm9front/9front.qcow2`.
  Hidas mutta tukee QEMU:n omia debug-lippuja.
- Molemmissa: käyttäjä `glenda`, salasana `~/.9front-pass`-tiedostossa.
  Yhdistä aina `-a 127.0.0.1 -s 127.0.0.1` (drawterm soittaa muuten auth-
  ja secstore-palvelimelle, joka roikkuu jos sitä ei ole paikallisesti).
- **rcpu kaatuu ajoittain** (tunnettu 9front/QEMU-kummallisuus, ei tämän
  projektin bugi). Jos yhteys ei vastaa: tarkista QEMU:n konsoli
  (`screendump` monitorin kautta) — järjestelmä on yleensä elossa, vain
  kuuntelija jumissa. Käynnistä listener uudelleen tai `fshalt -r`
  näppäinsyötöllä (`sendkey`) monitorin kautta.

## Ohjelmisto-arkkitehtuuri

```
UEFI firmware
  └─ bootx64.efi (sys/src/boot/efi/)
       kokoaa BootInfo:n (muistikartta, ACPI, framebuffer, RNG, TSC, RTC)
       └─ ExitBootServices
            └─ _efi64 (sys/src/9/pc64/l.s)     ← ainoa sisäänmeno, long mode koko ajan
                 └─ main() (pc64/main.c)
                      bootinfoinit → ... → bootinforandinit → ... → bootinfoclock
                      └─ exec("/boot/boot")
```

## AI/agentille

- `docs/status.md`: mitä on tehty, mitä seuraavaksi.
- `git log --oneline`: jokainen commit on itsenäinen, testattu askel.
- Älä oleta paikallista lähdepuun kopiota olevan täydellinen — se EI ole,
  tarkoituksella (ks. yllä). Käytä `tools/build.sh`/`tools/build.rc`-mallia
  minkä tahansa muun tiedoston lukemiseen käännöskoneelta.
