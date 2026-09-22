> **Historiallinen dokumentti.** Tämä kuvaa alkuperäisen rajauksen, jolla projekti aloitettiin
> (upstream-commit `320dd16`): mitkä tiedostot boot-polku koskettaa ja miksi. Repo ei enää
> peilaa näitä tiedostoja sellaisenaan — `sys/`-hakemistossa on nykyään vain ne tiedostot, joita
> olemme oikeasti muokanneet; loput haetaan aina tuoreena kääntökoneen `/sys/src`:sta
> (ks. `README.md`, `tools/build.rc`). Osa alla mainituista funktioista (`_protected`, Multiboot)
> on jo poistettu — ks. `docs/status.md`.

# 9front x86-64 UEFI -boot: rajattu tiedostojoukko

Lähde: upstream 9front (GitHub-peili), commit `320dd16`, 20.9.2026.  
Alue: `bootx64.efi`:n `start` -> kernel (`9pc64`) -> `main` -> `init0` -> initcode -> `exec("/boot/boot")`. Itse `/boot/boot`-ohjelma (boot.c, bootrc) on rajattu pois.

## Rajauskriteeri

Mukana on tiedosto, jos se
1. määrittelee funktion tai symbolin, joka on tällä polulla (ks. aiempi kutsupolku), tai
2. on lähdetiedoston suoraan tarvitsema otsake, tai build-/generointitiedosto, joka tuottaa polulla käytetyn taulun (`devtab[]`, `links()`, `knownarch[]`, kernelin sisäinen juuri `#/`).

Kernelin build valitsee `pc64/`-version aina kun sellainen on, muuten `pc/`-version (`REPCC`-sääntö `pc64/mkfile`:ssä). Siksi 386-kernelin `pc/l.s`, `pc/main.c`, `pc/mmu.c`, `pc/trap.c` ja `pc/fpu.c` eivät ole mukana.

**83 tiedostoa, yhteensä 33279 riviä.**

## 1 UEFI-lataaja

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `sys/src/boot/efi/efi.c` | 329 | `efimain`, `memconf` (*e820=), `acpiconf` (*acpi=), `screenconf` (*bootscreen=), `unload` (ExitBootServices) |
| `sys/src/boot/efi/efi.h` | 256 | UEFI-tyypit ja protokollarakenteet |
| `sys/src/boot/efi/fns.h` | 42 | lataajan funktioprototyypit |
| `sys/src/boot/efi/fs.c` | 167 | `fsinit`: ESP/SimpleFileSystem, avaa /plan9.ini |
| `sys/src/boot/efi/iso.c` | 245 | `isoinit`: ISO9660/BlockIO-levy, /cfg/plan9.ini |
| `sys/src/boot/efi/mem.h` | 47 | CONFADDR 0x1200, GDT-segmenttimakrot |
| `sys/src/boot/efi/mkfile` | 91 | bootx64.efi: x64.6 efi.6 fs.6 pxe.6 iso.6 sub.6 -> 6l -> aux/aout2efi |
| `sys/src/boot/efi/pxe.c` | 499 | `pxeinit`: PXE/TFTP, /cfg/pxe/<mac> |
| `sys/src/boot/efi/sub.c` | 394 | `configure` (plan9.ini -> BOOTARGS), `bootkern` (a.out-lataus 0x110000:een), `hexfmt` |
| `sys/src/boot/efi/x64.s` | 124 | PE-entry `start`, `reloc`, `eficall`, `jump` (paluu 32-bit-tilaan, hyppy kernelin `_protected`-osoitteeseen) |
| `sys/src/cmd/aux/aout2efi.c` | 237 | a.out -> PE/COFF (AddressOfEntryPoint = `start`, ImageBase 0x8000) |

## 2 Kernel pc64

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `sys/src/9/pc64/apbootstrap.s` | 171 | AP:n 16->32->64-bit bootstrap (APBOOTSTRAP 0x7000) |
| `sys/src/9/pc64/dat.h` | 347 | Mach, Conf, PCArch jne. |
| `sys/src/9/pc64/fns.h` | 178 | kernelin funktioprototyypit |
| `sys/src/9/pc64/fpu.c` | 572 | `mathinit`, `fpuinit` |
| `sys/src/9/pc64/l.s` | 1303 | `_protected`, `_warp64`, `_lme`, `_start64v` -> `main`; myös `gotolabel`, `touser`, `syscallentry` |
| `sys/src/9/pc64/main.c` | 368 | `main` (init-järjestys), `confinit`, `mach0init`, `init0` |
| `sys/src/9/pc64/mem.h` | 185 | KZERO, KTZERO, CONFADDR, CPU0PML4/MACH-osoitteet |
| `sys/src/9/pc64/mkfile` | 181 | 9pc64: linkitys `-T0xffffffff80110000 -l` (entry = `_protected`), initcode.out |
| `sys/src/9/pc64/mmu.c` | 730 | `mmuinit`, `preallocpages`, sivutaulut (KZERO/VMAP) |
| `sys/src/9/pc64/pc64` | 172 | kernel-konfiguraatio: laitteet, `misc` (archacpi/archmp/archgeneric), bootdir |
| `sys/src/9/pc64/squidboy.c` | 110 | `mpstartap`, `squidboy` (AP-prosessorien käynnistys) |
| `sys/src/9/pc64/trap.c` | 594 | `trapinit0`, `trapinit`, `kprocchild`, `syscall` |

