# CLAUDE.md

## Flashlight visibility

- Flashlight air scattering has a separate 0.20 weight and matches the surface
  cone exponent (26). Keep surface illumination independent: increasing the air
  term washes out the view. The shared fog factor also controls flare halos.

## Revolver iron sights

- Hold RMB to aim; F/L toggle the flashlight. Aim never toggles or queues reload.
- Reload requires fully lowered sights; an active reload finishes before aiming.
- Aim changes the viewmodel transform, FOV and look sensitivity, not asset skinning.
  The imported rear frame rib is solid: the eye must clear it slightly to see
  the front blade. Keep the blade tip on the camera ray when changing placement.

## Standard model pipeline (September 2026)

- Imported models are standard GLB files under assets/models. ModelAsset uses
  LoadModel, LoadModelAnimations, and UpdateModelAnimation; revolver code only
  controls gameplay pose adjustments. The former mesh.bin/poses.bin format is removed.
- The packaging callback embeds GLB bytes and falls back to normal file reads.
  New models need no custom binary parser. Reuse one loaded asset for static instances.
- Raylib 5.5 CPU skinning incorrectly translates normals. A version-gated fix
  removes weighted translation after its update; keep the weighted fixture test.
- Raylib samples animation every 17 ms. Prepared clips hold their final pose
  for 34 ms so the last sample reaches it. Regression failures now exit cleanly.
- See docs/model-pipeline.md for supported exports, limits, and performance.

## Previous graphics implementation notes (September 2026)

- Object detail maps use alpha 128 to identify an absolute gloss value in B;
  world maps and neutral maps use alpha 255 and retain level-relative gloss.
  This lets cloth remain matte and metal reflect in carpeted levels. Vertex
  alpha 254 still disables normal relief independently of material gloss.
- The revolver is the user-authorized CC0 model in assets/revolver. See its
  README for provenance and optional NumPy/Pillow reimport. src/revolver.cpp
  skins two material batches only when the pose changes; idle draws reuse buffers.
- The reload hinge and cartridge trajectories are reversed together at import,
  in the handle’s local frame, to open left. Do not mirror the complete gun/UVs.
- The source Reload clip already opens and closes the cylinder. Appending the
  separate open/close clips caused a visible snap. Shoot contains six shots;
  play its first cycle and retain the accumulated cylinder index. Cartridges
  stay attached during shooting because duplicate spent-case meshes are omitted.
- make and sandbox-build.sh run tools/embed-materials.py. Do not commit generated
  headers; commit converted assets and the reproducible import script.
- Authored metallic albedo needs diffuse attenuation in this renderer; importing
  the silver albedo unchanged made the gun look like white paint.
- Regression captures must stream all 25 visible chunks after relocating. The
  previous three render-only frames left missing black rooms in some screenshots.
- Can caps must map their triangle fan center to the atlas disc center. Mapping
  that vertex to angle zero stretched the lid detail into triangular wedges.


- `texture1` / MATERIAL_MAP_SPECULAR carries packed tangent slopes in RG and a
  gloss mask in B. The neutral map is (128,128,255). `texture2` remains occupancy.
- The old six-noise world-space bump is replaced by startup-generated material
  maps. Alpha 254 still opts out of relief. Historical bump notes below describe
  the earlier implementation and why that opt-out must survive.
- WORLD_VS must transform positions with matModel and normals with matNormal.
  Previously moved cans/decks were shaded at their mesh origin, making their
  brightness unrelated to where they actually stood.
- The prop atlas is 1024x512: original cardboard/metal occupies the left half;
  veneer and fabric occupy the right. Flat metal UV is (0.375,0.75).
- Fluorescent emissive geometry is at wallH-0.12, matching the light height.
- Revolver and flare are now 3D; their geometry shares the viewmodel shadow and
  ambient handling. Preserve depth testing and wall clearance.
- Native macOS startup checks for an active display before InitWindow: raylib
  5.5 crashed inside rlglInit with a null function when the laptop had no display.
  Keep the lid open or a monitor connected for remote graphics tests.
- Native macOS captures need WindowServer access; a sandboxed executable can
  hang with an XPC connection error. Linux still uses Xvfb.
- Use BACKROOMS_CLEAN=1 to remove the intro fade/HUD when judging exposure;
  frame 80 on a fast native GPU otherwise captures the opening black fade.
  BACKROOMS_TIME fixes rendering time only. See README for benchmark controls.
- Run `make regression`, the five-level/menu sweep, pool and flashlight shots.
  The regression harness needs a native display or an existing Xvfb display.

## What this is

