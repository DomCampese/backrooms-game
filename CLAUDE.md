# CLAUDE.md — working notes for this repository

Context for anyone (human or model) picking this up cold. The point of this
file is that no single long session should be the only place this knowledge
lives.

## What this is

A first-person backrooms horror game in C++17 on raylib + OpenGL 3.3.
**There are no asset files.** Every texture, sound and mesh is synthesized at
startup from code. If you are tempted to add a `.png` or a `.wav`, that is a
departure from the whole design — generate it instead.

### Module map

| file | what lives there |
|---|---|
| `main.cpp` | `init()`, the `while (!WindowShouldClose())` loop, `shutdown()` |
| `game.{h,cpp}` | all run state; per-frame update in `tick()` |
| `render.cpp` | 3D scene pass, viewmodels, HUD, overlays |
| `world.{h,cpp}` | infinite maze: chunk generation, mesh baking, collision, line of sight |
| `levels.{h,cpp}` | per-level look/feel table; CPU mirror of the shader's lighting |
| `shaders.cpp` | the world and post-process GLSL, as string literals |
| `textures.cpp` | every surface, procedurally generated |
| `sfx.cpp` | one-shot sounds, synthesized into `Wave` buffers |
| `audio.cpp` | the streaming ambience synth (hum, drone, water) |
| `entity.h` | `Entity` (Pirate Clark) and `Dog` state |
| `util.{h,cpp}` | hashes, RNG, value noise, shared helpers |

`tick()` calls the update functions in a fixed order — look, movement, dev
keys, weapons, flare, interaction, drink, ambience, entity, dogs, exits, then
chunk streaming, then render. Order matters: e.g. weapons run before the
entity update, so a shot lands before the entity decides what to do about it.

## Building

### Normal machine

`make` — needs raylib installed (Homebrew or pkg-config). `make run` builds
and runs.

### Sandboxes with no system raylib

This is the case in the Claude Code web environment, and reconstructing it is
the single most time-consuming thing about a cold start. There is no raylib
package available, but the **Python wheel ships a complete raylib shared
object**, and you can link C++ against it directly.

```bash
pip3 download raylib -d /tmp/rl --no-deps        # grab the wheel
cd /tmp/rl && unzip -o raylib-*.whl -d rlwheel   # it contains raylib/*.so
```

The wheel also carries `*.h.modified` files — cffi-preprocessed raylib
headers. Turn those into a usable shim:

1. Copy `raylib.h.modified`, `raymath.h.modified`, `rlgl.h.modified` to
   `rlshim/raylib.h`, `rlshim/raymath.h`, `rlshim/rlgl.h`.
2. Add `#pragma once` and wrap each in `extern "C" { ... }`.
3. cffi strips `#define`s, so restore what the game uses: `PI`, `DEG2RAD`,
   `RAD2DEG`, the named `Color` constants (`WHITE`, `RED`, …), and
   `MATERIAL_MAP_DIFFUSE` (which is an alias for `MATERIAL_MAP_ALBEDO`).
4. raymath declares its functions `inline`; strip that keyword or they will
   not resolve against the shared object.

Then build against the shim and link the wheel's `.so`:

```bash
c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers -Irlshim \
    src/*.cpp -o backrooms \
    /path/to/rlwheel/raylib/_raylib_cffi.cpython-*-linux-gnu.so \
    -lpython3.11 -lm -ldl -lpthread
```

Keep that as a one-line script. You will run it dozens of times.

## Running headless, and taking screenshots

There is no display in a sandbox, so run under Xvfb:

```bash
export DISPLAY=:99
Xvfb :99 -screen 0 1440x850x24 >/dev/null 2>&1 &
sleep 2
BACKROOMS_SEED=1337 BACKROOMS_LEVEL=0 BACKROOMS_POS="15,15,0.8" \
  BACKROOMS_SHOT=shot.png ./backrooms
```

