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
  - GOP:n framebuffer-tiedot luetaan firmwaren valmiiksi valitsemasta aktiivisesta
    tilasta. Loader ei kutsu `QueryMode`a eikä `SetMode`a, vaan välittää saadun
    resoluution sellaisenaan `BootInfo`ssa. Commitin `a93c397` suurimman tilan
    valinta peruttiin, koska `SetMode` jäi Dell Precision T5600:n suppeassa
    GOP-toteutuksessa pysyvästi jumiin jo ennen `ExitBootServices`ia.
  - Kernel mapittaa GOP-framebufferin täsmälleen `FrameBufferBase`-osoitteesta;
    `bootmapfb()` ei enää korvaa sitä PCI-kortin suurimman BARin osoitteella.
    Näkyvä leveys (`HorizontalResolution`) ja rivipituus (`PixelsPerScanLine`)
    kulkevat erillään myös `*bootscreen`-yhteensopivuuspolussa.
- **Rakenteen siivous.** `pcboot`-minimikernel ja koko `9front-x64-boot/`-
  peilihakemisto poistettu. `sys/`-hakemistossa on nyt vain tiedostot, joita olemme
  oikeasti kirjoittaneet tai muokanneet (ks. README). Windows-VM otettu takaisin
  käyttöön käännösnopeuden vuoksi.
- **Vaihe 2 — legacy-koodin poistot (valmis).** Periaate koko vaiheelle: ACPI/UEFI
  oletetaan aina saatavilla, eikä pre-ACPI/BIOS-varapolkuja pidetä "varmuuden vuoksi".
  Legacy-rauta voi käyttää legacy-käyttöjärjestelmiä; Plan2001 kohdistuu nykyiseen ja
  tulevaan laitteistoon. Neljä poistoa, kukin oma committinsa:
  - `archmp.c` (pre-ACPI MP-taulut `_MP_`) — konfiguraatiorivi pois, tiedosto jää
    VM:n puuhun koskemattomana muttei enää linkity.
  - `cga.c` (VGA-tekstitila 0xB8000) — vaati myös `mkfile`n (OBJ-lista) ja
    `devvga.c`n (`vgactl textmode`-komento) muokkaamisen, koska näitä ei ohjata
    konfiguraation kautta. Huom: QEMU:n oma `-vga std` emuloi yhä oikeaa
    CGA-laitteistoa, joten QEMU-testi ei todista 0xB8000:n olevan inertti
    oikealla raudalla — vain ettei normaali käynnistys- ja konsolipolku rikkoutunut.
  - PCI-mekanismi #2 ja BIOS32/PCI BIOS (`pcipc.c`, `pcibios.c`) — mekanismi #1
    (0xCF8/0xCFC) on ollut universaali kaikilla PCI-siruilla 1990-luvun alusta asti.
    QEMU on tässä luotettava testi: sen oma PCI-emulaatio toteuttaa vain
    mekanismi #1:n.
  - `uartisa` — ei oikea laitteistoprobe, vain manuaalinen `plan9.ini`-konfiguroitu
    reitti kiinteälle ISA-sarjaportille; ei tarvita, koska `uartpci` löytää
    oikean sarjaportin PCI:n kautta.
  - Yhteensä: kernelin koko pieneni 5774 tavua. Jokainen QEMU-testattu, ja jokainen
    myös asennettu VM:lle ja käynnistetty uudelleen täyteen userlandiin
    (cpu+auth-palvelut tarkistettu).