A first-person backrooms horror game in C++17 on raylib + OpenGL 3.3.
World generation and sound remain procedural. The user authorized external assets
for realism: assets/materials contains CC0 tiles and assets/revolver an animated CC0 gun.
Keep provenance and license information alongside additions; embed assets at build
time so the executable remains independent of its working directory.

### Module map

| file | what lives there |
|---|---|
| `main.cpp` | `init()`, the `while (!WindowShouldClose())` loop, `shutdown()` |
| `game.{h,cpp}` | all run state; per-frame update in `tick()` |
| `render.cpp` | 3D scene pass, viewmodels, HUD, overlays |
| `world.{h,cpp}` | infinite maze: chunk generation, mesh baking, collision, line of sight. `WallKind` / `PropKind` / `ChunkMesh` name the codes stored per cell |
| `levels.{h,cpp}` | per-level look/feel table; CPU mirror of the shader's lighting |
| `shaders.cpp` | the world and post-process GLSL, as string literals |
| `textures.cpp` | procedural surfaces and composed CC0 material tiles |
| `revolver.{h,cpp}` | embedded authored revolver, pose interpolation, two material batches |
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
| `BACKROOMS_NOBLACKOUT=1` | never schedule a blackout (on by default under `BACKROOMS_SHOT`; pass `0` to shoot one) |
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

It now **exits non-zero** on a shot that failed, on a shader-error line in any
run log, and on any frame whose mean luma falls outside the per-level band in
its own header. A failed shader compile does not crash — the frame goes black
and the frame rate goes *up* — and until this landed the sweep exited 0 on a
completely broken build. A clean exit is necessary, not sufficient: still
**look at the images**.

Note that a Level 3 frame is *supposed* to look almost black — the Red Halls sit
at a mean luma around 12 out of 255, so the regression shot for it is genuinely
near-black and is not a broken shader or a blackout. Confirming that cost a
build of the previous commit; take this line's word for it, and the band table
in `tools/sweep.sh` where the same number is written down. (It was 16 before the
fog started taking its brightness from the local light instead of a constant; an
unlit corridor no longer glows at the far end, which is most of where the
difference went.)

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

## Measuring the layout, which screenshots will lie to you about

`tools/mapdump.cpp` links the game's own `world.cpp` and calls `generate()`
directly — no window, no GL, no Xvfb, about a second for a 258 m square:

```bash
tools/sandbox-build.sh mapdump                       # second build target
./mapdump --level 0 --seed 1337 --cells 129 --no-plan
./mapdump --level 0 --cells 33 --plan 0 0 26 12      # ASCII floorplan too
```

It reports enclosure (share of edges that are solid, and the distribution of
cells by how many solid sides they have), sightline percentiles and occlusion at
20 m, a reachability flood fill over the same `canStep` the pathfinder uses, and
densities in m² per instance for pillars, props, hide spots, doubloons, soft
floor, valves, exits, pools and elevation.

**Use it for anything that touches the generator.** From screenshots alone the
halls looked like they ran for hundreds of metres; measured, the median
sightline was already 7.0 m. The defect the pictures hid was enclosure — 57% of
Level 0 cells had no wall on any side — and no screenshot makes that obvious. It
also answers questions a capture cannot: 8% of Level 0's open cells were
unreachable from the centre.

Level 0, seed 1337, over 16641 cells, before and after the room partition:

| measure | before | after |
|---|---|---|
| edges that are solid wall | 13.7% | 26.7% |
| cells with zero solid sides | 56.6% | 23.1% |
| cells with two or more | 10.3% | 27.0% |
| median sightline | 7.0 m | 3.5 m |
| 90th percentile sightline | 24.5 m | 9.5 m |
| hidden at 20 m | 85.4% | 98.4% |
| reachable from the centre | 91.8% | 94.3% |
| largest cut-off pocket | 41 cells | 3 cells |

`hideSpotAt` and `coinAt` live on `Game`, which would drag the renderer into the
harness, so mapdump mirrors those two rules. Change either in `game.cpp` and
change it there too, or the harness quietly reports the old world.

To compare frame cost against another build rather than eyeballing `fps=`
(which is a smoothed integer, and the sandbox swings about 15% run to run):

```bash
tools/bench.sh ./backrooms.old ./backrooms 2      # best-of-3 each, same level
```

For a change that is *meant* to be behaviour-preserving — a rename, a named
constant, a comment — there is a cheaper and far stronger proof than any
screenshot. Compile the file both ways and compare the generated assembly:

