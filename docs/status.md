# Tila ja suunnitelma

Tämä on kertaluontoinen aikajana ja suunnitelma, ei ylläpidettävä muistilista sessioista.
Yksityiskohdat kunkin vaiheen toteutuksesta ovat commit-viesteissä (`git log`).

## Tavoite

9frontin x86-64 UEFI-boot-polku (`bootx64.efi` → `9pc64` → `/boot/boot`) käyttää
mahdollisimman paljon UEFI-palveluita ennen `ExitBootServices`ia ja jättää pois
BIOS-perua olevan legacy-koodin, joka ei ole enää tarpeen puhtaalla UEFI-koneella.

## Vaiheet

- **Vaihe 0 — perustaso.** Alkuperäinen upstream-koodi (commit `320dd16`) muuttamattomana.
  Tehtiin testiympäristö: 9front-VM, `drawterm`-yhteys, käännös- ja QEMU-testiskriptit.
- **Vaihe A — yksi 64-bittinen sisäänmeno.** Kernelillä on nyt vain `_efi64`-sisäänmeno.
  Poistettu: `_protected`, Multiboot, 32-bittinen kiertotie loaderissa ja kernelissä.
  Loader tarkistaa ennen siirtymää: `CR4.LA57` on nolla, firmwaren sivutaulut eivät
  ole päällekkäin kernelin boot-alueen kanssa, ja kernelin muistialue on varattu
  `EfiLoaderCode`na (firmware merkitsee vapaan muistin NX:ksi).
- **Vaihe B — rakenteinen `BootInfo`.** Loader kokoaa versioidun `BootInfo`-rakenteen
  (`sys/include/bootinfo.h`) `plan9.ini`-tekstin sijaan: muistikartta, ACPI RSDP,
  framebuffer.
- **Vaihe 1 — UEFI-tietojen laajempi käyttö (lisäävä, mitään ei vielä poistettu).**
  - BootServices-muisti (koodi ja data) on nyt käyttökelpoista RAM-muistia
    `ExitBootServices`in jälkeen, UEFI-spesifikaation mukaisesti.
  - RNG-siemen `EFI_RNG_PROTOCOL`:lta, yhdistetty XOR:lla olemassa olevaan
    RDRAND-lähteeseen kernelin `hwrandbuf`issa.
  - TSC-taajuus mitataan loaderissa ja tulostetaan ristiintarkistukseksi kernelin
    omalle PIT/HPET-kalibroinnille (ei korvaa sitä — ks. commit `e70690f` miksi).
  - RTC-aika luetaan UEFI:n `GetTime`stä ja asetetaan kellolle heti bootissa.
  - **Ei tehty:** GOP:n `SetMode` (natiivi tarkkuus). Riski `bootfb`-diagnostiikkakanavalle
    raudalla ilman sarjaporttia; tehdään vasta nopeamman testisilmukan kanssa.
- **Rakenteen siivous (tämä commit).** `pcboot`-minimikernel ja koko `9front-x64-boot/`-
  peilihakemisto poistettu. `sys/`-hakemistossa on nyt vain tiedostot, joita olemme
  oikeasti kirjoittaneet tai muokanneet (ks. README). Windows-VM otettu takaisin
  käyttöön käännösnopeuden vuoksi.

## Seuraavaksi: vaihe 2 — legacy-koodin poistot

Ehdokkaat (ks. alkuperäinen arvio `docs/upstream-scope-manifest.md`:ssä, huomaa että se
kuvaa VANHAA `_protected`-pohjaista polkua):

- `pc/archmp.c` — pre-ACPI MP-taulut, varapolku jos ACPI epäonnistuu. UEFI-koneilla
  ACPI on aina saatavilla.
- `pc/cga.c` — VGA-tekstitila (0xB8000). UEFI antaa framebufferin; `bootfb.c`/`screen.c`
  käyttävät sitä jo.
- PCI-mekanismi #2 ja BIOS32-kysely (`pc/pcipc.c`).
- ISA-UART-tuki, jos sarjaportti aina löytyy PCI:n tai ACPI SPCR-taulun kautta.

Jokainen poisto omana committinaan, QEMU-testattuna ennen seuraavaa.

## Tunnetut avoimet asiat

- Kernelin oma "lataa uusi kernel" -polku (`rebootcode.s`, `/dev/reboot`) käyttää yhä
  32-bittistä luovutusta, joka ei enää täsmää `_efi64`-sisäänmenon kanssa. Ei vielä
  korjattu (ks. commit `409edd0`).
- `BootInfo`-muistikartta on rajattu 600 riviin (`BootInfoMaxMem`); ylivuodosta
  asetetaan lippu, mutta kernel ei vielä reagoi siihen.
