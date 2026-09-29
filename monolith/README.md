# Monolith

**Plan2001:n drawterm selaimessa.** Selain on Plan2001:n terminaali: se yhdistää
Plan2001:een cpu-palvelimena ja tarjoaa sille näytön, hiiren ja näppäimistön,
kuten drawterm tekee X11:n, Waylandin tai Win32:n päällä. CPU-palvelin
mounttaa ne `/mnt/term`iin. Plan2001:ssä ei ole rioa: jokainen ohjelma on
oma välilehtensä ja originsa (`https://acme.kone:17443/`), ja selain on
ikkunamanageri.

Nimi tulee Avaruusseikkailu 2001:stä: musta laatta, jonka kautta toinen
maailma tulee näkyviin. Monolith on osa [Plan2001](https://github.com/pereuna/Plan2001):tä
(hakemisto `monolith/`); se oli ennen erillinen repo pereuna/monolith.

## Miksi näin

drawterm jättää jo nyt kaikki laiteajurit asiakkaalle: grafiikan, syötteen ja
äänen hoitaa X11, Win32 tai Cocoa. Selain on tähän luonteva asiakas, koska sen
alla ovat valmiina GPU, NPU, näyttö, ääni ja kamera (WebGPU, WebNN ja muut
selainrajapinnat). Plan2001:n kerneliin ei tarvitse tuoda moderneja
GPU-ajureita, vaan ne jäävät selaimen ja asiakkaan käyttöjärjestelmän vastuulle.

```
Plan2001 CPU server
        │  9P (rcpu nyt, WSS myöhemmin)
        ▼
Selain: Monolith
   ├── JavaScript: WebSocket, näppäimistö, hiiri, leikepöytä, canvas/WebGPU
   └── WASM: drawterm (auth, 9P, /dev/draw, libmemdraw, libmemlayer)
```

## Tila

Vaihe 1: **9frontin drawterm kokonaisena WASMiksi** Emscriptenillä, ja
drawtermiin uusi `gui-web`-näyttöbackend. `drawterm.wasm` selaimessa kirjautuu
Plan2001:een (dp9ik, TLS, 9P). Vaihe 2: oma WSS-transportti ja
jatkuvat istunnot. Vaihe 3a: välilehti = Plan2001-ohjelma (origin `APP.kone`, `docs/app-origins.md`), ja
`apps` listaa istunnot. Käyttö: [docs/roadmap.md](docs/roadmap.md#käyttö). Suunnitelma ja vaiheet:
[docs/roadmap.md](docs/roadmap.md). Arkkitehtuuri:
[docs/architecture.md](docs/architecture.md).

## Lähteet

- `third_party/drawterm/`: 9frontin drawterm, vendoroitu pinnatusta
  commitista (`third_party/drawterm/VERSION`), MIT-lisenssi
  (`third_party/drawterm/LICENSE`). Päivitys: `tools/import-drawterm`.
- 9webdraw on JavaScript-referenssi (selain draw(3)-palvelimena,
  binäärinen WebSocket ja canvas), ei pohja.

## Lisenssi

MIT, ks. `LICENSE`. Vendoroitu drawterm on omalla MIT-lisenssillään.
