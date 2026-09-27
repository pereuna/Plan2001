# Monolith

**Plan2001:n drawterm selaimessa.** Chrome on Plan2001:n terminaali: se yhdistää
Plan2001:een cpu-palvelimena ja tarjoaa sille näytön, hiiren ja näppäimistön,
kuten drawterm tekee X11:n, Waylandin tai Win32:n päällä. CPU-palvelin
mounttaa ne `/mnt/term`iin, ja rio, sam ja acme toimivat tavalliseen tapaan.

Nimi tulee Avaruusseikkailu 2001:stä: musta laatta, jonka kautta toinen
maailma tulee näkyviin. Tämä on sen rinnakkaisprojekti,
[Plan2001](https://github.com/pereuna/Plan2001).

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
Chrome: Monolith
   ├── JavaScript: WebSocket, näppäimistö, hiiri, leikepöytä, canvas/WebGPU
   └── WASM: drawterm (auth, 9P, /dev/draw, libmemdraw, libmemlayer)
```

## Tila

Vaihe 1: **9frontin drawterm kokonaisena WASMiksi** Emscriptenillä, ja
drawtermiin uusi `gui-web`-näyttöbackend. Suunnitelma ja vaiheet:
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
