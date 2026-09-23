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

- **Alamuistivaraus ja loader-diagnostiikka (23.9.2026, muistio
  `Plan2001Plan/claudelle-uefi-alamuisti-2026-09-23.md`).** Edellä (22.9.)
  todettu "OVMF kieltäytyy joka kerta" ei johtunutkaan siitä, että alue olisi
  varattu — `CONFADDR` (`0x1200`) ei ole sivukohdistettu, joten
  `AllocateAddress`-pyyntö oli itsessään virheellinen ja UEFI:n **piti** hylätä
  sen. Commit `cf23532` kohdisti pyynnön sivulle `BOOTSCRATCHBASE` (`0x1000`)
  ja pysäytti koneen, jos varaus silti hylätään. Lisäksi 96 KiB:n lopullinen
  UEFI-muistikartta ei enää ole firmwaren antamassa pinossa vaan ennakkoon
  varatussa `EfiLoaderData`-poolissa. Loader tulostaa yksilöllisen
  build-tunnisteen ja numeroidut vaiheet `L01`–`L25`; `ExitBootServices`in
  jälkeen etenemisen näyttää 1–4 magentaa ruutua framebufferin vasemmassa
  alakulmassa.

  **Korjaus/korjaus (23.9.2026, myöhemmin samana päivänä).** Commit `5fc5e4d`
  perui pysäytyksen väittäen "T5600 hylkäsi kohdistetunkin pyynnön" — tälle
  väitteelle ei löydy tukea mistään: itse committiviesti mainitsee vain
  QEMU-testauksen, eikä `Plan2001Plan/`-hakemistossa ole mitään raporttia
  kohdistetun pyynnön testaamisesta oikealla raudalla. Ainoa koskaan havaittu
  hylkäys (sekä QEMU:ssa että T5600:lla) koski vanhaa **kohdistamatonta**
  pyyntöä, jonka UEFI-spesifikaatio pakottaa hylkäämään riippumatta siitä,
  mitä osoitteessa oikeasti on — se ei siis todista mitään kohdistetusta
  pyynnöstä. Pysäytys on nyt palautettu (commit tämän jälkeen): jos firmware
  joskus oikeasti hylkää kohdistetunkin pyynnön, se kertoo että muisti on
  jonkin muun käytössä, ja sinne kirjoittaminen silti (kuten upstream-9frontin
  BIOS-ajan loader teki, kysymättä koskaan ensin) on todellinen, ei
  hypoteettinen, korruptioriski. QEMU-testattu `bootargs`-kehotteeseen asti;
  T5600:n testaus on seuraava askel — se kertoo nyt myös vastauksen siihen,
  hylkääkö T5600 ylipäätään kohdistetun pyynnön.

- **Legacy-boot-lähteet pois loaderista (23.9.2026, ohje
  `Plan2001Plan/legacy_pois_loaderista.txt`).** "Legacy cleanup" ei tarkoita enää
  vain kernelin BIOS-era-laitteistopolkuja — myös UEFI-loaderin oma vanha
  monilähteinen boot-probe oli samaa periaatetta vastaan. `efimain()` kokeili
  ennen järjestyksessä `pxeinit()` → `isoinit()` → `fsinit()`. `isoinit()`in
  ISO9660 PVD -haku skannaa BlockIO-laitteita jopa tuhansilla synkronisilla
  `ReadBlocks`-kutsuilla; oikealla, vanhemmalla T5600-firmwarella tästä syntyi
  kymmenien sekuntien lisäviive jokaiseen testisykliin, vaikka Plan2001
  käynnistyy aina samalta UEFI Simple File System -volyymilta kuin
  `BOOTX64.EFI` itse. Poistettu: `pxeinit()`/`isoinit()`-kutsut ja niiden
  prototyypit; `pxe.c`/`iso.c` pudotettu `bootx64.efi`:n `mkfile`-riviltä
  (tiedostot itse jäävät VM:n puuhun koskemattomina — `ia32`/`aa64`-kohteet,
  joita Plan2001 ei rakenna, käyttävät niitä yhä). `fsinit()` (nyt seurattu
  tiedostona `fs.c`) on ainoa boot-lähde: se avaa ensisijaisesti sen SFS-
  volyymin, jolta loader itse ladattiin (`LoadedImageProtocol` →
  `DeviceHandle` → `SimpleFileSystemProtocol` → `OpenVolume`), ja vasta jos
  siltä ei löydy `plan9.ini`ta, skannaa muut SFS-volyymit (tätä toissijaista
  polkua ei poistettu — se on halpa, ei BlockIO-tason skannausta, eikä siis
  sama ongelma kuin `isoinit()`). Epäonnistuminen on nyt suoraan kohtalokas
  (`[P2 L04] FATAL: boot filesystem unavailable`, pysähtyy) sen sijaan että
  jatkaisi ketjussa. Samalla `fsread()`in lukukoko nostettiin 4096 tavusta 64
  KiB:iin — pienempi, mutta samaa syytä vastaan (vähemmän synkronisia EFI
  `File.Read`-kutsuja kernelin lataukseen). QEMU-testattu: `[P2 L04] boot
  filesystem: open` → `ready` suoraan, ei PXE/ISO-rivejä, `bootargs`-kehote
  saavutettu kuten ennen, `bootx64.efi` pieneni (n. 15.8 KB → 13.6 KB).

- **T5600 hylkää kohdistetunkin alamuistivarauksen — vahvistettu raudalla
  (23.9.2026).** Ensimmäinen oikea testi kohdistetulle `BOOTSCRATCHBASE`-
  pyynnölle: näyttöön tuli `[P2 L02] FATAL: firmware refused low scratch
  reservation` heti `AllocateAddress`-rivin jälkeen. Tämä on siis todellinen,
  ei enää hypoteettinen — 22.9. kirjattu epäily oli oikeansuuntainen, vaikka
  sitä ei silloin voitu perustella millään testillä. Käytäntö on nyt sama
  (pysähtyy), mutta lisätty diagnostiikka kertoo seuraavalla testauskerralla
  *miksi*: `efimain()` tulostaa nyt myös raa'an `AllocatePages`-`EFI_STATUS`-
  koodin (`status=0x...`) ja kutsuu uutta `diagscratchmap()`ia, joka hakee
  senhetkisen UEFI-muistikartan (`GetMemoryMap`, ei vielä lopullinen — boot
  services ovat yhä käytössä) ja tulostaa `BOOTSCRATCHBASE`n peittävän
  descriptorin `Type`n, sivumäärän ja täyden 64-bittisen `Attribute`n
  (mukaan lukien bitti 63, `EFI_MEMORY_RUNTIME` — jota `bootexit()`in oma
  `BootMem.attr` typistää pois, joten tätä ei voi päätellä lopullisesta
  kartasta). QEMU ei koskaan laukaise tätä polkua (OVMF myöntää varauksen),
  joten diagnostiikka on käännetty muttei vielä ajettu millään oikealla
  statuskoodilla. **Seuraava askel on ainoastaan tämä**: käynnistä T5600:lla
  ja lue näytöstä `status=0x...`- ja `diag: covering type=...`-rivit ennen
  kuin päätetään, onko jatkaminen turvallista millään ehdolla.

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
- **Loaderin low-memory-jatkopolku vaatii T5600-testin.** Uusi build tunnistuu
  ensimmäisestä rivistä `[P2 L01] Plan2001 loader 2026-09-23 debug-1`; jos teksti
  lakkaa `ExitBootServices`in jälkeen, alakulman ruutujen määrä kertoo viimeisen
  saavutetun vaiheen.
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
