# CLAUDE.md

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
keys, weapons, flare, tape deck, interaction, drink, ambience, entity, dogs,
exits, then chunk streaming, then render. Order matters: e.g. weapons run before the
entity update, so a shot lands before the entity decides what to do about it.

## Building

### Normal machine

`make` — needs raylib installed (Homebrew or pkg-config). `make run` builds
and runs.

### Sandboxes with no system raylib

This is the case in the Claude Code web environment. There is no raylib
package available, but the **Python wheel ships a complete raylib shared
object** plus cffi-preprocessed headers, and C++ links against both directly.

Reconstructing that by hand used to be the single most time-consuming thing
about a cold start. It is now two commands:

```bash
tools/sandbox-setup.sh     # fetch the wheel, generate rlshim/ — once
tools/sandbox-build.sh     # build ./backrooms — dozens of times
```

`rlshim/` and `.rlwheel/` are generated and gitignored. What the setup script
is doing, in case it ever needs changing: it copies the wheel's
`{raylib,raymath,rlgl}.h.modified` into `rlshim/`, wraps each in `extern "C"`,
puts back the `#define`s cffi strips (`PI`, `DEG2RAD`, `RAD2DEG`, the named
`Color` constants, `MATERIAL_MAP_DIFFUSE`), and strips raymath's `inline`
keyword so its functions resolve against the shared object.

Two things in there are load-bearing and non-obvious:

- `raylib.h.modified`'s last line is a declaration with a trailing `//` comment
  and **no trailing newline**, so appending `}` naively buries the brace inside
  that comment. The `extern "C"` then never closes and the build fails hundreds
  of lines away in `<initializer_list>` with "template with C linkage".
- The wheel's `.so` must be linked by **absolute path with an `-rpath`**. Link
  it relatively and the binary only runs from the repo root — it dies with
  "cannot open shared object file" the moment anything runs it from elsewhere,
  which `tools/shot.sh` does on every screenshot.

## Running headless, and taking screenshots

There is no display in a sandbox, so everything runs under Xvfb. Use the
script, which starts one if it needs to:

```bash
tools/shot.sh out.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=0 \
    BACKROOMS_POS=15,15,0.8 BACKROOMS_SHOTFRAME=80
```

`BACKROOMS_SHOT` runs the game, captures one frame, and exits. Combined with
`BACKROOMS_SEED` and `BACKROOMS_POS` the result is deterministic apart from
light flicker and the blackout schedule, so the same command twice gives you
comparable images. Frames land in `shots/` (gitignored).

**`TakeScreenshot` writes relative to the process working directory** *and*
rejects path separators outright, so it only ever gets a bare filename — which
is why the script `cd`s into the output directory rather than passing a path.

To look at the results numerically — which you should, because "it looks
better" has been wrong here more than once:

```bash
tools/pixdiff.py crop shot.png zoom.png 850 560 500 290 2.0   # zoom a detail
tools/pixdiff.py diff before.png after.png                    # what changed, and where
```

