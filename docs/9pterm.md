# 9pterm

drawterm tekstipäätteenä: ei grafiikkaa, ei ääntä. Plan 9/9frontin
tavallinen rcpu (dp9ik, TLS-PSK, 9P), ei vielä WSS:ää eikä 2001P:tä.
Tavoite: käyttökelpoinen pääte ihmiselle ja AI:lle - yksittäiset komennot
ja interaktiivinen istunto. Tausta: `docs/ai/2026-09-30/`.

## Rakenne

Build-muoto repon drawtermiin (`monolith/third_party/drawterm`, pin
64dcc24), ei kopio: upstreamin korjaukset tulevat kaikille muodoille.

- `Make.9pterm`: Linux, `GUI=none`, `AUDIO=none`, `-DNINEPTERM`, TARG=9pterm
- `gui-none/`: näyttöfunktiot tyhjinä, `gscreen = nil`
- `cpu.c`: `NINEPTERM` → aina `-G`
- `monolith/tools/build 9pterm` → `monolith/build/9pterm/9pterm`
  (riippuvuudet vain libc ja libm)

Käyttö (sillin CPU-VM):

```
PASS=… 9pterm -h 127.0.0.1 -a 'tcp!127.0.0.1!5670' -u glenda -c 'cat /dev/sysname'
```

## Vaiheet

| # | Vaihe | Tila |
|---|---|---|
| 1 | Build ilman GUI:ta; `-c` toimii | tehty 30.9.: `echo hi` 0,23 s, väärä salasana 0,1 s |
| 2 | AI-ystävällinen `-c`: paluuarvo (tyhjä status 0, muu 1, status stderr:iin; nyt statuksen 1. merkin koodi, esim. 114), stderr erikseen (nyt stdoutissa), aikarajat (yhteys, tunnistautuminen, komento), `--pass-file`, EOF stdinissä | |
| 3 | Interaktiivinen istunto: raw-tila, `/dev/kbd` palvelimen kbdfs:lle, rivieditori ja Delete | |
| 4 | Tiedostot: `put`/`get` (`/mnt/term/root`), tarkistussumma | |
| 5 | Pilvi: rcpu 17019 ja auth 567 OCI:ssa, tai WSS myöhemmin | |
| 6 | Windows (`Make.win64` + gui-none) | |