## 3 x86-alusta (pc/)

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `sys/src/9/pc/apic.c` | 500 | `lapicinit`, IOAPIC |
| `sys/src/9/pc/archacpi.c` | 1136 | PCArch archacpi: `identify` (*acpi=), `acpiinit`, `acpireset` |
| `sys/src/9/pc/archgeneric.c` | 95 | PCArch archgeneric (i8259/i8253) |
| `sys/src/9/pc/archmp.c` | 480 | PCArch archmp (varapolku jos ACPI epäonnistuu) |
| `sys/src/9/pc/bootargs.c` | 194 | `bootargsinit`, `getconf`, `setconfenv` |
| `sys/src/9/pc/cga.c` | 224 | `screeninit` (VGA-teksti) |
| `sys/src/9/pc/devarch.c` | 1129 | `ioinit`, `cpuidentify`, `archinit`, `cpuidprint` |
| `sys/src/9/pc/hpet.c` | 126 | `hpetprobe`, `hpetinit` (jos HPET) |
| `sys/src/9/pc/i8253.c` | 272 | `i8253init` (kello) |
| `sys/src/9/pc/i8259.c` | 232 | `i8259init` (PIC) |
| `sys/src/9/pc/init9.c` | 7 | initcoden `_main` -> `startboot` |
| `sys/src/9/pc/io.h` | 160 | vektorit, IRQ-numerot |
| `sys/src/9/pc/irq.c` | 303 | `irqinit`, `trapenable`, `intrenable` |
| `sys/src/9/pc/memory.c` | 584 | `meminit0`, `e820scan` (*e820=), `meminit`, `memreserve` |
| `sys/src/9/pc/mp.c` | 710 | `mpinit`: LAPIC, AP:iden käynnistys |
| `sys/src/9/pc/mp.h` | 165 | Apic-rakenteet |
| `sys/src/9/pc/mtrr.c` | 789 | `mtrrsync`, `mtrrexclude` (cpuidentify/meminit0) |
| `sys/src/9/pc/pcipc.c` | 662 | `pcicfginit` |
| `sys/src/9/pc/screen.c` | 810 | `bootscreeninit` (*bootscreen= -> framebuffer) |
| `sys/src/9/pc/screen.h` | 174 | VGAscr |
| `sys/src/9/pc/uarti8250.c` | 701 | `i8250console` |

## 4 Yhteinen ydin (port/)

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `sys/src/9/port/chan.c` | 1787 | `chandevreset`, `chandevinit`, `namec` |
| `sys/src/9/port/devcons.c` | 979 | `printinit`; #c (/dev/cons), jonka initcode avaa |
| `sys/src/9/port/devdup.c` | 144 | #d (initcode: bind #d /fd) |
| `sys/src/9/port/devenv.c` | 546 | #e, #ec (`ksetenv`; initcode: bind /env) |
| `sys/src/9/port/devproc.c` | 1696 | #p (initcode: bind /proc) |
| `sys/src/9/port/devroot.c` | 265 | #/ : palvelee sisäänrakennettua /boot/boot-tiedostoa |
| `sys/src/9/port/devshr.c` | 835 | #σ (initcode: bind /shr) |
| `sys/src/9/port/devsrv.c` | 688 | #s (initcode: bind /srv) |
| `sys/src/9/port/edf.h` | 54 |  |
| `sys/src/9/port/error.h` | 55 |  |
| `sys/src/9/port/initcode.c` | 48 | `startboot`: bindit, open /dev/cons, `exec("/boot/boot")` |
| `sys/src/9/port/lib.h` | 248 |  |
| `sys/src/9/port/page.c` | 401 | `pageinit` |
| `sys/src/9/port/pci.h` | 303 |  |
| `sys/src/9/port/portclock.c` | 296 | `timersinit` |
| `sys/src/9/port/portdat.h` | 1064 | Proc, Chan, Dev, Pgrp jne. |
| `sys/src/9/port/portfns.h` | 447 |  |
| `sys/src/9/port/proc.c` | 2067 | `schedinit`, `sched`, `runproc`, `kproc`, `linkproc`, `procinit0` |
| `sys/src/9/port/sd.h` | 220 |  |
| `sys/src/9/port/sdram.c` | 258 | `ramdiskinit` |
| `sys/src/9/port/userinit.c` | 96 | `userinit`, `proc0` (initcode-sivu, `init0`-kutsu) |
| `sys/src/9/port/xalloc.c` | 269 | `xinit`, `xspanalloc` |

