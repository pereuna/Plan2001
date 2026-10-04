# Plan2001 Boot ABI v1: wasm32-entry

Tämä on wasm32:n osuus Plan2001 Boot ABI v1:stä. Data-ABI (BootInfo-blob)
on yhteinen kaikille ISA:ille: `docs/boot-abi.md`. wasm32:lla
**selainsivu on firmware ja loader**: `platform.js`:n `boot()` (sen
`firmware()`) tekee saman BootInfo-datan kuin UEFI-loader amd64:llä ja
arm64:llä. `BootInfo.arch` = `BootArchWasm32` (4).

## Entry

Kernel (`9wasm32.wasm`, 3c ja `3l -k`) on WebAssembly-moduuli, joka tuo
muistinsa (`platform.memory`, jaettu, yksi kaikille Workereille). Sivu
käynnistää sen ensimmäisellä Workerilla:

| | |
|---|---|
| `_init()` | ensin, kerran: kernelin data muistiin (passiivinen segmentti) |
| `sp` | `(stacktop − 16) & ~7` |
| muisti osoitteessa `sp` | **BootInfo-blobin osoite** (32 bittiä, little endian) |
| `_start()` | kutsuu `main(pa)`:ta (`3l -E main`) |

Argumentti on siis C-kutsun ensimmäinen argumentti wasm32:n
kutsukonventiolla (argumentit muistissa SP:stä alkaen), samoin kuin
`platnewproc`in proseille. Muuta tilaa ei ole: ei keskeytyksiä eikä
sivutauluja. Osoite on lineaarisen muistin osoite, joka on wasm32:lla
"fyysinen" osoite (`bootearlymap()` palauttaa sen sellaisenaan,
`plan9/sys/src/9/wasm32/bootarch.c`).

## Muistin asettelu

Sivu tekee muistin ja kirjoittaa siihen ennen entryä:

```
0          kernelin moduuli: data, bss, pino (stacktop), ...   Conventional
           [end, 64 MB) kernelin keko                          (sama alue)
64 MB      levyn rekisterit (4 KiB), jos levy on                Reserved
           BootInfo-blob (header, config, muistikartta)        LoaderData
           juuren arkisto (#R, devrootfs.c), rdbase/rdlen       LoaderData
           kehyspuskuri, XRGB32, fbstride = fbwidth             Reserved
```

Jokainen alue on sivutasattu (4 KiB). Kartan Conventional-alue alkaa
nollasta, koska sivu ei lataa kernelin imagea: WebAssembly instansioi
moduulin itse, eikä sivu tiedä, mihin sen data päättyy. Kernel jättää
`[0, end)` itselleen (`confinit`, `main.c`), kuten muutkin kernelit
varaavat oman imagensa.

## Mitä BootInfossa on

| Kenttä | wasm32 |
|---|---|
| config | plan9.ini-teksti: `init=` (initin argv, rc:n lainaus, jonka `tokenize` ottaa) ja sivun `front.config`-rivit. Kernel lukee sen `getconf`illa (`port/bootargs.c`), ja ympäristöön se menee kuten 9frontissa (`setconfenv`). Ilman `init=`iä kernel pysähtyy C2a:n testin loppuun. |
| loader log, FDT, ACPI, `tscfreq` | ei ole (0) |
| `epoch` | `Date.now()` sekunteina |
| `rngseed` | 64 tavua `crypto.getRandomValues`ista; kernel sekoittaa sen `hwrandbuf`in kautta (`bootinforandinit`) ja nollaa |
| framebuffer | `fbbase`, `fbwidth` × `fbheight`, `fbstride` = `fbwidth`, 32 bittiä, `x8r8g8b8`. Kernel piirtää siihen (`screen.c`: `allocmemimaged` sen päälle) ja kertoo `platflush`illa, mikä muuttui; sivu piirtää sen canvasille. `fbbase` = 0: ei ruutua. |
| config: `*sdW0=regs tavut` | koneen levy (D6, `devsdw.c`): levyn rekisterisivu ja koko. Sivu on kartassa Reserved, heti 64 Mt:n jälkeen ennen blobia. Ei levyä: ei riviä eikä sivua. |
| `rdbase`, `rdlen` | juuren arkisto (`build-bin3`:n `root.fs`), jonka `devrootfs.c` lukee paikallaan. Lisätty headerin loppuun (v1). |

`plat*`-kutsuista poistuivat `platbootfs`, `platbootargs`, `platscreen`
ja `platfb`: niiden tieto on nyt BootInfossa. Jäljelle jäävät platform.h:n
laitteet (konsoli, Workerit, Atomics, verkko, näppäimistö, hiiri,
`platflush`, `platcursor`) ja `platnsec`/`platrandom`, joita kernel
käyttää ajon aikana kellona ja satunnaislukulähteenä.

## Kernelin puoli

- `main(uintptr pa)` asettaa `bootinfopa = pa` ja kutsuu
  `bootinfoinit()`, sitten `bootargsinit()`. Molemmat ovat samat kuin
  pc64:llä ja arm64:llä (`plan9/sys/src/9/port/bootinfo.c`,
  `bootargs.c`).
- Blob, jota kernel ei hyväksy, pysäyttää koneen. Syitä ovat väärä
  magic, versio tai `arch`, osio blobin ulkopuolella, tyhjä muistikartta,
  arkisto muualla kuin LoaderData-alueella ja framebuffer jaettavassa
  muistissa (`docs/boot-abi.md`, yhteensopivuus). Pysähdys on:
  `halt()` → `plathalt("bootinfo: no blob this kernel can take")`.
- `bootearlymap(pa, size)` hyväksyy vain sen, mikä on muistissa
  (`platmemsize()`, WebAssembly.Memoryn nykyinen koko), ei koko 4 GiB:n
  osoiteavaruutta. `confinit` leikkaa kartan muistin kokoon, ja
  `screen.c` sekä `devrootfs.c` tarkistavat alueensa sillä. Kartta, joka
  lupaa enemmän kuin muistissa on, ei siis johda wasm:n OOB-trappiin.
- `confinit` tekee `conf.mem[]`:n kartan Conventional-alueista
  (`[end, …)`), ja keon koko tulee niistä.
- `timersinit`in jälkeen `bootinforandinit()` ja `bootinfoclock()`.
