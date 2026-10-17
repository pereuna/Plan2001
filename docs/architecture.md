# Plan2001:n arkkitehtuuri (16.10.2026)

Plan2001 on 9frontin UEFI-fork kolmelle raudalle, jossa on muokattu uusi
kernel ja kernelin kehitystyökalut: repo on Plan 9 -juuri, joka kääntää
itsensä (`docs/selfhost.md`). Se ei ole 9front-yhteensopiva, ja legacy (BIOS, Multiboot,
32-bittinen) jää pois. Sovellukset ja jakelu ovat Monolithissa
(`docs/repos.md`).

| Arkkitehtuuri | Alusta | Tila |
|---|---|---|
| AMD64 | UEFI | käytössä (pc64, `docs/boot-abi-amd64.md`) |
| ARM64 | UEFI | suunniteltu, QEMU-kokeilu (`docs/boot-abi-arm64.md`) |
| RISC-V64 | UEFI | tuleva (`tools/targets/riscv64`) |

Kernel, prosessinhallinta, tiedostojärjestelmärajapinta ja verkkopino
pidetään yhteisinä (`port`, `ip`). Arkkitehtuurikohtaisia ovat
prosessorin käynnistys, MMU, kontekstinvaihto, poikkeukset ja keskeytykset
sekä alustan ajurit. Loaderin ja kernelin rajapinta on kaikille sama
BootInfo (`docs/boot-abi.md`).

Muutokset tähän mennessä: UEFI-boot-polku ilman kiinteitä osoitteita,
BootInfo, boot-merkit framebufferiin, konsoli (`vga.c`, `kbdfs`:n
rivieditori), virtio-levy, WireGuard (`devwg`, `docs/wireguard.md`) ja
ext4-juuri kernelissä (`#E`, `docs/ext4.md`): kernel mounttaa juuren ja
ajaa sen initin ilman boot-tiedostojärjestelmää (`docs/init.md`). Tila:
`docs/status.md`.

Terminaalit ja sovellukset ovat verkkokoneita ja käyttäjätasoa: ne
puhuvat kernelin kanssa järjestelmäkutsuilla ja muiden koneiden kanssa
9P:llä.
