# Plan2001

9frontin UEFI-fork kolmelle raudalle, AMD64, ARM64 ja RISC-V64, sen
muokattu uusi kernel ja kernelin kehitystyökalut. Plan2001 ei ole
9front-yhteensopiva, ja legacy jää pois. Arkkitehtuuri:
[docs/architecture.md](docs/architecture.md).

Repo on Plan 9 -juuri, joka kääntää itsensä: kernel, UEFI-loader,
työkaluketju, kernelin testaus ja profilointi sekä git ja verkko lähteiden
hakuun ([docs/selfhost.md](docs/selfhost.md)).
Sovellukset ja jakelu (9frontin julkaisukopio, asennin, palvelut, 9pterm)
ovat Monolithissa. Selaimen Plan 9 on
[Plan9-wasm32](https://github.com/pereuna/plan9-wasm32).
Jako: [docs/repos.md](docs/repos.md).

Käännös ja testaus: [docs/README.md](docs/README.md) ja `AGENTS.md`.

Historia: Plan2001 aloitti uuden git-historian 17.10.2026 (vain tarvittavat
tiedostot AI:n kontekstiin). Vanha historia (main `94028e8` asti, haarat ja
tagit) on repossa `pereuna/Plan2001-history` ja bundlena
`~/.cache/plan2001/history/`. Dokumenttien sitä vanhemmat commit-tunnukset
viittaavat siihen.