```bash
git worktree add /tmp/base HEAD --detach     # link rlshim/ and .rlwheel/ into it
c++ -std=c++17 -O2 -Irlshim -S -o new.s src/sfx.cpp
c++ -std=c++17 -O2 -Irlshim -S -o old.s /tmp/base/src/sfx.cpp
diff <(grep -vE '^\s*\.(file|ident)|^\.LF[BE][0-9]+:' old.s) \
     <(grep -vE '^\s*\.(file|ident)|^\.LF[BE][0-9]+:' new.s)
```

Identical output means the change cannot have altered behaviour, full stop —
no sweep needed for that file. Adding an `#include` renumbers internal labels
(`.LFB986` → `.LFB995`, `.LLSDA…`), which is why the filter is there; anything
left after it is a real instruction difference. Extracting a function *will*
change the assembly, because it changes inlining, so fall back to screenshots
there.

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

**Enclosure and sightlines are the same number, and you cannot have both.**
A random sightline's mean free path is about one cell divided by the share of
edges that are solid: 13.7% walled gave a 7.0 m median, 26.7% gives 3.5 m, and
the relation held to within a few percent at every point in between. The same
arithmetic decides occlusion at 20 m — `(1-p)^20` — so asking for 25-30% walled
edges *is* asking for a ~3.5 m median and ~98% hidden at 20 m, whether or not
the ticket asking for it says so. Do not go looking for the bug that shortened
the sightlines; there isn't one. Long runs have to be put back deliberately, as
corridors and halls, which is what the ring clear and `HALL` in `generate` are.

**Independent generator passes sharing one grid will seal cells, and no amount
of tuning any one pass fixes it.** Segments, the room partition, pillars and
props are placed by four loops that cannot see each other: a segment laid across
a room cuts it in two, and a desk dropped in a room's only doorway strands
everything behind it. Tuned in isolation this looked like a room-size problem —
every size left hundreds of one and two cell pockets and reachability around
85%. The fix is not in any of the four passes but after all of them: the
union-find at the end of `World::generate` floods the finished chunk and punches
a doorway across any edge that still has two regions either side of it. Add a
pass that can block a cell and it is already handled; add one that runs *after*
that flood and you have reintroduced the bug.

**`Rng::ri(0, n)` takes a modulo and dies on a negative `n`.** A BSP that
splits any rect at least `2*MINR - 1` wide computes its cut range as
`rw - 2*MINR`, which is -1 on the narrowest splittable rect: the whole game
exits with a bare `Floating point exception` and no other output, from a line
that looks like arithmetic on room sizes rather than a division. A side needs
`2*MINR` to hold two rooms, not `2*MINR - 1`.

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

**Blackouts are scheduled off wall-clock time, not frame count, and frame 80 is
not safe from them.** `applyLevel` sets `nextBlackout = GetTime() + 30 + rand*60`,
so the earliest one is 30 s in; the software rasteriser runs 2-3 fps, which puts
`BACKROOMS_SHOTFRAME=80` at roughly 30-40 s — *inside* that window, not before
it. This file used to claim frame 80 was "the guaranteed-lit window". It never
was: two identical `tools/shot.sh` runs at seed 1337 produced one frame at mean
luma 100 and one at 15, and the dark one looks exactly like the silent shader
fallback, which is how it costs someone an hour.

So headless captures no longer schedule blackouts at all — `BACKROOMS_SHOT`
turns `noBlackout` on, and everything that sets `nextBlackout` goes through
`Game::blackoutIn`, which returns `BLACKOUT_NEVER` in that mode. Pass
`BACKROOMS_NOBLACKOUT=0` to shoot one on purpose; the F3 `B` key still forces
one either way, because that is explicit. `blackoutIn` still *draws* its random
number before discarding it, so a capture's `grng` stream stays aligned with a
normal run's. Level 2 never blacks out regardless. Before this, `tools/shot.sh`
at frame 200 came back at mean luma 3.4 — the frame was simply a blackout.

**`pkill -f "some pattern"` can kill your own shell.** If the pattern appears
in the command line of the shell running it — which it does whenever you type
the command inline, or write a heredoc containing it — `pkill` matches itself
and the shell dies with no output. Use `pkill -x Xvfb`, or put the command in
a script file and run the file.

**`tools/shot.sh`'s shader-error grep used to match a line nothing was wrong
with.** Every headless run in this sandbox prints `error: XDG_RUNTIME_DIR is
invalid or not set in the environment` from the audio/GLFW stack, and the filter
was `grep -iE '...|ERROR:'` — case-insensitive, so it matched, so `shot.sh`
exited 1, so `set -e` killed the sweep after level 0 with no output that looked
like a cause. The `ERROR:` half is now case-sensitive, which is right anyway:
`ERROR:` in capitals is raylib's own TraceLog prefix. Keep it that way, and
never widen this filter with `-i` — it is the only thing that catches a silently
failed shader compile.