- **Ulkoinen katselmointi ("Codex Astra", 22.9.2026).** 9 löydöstä, kaikki
  itsenäisesti varmistettu koodista (yksi myös UEFI-spesifikaatiosta). 8/9 korjattu
  kahdessa committissa (`7f50e9a`, `bc7b88b`): RTC:n aikavyöhykkeen etumerkki oli
  väärinpäin (oma vaihe 1 -bugi), `memreserve()` pyöristi osasivuvaraukset alaspäin
  eikä ylös (nollasi ne, jos varaus alkoi sivurajalta — osui joka bootissa
  `archacpi.c`:n RSDP-varaukseen), `plan9.ini`-rivien rajoittamaton jäsennys
  (`bootfile=` saattoi ylivuotaa 128-tavuisen pinopuskurin), `bootinfoinit()`
  hyväksyi ennen myös tyhjän/katkenneen muistikartan (sulki `ramscan()`-varapolun
  pysyvästi), loader varasi tässä vaiheessa matalan muistin alueensa
  (`CONFADDR..BOOTSCRATCHEND`) UEFI:ltä ennen kirjoitusta (**varmistettu
  empiirisesti: OVMF kieltäytyy tästä varauksesta joka kerta** — huoli oli siis
  todellinen, ei vain teoreettinen; tuolloin arvioitiin "ei silti kohtalokas,
  boot jatkuu normaalisti" — **tämä osoittautui vääräksi, ks. alla 23.9.2026**),
  `reboot()` paniikkaa nyt ennen mitään
  palautumatonta laitesammutusta (ks. alla, ei korjaa itse 32-bittistä luovutusta),
  `bootkern()` vapauttaa `efialloc()`-muistin jokaisessa virhepolussa, `build.rc`
  keskeytyy `mk`-epäonnistumisesta sen sijaan että aina tulostaisi `BUILD-DONE`n.
  Yhdeksäs löydös (bootfb:n pikselimerkkien automaattitarkistus `test-qemu.sh`:ssa)
  jätetty tarkoituksella tekemättä käyttäjän omasta valinnasta — tarkistetaan
  manuaalisesti QEMU:sta tarvittaessa.

- **Alamuistivarauksen todellinen syy (Codex Astra, 23.9.2026, muistio
  `Plan2001Plan/claudelle-uefi-alamuisti-2026-09-23.md`).** Edellä (22.9.)
  todettu "OVMF kieltäytyy joka kerta" ei johtunutkaan siitä, että alue olisi
  varattu — `CONFADDR` (`0x1200`) ei ole sivukohdistettu, joten
  `AllocateAddress`-pyyntö oli itsessään virheellinen ja UEFI:n **piti** hylätä
  se, myös oikealla raudalla. Koodi tulosti tämän jälkeen varoituksen ja jatkoi
  kirjoittamalla `BOOTLINE`/`BOOTARGS`/`BootInfo`n silti varaamattomaan
  muistiin, ennen `acpiconf()`/`screenconf()`-kutsuja. Tämä on todennäköisempi
  selitys Dell Precision T5600:n `SetMode`-jumille kuin pelkkä viallinen
  GOP-toteutus (ks. vaihe 1 yllä): jos firmwarella on jotain tässä
  osoitealueessa, ylikirjoitus olisi voinut sotkea sen tilan juuri ennen
  jumiutunutta kutsua. **Korjattu, ei vielä committoitu:** varaus tehdään nyt
  sivukohdistetusta `BOOTSCRATCHBASE`ista (`0x1000`, uusi vakio `mem.h`:ssa) ja
  epäonnistuminen pysäyttää koneen sen sijaan että jatkaisi (`efiallocdata()`,
  uusi `EfiLoaderData`-varianttinsa `efialloc()`ista muistityypillä
  parametrisoituna). QEMU-testattu: varoitusrivi ei enää tulostu, boot etenee
  `bootargs`-kehotteeseen asti kuten ennenkin. Ei vielä testattu T5600:lla —
  tämä on nyt ensisijainen ehdokas sille, korjaako se myös `SetMode`-jumin
  kokonaan ilman että GOP-tilanvaihtoa tarvitsee ottaa takaisin käyttöön.

## Seuraavaksi

Vaiheen 2 neljä tunnistettua ehdokasta on tehty. Jatkoehdokkaita ei ole vielä
kartoitettu — seuraava askel on uusi katselmointikierros (esim. `mtrr.c` vs. PAT,
`archacpi.c`:n ja `archgeneric.c`:n suhde, tai ajastimien/keskeytysten muu legacy).

## Tunnetut avoimet asiat

- **Framebuffertilaa ei vaihdeta loaderissa.** Käytössä on firmwaren valitsema
  GOP-tila ja sen ilmoittama resoluutio; natiiviresoluution valinta jää firmwarelle.
- **Uusi GOP-mappaus vaatii raudalla varmistuksen.** PCI BAR -heuristiikan poisto
  ja erillinen framebuffer-stride on käännetty, mutta T5600/P2000-yhdistelmän
  näyttötesti on vielä tekemättä.
- Kernelin oma "lataa uusi kernel" -polku (`rebootcode.s`, `/dev/reboot`) käyttää yhä
  32-bittistä luovutusta, joka ei enää täsmää `_efi64`-sisäänmenon kanssa. `reboot()`
  paniikkaa nyt siististi ennen kuin mitään laitetilaa ehditään sotkea (commit
  `bc7b88b`) — turvallista, mutta itse 64-bittinen kexec-luovutus on yhä
  kirjoittamatta; `/dev/reboot` ei toimi ollenkaan ennen sitä.
- `BootInfo`-muistikartta on rajattu 600 riviin (`BootInfoMaxMem`). Kernel **reagoi**
  nyt ylivuotoon (`bootinfoinit()` pysäyttää koneen, commit `bc7b88b`) sen sijaan
  että käyttäisi katkennutta karttaa hiljaa — mutta itse 600 rivin riittävyyttä
  oikealla, pirstoutuneella UEFI-muistikartalla ei ole vielä arvioitu.
- Vaihe 1+2 on QEMU- ja VM-testattu, mutta ei vielä oikealla raudalla (vain vaihe
  A/B on). `pcboot`-pikatestikitti on vanhentunut rakenteen jälkeen; raudalla
  testaus vaatii `tools/build.sh` + `tools/test-qemu.sh` -tuloksen kopioinnin
  muistitikulle (ks. README).
