# Engine seams

The game runs on raylib today. These seams separate what the game *is* (the
building, the rules) from how it is drawn and heard, so that a port to another
engine keeps the first and replaces the second. The Unreal Engine 5 port plan is
in docs/unreal-handoff.md.

## Layers

| layer | directory | may depend on | contains |
|---|---|---|---|
| core | `src/core/` | the C++17 standard library only | math types, hashes and noise, per-level rules, chunk generation, storeys and features, collision, line of sight, pathfinding, the light-occlusion grid, the layout description |
| sim | `src/sim/` | core | game state and rules: player, hunter, dogs, health, pickups, exits, blackouts, storey changes. Takes an input frame and a clock, emits events |
| platform | `src/` | everything | the raylib backend: window, input, mesher, textures, shaders, rendering, audio, the revolver model |

Rules:

- core and sim do not include `raylib.h`, `rlgl.h` or any GL, audio or window
  header. `tools/core-check.sh` compiles them alone and fails if one does.
- core and sim never read the wall clock or a global random source. Time comes
  in as an argument; randomness comes from seeded `Rng`.
- Nothing in core or sim knows about textures, atlases, UVs or meshes. Where the
  platform needs to know what to build, core describes it (`ChunkLayout`) and
  the platform decides how it looks.
- core and sim avoid exceptions and RTTI, so they compile under Unreal's default
  module settings.

## Proving a seam change

Seam work is meant to change nothing the player can see or do.

| proof | command | pass |
|---|---|---|
| generator unchanged | `mapdump` runs below, `diff` against the previous build | identical text |
| core answers unchanged | `./contract --check` (Contract tests, below) | exit 0 |
| frames unchanged | `tools/proof-shots.sh DIR` before and after, then `--diff` | at the noise floor of one binary captured twice |
| behaviour unchanged | the regression harness (AGENTS.md) | exit 0 |

The mapdump set:

```bash
for lv in 0 1 2 3 4; do for v in 0 1; do
  ./mapdump --level $lv --seed 1337 --visit $v --cells 65 --plan -16 -16 32 32
done; done
./mapdump --level 0 --seed 1337 --visit 1 --list-stairs --no-plan
```

## Contract tests

`tools/contract_lib.cpp` generates core's answers and compares them with the
text files in `tests/golden`; `tools/contract.cpp` is its command line, and the
Unreal automation test (unreal/, docs/unreal-handoff.md) runs the same library.
Both link src/core alone. A port running the same core must reproduce every
line. It runs in under a second.

```bash
tools/sandbox-build.sh contract    # or: make contract-check
./contract --check                 # compare with tests/golden; exit 1 on any difference
./contract --write                 # regenerate tests/golden
```

| file | holds |
|---|---|
| `chunks.txt` | 121 chunks, one line each: a hash of the whole chunk and one per `ChunkData` field. Levels 0-4, visits 0 and 1, seed 1337, the 3x3 chunks round the origin and chunk (40,-37); on Level 0 also both storeys of every vertical feature among them and of the first feature of each kind near the origin |
| `chunks_full.txt` | 18 of those chunks field by field, one row per z: the origin chunk of every level and visit, and the Level 0 visit 1 feature chunks |
| `queries.txt` | `gatherCellAABBs`, `collideCircle`, `groundAt` (through a hole into the storey below, on flights, on a vending machine), `stairY` up each flight kind, `lineOfSight`, `canStep`, `pathStep`, `findOpenSpot`, `ceilY`, `buildOccupancy` over a 48-cell window, `vendFootprint` for each turn and flag. Probes are doors, a locked door, rails, a pillar, holes and vending machines found in the generated world, and seeded samples, each printed with the coordinates of the call |
| `mutations.txt` | `shiftEdge` and `unlockEdge` on chosen edges: the walls read back through `wallNVal`/`wallWVal`, the `staleChunks` entries appended, and a repeat that appends nothing |
| `layout.txt` | `chunkLayout` item by item, every field: the origin and far chunks of every level and visit, both storeys of each Level 0 feature kind at visit 1, and a Manila Room |

A failure names the file, line, chunk and field: `FAIL chunks.txt:4 [chunk L0
v0 s0 (0,-1)] fields elev`, or the field and row of a full dump. Floats are
printed with `%.9g`, which round-trips a float, so the comparison is exact.