**`beginDescent` clears the per-run tallies, and `dieRun` calls it.** So
anything you increment in `dieRun` before that call is wiped a line later:
`deathCount` did exactly that and the death card cheerfully reported your first
death on every run. A count that is supposed to outlive a descent — deaths, and
the records — must be kept out of that reset list, with a comment saying why,
because the reset list is otherwise the obvious place to add it.

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

**raylib 6.0 also restructured `Model` and made animation frames fractional.**
`Model.boneCount` / `Model.bones` moved into `Model.skeleton`,
`ModelAnimation.frameCount` / `framePoses` became `keyframeCount` /
`keyframePoses`, and — the one that bites at runtime rather than at compile
time — `UpdateModelAnimation`'s frame went from `int` to `float` and now
*interpolates*, so it reads keyframe `f` **and** `f+1`. A synthetic
one-keyframe clip, which is how you push a hand-built pose, therefore reads one
past the end and segfaults inside the library: the game died on startup with a
stack ending in `UpdateModelAnimation` and nothing in the log. Two identical
keyframes interpolate to themselves and are safe on both versions.
`src/model_asset.cpp` is the only file allowed to know any of this — ask
`ModelAsset` for the skeleton rather than reaching through `Model`.

That interpolation also moves the *poses*, not just the API. The sampled reload
reaches about 5% further at its extreme on 6.0 than on 5.5 — measured 0.2942 m
model-space against the old 0.28 the regression harness asserted — so landing
the 6.0 fix turned `make regression` red on `main` with nothing actually wrong:
the rule is that the viewmodel stays inside 0.34 m, and `0.155 + 0.2942 × 0.48`
is 0.296. **Assert the rule, not a proxy for it**, and give any threshold that
stands in for a real constraint a stated margin. A check that fails on a rule
the code does not have is worse than no check, because the next person relaxes
the number instead of reading it.

`tools/regression.cpp` also needs `BACKROOMS_TEST_ASSET_DIR` pointing at
`tests/fixtures` or it exits immediately on its first `CHECK` — which reads as
a broken build rather than a missing variable. `tools/sandbox-build.sh` has no
regression target, so nothing in the repo tells you that; build it by hand with
`src/*.cpp` minus `main.cpp` plus `tools/regression.cpp`.

**raylib 6.0 redefined `SetSoundPan`'s argument without renaming it.** 5.5 took
0..1 with **0 = hard right**; 6.0 takes -1..1 with **-1 = hard left**. The
signature is identical and the mixer accepts any float, so old values keep
"working": on 6.0 a 5.5-era 0.5 "centre" plays 75% right, hard left plays hard
right, and nothing ever reaches the left channel. It reads as a mix that was
always a bit odd rather than as a bug. Go through `panFor(bearing)` in
`sfx.h` — bearing is -1 left, 0 centre, +1 right — and never call `SetSoundPan`
directly. The sandbox links 6.0.1 while `brew install raylib` still gives 5.5,
so the two builds genuinely need different numbers. `tools/sandbox-setup.sh`
puts `RAYLIB_VERSION_*` back into `rlshim/` (cffi strips it with every other
`#define`) so that branch is real rather than assumed.

**raylib `Sound` has no loop flag.** `PlaySound` is one-shot. To sustain
something — the tape player's voice runs for 26 s off a 7.5 s clip — retrigger
it on `!IsSoundPlaying(snd)` each frame. There is a one-frame gap at the seam,
so a clip meant to loop has to begin and end somewhere quiet and be
crossfaded, or the join clicks audibly.

**`tools/pixdiff.py` needs Pillow, which a fresh sandbox does not have.**
`tools/sandbox-setup.sh` now installs it, and pixdiff prints a one-line
instruction instead of a traceback if it is still missing. Before that, every
diff died with `ModuleNotFoundError: No module named 'PIL'` — after the sweep
you had just waited fifteen minutes for. `pixdiff.py luma IMG...` prints each
frame's mean luma, which is what `tools/sweep.sh` reads to catch a black frame.

**A cross-run pixel diff is only meaningful if both runs hit the same frame
rate.** The light flicker is `sin(t*31)*sin(t*47.3)` on wall-clock `GetTime()`,
so by frame 80 a 3 fps run and a 4 fps run are at different points in that
phase, and the diff between them lands around 0.04% of pixels concentrated in
the upper bands — which reads exactly like "you moved the ceiling lighting".
The *same* binary shot twice at 3 and at 4 fps produced that 0.044%; the same
binary against a changed one, both at 4 fps, produced 4 pixels out of 1.22 M.
`tools/shot.sh` prints `fps=` on every run: check it matches before you believe
a diff, and re-shoot rather than reason about a mismatched pair. The frame rate
varies with what else is running on the box, so this is not something you set —
it is something you check.

