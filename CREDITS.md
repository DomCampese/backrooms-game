# Credits and third-party notices

Thank you to the Backrooms community and the authors who built its shared lore.
This is an unofficial hobby game, not an endorsed or official Backrooms release.

## Game license

THE BACKROOMS: a procedural horror game by Dominic Campese.
Copyright (C) 2026 Dominic Campese, for the project's original copyrightable contributions.

The game software is licensed under the GNU General Public License, version 3
only (SPDX: GPL-3.0-only). See [LICENSE](LICENSE) for the full terms.
Third-party material retains the licenses and notices identified below.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

## Backrooms lore and acknowledgments

The game's influences were not recorded during development. These credits
acknowledge recognizable community lore and its documented authors; they do not
claim that every linked article was consulted or that its current version is
reproduced faithfully. Author information was checked on September 19, 2026; the Smilers entry was added on September 24, 2026.

The following articles are from the [Backrooms Wiki](https://backrooms-wiki.wikidot.com/).
Their article text is offered under [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/).
Images and other embedded media may have separate licenses; these article credits
are not permission to copy those assets.

| Reference | Authors and version notes | Relationship to this game |
| --- | --- | --- |
| [Entity 3 — Smilers](https://backrooms-wiki.wikidot.com/entity-3) | Original by **Palico22**; rewritten by **Stretchsterz**, as cited for the page. | The hunter outside Level Fun: a procedurally drawn shadow body with glowing eyes and a toothed grin that shows in the dark. Its stalking, lunging, doubloon drops and weaknesses are the game's own. |
| [Entity 67 — Partygoers =)](https://backrooms-wiki.wikidot.com/entity-67) | Original by **1000dumplings**; current rewrite by **Robert Goerman** and **MC_Crafter_24_7**, as credited on the page. | A procedurally drawn yellow, smiling party-themed enemy. The game changes its appearance and uses its own stalking/chase behavior. |
| [Level Fun / Level 26 — The SS Fun =)](https://backrooms-wiki.wikidot.com/level-26) | Original Level Fun by **1000dumplings**; current rewrite by **Robert Goerman** and **MC_Crafter_24_7**. | The game uses the older party-room motif and name, with generated banquet rooms, balloons and confetti; it does not reproduce the current cruise-ship setting. |
| [Object 1 — Almond Water](https://backrooms-wiki.wikidot.com/object-1) | Original by **1000dumplings**; rewritten by **Natedagreat563** and **Poliacci**. | Adapted into a canned consumable with game-specific recovery effects, placement, artwork and drinking animation. |
| [(Archived) Level 0 — Tutorial Level](https://backrooms-wiki.wikidot.com/archived:level-0-2020) | **etoisle**, per the archived page's citation. | Acknowledgment of the community's yellow office-maze setting; this game generates its own layouts and adds its own combat and progression. |
| [Level 1 — Habitable Zone](https://backrooms-wiki.wikidot.com/level-1) | Current article by **Praetor3005** and **DivineAtlas**. Its history credits **EnderMitten** for the concept, **u/ThePizzaEater1000** for the original Wikidot article, and **etoisle** for an earlier rewrite. | Acknowledgment of the warehouse-level tradition; the game's layout, loading docks and progression are its own implementation. |
| [Level 37 — Sublimity](https://backrooms-wiki.wikidot.com/level-37) | **egglord**, per the page's citation. | Acknowledgment of Poolrooms lore, including its tall tiled halls and the absence of entities, which the game keeps as a refuge. The game's pools, vaults and swimming are generated and use different numbering and connections. |

These are adaptations and acknowledgments, not claims of ownership over the
Backrooms setting or of endorsement by its authors. Game-specific level ordering,
geometry, dialogue, survival rules and enemy behavior differ from the articles.

### How the lore and software licenses fit together

The original wiki works remain under CC BY-SA 3.0. The project's contributions to
adapted lore and its artistic presentation are additionally offered under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
Section 4(b) of [BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/legalcode)
permits adaptations under a later BY-SA version. BY-SA 4.0 provides a one-way
compatibility route to GPLv3 for contributions to adaptations integrated into
software. The game's software contributions are offered under GPLv3; this does
not erase the original authors' attribution or relicense their standalone works.

See the wiki's [game developer guidance](https://backrooms-wiki.wikidot.com/licensing-guide),
CC's [compatibility list](https://creativecommons.org/compatible-licenses/), and
[software guidance](https://creativecommons.org/faq/#can-i-apply-a-creative-commons-license-to-software).
Retain these credits and the applicable license notices when redistributing.

## External assets

- **Revolver Game Asset** by **loafbrr_1 / TheLoafbrr**, CC0 1.0.
  [Original source](https://opengameart.org/content/revolver-game-asset).
  [Local provenance and modifications](assets/revolver/README.md).
- **Poly Haven**: wood_table_001, metal_plate and denim_fabric, CC0 1.0.
  [Individual source links and conversion details](assets/materials/README.md).

- **Water sounds** — splashes, swim strokes and the underwater loop by the
  **Red Eclipse Team**, CC BY-SA 4.0 or later; the Poolrooms water ambience by
  **Paul Hertz ("ignotus")** via Red Eclipse, CC BY-SA 3.0.
  [Source files, commit and per-file licenses](assets/sounds/water/README.md).
- **LEVEL FUN music** — "Sketchbook 2024-02-21_02" by **Abstraction / Tallbeard
  Studios (Ben Burnes)**, CC0 1.0, from the
  [Music Loop Bundle](https://tallbeard.itch.io/music-loop-bundle).
  [Provenance and the creator's notice](assets/sounds/music/README.md).

These assets retain their original licenses (CC0, or CC BY-SA for the water
sounds). The project's GPL notice does not replace their original terms. The
CC BY-SA sounds are distributed unmodified; attribution and license links are
above and in their README.

## Software dependencies

- **raylib**, by **Ramon Santamaria (@raysan5)** and contributors, zlib license.
  [Project](https://github.com/raysan5/raylib).
  [Bundled raylib 5.5 license notice](LICENSES/raylib-5.5.txt).
  Retain the applicable upstream notices for the raylib version and any bundled
  dependencies used in a distributed build.
- **Libraries bundled inside raylib** — cgltf, miniaudio, the dr_libs, the stb
  libraries, qoi/qoa, par_shapes and others, under MIT, MIT-0, public domain,
  zlib and WTFPL terms. raylib compiles these into the library, so they are
  statically linked into everything released here, the `.wasm` included.
  [Authors and licenses](LICENSES/raylib-bundled-libraries.txt).
- **Emscripten**, by the Emscripten authors, MIT / University of Illinois NCSA.
  The web build ships Emscripten's generated JavaScript runtime alongside the
  `.wasm`, so that notice is distributed too.
  [License](LICENSES/emscripten.txt).

The raylib notice above names the version the **desktop** release is built
against. The web build pins its own raylib in `tools/web-build.sh`; keep that
tag and the notice in `LICENSES/` in step, because the notice is what ships.

## Distributing a build

Include LICENSE, CREDITS.md, LICENSES, and the asset provenance notices with native
releases. On a web demo, provide visible License, Credits and Source links.
`tools/web-build.sh` copies LICENSE, CREDITS.md and LICENSES/ into the published
directory, and `web/shell.html` carries the copyright, the warranty disclaimer
and those three links on the page itself — which is also what GPLv3 section 5(d)
asks of an interactive interface. A build that drops either is not distributable.
Provide the complete corresponding source for the exact released version,
including necessary build scripts and asset-generation inputs, at no extra charge.
Tag the source revision used for a release and link that revision from its download
or demo page; do not rely only on a moving default branch.

Source repository: https://github.com/DomCampese/backrooms-game
Web build: https://domcampese.github.io/backrooms-game/
Build instructions: [README](README.md#build--run).