Regenerate the goldens only with a deliberate change to what the generator or a
query produces, and say so in the commit message with what moved. A refactor,
a build change or a port must pass them unchanged. Not covered: the order of
`staleChunks` from `unloadFar`/`unloadAll`, which follows `unordered_map`
iteration and differs between standard libraries.

## Floating point

Core's results must not depend on the compiler. Two mechanisms, both needed:

| mechanism | where | covers |
|---|---|---|
| `-ffp-contract=off` | Makefile, `tools/sandbox-build.sh`, `tools/web-build.sh`, `tools/core-check.sh` | gcc, clang, em++ |
| `src/core/fp_strict.h`, included first by every core .cpp | the source | clang (`#pragma clang fp contract(off)`), MSVC (`float_control(precise, on)` then `fp_contract(off)`), others (`STDC FP_CONTRACT OFF`); GCC ignores pragmas and needs the flag |

Rule: no fused multiply-adds in core. A fused multiply-add rounds once where
the source rounds twice, and the generator thresholds noise (`fbm2(...) >
0.60f`), so contraction can flip a comparison and generate a different maze.
clang contracts within an expression by default (the Mac arm64 build); gcc
contracts across expressions in C++ whenever FMA instructions are enabled.
Measured on x86-64 with `-mfma`: world.cpp compiles to 28 FMA instructions
under gcc and 47 under clang, 0 with the flag, and 0 under clang with the header
alone. Without either, clang changed one contract answer (`stairY`'s ramp,
2.78999996 to 2.7900002); no chunk moved at seed 1337.

Not covered by the pragmas: clang's `-ffast-math` and `-ffp-contract=fast`
override them (7 answers change, including `stairY` returning a number where it
returns NaN off a flight), and `/fp:fast` on MSVC reassociates as well. The MSVC
pragmas are untested: there is no MSVC here. An
Unreal module compiling core must not use fast math; check the toolchain's
defaults and run the contract tests under it.

libm: generation uses no transcendental function. `sqrtf` (collision, sight)
is correctly rounded by IEEE 754 on every platform, and `floorf`, `ceilf` and
`fabsf` are exact. `vendFootprint` used `cosf`/`sinf` of a quarter turn;
`PROP_TURN` is 1.5708, not pi/2, so the results are tiny non-zero values whose
last bit another libm may round differently. It now reads a four-entry table of
glibc's values (hex float literals), so every result is unchanged here and the
same everywhere.

One libm dependency remains: `tubeHash` (layout.cpp) is `fract(sinf(...) *
43758.5453)`, the GLSL hash the shader also uses, and decides which fittings
are dead, dim or faulty. A `sinf` that rounds its last bit differently can flip
one of those on a port. `layout.txt` prints every fitting's state, so a port
fails there first, and only on fitting lines. Replacing the hash changes which
tubes are dead in the raylib build too, so it waits for the port to need it.

Evaluation order: a function's arguments are evaluated in no fixed order, and
gcc and clang differ. `railOn` passed two calls that both wrote `voidSide` to
`std::max`; gcc and clang gave different `voidSide` for a rail with holes on
both sides (one the mesher does not draw). The layout golden caught it on its
first clang run. Call anything with a side effect as its own statement.

## Status

| seam | state | check that holds it |
|---|---|---|
| 1 core | done | `tools/core-check.sh`, `./contract --check`, the mapdump set |
| 2 layout | props, fixtures, light fittings, openings done; floorplan geometry still built in the mesher | mesh-bake comparison (method below), `./contract --check` |
| 3 sim | done; src/sim depends on core only | `tools/core-check.sh`, the regression harness |

Each seam was proved against a build of main from before any of them
(374a84e): mapdump byte-identical, 0 differing pixels on every world frame of
`tools/proof-shots.sh`, regression harness exit 0. One capture of the final
tree, taken straight after another capture run, put 13 pixels of lv1.png over
the threshold (a small, slightly brighter patch; frame mean +0.5); two fresh
captures of the same binary gave 0. Some of what a Level 1 frame shows still
follows the wall clock, so recapture on a quiet machine before believing a
small difference.

### Seam 1: core (September 2026)