`BACKROOMS_SHOT` runs the game, captures one frame, and exits. Combined with
`BACKROOMS_SEED` and `BACKROOMS_POS` the result is deterministic apart from
light flicker, so the same command twice gives you comparable images.

**`TakeScreenshot` writes relative to the process working directory**, so pass
a bare filename and `cd` to where you want the file.

## Dev knobs

Environment variables, all read at startup:

| variable | effect |
|---|---|
| `BACKROOMS_SHOT=out.png` | headless: run, capture one frame, exit |
| `BACKROOMS_SHOTFRAME=n` | which frame to capture (default 600); use ~150 for quick sweeps |
| `BACKROOMS_SEED=n` | fix the world seed — repeatable maze |
| `BACKROOMS_LEVEL=n` | start on level n (0–4) |
| `BACKROOMS_POS="x,z,yaw"` | start at a specific spot and heading, in world metres/radians |
| `BACKROOMS_EXITS=1` | exit doors everywhere, for visual testing |
| `BACKROOMS_MENU=1` | hold on the title screen instead of starting the run |

**`BACKROOMS_NOENT` does not exist.** It appears in scratch scripts written
during development and is silently ignored — it never suppressed the entity.
If you want Clark gone in a test, use the F3 `H` hotkey or set
`ent.nextSpawn` far out.

In-game, press `F3` for the debug HUD; while it is up these are live:

| key | effect |
|---|---|
| `B` | force a blackout now |
| `E` | spawn Clark stalking ~12 m ahead |
| `C` | force a chase |
| `H` | banish him |
| `G` | refill flares and ammo |
| `N` | jump to the next level |

## Regression process

Before pushing anything that touches rendering, the shader, or world
generation, run all five levels plus the menu and check for shader errors:

```bash
for LV in 0 1 2 3 4; do
  BACKROOMS_SEED=1337 BACKROOMS_LEVEL=$LV BACKROOMS_POS="15,15,0.8" \
    BACKROOMS_SHOT=rg_lv$LV.png ./backrooms 2>/dev/null \
    | grep -iE "SHADER: .*(failed|error)|ERROR:"
done
BACKROOMS_SEED=1337 BACKROOMS_MENU=1 BACKROOMS_SHOT=rg_menu.png ./backrooms 2>/dev/null \
  | grep -iE "SHADER: .*(failed|error)"
```

Then **look at the images**. A clean exit code proves nothing; several real
bugs in this game's history rendered perfectly valid frames that were wrong.

A full sweep takes 10–20 minutes headless because the software rasteriser is
slow. Run it in the background and do something else. Do **not** run two
sweeps concurrently — see the Xvfb note below.

## Things that will bite you

These each cost real debugging time. They are not hypothetical.

**A failed shader compile does not crash — it goes black and gets faster.**
raylib silently falls back to its default shader. The frame rate goes *up*,
which is the opposite of the intuition, so a "faster but black" result means
a broken shader, not a win. `init()` checks for this and logs an error, but
**raylib's `TraceLog` writes to stdout**, so if your test script pipes stdout
through a `grep` for something else you will throw the message away. Always
let shader errors through your filter.

Two specific ways to break the shader silently:

- `flat` is a reserved GLSL keyword. Naming a varying `flat` kills compilation.
- Swizzles are checked: `shP` is a `vec2`, so `shP.z` is a compile error. The
  z-ish component of a 2D world-space point is `.y`.

**`pkill -f "some pattern"` can kill your own shell.** If the pattern appears
in the command line of the shell running it — which it does whenever you type
the command inline, or write a heredoc containing it — `pkill` matches itself
and the shell dies with no output. Use `pkill -x Xvfb`, or put the command in
a script file and run the file.

**Concurrent test scripts fight over Xvfb.** Every script here starts by
killing any running Xvfb, so two sweeps at once kill each other's display
mid-run. The symptom is a run reporting shader errors that are really GLFW
failing to find a display. One at a time.

