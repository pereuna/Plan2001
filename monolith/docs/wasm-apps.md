# Plan 9 -ohjelmat WebAssemblyna drawtermin ytimellä

Selain ajaa 9frontin ohjelman ilman CPU-palvelinta: ohjelma linkitetään
drawtermin ytimeen cpu.c:n tilalle, sen järjestelmäkutsut ovat drawtermin
(`include/user.h`: open on sysopen jne.) ja `/dev/draw` on drawtermin oma,
eli selaimen canvas. Ensimmäinen ohjelma on `clock` (1.10.2026).

    tools/build wasm-clock          build/wasm-clock/clock.{js,wasm}
    tools/serve                     http://127.0.0.1:18090/?wasm=clock
    tools/test-wasmapp clock        headless Chromium, aito aika: piirtääkö
    tools/test-wasmclang clock      sama, mutta clock.c käännettynä clang.wasm:lla

## Rakenne

- `wasmapp/appmain.c`: cpubody liittää `#i` (draw) ja `#m` (mouse)
  /dev:iin ja kutsuu ohjelman mainia (`-Dmain=wasmappmain`).
- `wasmapp/plan9.h`: mitä 9frontin libc.h:ssa on ja drawtermin ei (PI,
  Tm/localtime); `wasmapp/p9libc.c` niiden toteutus.
- `third_party/9apps/`: ohjelmat muuttamattomina (clock.c).
- `wasmapp/wasmapp.mk`: drawtermin oliot ilman cpu.o:ta + ohjelma.

## Yksi libdraw

Drawtermin libdraw oli vanha osajoukko 9frontin libdraw'sta ja sillä oli
oma draw.h. Kun clock linkitettiin molempien kanssa, kaksi erilaista
Display-rakennetta (9frontin `usrlock`) jumitti `namedimage`:n. Nyt
`third_party/drawterm/libdraw` on 9frontin libdraw ja `include/` 9frontin
draw.h, event.h, mouse.h, keyboard.h; RWLock on ytimen oma `lib.h`:ssa.
Erot 9frontiin: `libdraw/README.9front`. Tärkein muutos: event.c:n
orjaprosessit (hiiri, näppäimistö, ajastin) ovat kprocceja, koska
selaimessa ei ole forkia. libthreadia käyttävät tiedostot ovat mukana
mutta niitä ei käännetä.

## Mitä clock vaati

- clang ei hyväksy enumia int-parametrin paikalle kuten Plan 9:n C:
  stringbg.c:hen `Drawop op` (4 funktiota).
- libc-aukot: PI, localtime/Tm, access-tilat (AREAD...).
- fork/rfork/notes: event.c kprocceiksi. Sama työ libthreadille on
  seuraava iso askel: useimmat graafiset 9front-ohjelmat käyttävät sitä.
- Koko: clock.wasm 680 kB, josta suurin osa on drawtermin ydintä; nyt
  jokainen ohjelma on oma kokonainen tiedostonsa.

## Testi: kääntäjä selaimessa (clang.wasm), 1.10.2026

Kysymys: voisiko selaimen clang.wasm kääntää järjestelmän käyttäjätilan
wasm-ohjelmiksi? `tools/test-wasmclang` kääntää ohjelman clang.wasm:lla
Nodessa, kuten selain sen ajaisi: ei levyä, vain muistissa annetut
tiedostot (`tools/wasmclang-cc.mjs`). Emcc (natiivi wasm-ld) linkittää
syntyneen olion omansa tilalle ja test-wasmapp tarkistaa, että kello
piirtyy. Lopuksi buildiin palautetaan emcc:n olio.

Kääntäjä: YoWASP `@yowasp/clang` 22.0.0-git20542-10 (LLVM 22, clang ja
lld), haetaan kerran `~/.cache/plan2001/yowasp-clang/`.

| Osa | Pakkaamaton | gzip |
|---|---|---|
| llvm.core.wasm (clang + lld) | 75,5 MB | 22,7 MB |
| llvm-resources.tar (otsakkeet; enimmäkseen libc++) | 29,7 MB | 4,2 MB |
| npm-paketti yhteensä | 101 MB | 27 MB |
| vertailu: 6c.wasm (Plan 9 -kääntäjä selaimessa) | 0,22 MB | 0,09 MB |

Tulos: PASS. clock.c sellaisenaan kääntyi 2,7–3 sekunnissa; sisään 42
tiedostoa, 199 kB otsakkeita (drawtermin ja 32 Emscriptenin sysrootista;
clangin omat sisäänrakennetut otsakkeet ovat clang.wasm:n), ulos 4 kB
olio. LLVM 22:n olio kelpasi LLVM 19:n wasm-ld:lle sellaisenaan.

Mitä selvisi:

- YoWASP:n oletuskohde on WASI (`wasm32-wasip1`), mutta
  `--target=wasm32-unknown-emscripten` ja emcc:n omat liput (`-pthread
  -matomics -mbulk-memory`, sysrootin include-polut; ks. `emcc -v`)
  riittävät: olio on drawtermin ytimen kanssa yhteensopiva.
- Ohjelmakohtainen kääntäminen selaimessa on kevyttä: kääntäjä (27 MB)
  ladataan kerran, ohjelma tarvitsee vain omat lähteensä ja ~200 kB
  otsakkeita.
- Vielä natiivia: linkitys. lld on paketissa, mutta drawtermin ydin
  (valmiiksi käännetty kirjasto) ja Emscriptenin JS-liima (clock.js)
  tekee nyt emcc. Seuraava askel: ydin yhdeksi valmiiksi linkitettäväksi
  kirjastoksi ja sama JS-liima kaikille ohjelmille, jolloin lld.wasm voi
  linkittää selaimessa.
- Vaihtoehto clang.wasm:lle on 6c:n wasm-taustaosa: satoja kilotavuja ja
  9frontin C sellaisenaan, mutta koodigeneraattori ja linkitys pitäisi
  kirjoittaa itse.

## Jatko: 3c (1.10.2026)

clang.wasm jää vertailutoteutukseksi. Plan2001:n oma suunta on 3c ja 3l:
WebAssembly on arkkitehtuuri muiden joukossa (`objtype=wasm32`, .3), ja
kääntäjä on 9frontin cc-frontend omalla backendillä. 3c kääntää 9frontin
libc:n sekä itsensä: 3c.wasm on 447 kt, kun clang.wasm on 75 Mt. Ks.
../../docs/wasm32.md.
