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

| policy | kyky | mitä se antaa |
|---|---|---|
| `rcpu` | terminal | WebSocketit `/rcpu`, `/resume/…`, `/567` (auth): graafinen istunto |
| `cr` | compute-provider | WebSocket `/17030`: selain tarjoaa laskentaa pooliin (CR) |
| `cpu` | wasm32-pääte | WebSocket `/17019`: päätteen oma rcpu omassa TLS:ssään |
| `secstore` | avaimet | WebSocket `/5356`: secstore, josta koneen `/boot/init` hakee factotumin avaimet (`docs/architecture.md`, "Avainten paikka") |
| `compute` | compute-consumer | istunnon nimiavaruuteen `/compute` (webterm antaa `$computepool=1` sovelluksen namespace-tiedostolle) |

Kyvyt ovat erillisiä: yksi ei anna toista. `term.kone` = `rcpu cpu compute secstore`,
`compute.kone` = `cr`, editorit = `rcpu`.

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
mielivaltaista JavaScriptiä: luotettu Plan2001-host (D7:stä lähtien
wasm32-koneen firmware, `kernel.html` ja `platform.js`) antaa wasm-sovellukselle vain sen policyn
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

## D7: originin kone on wasm32-kone (4.10.2026)

Drawterm on poissa selaimesta. Jokainen origin saa saman sivun, joka
käynnistää wasm32-koneen (`plan9/sys/src/9/wasm32`):

- Sivu kysyy palvelimelta sovelluksensa (`GET /app`, rc-httpd:n
  `plan2001-app`). Päätös on sama kuin webtermin `hostapp`: `APP`, jos
  host on `APP.KONE` ja `/lib/app/APP` on olemassa, muuten `term`. Sivu
  kirjoittaa sen koneen BootInfoon (`app=`, `cpu=`).
- `term`: kone ajaa omaa rioaan ja rcpu:ta (`rcpu` ilman `-h`:ta menee
  `$cpu`:hun). rcpu kulkee `/17019`:n kautta TLS-PSK:lla. Sen sallii
  policyn sana `cpu`, joka on vain `term`illä.
- `APP`: `/boot/app` ajaa rcpu:n, jonka yhteys on `aux/wsrcpu`
  (`/boot/rconnect.app`): webtermin `/rcpu`-istunto, p9any ilman
  TLS:ää, koska yhteys on jo wss. Webterm ajaa sovelluksen namespace- ja
  image-tiedostot, ei asiakkaan skriptiä. Sovellus piirtää koneen
  ruudulle, ja näppäimistö kulkee koneen konsolin kautta. Istunnon loppu
  on rcpu:n palvelimen kaltainen: cpunote, tila ja `rfailed`.
- Sivu hoitaa istunnon protokollan (`s`, `r`, `a`, `e`, `x`): kuittaukset,
  lähetetyn tallessa pitämisen ja jatkon (`/resume/TOKEN/N`), jos
  WebSocket katkeaa. Ydin näkee yhden yhteyden. Testi on
  `tools/test-9wasm32 app`.
- `compute.kone` (CR selaimessa, `cr.html`) poistui Monolithin JS:n mukana.
  D8 odottaa uutta suunnitelmaa (`docs/architecture.md`, "D8 odottaa").

## Eteneminen

Haara `origin-apps` (29.9.): kohdat 1–10 tehty, `monolith/tools/test-apps`
PASS (myös `--drop`) ja `monolith/tools/test-compute` PASS compute-originista.
Toteutus: webterm (`hostapp`, `allowed`, oma rcpu-skripti `appscript`,
istunnon `origin`), sovelluspohjat `plan2001/lib/app/`, `tools/vm-cpu` (asennus,
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