`src/core/` compiles alone under `tools/core-check.sh` (C++17,
`-fno-exceptions -fno-rtti`, only `src/` on the include path).

| file | holds |
|---|---|
| `vec.h` | `Vec2`, `Vec3`, `TAU`, `clampf` |
| `hash.{h,cpp}` | `hash64`, `Rng`, `ih`, `lat`, `vnoise2`, `fbm2` |
| `fp_strict.h` | no floating-point contraction for the file that includes it first (Floating point, above) |
| `level_rules.h` | `LevelRules` (wall height, light pitch, storey pitch, name, which tubes work), `LEVEL_RULES`, `EXIT_NEXT` |
| `world.{h,cpp}` | `ChunkData`, generation, storeys and vertical features, the wall/prop/floor/ceiling accessors and overlays, collision (`gatherCellAABBs`, `collideCircle`, `groundAt`), `lineOfSight`, `canStep`, `pathStep`, `findOpenSpot`, `buildOccupancy`, the Manila Room, vending placement (`vendFootprint`, `liftHash`) |

The platform side of the seam:

| file | holds |
|---|---|
| `src/world_mesh.{h,cpp}` | `bakeChunk` (the mesher), `ChunkMesh` slots, `ChunkMeshCache` |
| `src/mesh_builder.{h,cpp}` | `MB`, `addPropBox`, `addSolidBox`, `PLAIN_UV` |
| `src/object_meshes.{h,cpp}` | can, deck, reels, lamp, flare, crate and lid |
| `src/vec_rl.h` | `toRl` / `fromRl` |
| `src/levels.{h,cpp}` | `LevelCfg : LevelRules` (the look), `lightAtCPU` and the other CPU lighting mirrors |
| `src/util.{h,cpp}` | `cl8`, `SAMPLE_RATE`, `PARTY` |

How meshes follow core's chunks: `ChunkData` holds no meshes. Core appends a
`ChunkRef {storey, cx, cz}` to `World::staleChunks` wherever geometry goes
stale (`unloadFar`, `unloadAll`, `rebuildChunk`, and through it `shiftEdge`
and `unlockEdge`). `ChunkMeshCache` keys baked chunks by absolute storey and
chunk and drains that list at the start of every call, so a mesh lives exactly
as long as its chunk data and `setStorey` needs no event.

Still coupled, and why:

