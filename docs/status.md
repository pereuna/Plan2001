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
  statuskoodilla.

  **Löytö ja korjaus samana päivänä: diagnostiikkarivit olivat itse rikki.**
  Ensimmäinen T5600-ajo uudella diagnostiikalla näytti saman oireen kuin
  aiempi GOP-debug-jumi: `status=0x`-tekstin jälkeen ei tullut yhtään
  heksanumeroa, ja seuraavan `print()`-kutsun teksti jatkui samalta riviltä.
  Syy ei ollut firmwaressa eikä `print()`issä — molemmat uudet
  `memmove(s, "...", N)`-kutsut käyttivät väärää `N`:ää (43 merkin literaali
  merkitty 45:ksi, 29 merkin 30:ksi), jolloin `s` osoitti 1-2 tavua
  todellisen tekstin ohi ja loppuosa (heksaluvut) kirjoittui satunnaisen
  pinoroskan päälle; jos roska sisälsi `\0`:n, `print()` pysähtyi siihen.
  Samalla huomattiin `FATAL`-rivin puskuri (`char b[48]`) liian pieneksi
  täydelle 64-bittiselle `EFI_STATUS`-heksaluvulle (43+16+2=61 > 48) —
  todellinen pinon ylivuoto, ei vain kosmeettinen bugi. Korjattu: oikeat
  `N`:t, puskurit kasvatettu (`b[80]`, `b[96]`). **Todennettu QEMU:ssa
  keinotekoisesti**: sama alue varattiin tarkoituksella etukäteen, jotta
  varsinainen kutsu epäonnistuisi aidosti samalla tavalla kuin raudalla;
  tulosteeksi saatiin täysi, ehjä rivi — `status=0x800000000000000e`
  (`EFI_NOT_FOUND`) ja `diag: covering type=2 pages=6 attr=0xf` (tässä
  keinotekoisessa testissä `EfiLoaderData`, koska testi varasi sen juuri
  sellaisena — ei tietoa siitä, mitä T5600 oikeasti raportoi).

  **Todellinen T5600-ajo ja arkkitehtuurin korjaus (23.9.2026, myöhemmin
  samana päivänä).** Todellinen tulos: `status=0x800000000000000e`
  (`EFI_NOT_FOUND`), `diag: covering type=3 pages=8 attr=0xf` —
  `EfiBootServicesCode`, ei `EFI_MEMORY_RUNTIME`-bittiä. Kyse ei siis ole
  varatusta/tuntemattomasta alueesta vaan firmwaren **omasta, parhaillaan
  suoritettavasta boot-aikaisesta koodista** täsmälleen samassa
  matalassa muistialueessa, jota loader on aina kirjoittanut ilman
  varausyritystä. Sinne kirjoittaminen ennen `ExitBootServices`ia olisi
  siis todellinen riski, ei vain teoreettinen.

  Käyttäjän oma erillinen projekti (`~/AIOS`, `baremetal/platform.c`)
  käynnistyy samalla T5600-raudalla onnistuneesti ja käyttää
  `AllocatePages`ia — mutta aina `AllocateAnyPages`illa, ei koskaan
  kiinteällä osoitteella, eikä koskaan tätä matalaa aluetta. Sama malli
  löytyy Linuxin EFI-stubista (`efi_allocate_pages()`:
  `EFI_ALLOCATE_MAX_ADDRESS`, ei koskaan kiinteä osoite; `boot_params`
  välitetään ytimelle RSI-rekisterissä) ja OpenBSD:n `efiboot`:sta
  (`run_loadfile()`: `BOOTARG_OFF`-vakio-osoitetta ei koskaan varata
  UEFI:ltä, ja sekä bootarg-kopio että kernel-image siirretään lopulliseen
  paikkaansa **vasta** `efi_cleanup()`/`ExitBootServices`in jälkeen — koodin
  oma kommentti: "Move the loaded kernel image to the usual place after
  calling ExitBootServices()").

  Korjattu vastaavasti, ilman mitään kernelin puolen muutosta: `confaddr`
  (`sub.c`:n `BOOTLINE`/`BOOTARGS`, oli jo muuttuja eikä vakio) ja `bi`
  (`BootInfo`) osoittavat nyt `AllocateAnyPages`illa varattuun
  väliaikaismuistiin koko `configure()`/`eficonfig()`/`bootexit()`-ajan.
  Uusi `bootrelocate()` (`efi.c`) kopioi molemmat kiinteisiin
  `CONFADDR`/`BOOTINFO`-osoitteisiin vasta `bootkern()`:ssa (`sub.c`),
  heti onnistuneen `bootexit()`in (`ExitBootServices`) jälkeen, ennen
  `jump64()`ia — täsmälleen OpenBSD:n järjestys. Tämä on turvallista
  UEFI-spesifikaation mukaan: `EfiBootServicesCode`/`Data` vapautuu
  käyttöjärjestelmälle heti `ExitBootServices`in onnistuttua, eikä ennen
  sitä. Poistettu tarpeettomana: `efiallocdata()`, `diagscratchmap()`,
  `BOOTSCRATCHBASE`. QEMU-testattu täydestä bootista `bootargs`-kehotteeseen:
  `plan9.ini`n sisältö (`bootfile=`, `console=`) ja muistikartta
  (`122 holes free`, `854212608 bytes free`) luettiin oikein kiinteistä
  osoitteista, eli kierrätys toimii päästä päähän, ei vain käänny.

  **Vahvistettu T5600:lla (23.9.2026, commit `fa5c461`).** Kone bootin
  onnistuneesti tällä ratkaisulla. Kernel on bootannut koko projektin ajan
  muulla oikealla raudalla (kaksi kannettavaa) — uutta on nimenomaan tämä
  T5600, joka epäonnistui johdonmukaisesti juuri diagnosoituun
  `EfiBootServicesCode`-konfliktiin asti. Kaksi vuorokautta kestänyt
  alamuistivarauksen jäljitys tällä koneella (misalignment → halt-vs-continue
  → väärä diagnostiikka → todellinen syy `EfiBootServicesCode`-konfliktissa →
  arkkitehtuurin korjaus) päättyi tähän.

- **`fsinit()`in piilevä virhe korjattu; ISO9660-live-boot kokeiltu ja
  hylätty (23.9.2026).** "Täysi userland" -testiä varten kokeiltiin
  käynnistää T5600/QEMU oikealla 9front-asennus-ISO:lla (`cdboot=yes`,
  kopioitu käyttäjän oma ISO, vain `9pc64`/`bootx64.efi` vaihdettu
  Plan2001:n omiksi). Tässä prosessissa löytyi ja korjattiin oikea,
  perimmäinen `fs.c`:n virhe: `fsinit()`in varavolyymiskannauksessa
  `fsroot`-muuttuja asetettiin ehdoitta joka kierroksella, joten se jäi
  osoittamaan viimeksi kokeiltua volyymia, vaikka `plan9.ini`ta ei
  löytynyt mistään — `if(fsroot == nil) return -1` ei siis koskaan
  lauennut, ja `fsinit()` raportoi onnistumisen `*pf`:n jäädessä silti
  asettamatta. Seurauksena `configure()` päätyi interaktiiviseen
  `readline()`-kehotteeseen (`>`) täysin hiljaa, ilman mitään virheilmoitusta
  — täsmälleen sama oire kuin aiemmissa print-bugeissa, mutta tällä
  kertaa syy oli tämä. Korjattu: `fsroot = nil` epäonnistuneen kierroksen
  jälkeen, ennen seuraavaa yritystä. Peritty muuttamattomana upstreamista;
  ei koskaan aiemmin lauennut, koska jokainen aiempi testi käytti FAT/ESP-
  mediaa, jossa `plan9.ini` löytyy heti ensimmäiseltä volyymilta.

  Itse ISO-testi paljasti myös, ettei OVMF tarjoa `SimpleFileSystemProtocol`a
  koko ISO9660-levylle El Torito -CD-käynnistyksessä — vain 1 SFS-handle
  löytyy, ja se on pelkkä upotettu käynnistys-FAT-image (`386/efiboot.fat`,
  sisältää vain `efi/boot/*.efi`). `cfg/plan9.ini` ja `amd64/9pc64` eivät
  siis ole tavoitettavissa `fsinit()`in kautta lainkaan tällä
  käynnistystavalla — tähän tarvittaisiin `iso.c`/`isoinit()` (BlockIO-
  tason ISO9660-luku), joka poistettiin tarkoituksella aiemmin tänään.
  **Päätös: ISO9660-live-boot-tukea ei oteta takaisin.** Plan2001:n
  kohde on USB/flash-media, jolla on oikea Plan9-osio, ei live-CD.
  Kokeilun tuotokset (`images/t5600-live-isoroot/`, ISO-tiedostot)
  poistettu.

- **Käynnistysnäytön siivous: yksi merkkirivi, splash pois, loaderin teksti
  säilyy (23.9.2026).** Aiemmin oli kaksi erillistä merkkijärjestelmää
  (loaderin 4 magentaa ruutua alavasemmalla, kernelin 5 ruutua ylävasemmalla)
  ja lisäksi `pc/vga.c`:n koristeellinen "Plan 9 Console" -laatikko
  (turkoosi tausta, musta reunus, harmaa otsikkopalkki), joka sekä hukkasi
  ruututilaa että peitti kaiken loaderin oman `[P2 Lxx]`-tekstin heti kun
  kernelin konsoli alustui — se piirtää oman puskurinsa tyhjästä, ei jatka
  loaderin UEFI ConOut -tulostetta.
  - **Yksi yhteinen merkkirivi.** `bootfb.c` jatkaa nyt loaderin riviä
    kohdasta `LoaderMarks` (=4) eikä aloita omaa riviään alusta; loader
    (`efi.c`) siirsi omat merkkinsä alavasemmalta ylävasemmalle, samaan
    ruudukkoon (`Margin`/`Size`/`Gap`, synkassa kommentein kahden
    tiedoston välillä, koska ne käännetään erikseen). Molemmat pienensivät
    ruudut 24px→8px, marginaalin 16px→2px — rivi vie nyt ~90×12px, ei enää
    huomattavaa ruututilaa. Jokaisella 9 vaiheella (4 loaderin + 5 kernelin)
    on oma värinsä (`markcolor[]` efi.c:ssä, `stagecolor[]` bootfb.c:ssä) —
    bugitilanteessa viimeinen nähty väri kertoo täsmälleen minne päästiin,
    ei tarvitse laskea ruutuja. Loaderin vaihe 4 (kernel entry, piirretään
    `l.s`:stä assemblerilla ennen C-koodia) pysyy magentana, koska se on
    symmetrinen RGB/BGR-kanavajärjestyksessä eikä siis tarvitse
    kanavatarkistusta assembly-tasolla.
  - **`vga.c` (nyt seurattu tiedostona) splash pois.** `vgascreenwin()`
    piirtää enää tasaisen taustan (ei laatikkoa/reunusta/otsikkoa) ja jättää
    `bootmarkheight()`in verran tilaa ylös koskemattomaksi merkkiriville.
  - **Loaderin teksti säilyy kernelin konsolissa.** Uusi `BootInfo`-kenttä
    `logbase`/`logsize` (versio 2→3, käyttää jo varattua `rsvd[]`-tilaa —
    `mem[]`-budjettiin ei koskettu). Loaderin `print()` (`sub.c`) tallentaa
    jokaisen tulostetun merkin myös 4 KiB:n `AllocateAnyPages`-puskuriin
    (`efimain()` varaa tämän ennen ensimmäistäkään tulostetta);
    `bootrelocate()` kirjoittaa lopullisen osoitteen/pituuden `bi`:hin ennen
    kiinteään `BOOTINFO`-osoitteeseen kopiointia. Kernelin puolella uusi
    `bootlogtext()` (`bootfb.c`, sama `vmap()`-kuvio kuin framebufferille)
    palauttaa tämän tekstin; `vgascreenwin()` toistaa sen ENNEN `kmesg`in
    omaa replaytä, joten loaderin `[P2 Lxx]`-rivit näkyvät ensin ja
    kernelin omat viestit jatkuvat niiden perään, samalla ruudulla.
  - **Kolme saraketta scrollauksen sijaan.** `vga.c`:n `vgascroll()`
    kirjoitettiin uusiksi: pohjaan törmätessä siirrytään seuraavaan
    sarakkeeseen (`Ncol`=3) tyhjentämättä mitään, ja vasta kaikkien kolmen
    sarakkeen täytyttyä koko alue tyhjennetään ja aloitetaan sarakkeesta 0.
    Historiaa mahtuu näkyviin 3× enemmän ennen ensimmäistä oikeaa scrollia.
  - **QEMU-testattu ja kuvakaapattu**: kaikki `[P2 L01]`–`[P2 L25]` -rivit
    näkyvät, `Plan 9`-banneri ja sen jälkeiset kernelin rivit jatkuvat
    saumattomasti perään (osa jo toisessa sarakkeessa), merkkirivi on
    pieni eikä peitä mitään. Ei vielä testattu T5600:lla.

  **Katselmoinnin löydökset ja korjaukset (23.9.2026, `/code-review`).**
  Kolme löydöstä edellisestä committista, kaikki vahvistettu koodista ennen
  korjausta:
  - **`l.s`:n vaiheen 4 merkki ei skaalautunut mukana.** Kun merkkiruutujen
    koko pieneni 24px→8px (`efi.c`), assembly-piirretty neljäs ruutu
    (kernel entry, ennen mitään C-koodia) jäi kiinteäksi 12×12px:ksi —
    valuu naapuriruutuun ja `Barh`-rajan yli juuri sillä hetkellä kun
    kehittäjä katsoisi merkkiriviä debugatakseen varhaista jumia. Korjattu:
    `l.s` piirtää nyt 8×8.
  - **Loaderin talteenotettu teksti ei ollut suojattu kernelin omalta
    allokaattorilta.** `logbuf` (`efiallocany(..., EfiLoaderData)`) luokitellaan
    `bootmemkind()`:ssä tavalliseksi `MemRAM`:ksi — toisin kuin `CONFADDR`/
    `BOOTINFO`, sitä ei koskaan kopioida suojattuun matalaan alueeseen, joten
    se jää mihin tahansa firmware sen sattui laittamaan. `xinit()` (yleinen
    allokaattori) käynnistyy ennen `bootscreeninit()`ia (joka lukee sen
    `bootlogtext()`in kautta) — kernelin oma varhainen muistinvaraus olisi
    voinut ehtiä kirjoittaa juuri niiden sivujen päälle ennen kuin teksti
    luetaan. Korjattu: uusi `memreserve(bootinfo->logbase, bootinfo->logsize)`
    `meminit0()`:ssa (`memory.c`), samaan tapaan kuin `[0, CPU0END)`.
  - **`vgascroll()`in flush-alue oli aina koko konsoli**, vaikka kaksi
    kolmesta sarakkeenvaihdosta ei tyhjennä mitään (vain sarakkeesta 0
    kiertäminen tekee). Ei virhe, mutta tarpeeton täysi uudelleenpiirto
    joka rivinvaihdolla. Korjattu: `vgascroll()` ottaa nyt `flushr`in
    parametrina ja yhdistää siihen vain sen minkä oikeasti piirsi;
    kutsuja lisää vielä uuden sarakkeen alueen.
  - QEMU-testattu uudelleen käännöksen jälkeen: `PASS`, kuvakaappaus
    näyttää samalta kuin ennen (merkkirivi pieni, teksti ehjä). Ei vielä
    testattu T5600:lla.

- **Konsolin reunat (24.9.2026).** Oikeilla koneilla (kaikilla kokeilluilla
  toimii) alin tekstirivi katkesi. Syy: `vgascreenwin()` ei pyöristänyt
  konsoli-ikkunan korkeutta kokonaisiin fonttiriveihin, vaikka
  `vgascreenputc()`in vieritystesti olettaa sen (alkuperäinen koodi teki
  `(Dy/h)*h`) — viimeinen rivi saattoi jäädä osittain ruudun ulkopuolelle.
  Nyt ikkuna pyöristetään kokonaisiin riveihin ja yksi rivi jätetään
  tyhjäksi alareunaan; samoin oikeaan reunaan jätetään yksi tyhjä
  merkkisolu, ja jokaisen sarakkeen vasempaan reunaan tulee yksi tyhjä
  merkki (luettavuus, sarakkeet eivät kasva kiinni toisiinsa).
  QEMU-testattu ja kuvakaapattu.

## Seuraavaksi

T5600 bootii nyt onnistuneesti (23.9.2026, commit `fa5c461`) — tämä
nimenomainen kone oli aiemmin johdonmukaisesti epäonnistunut, kun taas kernel
on bootannut koko projektin ajan muulla raudalla (kaksi kannettavaa).
Seuraavaksi: varmistaa täysi userland (cpu+auth-palvelut, samaan tapaan kuin
vaiheen 2 kohdat aiemmin VM:llä) myös T5600:lla, ei vain
`bootargs`-kehotteeseen asti — tämä vaatii USB-medialle oikean Plan9-osion
(fossil/cwfs, täysi dist-sisältö), ei live-ISO:a (ks. yllä 23.9.2026:n
päätös). Pidemmän tähtäimen tavoite (käyttäjän hahmottelema, ei vielä
suunniteltu): valmiiksi asennettu flash/USB-image joka generoidaan
suoraan (esim. 8G image), sen sijaan että live-mediaa käynnistettäisiin
ja asennettaisiin erikseen joka kerta. Sen jälkeen
vielä avoinna: GOP-framebufferin PCI-BAR-korjauksen (ks. yllä) erillinen
näyttötesti T5600:lla, ja `reboot()`in 64-bittinen kexec-luovutus.

Vaiheen 2 neljä tunnistettua ehdokasta on tehty. Jatkoehdokkaita ei ole vielä
kartoitettu — seuraava askel on uusi katselmointikierros (esim. `mtrr.c` vs. PAT,
`archacpi.c`:n ja `archgeneric.c`:n suhde, tai ajastimien/keskeytysten muu legacy).

## Tunnetut avoimet asiat

- **Framebuffertilaa ei vaihdeta loaderissa.** Käytössä on firmwaren valitsema
  GOP-tila ja sen ilmoittama resoluutio; natiiviresoluution valinta jää firmwarelle.
- **Uusi GOP-mappaus vaatii raudalla varmistuksen.** PCI BAR -heuristiikan poisto
  ja erillinen framebuffer-stride on käännetty, mutta T5600/P2000-yhdistelmän
  näyttötesti on vielä tekemättä.
- ~~Loaderin alamuistiratkaisu vaatii T5600-testin~~ **Tehty ja vahvistettu
  (23.9.2026, commit `fa5c461`).** Dynaaminen `AllocateAnyPages`-varaus +
  relokointi `ExitBootServices`in jälkeen, ei enää kiinteän osoitteen
  `AllocateAddress`-varausta — ks. yllä 23.9.2026. T5600 bootti onnistuneesti.
- Kernelin oma "lataa uusi kernel" -polku (`rebootcode.s`, `/dev/reboot`) käyttää yhä
  32-bittistä luovutusta, joka ei enää täsmää `_efi64`-sisäänmenon kanssa. `reboot()`
  paniikkaa nyt siististi ennen kuin mitään laitetilaa ehditään sotkea (commit
  `bc7b88b`) — turvallista, mutta itse 64-bittinen kexec-luovutus on yhä
  kirjoittamatta; `/dev/reboot` ei toimi ollenkaan ennen sitä.
- `BootInfo`-muistikartta on rajattu 600 riviin (`BootInfoMaxMem`). Kernel **reagoi**
  nyt ylivuotoon (`bootinfoinit()` pysäyttää koneen, commit `bc7b88b`) sen sijaan
  että käyttäisi katkennutta karttaa hiljaa — mutta itse 600 rivin riittävyyttä
  oikealla, pirstoutuneella UEFI-muistikartalla ei ole vielä arvioitu.
- **T5600 vahvistettu (23.9.2026): bootti onnistuneesti** commitilla
  `fa5c461` (alamuistin dynaaminen varaus + relokointi). Kernel on bootannut
  koko projektin ajan muulla raudalla (kaksi kannettavaa); tämä koski
  nimenomaan T5600:aa, joka epäonnistui johdonmukaisesti juuri diagnosoituun
  konfliktiin asti. Yksittäisiä kohtia (GOP-framebufferin PCI-BAR-korjaus,
  vaiheen 2 legacy-poistot yksitellen) ei ole vielä kaikkia erikseen
  T5600:lla varmistettu, mutta kokonaisuus käynnistyy. `pcboot`-pikatestikitti
  on vanhentunut rakenteen jälkeen; raudalla testaus tapahtuu
  `tools/build.sh` + `tools/test-qemu.sh` -tuloksen kopioinnin kautta
  (`tools/test-qemu.sh` jättää ESP:n imageksi `build/esp.img`, ks. README).

## K0: rivieditori ja kaksi konsolitilaa (24.9.2026)

- `aux/kbdfs` (`lineproc()`) sai rivieditorin: nuolet, Home/End, Delete, Backspace,
  `^K` leikkaa kursorista loppuun, `^U` leikkaa alusta kursoriin, `^W` sana,
  `^V` liitä, ylös/alas-historia (32 riviä, muistissa). rc ja `bootrc` ennallaan.
- `vga.c`: ESC[nC, ESC[nD, ESC[K, ESC[H ja ESC[2J. ESC[2J (kbdfs lähettää sen
  käynnistyessään) vaihtaa boot-lokitilasta (3 saraketta) interaktiiviseen tilaan:
  yksi täysleveä sarake, oikea scrollaus, pehmeästi rivitettyjen rivien seuranta
  (muokkaus rivinvaihdon yli) ja DOS-tyylinen ohjepalkki yläpalkissa.
  Boot-merkkiruudut saavat kadota interaktiivisessa tilassa.
- `devcons.c` (uusi seurattu kopio): ESC-alkuiset kirjoitukset eivät päädy
  kmesgiin, jotta editorin uudelleenpiirto ei täytä lokia.
- `tools/build.rc` kääntää kbdfs:n ja sitoo sen `/amd64/bin/aux`iin ennen
  kerneliä, jolloin se päätyy `bootfs.paq`iin.
- Testattu QEMUssa (sendkey): kursorin siirto, lisäys keskelle, Home/End,
  ^U/^K/^W/^Y, historia. Raudalla testaamatta. Huom: jos kirjoittaa ennen
  kehotetta (bootrc vielä tulostaa), editorin uudelleenpiirto sekoittuu tulosteeseen.

### Rivieditorin hionta (24.9.2026)

- Kursori piirretään pystyviivana merkkien väliin (ylösalaisin käännetty T:
  2 px viiva, lyhyt 4 px jalka alhaalla, ei yläpalkkia, jottei se sekoitu
  I-kirjaimeen). Solun pikselit tallennetaan ja palautetaan ennen jokaista
  tulostetta (`vga.c`, `txtcuron`/`txtcuroff`).
- Leikkaus siirrettiin Ctrl-yhdistelmistä Shift-näppäimille: Shift+Home leikkaa
  alkuun, Shift+End loppuun, Shift+←/→ yhden merkin kerrallaan (peräkkäiset
  painallukset kasvattavat leikattua tekstiä). Ei erillistä valintatilaa.
  Liitä on `^V`; `^K`, `^U` ja `^Y` poistuivat, `^W` säilyi. Shift+nuolet ja
  Shift+Home/End saavat kbdfs:ssä omat runet (`Lshiftesc1`-taulukko).

## Kehitysympäristö Debian 13:een (25.9.2026)

`docs/plan-linux-env.md`, vaihe 1. Windows-VM (WHPX), WSL, drawterm ja rcpu
poistettu kokonaan. Isäntä on Debian 13 + KVM, ja VM:ää ohjataan vain
sarjakonsolin tekstillä.

- `tools/vm-setup` lataa kiinnitetyn 9front-releasen (`tools/9front.release`,
  11952, sha256 julkaisutiedotteesta) ja asentaa sen `base.qcow2`:een ilman
  käsin tehtyjä askelia noin 2,5 minuutissa. `tools/build.sh` vie kertakäyttöisestä
  overlaysta käännöksen läpi noin 25 sekunnissa, ja `tools/test-qemu.sh`
  bootaa tuloksen noin 5 sekunnissa.
- Havainnot, joihin ratkaisut perustuvat:
  - 9frontin EFI-loader lukee myös sarjakonsolia (OVMF ohjaa ConInin COM1:een)
    ja odottaa `plan9.ini`n jälkeen yhden sekunnin näppäintä. Tällä asetetaan
    ISO-bootissa `console=0` ilman ISOn muokkausta. Asennusohjelma kopioi
    asetuksen asennetun levyn `plan9.ini`hin, ja `vm-setup` lisää
    `nobootprompt=` ja `user=`.
  - glendan profiili käynnistää rion aina. Jos VM:llä on näyttölaite (OVMF:n
    GOP-framebuffer), rio vie näppäimistön, ja sarjasyöte päätyy
    satunnaisesti fokuksessa olevaan ikkunaan. Siksi käännös-VM:ssä on
    `-vga none`: rio ei käynnisty, ja rc jää COM1:een.
  - Sarjasyöte lähetetään 8 tavun paloina, koska muuten UART pudottaa
    merkkejä. QEMU pysäyttää vierasjärjestelmän tulosteen, jos yhdistetty
    asiakas ei lue sokettia. Siksi `tools/9run` tyhjentää soketin jatkuvasti
    ja lukee sisällön QEMU:n lokista (`logfile=`). Lisäksi se lähettää rivin
    uudelleen, jos kaiku ei tule.
  - FAT ei kelpaa lähdekanavaksi, koska hakemisto `aux` on varattu
    DOS-laitenimi. Molemmat suunnat ovat siksi raakoja tar-levyjä
    (`sdE1` sisään, `sdE2` ulos). mtoolsia käytetään vain ESP-imageen.
  - `/dev/kvm`: pelkkä logindin ACL katoaa näytön lukittuessa, joten käyttäjä
    lisätään `kvm`-ryhmään.

## Asennusaikainen osajoukko (25.9.2026)

`docs/plan-linux-env.md`, vaihe 2. Tarkemmin: `docs/install-subset.md`.

- 9front lukitaan toistaiseksi versioon 11952, ja osajoukko kopioidaan repoon
  (`subset/9front/`, 2840 tiedostoa, 26 Mt). Kopio on koskematon
  upstream-kopio, ja omat muutokset tehdään edelleen `sys/`-hakemistoon.
- Johdettu kolmella tavalla: boot-säännöt, dynaaminen jäljitys (cwfs:n atime
  oikeassa asennuksessa) ja staattinen rc-analyysi. Aukot katettiin
  iteratiivisella testillä. Tuloksena 261 ajonaikaista tiedostoa, joista tehty
  asennus-ISO on 21 Mt (täysi 505 Mt). `tools/subset/test`: ISO asentaa
  järjestelmän, joka käynnistyy rc-kehotteeseen (PASS).
- Samalla korjattu sarjakanava: QEMU:n sokettiin yhdistää nyt koko VM:n ajaksi
  yksi `tools/9run --relay`. Aiemmin vaihtuvat asiakkaat kadottivat syötettä
  yhteyden alussa, pysäyttivät tulosteen ja saivat QEMU:n lokiin kahdennettuja
  merkkejä, mikä rikkoi kehotteiden tunnistuksen.
- Asennuksen skriptaus on yhteistä: `tools/vm-install` + `tools/inst.dialog`
  (käytössä `vm-setup`issa, jäljityksessä ja osajoukon testissä).
- Seuraavaksi asennusohjelmaa voidaan alkaa muokata. Muutokset tehdään
  `sys/`-kerrokseen, ja `tools/subset/test` toimii regressiotestinä.

## USB-asennin, vain UEFI (25.9.2026)

- Osajoukko bootattiin ensin 9frontin ISOn tavalla (ISO9660, BIOS-loaderit).
  Se oli ristiriidassa Plan2001:n periaatteen kanssa, ja Plan2001:n loader ei
  tue ISO9660:aa. Nyt media on **GPT-levykuva USB-tikulle** (`tools/subset/mkusb`):
  ESP:ssä on Plan2001:n `bootx64.efi` ja `9pc64`, ja Plan 9 -osiolla hjfs, jossa
  on osajoukko. `mk9660`, `9660srv`, `pbs`, `mbr`, `9bootfat` ja `bootia32.efi`
  on jätetty pois perusteineen (`subset/files`: `excluded`).
- `tools/subset/test`: asennus tikulta, ja asennettu levy bootaa rc:hen
  Plan2001:n loaderilla. PASS kahdesti peräkkäin samalla tikulla.
- Juurilevy valitaan `bootargs`-kehotteessa kuten 9frontissa. bootrc:n
  oletus on tikun `fs`-osio.
- Löydös: kirjoitettava tikku muisti edellisen asennuksen tilan
  (`/tmp/copydone`). Nyt ylimmän tason hakemistot ovat `distproto`n mukaan
  (`tmp d555`), joten `/tmp` on ramfs kuten ISOlla.
- Seuraavaksi siivotaan asennusohjelman legacy-haarat (mbr-vaihtoehto,
  pbs/9bootfat, 9660/cdboot) `sys/`-kerrokseen.