**Game time is not wall-clock time.** `dt` is clamped, so each headless frame
advances the simulation about 0.05 s while the wall clock advances ~0.25-0.3 s.
Anything driven by `dt` — a flare's 9 s burn, reload timers — needs roughly 20
frames per simulated second, so `BACKROOMS_SHOTFRAME=80` is only ~4 s into a
burn. Anything scheduled off `GetTime()` — blackouts, the flicker above — runs
on the wall clock instead. A test that needs a flare to reach the end of its
burn needs ~180 frames and will therefore also cross the blackout window.

**Screenshots are not byte-reproducible, so `md5sum` is not a regression test.**
Light flicker and the menu camera drift are driven by `GetTime()`, and the
software rasteriser's frame rate varies run to run, so the same binary
screenshotted twice differs in ~0.1% of pixels. Establish that noise floor by
sweeping the *same* binary twice, then compare it against the before/after
diff. A change that stays at the noise floor is clean; one that lands an order
of magnitude above it moved something.

**Timing in this sandbox swings about 15% run to run**, which is wider than most
of the changes worth measuring, and `fps=` is a smoothed integer on top of that.
A single before/after pair will happily tell you a change made things 20% slower
when it only ever removed work. Use `tools/bench.sh`, which takes the best of N
runs per binary; on anything close, N=3 is not enough — one level read 1.07,
1.08 and 1.17 across three of them and settled at 1.04 at N=5.

**A rule that has never visibly fired is not necessarily a rule that works.**
The relief-bump opt-out was documented, used at every smooth-object site in the
mesher, and did nothing at all: the shader tested `fragC.a > 0.995` and 254/255
is 0.9961. Nobody caught
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

**A HUD authored in pixels is a HUD that only works at one resolution.** Every
`DrawText` size and every offset from a screen edge in `render.cpp` goes through
`hud(px)`, which scales from `GetScreenHeight() / 850` — 850 being the height
the window opens at, which is what all those numbers were eyeballed against.
Fractions of the screen (`sh / 2`, `sw / 3`) are already independent of it and
must *not* be scaled, or the layout drifts off centre. Draw HUD strings with
`hudText` / `hudTextC` / `hudTextR` rather than `DrawText`: they put a scaled
dark offset behind the string first, which is the only reason the bottom-left
inventory block is legible over the Poolrooms' white tile. `hudTextC` measures
at the *scaled* size — measure at 16 and draw at 38 and the line sits off
centre. The viewmodel's `k = 1.25f` is not this: that one is in gun-local metres.

**An actor's floor height is not the same number as the height it is drawn at.**
`ent.dispY` / `Dog::dispY` are smoothed floor heights, and they now feed
`collideCircle` and the shot hit tests as well as the billboard — the six actor
`collideCircle` calls used to pass the default `feetY = 0`, so Clark crossed a
drop as if it were flat and stood inside a loading dock. They are smoothed
*after* movement each frame, so collision sees last frame's value; that is safe
only because `canStep` refuses to route across anything taller than `MAX_STEP`
and a spawn sets `dispY` exactly, so nothing ever legitimately stands on a cell
whose riser it would otherwise be pushed off.

**Vertical faces need three separate things to agree, or terrain is decorative.**
`MAX_STEP` (world.h) is the whole rule: `gatherCellAABBs` emits a full-height
blocker on any cell more than that above a neighbour, `canStep` refuses to route
across one, and the mover leaves the floor instead of gliding down one. The
generator *also* relaxes its own elevations to within `MAX_STEP` — without that
pass, enforcing the rule seals every sunken lounge in the game into a pit you
can fall into and not climb out of, because there are no stair meshes yet
(WORLD-06). Two traps around it: `lineOfSight` walks the same AABB list, so a
riser box would make an elevated cell opaque and blind anything standing on it —
it tests `top >= wallH` to look at full-height blockers only. And pools are
exempt on both sides: a pool floor is 0.6 m down, and a blocker there would
override the `poolAt` branches that are what getting in and out of one *is*.

**A rule the generator can no longer trigger still has to be tested.** The
elevation relaxation means nothing the world produces is taller than `MAX_STEP`,
so the riser blocker never fires in a normal run — exactly the shape of failure
the relief-bump opt-out had. `tools/regression.cpp` therefore writes a 2.5 m
terrace into a chunk by hand and asserts the box appears, that a body below is
pushed back and a body on top is not, and that `canStep` refuses both ways.
Assert the mechanism, not the map.

