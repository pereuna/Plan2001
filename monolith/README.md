# monolith/

Linuxin työkalut Plan2001:n wasm32-koneelle ja CPU-palvelimelle. Selaimessa
ajetaan Plan2001:n wasm32-ydintä (`plan2001/sys/src/9/wasm32`), jonka sivu
(`kernel.html`, `platform.js`) on koneen firmware. Pääte ja sovellusten
originit ovat wasm32-koneita (`docs/architecture.md`, vaiheet C ja D;
`docs/app-origins.md`).

- `tools/build-cc`, `build-libc3`, `build-3c3`, `build-9wasm32` ja
  `build-bin3`: 3c/3l, kirjastot, ydin ja juuri (`build/wasm32`).
- `tools/test-9wasm32` (headless Chromium, `test-wasmapp`,
  `test-authsrv`), `test-3c` ja `test-cc`. Testausohje on repon
  `AGENTS.md`:ssä.
- `tools/pages` kokoaa sivun, ja `deploy` asentaa sen CPU-VM:ään, jossa
  rc-httpd tarjoaa sen. `tools/serve` on paikallinen kehityspalvelin.
- `tools/build 9pterm`: natiivi drawterm tekstipäätteenä pilven hallintaan
  (`third_party/drawterm`, `docs/9pterm.md`).

Monolith oli ennen erillinen repo (pereuna/monolith), joka toi drawtermin
selaimeen. Sen vaiheet ovat `docs/`:ssa. Drawtermin selainosat, host3,
wasmapp ja Monolithin JS poistettiin D7:ssä (4.10.2026).
