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