**The head bob and the footstep are one phase, and it counts footfalls.**
`bobPhase` gains 1 per stride, so an integer value is a foot landing: the step
sound fires on the integer crossing and the bob is `-cos(2*PI*bobPhase)`, whose
low point is exactly there. They used to be two unrelated numbers — a 2.32 m
stride against a 1.21 m bob cycle, 1.92 bobs per step and the ratio drifting
with speed — and the result read as a floaty, sluggish walk rather than as a
bug. Two things depend on the units now: `render.cpp`'s viewmodel sway reads
`sinf(bobPhase * PI)`, which on this phase is one lateral cycle per *two*
footfalls, which is what lateral sway actually tracks; and the wrap at 4096 has
to stay an even integer or both the crossing test and that sway jump at it.

**A hit test that only measures horizontal distance ignores where you aimed.**
Both actor hit tests projected onto `f2x`/`f2z` and measured the miss distance
in the plane, so you could aim at the ceiling and still land the round.
`shotHitsBody` takes the 3D `fwd`, walks the aim line out to the target's
horizontal distance and checks the height it has reached against the body's
span. Falls out of that: the eye rides at 1.62 m and a dog stands 0.92 m, so a
dead-level shot goes over a dog's back at any range — you have to put the
crosshair on it, which is the point. `popBalloonsAlongAim` had no sight test at
all and popped the party through walls; it gates on `lineOfSight` per balloon.

**An `osc()` index is an ownership claim, not a scratch slot.** `AudioSynth::ph[]`
is one running phase per oscillator, and two signals sharing an index advance it
at *both* their frequencies — so each one gets the other's detune folded in and
both come out subtly wrong rather than obviously broken. The hum's new beat
frequency and the blackout ring were written against 9-12 first, which the L1
drone and the poolroom water already owned. The header now lists the owners; add
slots to `ph[]` rather than borrowing one.

**The actors' sprites are sheets now, and the frame count lives in two files.**
`ENT_FRAMES` / `ENT_ROWS` / `DOG_FRAMES` (textures.h) size the atlas in
textures.cpp and index the source rect in render.cpp. Disagree and you get a
sliver of the neighbouring frame down one edge of every sprite, which reads as a
texture-bleed bug rather than as a count bug. One row is a *full* stride, not
half of one mirrored: Clark has a real leg and a peg leg, so the halves of his
gait genuinely differ.

**`DrawBillboardRec` is `DrawBillboardPro` with `origin = size*0.5`.** So any
draw that wants rotation has to pass exactly that to stay where it was. Both
`{0,0}` and the obvious "pivot about his boots" of `{0, -size.y/2}` slide the
sprite most of a body height up the screen and leave it hanging off the ceiling,
perfectly upright — which reads as a height or a lighting bug and sends you
looking in the wrong file. Rotation is then about the sprite's middle; on a
1.96 m billboard at 7 degrees the feet swing about 12 cm, which is not worth
fighting the API over.

**An actor's gait must come from distance travelled, not from intended speed.**
`Entity::gait` and `Dog::gait` count footfalls the way the player's `bobPhase`
does — 1 per stride, an integer is a foot landing — and they drive the walk-cycle
frame and the footfall sound off the same number, so the frame his boot lands on
is the frame you hear it. The old `entStepAcc` accumulated `chaseSpd * dt`, so a
Clark grinding against a wall still sounded like one crossing the room; and it
lived inside the Chase block, so nothing could animate him in any other state.

**Walls are read through `wallNVal` / `wallWVal`, and that is where the
non-Euclidean overlay has to land.** `World::shifted` is the set of doorways the
building has closed behind you (PAC-03). Collision, the pathfinder, line of
sight, the occupancy grid the shader marches, and the mesher all come through
those two accessors — put the overlay anywhere else and the lighting and Clark
disagree with the geometry the player can see. Two consequences to keep: shifting
an edge must rebake *both* chunks that touch it (`World::shiftEdge` does), and it
must set `occValid = false`, because `updateOccupancy` only rebuilds after you
have walked six cells and a wall that appears in between lights as though it
were not there.

**A pattern inside a tiling texture must have a period that divides its size.**
The textures are 512 square and repeat. A feature grid at any other pitch —
form-tie holes every 171 px, brick courses every 42 — is fine inside one copy
and breaks at the wrap, putting a row of half-features down every seam in the
world. The brick had shipped that way for a while and nobody saw it, because
until each brick got its own tone there was nothing at the seam to mismatch.

