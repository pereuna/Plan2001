Keskustelun päätös:

Juuri näin. Tässä mallissa kannattaa säilyttää CPU-palvelin vanhassa Plan 9 -merkityksessä ja erottaa siitä kokonaan uusi käsite: palvelimeen liitetty laskentaresurssipooli.

CPU-palvelin olisi edelleen natiivi Plan2001-kone:

Plan2001 CPU server
├── kernel
├── processes
├── namespaces
├── filesystem
├── auth
├── scheduler
└── terminal/session services

Se voi olla pieni Raspberry Pi tai suuri palvelin. Sen tehtävä ei ole välttämättä olla varsinainen laskentateho, vaan omistaa järjestelmän tila, käyttäjät, namespace ja resurssien koordinointi.

Siihen liittyvät asiakkaat exporttaavat omat laskentaresurssinsa:

user A browser
├── CPU: 16 threads
├── GPU
└── NPU

user B browser
├── CPU: 24 threads
├── GPU
└── NPU

user C terminal
└── native remote CPU

              ↓

        Plan2001 CPU server

              ↓

        shared compute pool

Oleellinen käännös vanhaan malliin on juuri tämä:

Plan 9 noin 1990:

many terminals
      ↓
large CPU server
      ↓
shared expensive compute

uusi Plan2001:

many powerful terminals
      ↓
small/ordinary CPU server
      ↓
aggregated terminal compute

Eli:

OLD:
users → compute

NEW:
users ←→ compute

Käyttäjä ei ole enää vain resurssin kuluttaja. Jokainen kirjautunut käyttäjä voi samalla tuoda järjestelmään CPU-, GPU- ja NPU-kapasiteettia.

Namespace voisi näkyä näin

CPU-palvelimen globaalissa resurssinimiavaruudessa:

/compute
├── browser
│   ├── session-123
│   │   ├── cpu
│   │   ├── gpu
│   │   └── npu
│   ├── session-124
│   │   ├── cpu
│   │   └── gpu
│   └── ...
│
├── native
│   ├── server2
│   └── server3
│
└── local
    ├── cpu
    └── gpu

Tai vielä abstraktimmin:

/compute/001
/compute/002
/compute/003
...

ja jokainen resurssi kertoo ominaisuutensa:

/compute/217/type       browser-gpu
/compute/217/api        webgpu
/compute/217/memory     8192M
/compute/217/owner      session-123
/compute/217/state      idle

Schedulerin ei tarvitse tietää, että resurssi on fyysisesti Helsingissä Chrome-selaimessa tai PCIe-väylässä paikallisessa koneessa.

Tärkeä ero käyttäjä- ja globaalin namespacen välillä

Itse tekisin kaksi näkymää:

/global/compute

sisältää kaikki CPU-palvelimeen liitetyt resurssit.

Yksittäisen käyttäjän namespaceen bindataan politiikan sallima osa:

/compute

Esimerkiksi tavallinen käyttäjä voisi nähdä:

/compute/shared/*
/compute/self/*

mutta ei välttämättä toisen käyttäjän resurssin hallintaliittymää suoraan.

Scheduler sen sijaan näkee koko poolin.

Tämä säilyttää Plan 9:n perusidean:

> resurssi tulee prosessin näkyviin namespace-polun kautta.



Mutta resurssi ei enää tarkoita vain tiedostoa, laitetta tai verkkoa vaan myös suorituskykyä.

Selain on tässä erityisen hyvä resurssin exportteri

Selain tekee vaikean osan valmiiksi:

browser sandbox
├── CPU threads → WASM/WebWorkers
├── GPU         → WebGPU
├── NPU         → browser/platform API
├── memory
└── encrypted network connection

Plan2001:n ei tarvitse asentaa asiakkaalle ajuria tai daemonia.

Selain muodostaa yhteyden:

browser → Plan2001

ja ilmoittaa esimerkiksi:

I provide:
    wasm workers = 24
    webgpu adapter = RTX 5070
    gpu memory = ...
    npu = available

Palvelin rekisteröi nämä:

browser session
        ↓
resource registration
        ↓
/compute/browser/xyz

Kun välilehti sulkeutuu:

/compute/browser/xyz

häviää.

Tämä sopii erittäin hyvin Plan 9:n ephemeral-resurssiajatteluun.

100 käyttäjän järjestelmä

Jos vaikka jokaisella on keskimäärin:

CPU   16 thread
GPU   10 TFLOPS

niin palvelin voisi hallita:

1600 CPU threads
~1000 TFLOPS GPU

vaikka itse Plan2001-palvelin olisi:

Raspberry Pi
4–8 cores
8 GB RAM

Raspberry Pi:n tehtävä olisi tällöin enemmän:

namespace server
auth server
filesystem server
scheduler
job coordinator

ei varsinainen laskentakone.

Tämä muistuttaa tavallaan cluster head nodea, mutta Plan2001-mallissa olennainen ero olisi, että resurssit integroidaan namespaceen, eivät erilliseen cluster-management-järjestelmään.

Tässä on mielestäni arkkitehtuurin keskeinen lause

Plan2001 CPU server ei ole se kone, jossa suurin laskentateho sijaitsee. Se on kone, joka omistaa laskentaympäristön.

Ja:

käyttäjien päätelaitteet eivät ole vain terminaaleja, vaan CPU-palvelimen dynaamisesti liittyviä laskentasolmuja.

Se on varsin elegantti käännös alkuperäisestä Plan 9:n maailmasta:

1990:
powerful server + dumb terminals

Plan2001:
small server + powerful terminals
             ↓
        one shared computer

Tässä alkaa jo olla mielestäni selkeä Plan2001:n oma arkkitehtoninen idea, eikä vain modernisoitu Plan 9.
