# Sovellus on origin

Plan2001:n selainsovellusten turvamalli (Monolith). Tausta:
`docs/cpu-server-design.md` (CPU-palvelin, laskentapooli) ja
`monolith/docs/architecture.md` (vaihe 3: välilehti on ohjelma).

## Periaate

> Selaimen origin on sovelluksen turva-alue. Palvelin päättää sovelluksen
> originista; asiakas ei kerro palvelimelle, mikä turva-alue se haluaa olla.

```
https://cad.cpu.example
        │ määrittelee
        ▼
CAD-turva-alue: /lib/app/cad/{image,namespace,policy}
        │
        ├── välilehti / prosessi 101 → nimiavaruus, klooni A
        └── välilehti / prosessi 117 → nimiavaruus, klooni B
```

Origin ei ole yksi elävä nimiavaruus vaan **pohja**: jokainen välilehti
saa oman rcpu-istunnon ja siten oman nimiavaruutensa (Plan 9:n
prosessikohtainen nimiavaruus), joka rakennetaan sovelluksen pohjasta. Kaksi
CAD-välilehteä ovat kaksi prosessia, eivät yksi.

## Sovellus Host-headerista

`https://APP.kone/` → webterm lukee `Host: APP.kone`, ja sovellus on APP,
jos `/lib/app/APP` on olemassa (muuten `term`). `/app/APP` ja `?app=`
poistuvat. Palvelin ei aja asiakkaan lähettämää komentoa: rcpu-skripti on
webtermin oma (liittää päätteen `/mnt/term`iin kuten drawtermin skripti),
ja se ajaa sovelluksen pohjan:

```
/lib/app/APP/
    image       ohjelma (rc-skripti tai binääri), jonka välilehti ajaa
    namespace   rc-skripti: mitä prosessin nimiavaruuteen sidotaan
    policy      mitä originin WebSocketit saavat avata
```

Esimerkki, CAD:

```
policy:     rcpu compute
namespace:  bind /global/compute/webgpu /compute/gpu
            bind /global/compute/webnn /compute/npu
```

Editori: `policy: rcpu`, eikä nimiavaruudessa ole `/compute`:a lainkaan.

> Resurssi, jota nimiavaruudessa ei ole, ei ole käytettävissä.

## Origin → palvelut

webtermin Origin-tarkistus (Origin = `https://` + Host) estää muiden
sivustojen WebSocketit (CSWSH), mutta originit ovat nyt eri sovelluksia,
joten jokaisella on omat palvelunsa (`policy`):

| policy | WebSocketit |
|---|---|
| `rcpu` | `/rcpu`, `/resume/…`, `/567` (auth) |
| `cr` | `/17030` (laskentapooli, CR:n rekisteröinti) |

`cad.kone` ei saa avata `/17030`:aa, ellei sen policy salli sitä.

## Istunto sidotaan originiin

Istunnolla on käyttäjä, sovellus, origin ja capability (jatkotunniste).
`/resume/TOKEN/N` hyväksytään vain, kun pyynnön Origin on istunnon origin
ja tunniste on oikea: editorin origin ei voi käyttää CAD:n tunnistetta.

## Laskentapooli kahdella tasolla

- `/global/compute`: palvelimen ja ajoittajan näkymä, kaikki CR:t (`crsrv`).
- `/compute`: prosessin näkymä, sovelluksen `namespace`-tiedoston sitoma osa.
  `rcc` käyttää `/compute/cc`:tä.

CR:n rekisteröinti on oma originsa (`compute.kone`, policy `cr`). CR:n
omistaja tulee tunnistautumisesta, ei selaimelta (`?owner=` poistuu), ja
palvelin antaa CR:lle identiteetin; `crsrv -k` ei ole turvamekanismi.

## Selaimen rajapinnat

Nimiavaruus rajaa Plan2001:n resurssit, mutta originin JavaScript voisi
silti kutsua esimerkiksi `navigator.gpu`:ta. Siksi sovellus ei ole
mielivaltaista JavaScriptiä: luotettu Plan2001-host (nyt drawterm.wasm ja
sivu, myöhemmin host.js) antaa wasm-sovellukselle vain sen policyn
sallimat importit (fs, ui, compute/webgpu, …). Permissions-Policy-otsake on
toinen suojakerros, ei capability-malli.

## Selaimen tallennus: `/local`

Originin IndexedDB/OPFS on luonteva `/local`: eri originien tallennus ei
näe toisiaan. Jos samassa selaimessa kirjautuu useampi Plan2001-käyttäjä,
he jakavat originin tallennuksen; siksi `/local` on aluksi vain
välimuisti, ei auktoritatiivista tai salaista dataa. (Tiukempi malli:
palvelimen antama läpinäkymätön turva-alue originissa, `cad-7fa31.kone`.)

## Kehitysympäristö

Chrome ohjaa `*.localhost`-nimet loopbackiin ilman DNS:ää:
`https://term.localhost:17443/`, `acme.localhost`, `clock.localhost`,
`compute.localhost`. Varmenteeseen nimet luetellaan erikseen
(`CPU_CERT_SANS`; `*.localhost`-jokerimerkkiä selaimet eivät hyväksy).

Lähiverkossa: `PLAN2001_LAN=IP` (`tools/vm`, `tools/vm-cpu`, `tools/deploy`)
avaa https-portin 17443 myös osoitteeseen IP ja nimeää sovellukset
`APP.IP.nip.io` (nip.io:n nimipalvelu palauttaa IP:n), esim.
`https://compute.10.80.73.196.nip.io:17443/cr.html`. Muiden koneiden
selaimet luottavat kehitys-CA:han (`https://…/plan2001-ca.crt`) tai
hyväksyvät varoituksen kerran kutakin originia kohden.

## Eteneminen

Haara `origin-apps` (29.9.): kohdat 1–10 tehty, `monolith/tools/test-apps`
PASS (myös `--drop`) ja `monolith/tools/test-compute` PASS compute-originista.
Toteutus: webterm (`hostapp`, `allowed`, oma rcpu-skripti `appscript`,
istunnon `origin`), sovelluspohjat `lib/app/`, `tools/vm-cpu` (asennus,
`*.localhost`-nimet varmenteeseen). Testi tarkistaa lisäksi, että sivu, joka
pyytää drawtermilla `-c 'sleep 777'`, saa silti originin sovelluksen.
Kohdat 9–10: `crsrv` on palvelimen nimiavaruudessa `/global/compute`
(`cpustart`), ja termin `namespace` liittää sen `/compute`:ksi; kellolla
sitä ei ole (testi katsoo prosessin `/proc/N/ns`:stä). `rcc` käyttää
`/compute/cc`:tä eikä liitä poolia itse.

1. `*.kone` → sama webterm
2. Host määrää sovelluksen
3. `/app/APP` ja `?app=` poistuvat; palvelin ajaa sovelluksen pohjan, ei
   asiakkaan komentoa
4. istuntoon origin ja sovellus
5. jatko (resume) tarkistaa originin
6. `acme.localhost` ja `clock.localhost` ovat eri originit
7. testi: niiden IndexedDB:t eivät näe toisiaan
8. testi: acmen tunniste ei toimi clockin originista

Sitten:

9. `/global/compute` palvelimelle
10. sovelluskohtainen `/compute`
11. `compute.kone`: tunnistautunut CR:n tarjoaja
12. wasm-sovellusten host-rajapinta → `/compute/gpu`