**`tools/bench.sh` aborts on any line containing "error:", including ALSA's.**
It merges the child's stderr into stdout and greps that for
`SHADER: .*(failed|error)|ERROR:` case-insensitively, so the sandbox's usual
`snd_func_card_id returned error` spam — and GLFW's `error: XDG_RUNTIME_DIR is
invalid` — kill the run after the first binary. What you get is one `BENCH`
line and no comparison, which looks like the harness working rather than the
harness dying. Setting `XDG_RUNTIME_DIR` fixes one of the two; ALSA's is
unavoidable while there is no sound card. Until the filter is narrowed, run the
two binaries yourself with `BACKROOMS_BENCH=1 BACKROOMS_TIME=4` and read
`mean_ms` off stdout, which does not carry the noise — alternate the order and
take the best of five, exactly as bench.py would.

**Decal geometry has to clear the wall, not the wall's centreline.** `WT` is
half-thickness (0.11), so a fitting built at ±0.028 about `gz` is sealed inside
the plasterboard. The conduit runs shipped that way for an afternoon: hundreds
of them generated, counted, and baked into the mesh, and not one pixel of any
of them on screen. Decals stand off the *face* (`gz ± WT ± clearance`), and the
face has to be chosen before the offset is applied, not after.

**A sine is the wrong curve for a walk cycle, and it wastes half your frames.**
`sin(60 deg)` and `sin(120 deg)` are the same number, so an evenly sampled sheet
driven by a sine puts the ankle in the same place twice: the first six-frame
Clark sheet had frames 1 and 2 differing by 129 silhouette pixels out of ~1900,
and frames 4 and 5 by 96 — three poses wearing six frames' worth of texture. A
real leg is planted for about 60% of the cycle, sliding backwards under the
body, then swings through in the other 40% with the foot off the floor;
`legPose` in textures.cpp is that, and it makes every frame distinct (worst pair
646 px) as well as stopping the walk from skating. Measure a sheet by counting
silhouette pixels that change between consecutive frames — the eye will happily
tell you six near-identical poses look fine.

**`DrawBillboardPro` does not place a billboard where `DrawBillboardRec` does.**
Swapping one for the other to get a rotation moved the entity clean off the
frame, and no value of `origin` brought it back to the same place. Whatever its
convention is, it is not "Rec plus an angle", so it is not a drop-in. If a
billboard needs to tilt, shear it in the sprite instead: the entity sheet's
lean rows are exactly that, and they cost a texture row rather than an
afternoon.

**Details stamped onto a sprite after its body is drawn must use the same
offsets the body used.** Those stamps — Clark's eyepatch, his eye, the skull on
the hat, the bandolier — only recolour pixels that are already opaque, so one
drawn at the unsheared position silently lands on empty background and is
dropped. The symptom is not a misplaced detail, it is a *missing* one: on the
first lean row his eye simply went out, which is the one thing on that sprite
that must never happen by accident. `bodyOff(y)` is the single function both
the scanline loop and the stamps go through.

**A build failure looks exactly like a passing build if you only read the last
line.** `tools/sandbox-build.sh` prints its error and then exits, so
`build.sh 2>&1 | tail -1` shows you a compiler note rather than the word
"built" — and the previous binary is still sitting there, so the next capture
runs happily and shows you the *old* behaviour. Two separate sessions of
"why is the entity missing" were this, both times from a missing `#include
<cstdio>` for a temporary `printf`. Check that the last line actually starts
with "built", or grep the output for "error".

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
documented escape hatch did not work: everything marked 254 — the can, the tape
deck, its reels — was getting the relief it was explicitly opted out of, and had
been all along. It stayed invisible because the old
lighting wrapped so far around the terminator that a perturbed normal barely
changed the shade. The moment the terminator sharpened, the ceiling came out
covered in dark blotches about a tile across, like mould. Two lessons: pick
thresholds that clear a byte quantum (0.998 sits between 254 and 255 with room
either side), and a rule that has never visibly fired is not necessarily a rule
that works.

### Chunk mesh slots

`ChunkData::meshes[MESH_COUNT]` is indexed by `enum ChunkMesh` (world.h) and
`Game::mats[MAT_COUNT]` by `enum MatSlot` (game.h). The first four entries of
each are the same four surfaces in the same order — floor, ceiling, walls,
props — which is what lets `renderScene` draw them in one loop. Keep that
alignment if you add slots.

Every material carries the occupancy grid in its **normal-map slot**, because
`DrawMesh` reliably binds that as `texture2` where `SetShaderValueTexture` did
not. If you add a material, wire that up or its shadows will be wrong — an
unbound sampler reads as white, which the occlusion code interprets as "wall
everywhere", and the object goes black.

That wiring is a loop in `init()`. It used to carry its own hardcoded count,
separate from the array size, so bumping the array and forgetting the loop left
the new material with no shader and no occupancy texture — a failure that looks
nothing like a material problem. Both now come from `MAT_COUNT`; leave it that
way.

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

The shader has exactly **one** flare point light (`uFlarePos` / `uFlareInt`),
and the muzzle flash borrows the same one when nothing is burning. You can have
up to `MAXFLARES` alight at once, so one of them has to be chosen to feed it;
every other fire still burns, wards Clark and the pack, and hisses, it just
doesn't light the room on its own.

**Choose it by `Game::flarePresence`, not by distance.** Presence is
`burn-fraction / (1 + FLAREFALL·d²)` — how much of a fire actually reaches a
point — and both `dominantFlare` (the point light) and the hiss in
`updateFlare` go through it, so the fire you hear is always the fire you see
by. Picking the *nearest* instead looks right and is wrong at the end of a
burn: a flare with half a second left lying a few centimetres nearer than a
fresh one holds the light while `flareGlow` fades it to nothing, and then the
light jumps across the room the frame it dies — one wall goes dark, another
lights up. An instrumented two-flare run had the nearest-flare pick naming the
guttering one for 13 frames at a presence of 0.008 against the fresh one's
0.248. Presence hands it over while both are still bright, so there is nothing
left to jump.

What presence cannot fix is two *equally* fresh flares either side of you: the
light still belongs to one of them, and walking across the midpoint swaps which
wall it lights. That is the one-light budget itself, not the picker.

Each flare's halo spheres are drawn from *its* own burn, not from the uniform —
share the uniform's intensity between them and the far ones pulse with the near
one's flicker.

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

- **Cell codes are enums, not integers.** What sits on a cell edge is a
  `WallKind` (`WALL_SOLID`, `WALL_EXIT`, `WALL_WINDOW`), what stands in a cell
  is a `PropKind` (`PROP_VENDING`, `PROP_PARTY_TABLE`, …), and the item lying
  loose in it is a `Pickup`. `blocksEdge()` is the "can a body get through this
  edge" test that collision, pathfinding, line of sight and the mesher share.
  Light does not use it: `buildOccupancy` tests `WALL_SOLID` alone, because
  glass stops you but not a fluorescent. Don't reintroduce bare `== 1` / `== 3`.
- **An atlas cell and the quad that carries it must be the same shape.** The
  quad stretches its cell to fit, so a fitting drawn at one aspect and hung at
  another comes out squashed — and on something as familiar as a faceplate that
  reads as wrong instantly. `FIXTURES` (textures.h) is one table holding both
  the atlas rect and the size on the wall, so the two cannot drift; the fittings
  get rects of their own proportions rather than slots in a uniform grid. The
  first version drew each fitting inside part of a square cell and hung the
  whole cell, and every outlet in the building came out a narrow vertical
  sliver.
- **`addSolidBox` hardcodes UV (0.375, 0.75)** for every face it emits, so any
  atlas it draws from needs plain, opaque material at that spot. Both the props
  atlas and the fixtures atlas keep their metal swatch there deliberately. Move
  it and the boxes sample a transparent cell and disappear.
- **The scrawl atlas grid lives in two places and they must agree**:
  `makeScrawlTex` (textures.cpp) lays the phrases out 4 across and 8 down, and
  `SCRAWL_PHRASES` / the `uvOf` lambda in world.cpp cut the UVs to match. Add a
  phrase without changing both and walls start showing you half of one line and
  half of another. The pen clips every dab to its own cell for the same reason —
  an atlas cell that bleeds puts a stray stroke from a neighbouring phrase on a
  wall, and bilinear filtering makes one pixel of bleed visible.
- **A passable edge and an edge with no geometry are different things.**
  `WALL_DOOR` is passable — `blocksEdge` says no, so pathfinding, line of sight
  and `buildOccupancy` all let it through, exactly as the bare gap it replaced
  did — but its jambs are solid and `gatherCellAABBs` gives them their own
  boxes. Skip that and you walk through the door frame, which reads worse than
  the gap did. `WALL_EXIT` still has no collision at all, so its jambs are
  phantom; that is pre-existing, not a pattern to copy.
- **A prop's height lives in three places and they must agree**: `addProp`
  builds it (world.cpp), `gatherCellAABBs` gives it a collision box, and
  `Game::bottleShelfY` says how high a carton stands on it. Change one, change
  all three, or you get furniture you fall through or cartons floating.
- Comments explain *why*, not *what*. Several in here record a bug that a
  reasonable-looking change would reintroduce; keep those.
- Prefer procedural world content; licensed external models and textures are user-authorized. Keep provenance beside each asset.
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