`diff` reports which eighth of the screen the differences land in, which is how
you tell "I changed the HUD" from "I changed the world pass".

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
| `BACKROOMS_FLASH=1` | start with the flashlight on |

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
tools/sweep.sh
```

Then **look at the images**. Note that a Level 3 frame is *supposed* to look
almost black — the Red Halls sit at a mean luma around 12 out of 255, so the
regression shot for it is genuinely near-black and is not a broken shader or a
blackout. Confirming that cost a build of the previous commit; take this line's
word for it instead. (It was 16 before the fog started taking its brightness
from the local light instead of a constant; an unlit corridor no longer glows
at the far end, which is most of where the difference went.)

The sweep is a fixed spot in a corridor and it will not show you everything.
Two shots worth taking by hand when you touch lighting, because each exercises
a path the sweep never reaches:

```bash
tools/shot.sh pool.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=2 BACKROOMS_POS=95,79,1.2
tools/shot.sh beam.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=1 BACKROOMS_FLASH=1
```

The first is an open pool hall — white tile at high albedo under a whole grid
of fittings, which is where an exposure that is merely bright elsewhere turns
into blank white paper. The second is the only way to see the torch, which is
the one light in the game you aim and therefore the one a fixed-position shot
cannot otherwise reach.

To compare frame cost against another build rather than eyeballing `fps=`
(which is a smoothed integer, and the sandbox swings about 15% run to run):

```bash
tools/bench.sh ./backrooms.old ./backrooms 2      # best-of-3 each, same level
```

If the change should not have touched the world pass, prove it: build the
previous commit somewhere else and `tools/pixdiff.py diff` the two frames. A
HUD-only change puts essentially all of its differing pixels in one corner;
anything scattered through the frame means you moved the world. A clean exit
code proves nothing; several real bugs in this game's history rendered
perfectly valid frames that were wrong.

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

**`TextFormat` hands back a slot in a small rotating buffer.** Four or so
calls in, the pointer you kept from the first one now holds a later string.
The bottom-left inventory block was already close to the limit; adding one
more row made the flare line render as the tape-player line. Draw each line
straight off its `TextFormat` call — never collect `const char *` results and
`DrawText` them all at the end.

**raylib's default font stops at Latin-1, so an em dash draws as `?`.** `×`
(U+00D7) is fine; `—` (U+2014) is not, and several HUD strings were rendering
a literal question mark on screen. Use `·` (U+00B7) instead. Window titles are
unaffected — those go to the window manager, not the font atlas.

**Emissive detail sitting flush on a surface z-fights, and loses at range.**
The tape player's record lamp sat 0.1 mm proud of the shell: perfect in the
viewmodel a foot from the camera, completely gone once the deck was on a floor
four metres away — which is exactly when it had a job to do. Stand small decal
geometry ~1.5 mm off its host surface. A close-up screenshot will not catch
this; check the thing at the distance it is actually used.

**Blackouts are scheduled off wall-clock time, not frame count.** `applyLevel`
sets `nextBlackout = GetTime() + 30 + rand*60`, so a headless capture is only
repeatable if it lands before that window. The software rasteriser runs about
2.5 fps, so `BACKROOMS_SHOTFRAME=150` is already ~60 s in and can capture a
pitch-black frame that looks exactly like a broken shader. For iteration use
`BACKROOMS_SHOTFRAME=80` (~30 s), which is inside the guaranteed-lit window
and twice as fast. Level 2 never blacks out at all.

**`pkill -f "some pattern"` can kill your own shell.** If the pattern appears
in the command line of the shell running it — which it does whenever you type
the command inline, or write a heredoc containing it — `pkill` matches itself
and the shell dies with no output. Use `pkill -x Xvfb`, or put the command in
a script file and run the file.

**Do not run two sweeps at once.** `tools/shot.sh` reuses a running Xvfb
rather than killing and restarting one — the older scripts here killed it,
which meant two sweeps tore down each other's display mid-run and reported
"shader errors" that were really GLFW failing to find a display. That specific
failure is gone, but concurrent runs still write the same filenames into
`shots/` and still contend for one slow software rasteriser. One at a time.

**`vnoise2` returns 0..1, not -1..1** — and it is `vnoise2`, not `vnoise`.
Writing the usual `noise * 0.5 + 0.5` on it silently gives you half the range
sitting in the top half of it, which reads as a flat, washed-out texture
rather than an obviously broken one. `fbm2` is the same.

**raylib `Sound` has no loop flag.** `PlaySound` is one-shot. To sustain
something — the tape player's voice runs for 26 s off a 7.5 s clip — retrigger
it on `!IsSoundPlaying(snd)` each frame. There is a one-frame gap at the seam,
so a clip meant to loop has to begin and end somewhere quiet and be
crossfaded, or the join clicks audibly.

**A rule that has never visibly fired is not necessarily a rule that works.**
The relief-bump opt-out was documented, used in four places, and did nothing at
all: the shader tested `fragC.a > 0.995` and 254/255 is 0.9961. Nobody caught
it because the old lighting wrapped so far past the terminator that a perturbed
normal barely changed the shade — so the bug had no symptom until the
terminator sharpened, and then the whole ceiling came out blotched like mould.
Pick thresholds that clear a byte quantum. See the alpha table below.

**Every ambient in the level table is calibrated against the tone curve.** They
are small numbers going into a curve whose slope near black decides what they
are worth, and that slope is not close to 1: the old `1 - exp(-1.25x)` returned
about 1.25x its input there, a filmic curve returns about 0.21x. Swapping the
curve therefore darkens every shadow in the game about six-fold while leaving
the lit surfaces looking fine, which does not present as a tone-curve problem —
it presents as "why is Level 3 completely black now". Whatever compensates for
that has to decay as ambient rises, or the one level whose ambient was never in
the toe (the poolrooms, four times any other) blows out to white paper instead.

**A pattern inside a tiling texture must have a period that divides its size.**
The textures are 512 square and repeat. A feature grid at any other pitch —
form-tie holes every 171 px, brick courses every 42 — is fine inside one copy
and breaks at the wrap, putting a row of half-features down every seam in the
world. The brick had shipped that way for a while and nobody saw it, because
until each brick got its own tone there was nothing at the seam to mismatch.

**Timing in this sandbox swings about 15% run to run**, which is wider than most
of the changes worth measuring, and `fps=` is a smoothed integer on top of that.
A single before/after pair will happily tell you a change made things 20% slower
when it only ever removed work. Use `tools/bench.sh`, which takes the best of N
runs per binary; on anything close, N=3 is not enough — one level read 1.07,
1.08 and 1.17 across three of them and settled at 1.04 at N=5.

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
| `> 0.998` | additionally gets **world-space relief bump** |

That last row is a trap. Relief is right for grimy walls and carpet, wrong for
small curved objects, and badly wrong for anything that *moves* — the noise
field is fixed in world space, so a moving object swims through it. Opaque
geometry that should stay smooth uses alpha **254** (0.996): textured, visually
opaque, below the relief threshold.

**That threshold was `> 0.995` for a long time, and 254/255 is 0.9961.** So the
documented escape hatch did not work: everything marked 254 — the can, the hand
on it, the tape deck, its reels — was getting the relief it was explicitly
opted out of, and had been all along. It stayed invisible because the old
lighting wrapped so far around the terminator that a perturbed normal barely
changed the shade. The moment the terminator sharpened, the ceiling came out
covered in dark blotches about a tile across, like mould. Two lessons: pick
thresholds that clear a byte quantum (0.998 sits between 254 and 255 with room
either side), and a rule that has never visibly fired is not necessarily a rule
that works.

### Chunk mesh slots

`ChunkData::meshes[8]`: 0 floor, 1 ceiling, 2 walls, 3 props, 4 water,
5 wall scrawl, 6 window glass, 7 baked AO. Materials are `Game::mats[8]`:
0 floor, 1 ceiling, 2 walls, 3 props, 4 scrawl, 5 baked AO, 6 the can,
7 the tape player.

Every material carries the occupancy grid in its **normal-map slot**, because
`DrawMesh` reliably binds that as `texture2` where `SetShaderValueTexture` did
not. If you add a material, wire that up or its shadows will be wrong — an
unbound sampler reads as white, which the occlusion code interprets as "wall
everywhere", and the object goes black.

That wiring is a loop in `init()` with its **own hardcoded count**, separate
from the array size: `Material mats[N]` and `for (int i = 0; i < N; i++)` are
two literals that have to be changed together. Bump the array and forget the
loop and the new material is never initialised at all — no shader, no
occupancy texture — which is a worse failure than the black object above and
does not look like a material problem when you hit it.

### Lighting

Ceiling panels sit on a grid of `uLS` metres (8 on most levels, 12 on Level 1).
The shader sums only the **3×3 panels** around the shaded point, and each panel
fades out before it leaves that window — without the fade you get a visible
brightness seam along every light-cell boundary.

A panel is shaded as the **1.24 m square of glowing plastic it actually is**,
not as a point: `PANEL_HALF` in `shaders.cpp` is the same half-extent as `hp`
in world.cpp's panel mesher, and the shading point is `P` clamped into that
rectangle. Change the quad's size and change `PANEL_HALF` with it. Two things
fall out of treating it as an area:

- The terminator softens by how big the panel *looks* from the shaded point
  (`w`, the sine of its half-angle), not by a fixed wrap. This matters most on
  the ceiling, which hangs level with the fittings and so is lit edge-on by
  every one of them: a fixed hard terminator there turns any surface relief
  into blotches, and a fixed soft one flattens the whole building into the one
  even wash the place used to be.
- **Specular is evaluated once, outside the panel loop.** A reflection ray hits
  the ceiling plane at exactly one point; look up whichever fitting is there and
  clamp to its rectangle. Summing a lobe per light cost about a fifth of the
  frame on every level — including the matte ones, whose `gloss` is far too low
  for it to be visible — and gave a pinprick highlight instead of the panel's
  own shape stretched across the floor. It is skipped entirely below
  `uGloss > 0.10`.

The tone curve is filmic (an ACES fit) and it **has a toe**, which the old
`1 - exp(-1.25x)` did not. Near black the old curve returned about 1.25× its
input and this one returns about 0.21×, so every ambient figure in the level
table arrives roughly six times darker than it was authored to. `roomLight`
lifts them back with a compensation that decays as ambient rises — a flat
multiplier is wrong, and the way it goes wrong is not subtle: the poolrooms'
ambient is four times any other level's, high enough that it was never in the
toe, and a flat six-fold lift blew that whole level out to white paper. **If
you touch the tone curve, that compensation moves with it.**

The flashlight and a burning flare also light the *air* between the eye and the
fragment. There is no ray marching: the eye and the torch are the same point,
so the cone term is constant along a view ray and the whole in-scatter integral
collapses to one `atan`; the flare is the same integral about the ray's closest
approach to it. Both scale with the level's own fog density, so a level with
clear air has no visible beam.

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
That now includes the panel rectangle, the angular terminator, the ambient toe
compensation *and the tone curve itself* — props and billboards draw with
raylib's unlit shader and never go through the world pass, so the estimate has
to come out of the same curve or every sprite in the game sits at a different
exposure from the room it is standing in.

### Viewmodels

The drink can and the tape player are real 3D geometry drawn inside the 3D
pass, and both obey the same three rules, learned the hard way:

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
- **Keep this file current as you work.** Every entry under "Things that will
  bite you" is here because it cost someone an hour. When you lose time to
  something that was not obvious from the code — a silent failure, a library
  behaviour that surprised you, a number that had to change in two places —
  add it before you finish, in the same commit as the work that found it. Say
  what the symptom looked like, not just what the rule is: the symptom is what
  the next person will actually be holding when they come looking.
