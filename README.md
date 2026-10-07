# Plan2001

Plan2001:n ei rakenna Internetin vapaaehtoisista koneista luotettua infrastruktuuria. Se rakentaa tarkoituksella epäluotettavan kerroksen, jonka päällä on luotettava verifiointi

Plan2001 on moderni järjestelmä, joka perustuu 9frontiin ja
[Plan9-wasm32](https://github.com/pereuna/plan9-wasm32):een. Se kopioi
niistä koodia, ideoita ja protokollia, mutta ei ole yhteensopiva niiden
kanssa, ja legacy jää pois. Profiilit: palvelin (CPU, auth, fs) ja
terminaali (nyt wasm32-kone selaimessa ja/tai sarjakonsoli, lopulta
vahvasti autentikoitu ja autorisoitu HMI-istunto riittävällä GUI-resurssilla). Suhde
Plan9-wasm32:een ja vaiheet: [docs/plan9-wasm32.md](docs/plan9-wasm32.md).
