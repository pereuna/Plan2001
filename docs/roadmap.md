# Vaiheet

| Vaihe | Tavoite | Hyväksyntä |
|---|---|---|
| 1a | Natiivi drawterm → Plan2001-VM cpu-palvelimena | `drawterm -G -c 'echo MONOLITH-OK'` tulostaa merkin |
| 1b | `drawterm.wasm` ilman grafiikkaa (`-G`) Chromessa | headless Chromiumin konsolilokissa `MONOLITH-OK` |
| 1c | `gui-web`: rio selaimessa | headless-testi raportoi rion flushin, ja käyttäjä kokeilee Chromessa |
| 2 | JS + WASM -raja, oma WSS-transportti, Plan2001:n webterm-kuuntelija, DP9IK ilman sisäkkäistä TLS:ää | selain → WSS → rio ilman WS→TCP-siltaa |
| 3 | WebGPU: ensin esitys, sitten GPU-backend pikselivertailulla | referenssikuvat vastaavat |

## Kehitysympäristö

- Plan2001-repon VM: `tools/vm --net` ja `tools/vm-cpu` (cpu- ja
  auth-palvelin, porttiohjaukset rcpu 17019 ja auth 567).
- Emscripten 3.1.69 (Debian), Chromium (headless-testit), WS→TCP-silta
  vaiheissa 1b ja 1c.
