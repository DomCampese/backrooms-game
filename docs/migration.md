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
| frames unchanged | `tools/proof-shots.sh DIR` before and after, then `--diff` | at the noise floor of one binary captured twice |
| behaviour unchanged | the regression harness (AGENTS.md) | exit 0 |

The mapdump set:

```bash
for lv in 0 1 2 3 4; do for v in 0 1; do
  ./mapdump --level $lv --seed 1337 --visit $v --cells 65 --plan -16 -16 32 32
done; done
./mapdump --level 0 --seed 1337 --visit 1 --list-stairs --no-plan
```

## Status

Filled in as each seam lands.

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

**Still coupled, and why.**

| coupling | why | goes when |
|---|---|---|
| `sim_math.h` includes raylib.h and raymath.h | `Vector2/3`, `Color`, `Ray`, `BoundingBox`, `RayCollision` and the Vector3 functions | the lead swaps it to `src/core/vec.h` |
| sim.h includes ../world.h, ../levels.h, ../util.h, which include raylib.h | `ChunkData` holds `Mesh` handles; `LevelCfg` uses `Vector3`; util's `PARTY` palette is `Color` | seams 1 and 2 (core) |
| `applyLevel` calls `world.unloadAll()`; `unlockEdge`/`shiftEdge` rebake chunks | `World` still owns the meshes | the mesher leaves world.cpp |
| confetti and balloon colours are `Color` | the palette the renderer draws | core's colour type |
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
| layering: `grep` of src/sim | the only raylib include is sim_math.h; the only raylib calls are raymath's `Vector3*` |
| web build | not compiled (no Emscripten here); the `PLATFORM_WEB` branches of every changed file pass `-fsyntax-only` against a stub emscripten.h |
| rayBox against raylib | bit-identical over 4 million random rays, and 200k in the harness |

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