## 5 Raja: exec /boot/boot

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `sys/src/9/port/sysfile.c` | 1400 | `sysbind`, `sysopen`, `sysdup`... initcoden käyttämät syscallit |
| `sys/src/9/port/sysproc.c` | 1480 | `sysexec`: itse exec-kutsu |

## 6 Build ja generointi

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `amd64/mkfile` | 6 | 6c/6a/6l-liput |
| `sys/src/9/boot/bootmkfile` | 29 | /boot/boot ja bootfs.paq -rakennus (itse boot.c jää exec:n jälkeiseksi) |
| `sys/src/9/port/mkbootrules` | 59 | bootdir-säännöt (/boot/boot ja bootfs.paq kernelin juureen) |
| `sys/src/9/port/mkdevc` | 224 | generoi `devtab[]`, `links()`, `knownarch[]` konfiguraatiosta |
| `sys/src/9/port/mkdevlist` | 42 | laitelista konfiguraatiosta |
| `sys/src/9/port/mkfilelist` | 15 | pc/-tiedostojen poiminta (REPCC) |
| `sys/src/9/port/mkroot` | 15 | tiedosto -> $CONF.root.s |
| `sys/src/9/port/mkrootall` | 31 | kokoaa root-taulun |
| `sys/src/9/port/mkrootc` | 54 | generoi $CONF.rootc.c (devroot-taulu) |
| `sys/src/9/port/portmkfile` | 125 | kernel-build: initcode.i, generointisäännöt |

## 7 Otsakkeet

| Tiedosto | Rivejä | Rooli |
|---|---:|---|
| `amd64/include/u.h` | 77 |  |
| `amd64/include/ureg.h` | 30 |  |
| `sys/include/a.out.h` | 47 | a.out-header (bootkern) |
| `sys/include/pool.h` | 60 |  |
| `sys/include/tos.h` | 24 |  |

## Generoidut tiedostot (eivät ole repossa, syntyvät buildissa)

`pc64.c` (`devtab[]`, `links()`, `knownarch[]`; `mkdevc`), `initcode.i` (initcode.c + init9.c), `errstr.h`, `../port/systab.h`, `apbootstrap.i`, `rebootcode.i`, `pc64.root.s` / `pc64.rootc.c` (kernelin sisäinen juuri: `/boot/boot`, `bootfs.paq`).

## Ei mukana, ja miksi

| Ryhmä | Tiedostot | Syy |
|---|---|---|
| Muut arkkitehtuurit | `sys/src/boot/efi/ia32.s`, `aa64.s`, `sys/src/9/{arm64,...}` | eivät x86-64 |
| BIOS-boot | `sys/src/boot/pc/*` | ei UEFI-polku |
| 386-kernel | `sys/src/9/pc/{l.s,main.c,mmu.c,trap.c,fpu.c}` | pc64 korvaa nämä |
| exec:n jälkeen | `sys/src/9/boot/boot.c`, `bootrc`, `*.proto`, `*.rc` | `/boot/boot` on jo käynnissä |
| Reboot | `pc64/rebootcode.s` | uudelleenkäynnistys, ei boot |
| Laiteajurit | `ether*`, `usb*`, `sd*` (paitsi `sdram.c`), `vga*`, `devdraw.c`, `uart*` jne. | `devtab[i]->reset/init` kutsuu niitä epäsuorasti, mutta ne eivät ole polun logiikkaa. Mukana vain initcoden bindaamat laitteet |
| Framebufferin apuketju | `pc/vga.c`, `pc/vgasoft.c`, `port/devdraw.c` | `bootscreeninit` kutsuu (`vgascreenwin`, `drawcmap`), mutta ei polun ohjaus |
| ACPI-apu | `pc/ec.c`, `sys/src/libaml` | `acpiinit` kutsuu AML-tulkkia (`amlinit`, `amlenum`) |
| Ytimen apufunktiot | `port/{alloc,memmap,iomap,dev,pgrp,segment,fault,taslock,qlock,qio,print,tod}.c` | polun funktiot kutsuvat näitä, mutta niitä ei ole nimetty polulla |
| Kirjastot | `libc` (`quotefmtinstall`, `memmove`...), `libmemdraw`, `libip`, `libsec` jne. | linkitetään kerneliin, eivät boot-logiikkaa |