- The mesher reads the world through `World`'s accessors (`bakeChunk(World &,
  ...)`). What it used to decide from hashes (fixtures, fittings, props, doors)
  is now core's `ChunkLayout`: see seam 2 below for what is left.
- Game rules that hash cells (hide spots, coins, crates) live in the sim
  (src/sim/items.cpp), and `tools/mapdump.cpp` mirrors `hideSpotAt` and
  `coinAt`.
- A prop's height is written in three places: `addProp` (mesher),
  `gatherCellAABBs` (core) and `Sim::bottleShelfY`.
- Lighting constants are mirrored by hand between core, the mesher, the
  shader and `lightAtCPU`: the panel half-size (0.62), the light plane
  (core's `LIGHT_DROP`; render.cpp and the shader uniforms still write
  `wallH - 0.12`), the occupancy bits, the tube hash (`tubeHash` in core, `lhash`
  in GLSL).
- The render loop reads `ChunkData` directly for the stairwell landing lights
  (`nfeat`, `feats`).

Proofs, against a build of main before the seam (374a84e; two captures of
that binary give the noise floor):

| proof | result |
|---|---|
| mapdump set (this file) | byte-identical, all 11 runs |
| `tools/proof-shots.sh` | all 11 world frames 0 pixels differing (>16), the noise floor; menu.png 3613 against a floor of 736-3054 across three baseline pairs (its camera drifts on the wall clock) |
| regression harness | exit 0, 59 captures |
| `tools/core-check.sh` | passes; fails as expected on a core header that includes `src/util.h` or any `src/` header |
| code that only moved or was renamed | identical assembly: `ih`, `lat`, `vnoise2`, `fbm2` in their new file; `world_mesh.cpp`, `mesh_builder.cpp`, `object_meshes.cpp` before and after naming shared constants |
| comment rewrite | every touched file token-identical with comments removed (`c++ -fpreprocessed -E`) |

### Seam 2: layout (partial, September 2026)

`chunkLayout(world, cx, cz)` (`src/core/layout.{h,cpp}`) describes what a chunk
of storey `qs` holds besides its floorplan, in world metres in that storey's
frame. `bakeChunk` builds each item and decides nothing about where it goes.
`./mapdump --layout CX CZ` prints it.

| list | holds |
|---|---|
| `props` | kind, cell, centre (after `vendFootprint`), floor height, quarter turns and yaw, the against-wall flag, the variation hashes the mesher builds from |
| `fixtures` | outlets (whole and broken), switches, grilles, exit signs, diffusers, sprinklers, conduit runs, scrawl (phrase, size, tilt, tint), lift doors, spalls on walls and columns, pipe runs, valve standpipes; per chunk, the Manila Room and Level 4's streamers |
| `fittings` | every light fitting on the level's grid in the chunk: position, the ceiling it hangs from, why one is not there (under an opening, in the Manila Room), Level 1's turn, and the static tube state (dead, output, faulty) from the same hash as the shader and `lightAtCPU` |
| `openings` | doorways (with joined neighbours), locked doors, exits and cursed exits (with their glyph and room for an open leaf), windows, rails (cap heights, which side is a void): the opening, wall base and top, header, sill |

The per-level choice of decorations is one table (`DECOR` in layout.cpp).
Flicker, blackouts and the hunter's dead-light pool vary with time and stay in
the renderer. Which tubes work (`dead`, `vary`, `faulty`) moved to
`LevelRules`, and `tubeHash`/`storeyHashOffset` to core.

Still decided in the mesher, and why:

| what | why it stays |
|---|---|
| walls, floors, ceilings and soffits, stairs, pools, the Poolrooms' arches, baked AO, skirting | the follow-up seam: geometry of the floorplan itself |
| Level 1's dock safety edging | follows floor drops, which is floor geometry |
| the Red Rooms tint | a colour field round cursed exits in the 3 x 3 chunks, read from core's `cursedExit`; it is how the walls look, not something standing there |
| the Manila Room's floorboards, paper, notes and chandelier | built from one `Rng` seeded by the layout's seed; the notes share the stream with the boards, so moving them means moving the boards |
| each prop's shape and variation (stacked cartons, the dead vending machine, cup count) | derived from the layout's hashes; how a prop looks |
| fitting style per level (troffer, batten, tray), exit glow colours, which exits get a glyph (Level 1), Level 2's emissive window panes | looks |
| the stairwell landing lamp's body | stairs |

Proofs, against the tree before this seam (a2755e9):

| proof | result |
|---|---|
| mapdump set (this file) | byte-identical to the baseline, all 11 runs |
| mesh bake | 1900 chunk bakes, old mesher against new, every slot's vertex, uv, normal, colour and index arrays byte-identical (43.5 M vertices a side). 100 chunks (cx, cz in -5..4) for each of: every level at visits 0 and 1, Level 0 storeys -1 and +1, exits everywhere on every level, the pinned Manila Room. Every fixture, opening and fitting-gap kind occurs. Moving the decal standoff by 0.1 mm fails it in 3037 slots |
| `tools/proof-shots.sh` | all 11 world frames 0 pixels differing (>16) against the baseline; menu.png 18% (its camera runs on the wall clock). Below the threshold the frames wobble with frame rate: the baseline binary recaptured differs from its own earlier lv1 by 624k pixels (max 13 levels), and from the new binary captured beside it by 95k (max 4) |
| regression harness | exit 0, 59 captures |
| `tools/core-check.sh` | passes, layout.h and layout.cpp included |

The mesh bake proof compiles the saved old `world_mesh.cpp` with
`-DbakeChunk=bakeChunkOld -DChunkMeshCache=ChunkMeshCacheOld` beside the
current one, opens a hidden raylib window (meshes upload to GL), bakes each
chunk with both and `memcmp`s the arrays. It lived in scratch space.

### Seam 3: simulation and presentation

The game's state and rules are one `Sim` in `src/sim/`. `Game` (src/game.cpp,
render.cpp, game_audio.cpp) owns a `Sim` and everything raylib: the window,
input, assets, audio and rendering.

| file | rules |
|---|---|
| sim.cpp | the tick order, level entry, descents, death and escape, pause, records |
| player.cpp | look, movement, sprint, squeeze, swimming, the soft floor, gait, hiding, falls, storey changes |
| weapons.cpp | aim, the revolver and its rounds, flares, the party balloons |
| items.cpp | pickups, drinking, the tape deck, the use key, chalk, supply crates |
| hunters.cpp | the hunter (Clark, the Smiler, the Partygoer) and the Red Halls pack |
| place.cpp | blackouts, whispers, sanity and the slide, shifting walls, exits, the Manila Room |

**Entry points.** `Sim::step(const InputFrame &, float dt, double now)` runs one
tick of play. The title screen is `menuDrift(dt, now)`, then the platform
streams chunks, then `menuBegin(in, now)`. `setPaused(on, now)` slides every
schedule by the pause. `applyLevel` and `beginDescent` set a level or a descent
up; `Game::init` calls them in the order that keeps grng's draws where they were.

**Tick order** (`Sim::step`): flashlight, stranger's chalk, look, movement, dev
keys, weapons, bullets, flare, tape deck, interaction, drink, ambience, Manila
Room, crates, the loop-audio cue, entity, dogs, exits, timers. Each may rely on
the ones before it.

**Clock.** `dt` is `GetFrameTime()` clamped to 0.05 s; `now` is `GetTime()`,
read once at the top of the tick. The sim reads no other clock. Blackouts,
spawns, whispers, the pack and flare regeneration are scheduled against `now`,
so they run on the wall clock as before. `clockSeed` (wall-clock seconds, set
each tick) seeds a new descent unless `fixedSeed`.

**Randomness.** Everything drawn at run time comes from `Sim::grng`, seeded from
`world.seed`; placement uses hashes of the seed, level, visit and storey. The
draw order is part of the contract: moving a draw moves every later spawn,
blackout and pickup line.

**`InputFrame`** (src/sim/input_frame.h), filled once a tick by
`Game::readInput` after the cursor is captured or released:

| field | meaning |
|---|---|
| `playing` | mouse captured or touch controls up; gates look, aim, fire, throw, reload, squeeze |
| `touch` | touch controls up (no double-tap run) |
| `forward back left right`, `forwardPressed` | movement held; W's down edge |
| `moveScale` | a thumbstick's share of full speed, 1 for keys |
| `sprint crouch squeeze jumpHeld` | held |
| `jumpPressed` | down edge |
| `look` | mouse or drag delta, px |
| `wheel`, `pickRevolver pickFlare pickDeck` | weapon selection |
| `fire` | primary press, less a click that captured the mouse |
| `aim` | secondary held; touch latches it |
| `reload throwFlare flashlight use drink chalk` | down edges |
| `begin` | title screen: any key but F11, a click, a touch start gesture |
| `dev` | F3-HUD keys (blackout, spawn, chase, banish, refill, storey up/down, next level), false unless the HUD is up |
| `screenFov` | the window's base vertical FOV (`Game::fovForWindow`) |
| `forceSpawn` | headless capture: put the hunter in view on this tick |

**`AudioEvent`** (src/sim/audio_events.h), queued in `Sim::audio` and played in
order by `GameAudio::play` after each tick:

| kind | platform does |
|---|---|
| `PLAY` | set the flagged pitch, volume, pan (a bearing through `panFor`) on clip `sfx[variant]`, then play it. Unflagged properties keep the clip's last values, as raylib does. |
| `VOICE` | start the tape voice if it has ended, then set its pan and volume |
| `VOICE_STOP` | stop the tape voice if it is playing |
| `LOOPS` | ease and feed the underwater loop and LEVEL FUN's music for `dt`, by `LoopCue` |
| `AMBIENCE` | give the synth `mix` (an `AmbienceMix`) and let it fill its buffers |

**Other outputs**, each cleared by the platform when it acts: `shadowsStale`
(rebuild the light-occlusion grid), `dropAimLatch` (release a latched touch
AIM), `recordsChanged` (save `best`). `levelEntries` counts level entries;
`Game::syncLevelLook` applies surfaces, uniforms and the window title when it
moves. HUD text the rules produce is state: `deckNote`, `sanityLine`,
`tapeLine`, `deathBy`, `deathTitle`, `MANILA_NOTES`, with their timers.

**Platform services.** `SolidTracer::nearestSolid` finds where a round meets
level geometry; `MeshTracer` (game.cpp) answers from the chunk meshes of your
storey and the ones above and below. Actor bodies use `rayBox`, which is
raylib's `GetRayCollisionBox` step for step.

**Math and colour types.** src/sim includes core and the standard library only
(`tools/core-check.sh` checks it). Positions and directions are core's `Vec2`
and `Vec3`. `sim_math.h` holds the vector functions the rules call (`add`,
`sub`, `scale`, `negate`, `divide`, `lerp`, `normalize`), a ray (`Ray3`), a box
(`Box3`), a hit record (`RayHit`) and `rayBox`. Each copies raymath's formula
in raymath's operation order, so a build with `-ffp-contract=off` gets the same
bits the raylib build got. Level numbers come from core's `LEVEL_RULES`.
Balloon and confetti colours are indices below `PARTY_COLOURS`; the platform
draws them from `PARTY` (src/util.h). The platform converts at the boundary
with `toRl`/`fromRl` (src/vec_rl.h), including `Ray3` and `Box3` for
`MeshTracer` and the harness.

**Still coupled, and why.**

| coupling | why | goes when |
|---|---|---|
| HUD strings are `const char *` in sim state | text is content the rules choose | a port maps them to its own text type |

**Behaviour that changed, knowingly.** Schedules set during a tick (applyLevel's
blackout, pack and howl; the soft floor's groan; the landing and storey
changes) start from the tick's `now` rather than a `GetTime()` read at that
line. The difference is the time spent earlier in the same tick; on a first
entry to a level that includes generating its surfaces (0.2-0.7 s). `Game::init`
applies a level's look before its rules, so startup reads the clock after the
surfaces as it did. A new descent's seed reads `time()` at the top of the tick
instead of inside `beginDescent`. The dead `noclipped` flag, its HUD branch and
`makeNoclip` are gone; nothing set or played them once exits became doors.

**Proofs.**

| proof | result |
|---|---|
| frames: `tools/proof-shots.sh`, `--diff` against the main build | 0 pixels differing >16 on all 11 world frames. menu.png 0.43%: its camera follows `GetTime()`, and the main binary recaptured under the same load differs from its own capture by 1.17% |
| regression harness | exit 0, 59 captures; adds a live sprint check, the synth-feed check and rayBox against raylib |
| layering: `tools/core-check.sh` | every src/sim file compiles alone with only core and the standard library |
| web build | not compiled (no Emscripten here); the `PLATFORM_WEB` branches of every changed file pass `-fsyntax-only` against a stub emscripten.h |
| rayBox against raylib | bit-identical over 4 million random rays, and 200k in the harness |
| sim_math.h against raymath (the swap off raylib types) | 4 million inputs per function, floats of every shape including NaN, infinities and denormals: every non-NaN result bit-identical; a NaN result can carry the other input's payload when both inputs are NaN (x86 keeps the first operand's, and the compiler may swap a commutative operation's operands). `rayBox` bit-identical to the old raylib-typed one on 4 million rays. Against the library's compiled `GetRayCollisionBox`, the old and new versions both differ only in the sign of a zero distance, on boxes and rays with zero coordinates; none on actor-shaped boxes and shots |

**For the Unreal port.** Mirror `Sim` as a plain C++ module the game mode owns;
tick it from one place with the frame's input, clamped `dt` and a game clock,
then play `audio`, clear the flags and draw. What a port must supply: an
`InputFrame` per tick, a `SolidTracer` (a line trace against level geometry),
a way to play the `Sfx` ids with persistent per-clip pitch, volume and pan,
the tape voice and the two loops, and the ambience synth fed from
`AmbienceMix`. What it reads to draw: the player (`px, py, pz, eyeY, yaw,
pitch, fwd, fov`, lean, roll), the hunter (`ent`), `dogs`, `litFlares`, `deck`,
`bullets`, `bulletImpacts`, `coinsWorld`, `confetti`, `chalk`, the pickup and
balloon functions, and the HUD state above. Replays are deterministic given the
seed and the sequence of InputFrames, `dt` and `now`, with one exception: a
round meets level geometry only where `SolidTracer` finds it, and `MeshTracer`
finds only chunks the platform has meshed, so a shot into an unstreamed chunk
flies on.