**Temporary test hooks must be removed by exact string, not by slicing.**
Cutting from `s.index(start)` to `s.index(end)` is dangerous when the end
anchor appears more than once — `if (shotPath && frame == shotFrame)` occurs
in three places, and slicing to the first one deletes hundreds of lines of
real code. Use a unique multi-line anchor, and `git diff --stat` afterwards.

## Architecture invariants

### The shader's alpha coding

`shaders.cpp` selects a lighting path from the **vertex colour's alpha**.
Getting this wrong produces surfaces that are subtly or wildly mislit:

| `fragC.a` | meaning |
|---|---|
| `< 0.1` | light panel — emissive, flickers |
| `< 0.3` | raw emissive (exit glow) |
| `< 0.45` | window glass |
| `< 0.62` | water surface |
| `>= 0.62` | textured; `aOut = fragC.a * texel.a` |
| `> 0.995` | additionally gets **world-space relief bump** |

That last row is a trap. Relief is right for grimy walls and carpet, wrong for
small curved objects, and badly wrong for anything that *moves* — the noise
field is fixed in world space, so a moving object swims through it. Opaque
geometry that should stay smooth uses alpha **254** (0.996): textured, visually
opaque, below the relief threshold.

### Chunk mesh slots

`ChunkData::meshes[8]`: 0 floor, 1 ceiling, 2 walls, 3 props, 4 water,
5 wall scrawl, 6 window glass, 7 baked AO. Materials are `Game::mats[7]`:
0 floor, 1 ceiling, 2 walls, 3 props, 4 scrawl, 5 baked AO, 6 the can.

Every material carries the occupancy grid in its **normal-map slot**, because
`DrawMesh` reliably binds that as `texture2` where `SetShaderValueTexture` did
not. If you add a material, wire that up or its shadows will be wrong — an
unbound sampler reads as white, which the occlusion code interprets as "wall
everywhere", and the object goes black.

### Lighting

Ceiling panels sit on a grid of `uLS` metres (8 on most levels, 12 on Level 1).
The shader sums only the **3×3 panels** around the shaded point, and each panel
fades out before it leaves that window — without the fade you get a visible
brightness seam along every light-cell boundary.

Shadows are a 2D DDA over an occupancy grid (`World::buildOccupancy`) holding
only full-height blockers: bit 0 north wall, bit 1 west wall, bit 2 pillar.
Doorways, glass and furniture are deliberately absent so light pours through
them. The march is capped at 8 cells; each light's shadow fades out by 15 m so
the cap can never truncate a shadow in view. **If you raise the shadow range,
raise the cap too** — where the march runs out the shadow simply stops, and
because the DDA spends one step per cell the cutoff contour is a diamond,
which reads as straight diagonal lines drawn across the floor.

`lightAtCPU` in `levels.cpp` mirrors this maths on the CPU for billboard
tinting. **Change one, change the other**, or sprites stop matching the room.

### Viewmodels

The drink viewmodel is real 3D geometry drawn inside the 3D pass. Three rules
were learned the hard way:

- **Hold it inside 0.34 m.** Collision guarantees you are never closer than
  that to anything solid, so a viewmodel nearer than that can never be clipped
  by a wall. Held further out, corridors cut straight through it.
- **Do not disable depth testing.** The textbook fix for viewmodel clipping
  breaks this one: without depth, the far wall of the can's barrel draws over
  the near one and you read the label from the inside, mirrored.
- **Exempt it from world shadowing and give it an ambient floor.** Held next
  to a wall it otherwise falls inside that wall's shadow and goes black in
  your hands. `uOccN = 0` for the draw disables occlusion; both it and `uAmb`
  are restored immediately after.

## Conventions

- Comments explain *why*, not *what*. Several in here record a bug that a
  reasonable-looking change would reintroduce; keep those.
- No asset files. Everything procedural.
- The game is deterministic given a seed — preserve that. It is the only
  reason A/B screenshot comparison works.
- When fixing something visual, prove it: capture the same frame before and
  after and compare, or measure pixels. "It looks better" has been wrong here
  more than once.
