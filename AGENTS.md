# Texture filtering and Pages caching (September 2026)

- raylib 5.5's SetTextureFilter(ANISOTROPIC_*) only sets the anisotropy level.
  Set TRILINEAR first, or min/mag stay GL_NEAREST from LoadTexture and the
  generated mips are never sampled.
- Panel centres are always cell corners. lightVis starts 1 cm along the ray,
  or the DDA crosses corner edges at t = 0 and a wall touching the corner
  falsely shadows the light: rings round every panel, dark ceiling patches.
  Near panels, panelVis clamps both penumbra taps into that same start cell:
  a tap pushed across a wall through the corner halved the light inside 6 m
  (hard dark ovals). The taps also fade into the single far tap (4-6 m).
- Pages serves everything with max-age=600 and no header control. web-build.sh
  stamps a build id: index.js/index.wasm load with ?v=ID, and the shell fetches
  version.txt with no-store and reloads once under ?b=ID when it is stale.

# Web reflection and pillar shadow fixes (September 2026)

- Desktop Chrome/ANGLE rendered black shards on the revolver when `lightState`
  returned early for dead lamps inside the reflection branch. Keep its single
  return with the dead-lamp mask; depth-buffer changes did not fix this.
- Pillar shadows must intersect the actual 0.42–1.58 m footprint, including
  the first and last ray cells. Skipping those cells made a bright square halo.
  Keep `pillarBlocks` single-exit too: the native Metal compiler stalled with
  an extra early return inside this nested lighting path.
- Pillar contact shadows use the same fading AO skirt as furniture. A flat
  dark quad in the wall mesh made a visible rectangular border.
- `tools/web-render-check.mjs` needs Playwright (resolvable by Node/NODE_PATH),
  a served web build in GAME_URL, and BROWSER_CHANNEL=chrome for hardware ANGLE.
  It tests 20 GPU visibility cases and a fixed-seed barrel crop. The old build
  fails the pixel check (16), while the corrected build passes (70).

# AGENTS.md

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
  play its first cycle and retain the accumulated cylinder index. Live cartridges
  stay attached during shooting; spent cases are visible only during unloading.
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
- Fluorescent emissive geometry is 0.12 below the ceiling of the cell it hangs
  in (`ceilY`, not a fixed wallH), matching the light height.
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
| `entity.h` | `Entity` (the Smiler; the Partygoer on Level Fun) and `Dog` state |
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

## Building for the web

`tools/web-build.sh` produces `web/dist/index.{html,js,wasm}` and the GitHub
Pages workflow runs exactly that script, so if it works locally it works in CI.
It compiles raylib for the browser itself, from a pinned tag, into `.raylib-web/`.

Everything platform-specific is behind `PLATFORM_WEB`, which the script defines.
The native build is untouched by all of it — `src/shaders.cpp`'s split was
verified with the assembly diff above, which came back identical.

Three things about that port are load-bearing:

- **`main()` cannot loop.** A browser tab owns its event loop, so
  `while (!WindowShouldClose())` hangs the page before the first frame is
  presented. `src/main.cpp` hands `tick()` to `emscripten_set_main_loop`
  instead. `shutdown()` is therefore unreachable on the web — anything that has
  to happen before the player leaves cannot live there.
- **It must be WebGL 2, not WebGL 1.** The world shader uses `dFdx` and
  `texelFetch`, which are core in GLSL ES 3.00 and simply absent from ES 2.0 —
  so an ES2 build fails to compile the shader, and a failed compile does not
  crash, it falls back to raylib's default and renders a black frame *faster*.
  raylib's own Makefile defaults PLATFORM_WEB to ES2; the build script overrides
  it with `GRAPHICS=GRAPHICS_API_OPENGL_ES3`, and `-sMIN_WEBGL_VERSION=2`.
- **Records live in IndexedDB.** A tab's filesystem is a heap that dies with the
  page. `web/shell.html` mounts IDBFS over `$HOME` in `preRun` and holds main()
  back with `addRunDependency` until the load finishes, because `Game::init()`
  reads the records file during startup. `Game::saveBest` flushes with
  `FS.syncfs` — and since `shutdown()` never runs, that is the only place on the
  web where records are written.

### Touch controls

A phone has no keyboard, no mouse and no pointer to lock. `src/input.h` is the
one place the game asks whether the player is doing something: natively, and in
a desktop browser, every wrapper is the raylib call it is named after and the
generated code is identical — the same frame captured before and after the
change differed by **0 pixels**. On a touch device `src/input_web.cpp` answers
from the on-screen controls `web/shell.html` draws instead.

Read player intent through those wrappers, not through raylib directly, or the
new control will work on a desktop and do nothing on a phone. Window-level keys
(F11) are not player intent and stay direct.

**`IsCursorHidden()` is the game's "am I actually playing" test**, and it gates
looking, firing, reloading, throwing and the aim — nine call sites. There is no
pointer lock on a phone, so it is false forever there, and the game comes up
rendering perfectly while ignoring every input. `inCursorHidden()` returns true
whenever the touch controls are up, because on that platform they *are* the
playing state.

**The virtual button bits are written down in two files** — the enum in
`src/input_web.cpp` and `BTN` in `web/shell.html` — and nothing checks that they
agree. Get them out of step and a button still works, it just does another
button's job, which reads as a game bug rather than a mapping bug.

**A fixed thumbstick is the wrong thing to build.** Anchored to one spot, every
grab that lands slightly off it becomes a look-drag instead of a step, and on a
phone that is most grabs. The stick floats to wherever the thumb lands in the
lower-left zone. It also reaches full deflection at 72% of its radius: a thumb
pivots rather than reaches, so requiring the whole radius means the player can
never sprint.

**Lay the buttons out from the corners they sit in, never from the far edge.**
`DUCK` addressed as `left: 42vmin` and `USE` as `right: 40vmin` are nowhere near
each other on a desktop and directly on top of each other on a 412px phone.
`tools/`-style checking does not catch this; the mobile test asserts that no two
controls' bounding boxes intersect, at portrait, landscape and 360×640.

**`setPointerCapture` throws if the pointer is already gone** — a fast tap, or a
synthetic event from a test driver. The throw aborts the rest of the handler,
so the press is registered and never released: the player ends up walking, or
firing, forever. Both calls are wrapped.

**raylib sizes the web canvas from `window.innerWidth`/`innerHeight`, not from
the element's box** — `EmscriptenResizeCallback` in `rcore_web.c` reads the two
and calls `emscripten_set_canvas_element_size`. The shell's canvas is stretched
by CSS over `#stage`, which is the viewport *minus the footer bar*, so the two
never agreed and the world was drawn squashed to cover the difference. Worse,
that callback clamps to `CORE.Window.screenMin` first: `SetWindowMinSize(640,
400)` rendered a 412 px phone at 640 wide and let CSS compress it by a third.
Neither reads as a window-size bug — it reads as a bad field of view, which is
why it shipped. The web build now asks for a 240 px minimum and `fitCanvas()` in
the shell gives the element its backing store's own aspect ratio inside the
stage, letterboxing the remainder, so nothing is stretched whatever raylib picks.

**A flex container that centres content taller than itself clips it, both ends,
and does not scroll.** The splash used `justify-content: center`, and on a
landscape phone (about 640x330 of stage) the title went off the top and the
ENTER button off the bottom: the page looked like it had failed to load rather
than like it had more to show. `overflow-y: auto` on the scroller plus
`margin: auto` on an inner block is the fix — `margin: auto` centres while the
content fits and hands the overflow to the scroller when it does not, which
`justify-content` will not do.

**`hud()` scales type to the window's *height*, and a phone is short of width.**
Every centred HUD line is a full-width row: at 412 px across, the title card's
letter-spaced name measured about 700 px and ran off both edges, and the intro
card's control list off the right. `hudTextC` now shrinks a line to 92% of the
screen before drawing it (`fitSize`), so nothing clips; `hudText`/`hudTextR` are
corner blocks and deliberately do not. The title itself draws through `DrawText`
for its own drop shadow, so it calls `fitSize` by hand — change one, change both.

**Hold-to-aim is the wrong gesture on a touch screen, and latching it deadlocks
the reload.** A held AIM button parks the right thumb for the length of a
gunfight, leaving nothing to fire with. The latch lives in `input_web.cpp`
rather than the shell because the *game* decides whether an aim is legal
(`Game::updateAim`), so `webReleaseAim()` drops a latch the game refused and the
button cannot sit lit over a gun that never comes up. The trap: `canReload()`
refuses while the sights are up, so a latched aim silently swallows every tap of
LOAD and the revolver can never be reloaded again — the poll drops the latch on
a RELOAD press for exactly that reason. A reload already running keeps the
latch, because natively holding RMB through one raises the sights when it ends.

**"Begin" must be an edge, never a held state — the death card is what proves
it.** The title screen starts a run on `webStartGesture()`: a tap on open
screen, or the stick crossing into a real push. The obvious implementation is to
read the stick's *deflection* instead, and it is wrong in a way that only shows
up at the worst moment — a thumb still resting on a pushed stick at the instant
you die is still pushed a second later, so the death card dismisses itself the
frame its read-it-first hold expires, and the player never sees how the run
ended. The crossing is recorded once per grab (`r.pushed` in the shell), so a
finger already down when a card appears has to lift and act again. The same
applies to the tap: it is counted on `pointerup`, not `pointerdown`.

**A synthetic pointer event's target is whatever you dispatched it on, and this
handler branches on the target.** `pointerdown` asks
`e.target.closest('.tbtn')` to tell a button press from open screen, so a test
that fires every event at `#touch` makes *every* gesture look like open screen —
including the button press that must not start a run. Dispatch at
`document.elementFromPoint(x, y)` and let it bubble. (The check caught this on
its first run, which is the argument for writing the negative cases too: had it
only asserted that taps start the game, it would have passed while proving
nothing.)

**`TouchFrame`'s field order in `input_web.cpp` is the `HEAPU32`/`HEAPF32` index
in the `EM_ASM` block below it.** Insert a field in the middle rather than
appending and every field after it reads its neighbour's value — the look drag
becomes the thumbstick, which reads as a control gone haywire rather than as a
struct layout mistake.

**Sprint is past the ring, and the ring is the only thing that can tell you
where the line is.** The stick's movement magnitude saturates at 0.72 of the
radius — a thumb pivots rather than reaches — and sprint used to fire at 0.92
of *that*, which is 0.66 of the radius: inside the circle, invisible, and easy
to cross while just walking briskly. It is now a deliberate push past the drawn
edge (`SPRINT_ON` 1.08 radii, `SPRINT_OFF` 0.98 for a dead band, because a
thumb resting on the line otherwise flickers in and out of a sprint several
times a second and the stamina drain turns that into a stutter). The ring gains
a `.run` class while it is held: the knob has already saturated at 0.62 of the
radius and stops moving long before the thumb does, so without that nothing on
screen says the line exists. Stamina was 16 seconds of running against 6 to
recover, up from 10 — a sprint that ends before you have crossed a 6 m hall is
a sprint nobody uses.

**Sprint limiting is currently off, and it is switched off in one number.**
`SPRINT_DRAIN` in `Game::updateSprint` is 0, so stamina never falls and
`sprintExhausted` never latches in normal play. Nothing was deleted to do it:
the hysteresis, the 6 s recovery, the HUD meter and the regression check are
all still there and still exercised — the check forces `stamina = 0` by hand
rather than sprinting the meter down, so it tests the exhaustion *mechanism*
rather than the drain rate, and it keeps passing with the drain at zero. That
is deliberate: a check that only fires when a tuning number is nonzero tells
you nothing the moment someone turns that number off, which is the same trap as
the relief-bump opt-out. Put a limit back by setting `SPRINT_DRAIN` to
1/seconds — it was 1/16, and 1/10 before that.

**Two taps on iOS is a page zoom, and in a first-person game two taps is
ordinary play.** A double tap on FIRE zoomed the whole shell instead of firing
twice, which reads as the game freezing and the HUD growing rather than as a
browser gesture. `touch-action: manipulation` on `html, body` keeps panning and
pinch-zoom and drops only double-tap-to-zoom; the canvas takes `touch-action:
none` because the game owns every gesture over the world. It has to sit on the
document and not on `#touch` alone — the gesture is recognised on whatever the
two taps land on, and half the screen is canvas, footer and gate.
`user-scalable=no` in the viewport meta is *not* the fix: iOS has deliberately
ignored it since iOS 10, so the obvious one-line change does nothing at all and
looks like it should have worked.

**A probe point picked as a fraction of the viewport lands on a control.** The
sprint check first grabbed at `(0.14w, 0.72h)`, which is DUCK on a tall phone:
the reading came back with the crouch bit set and no stick role at all, which
looks exactly like "sprint is broken" rather than "the test poked the wrong
button". Grab the stick by its own bounding rect. And read the touch state
*before* the `pointerup` — lifting zeroes `moveX`/`moveY` and releases the bit,
so anything measured after it is 0.

**Touch look sensitivity is a product, not a constant.** The shell reports a
drag in the same pixels a mouse delta is and `updateLook` multiplies by 0.0030
rad/px, so what a player feels is `LOOK_GAIN * px * 0.0030` — and a thumb,
unlike a mouse, cannot be lifted and replaced mid-gesture, so the number that
matters is how far *one drag* turns you. At the original 1.35 a swipe clean
across a 412 px phone was about 95 degrees, so looking behind you took four of
them while something walked at you. It is 2.4 now, and `mobile-check` asserts
the product (a 200 px drag turns 60-110 degrees) rather than either half, because
either half can move.

**`vmin` is the wrong unit for anything positioned inside `#stage`, and iOS is
where that shows.** The touch controls are absolutely positioned inside the
stage, which is the viewport minus the footer — but `vmin` resolves against the
*viewport*, and on iOS Safari in landscape the layout viewport is the whole
screen height while the visible stage is about two thirds of it. So a
bottom-anchored `JUMP` at `31vmin` and a top-anchored `DRINK` at `17.5vmin`
both reached further into the stage than their numbers implied and landed on
top of each other. Photographed on an iPhone: JUMP over DRINK, MARK over LOAD,
USE over AIM. `layoutControls()` derives one unit from the stage's own box and
writes px; nothing in the control CSS is a viewport unit any more.

Two things fell out of fixing it, both of which shrank the buttons for nothing:

- **The vertical budget is per side.** The left column is the stick with DUCK
  above it; the right is FIRE/AIM/LOAD/JUMP under the top cluster. They never
  share a row, so adding one column's height to the other's and calling it the
  budget costs a fifth of every button's size to prevent a collision that
  cannot happen. Horizontally they *do* share rows, so that one is a sum.
- **Budgets that are exact come out touching.** With no clearance term the
  clusters met to the pixel and rounded into an overlap, which is
  indistinguishable from the bug. `GAP` is 4% of the unit.

`min(stageW, stageH)` is also the wrong clamp on a stage three times wider than
it is tall — the budgets are the real constraint and they already account for
shape.

**Below about a 330 px stage, 40 px touch targets and no overlap are
arithmetically incompatible.** 13 controls, the smallest at 0.12 of the unit,
need a unit of 333 to clear 40 px — and the unit is the stage height over the
budget. So `mobile-check` asks for 40 px on stages a device really produces and
asks the squeezed stress case only to stay non-overlapping and on-stage. Do not
"fix" that by lowering the real floor; it is a statement about how many
controls fit, not a tuning number.

**`flex: 0 0 auto` collapses a flex item to its content height, so measure
before you set it.** The stage-squeezing case read `clientHeight` after
switching the stage out of `flex: 1`, got 132 px instead of 891, and reported
every button as impossibly small — a broken test that looked exactly like a
broken layout.

**`node tools/mobile-check.mjs` is the only check that sees any of this.** It
loads `web/shell.html` in Chromium at eight sizes — including an iPhone on its
side with both Safari bars showing, which is the shape the control overlap was
photographed on — and asserts no two controls overlap (at the full stage and
again with the stage squeezed to 64%, because a short stage is its own case), that the splash fits or scrolls with ENTER on screen, that the canvas
keeps its aspect ratio at three different backing sizes, and that the seven
title-screen gestures resolve the way they should — a middle tap, a slightly
sloppy tap, a stick push and a stick tap all begin a run; a button press, a look
drag and a cancelled press all do not. Run it for anything that touches the
shell. Two notes on running it: ESM `import` ignores
`NODE_PATH`, and the sandbox's playwright is installed *globally* and is
CommonJS, so it arrives under `.default` — the script handles both, and without
that the failure is a bare `ERR_MODULE_NOT_FOUND` that looks like a missing
package.

**`navigator.maxTouchPoints` describes capability, not the primary input.**
Chrome on a touchscreen laptop can report several touch points while the player
is using a mouse or trackpad, so treating any nonzero value as mobile covers a
desktop game with the touch overlay. Default from `(pointer: coarse)`, which
describes the primary pointer, and leave hybrid/tablet overrides to `?touch=1`
and `?touch=0`. The mobile check simulates a fine-pointer desktop with ten touch
points so this does not regress.

**`emcc` will compile this and then fail to link it.** Every C++ symbol comes
back undefined — `operator new`, `operator delete`, `std::__2::__next_prime` —
which reads as a missing stdlib or a broken sysroot. It is neither: `emcc` is
the C driver and does not link libc++. Use `em++`. (Emscripten's own hint about
this is the last line of a hundred-line error, well past where you stop
reading.)

**Emscripten stopped hanging the heap views off `Module`, and miniaudio still
reaches for them.** raylib's audio callback does `Module.HEAPF32.buffer`, so
without `-sEXPORTED_RUNTIME_METHODS=...,HEAPF32` every audio callback throws
`Cannot read properties of undefined (reading 'buffer')` — about forty times a
second, from a stack that names only `device.scriptNode.onaudioprocess`. The
game renders perfectly throughout and is completely silent, so if you are
looking at the picture you will not notice at all.

**A browser capture can land in a blackout, and there is no `BACKROOMS_SHOT` to
stop it.** Natively, headless captures turn blackouts off; on the web nothing
does, so a screenshot taken 30-90 s in is a black world with a live HUD — which
looks exactly like the silent shader fallback. Four of five level captures in
the first sweep were this. Pass `?noblackout=1`. A luma timeline settles it in
one run: the world sat at ~100, dropped to 13 for one sample, and came back to
87.

**The env knobs do not survive the title screen, on either platform.**
`BACKROOMS_LEVEL` is applied in `init()`, and then `startRun` calls
`beginDescent`, which puts you on Level 0 — and `updateMenu` clears `flashOn`
every frame it runs. Natively this never shows, because `BACKROOMS_SHOT` skips
the menu (`inMenu = shotPath == nullptr`); on the web there is no way to skip
it, so `?level=3` genuinely starts a run on Level 0. Three separate sweeps
"proved" the ES3 shader worked on every level and were all photographing
Level 0. To reach another level in a browser you have to descend, or use the F3
debug keys — and note that synthetic `F3`/`N` key events from a test driver do
not reach raylib, so this is not scriptable the way the native sweep is.

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
| `E` | spawn the Smiler stalking ~12 m ahead |
| `C` | force a chase (also squeeze, so holding it while F3 is up does both) |
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

It also reports **how full the collision scratch gets**, which is the one thing
in this engine that fails silently by design: `gatherCellAABBs` accumulates the
3×3 of cells around a point into a single `MAX_NEARBY_AABBS` buffer and *drops*
every box past the cap, so an overcrowded cell is a cell you walk through a wall
in and nothing anywhere says so. mapdump prints the worst 3×3 count, where it is,
and how many cells are over. Measured after the doors and 6 m halls landed:
**worst 20-22 of 48, zero cells over** on Levels 0, 1 and 3 — so when a wall does
not stop you, this is not why, and you can stop looking here. Check it again
after anything that adds geometry per cell.

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

And again after the corridors were widened to `HALL_LO + HALL_HI` cells (6 m),
at `--visit 1`, which is what a capture shows. Halls cost enclosure — that is
the same arithmetic as above, not a bug to hunt:

| measure | 4 m halls | 6 m halls |
|---|---|---|
| edges that are solid wall | 26.7% | 22.8% |
| cells with zero solid sides | 23.1% | 33.5% |
| median sightline | 3.5 m | 4.0 m |
| 90th percentile sightline | 9.5 m | 13.5 m |
| hidden at 20 m | 98.4% | 95.3% |

A third of the cells having no wall on any side sounds like the wall-less world
the partition was built to fix, and is not: the ring is 87 of a chunk's 256
cells, so 34% of the floor *is* corridor, and a corridor has no walls. Compare
the two numbers before reading anything into either.

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

**`mapdump` defaults to visit 0 and a headless capture runs at visit 1, so the
two describe different worlds at the same seed.** `Game::applyLevel` does
`world.visit = visits[lv]++`, and by the time `BACKROOMS_SHOT` takes its frame
that has already happened twice — while `tools/mapdump.cpp` constructs a World
and leaves `visit` at 0. The aggregate numbers still describe the generator
faithfully, because it is the same generator; but *positions* do not line up,
and an afternoon went into pointing a camera at a locked door mapdump had
reported at a spot where the game had put open floor. Pass `--visit 1` whenever
you want mapdump to describe the world a capture will show you.

**The doorways are punched by three passes that cannot see each other, and it
shows from inside a room, not on the floorplan.** The segment runs, the room
partition and the connectivity flood each open edges independently, and they
regularly land two and three in the same wall. A 1.3 m opening in a 2 m cell
leaves 0.7 m of plasterboard between each pair, so the wall becomes alternating
holes and piers — which reads as unfinished geometry rather than as a building.
Nothing in mapdump shows it; it took a screenshot. Two things now handle it, and
both are needed because neither is enough:

- `World::generate` closes one of every adjacent pair where the floor stays as
  connected as the flood left it, and where it does not — which is most of them,
  because a room reached only through its own door cannot lose it — *moves* the
  door instead, into blank wall on the same line, as near its old place as will
  take it. Measured over a 129-cell sample: 520 crowded pairs, 200 closed, 104
  moved, 216 with nowhere on the line to go.
- The mesher drops the jamb *between* two doorways that must stay adjacent, so
  those 216 become one wide opening under a continuous header rather than two
  holes with a sliver between them. `gatherCellAABBs` drops the matching jamb
  boxes, or you walk into a 0.7 m pier that is not there.

**"Does the floor stay connected" is a bridge test, and counting what one flood
reaches is not the same question.** The first version of the door thinning
compared the size of one flood before and after closing an edge. That is wrong
whenever the chunk's floor is already in two pieces — both reachable from the
world through different seams — because one of them is outside the count, so
every closure inside *that* piece looks free. It shipped 24 newly stranded cells
and took the largest cut-off pocket from 2 to 8. Closing one edge splits the
graph into at most two pieces, and it does so exactly when that edge's own two
cells end up in different ones: ask that, and nothing else.

**A flood written to stand in for `canStep` has to enforce the riser rule too.**
`canStep` refuses a step across more than `MAX_STEP`, so a flood that only tests
walls walks up a terrace face the pathfinder will not, calls a closure safe, and
strands what was behind it. Pools are exempt on both sides, exactly as there.

**Widening the corridors moved Clark's arrivals, and the regression harness
prints it rather than failing on it.** `ENT-03 arrivals` went from 78% inside
20 m to 36% when the halls went from 4 m to 6 m, because a spawn has to be both
close *and* out of sight and there is now less out-of-sight within 20 m — the
same enclosure arithmetic again. The assert is `nearUnseen > total/5`, so 36%
still passes with margin, but it is much nearer the floor than it was. If that
number goes under 20 and the diff did not touch the entity, look at enclosure
before you look at `updateEntity`.

**A locked door is *supposed* to make mapdump's reachability look worse.** About
one chunk in three gets a `WALL_LOCKED` door with its key nearby, and the door is
only placed where it either strands nothing or strands a closet of at most 12
cells. mapdump floods the floor and does not know the player has a key, so those
closets read as cut-off pockets: 19 locked doors cost about 0.2 points of
`reached from centre` in a 129-cell sample. That is the feature, not a
regression — check the *largest pocket* stays inside `CLOSET_MAX` instead.

**The wall mesher used to read `dd.wallN[i][kk]` raw rather than through
`wallNVal`, and that is exactly how a door ends up with no collision.** Every
overlay that changes what a wall *is* lands in `wallNVal` / `wallWVal` —
`unlockedDoors` for a door you have turned a key in, `shifted` for the doorways
the building closes behind you — so reading the raw array made the geometry the
one system in the game that never saw them, and it went wrong in both
directions at once: an unlocked door went on *drawing* its locked leaf while
collision happily let you walk through it, and a shifted doorway kept its
opening on screen while collision had already sealed it. Neither reads as a
mesher bug. The first reads as "doors have no collision"; the second as a
corridor you can see down and cannot enter. The mesher now goes through the two
accessors like everything else, which is what this file always claimed it did.
`World::unlockEdge` still rebakes both chunks that touch the edge — the
accessors decide what the geometry *is*, not when it is rebuilt — and anything
added to those overlays still needs its own rebake.

`tools/regression.cpp` pins both halves. Collision: a `WALL_DOOR` is passable
and emits exactly its two jamb boxes, a body in the opening passes and a body
in a jamb is pushed out, two adjacent doors emit only their outer jambs (the
mesher drops the shared one, so the AABBs must too), and a `WALL_LOCKED` blocks
bodies, light and `canStep` until `unlockEdge` turns it into a `WALL_DOOR`.
Geometry: it bakes the chunk, changes what the overlays say the wall is, bakes
again, and asserts the mesh moved. Reverting `nv` to the raw array fails that
assert, which is the only reason to believe it.

**Which mesh slot the difference lands in is not obvious, and guessing wrong
gives you a check that can never fail.** Locking a door does not change
`MESH_WALLS` at all: the locked and open branches emit the *same* three wall
boxes — two jambs and a header — and what differs is the leaf, its handle and
the threshold strip, which all go into `MESH_PROPS`. Asserted against
`MESH_WALLS` that check passes whatever the mesher does. Shifting an edge from
open to solid *is* a `MESH_WALLS` change, so the two halves of the same check
watch two different slots on purpose.

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
**Replacing one quad with a quad per cell costs 4% of the frame, for nothing.**
The ceiling used to be a single chunk-wide quad. Making it per-cell so a raised
deck could carry its ceiling up turned 1 quad per chunk into 256 coplanar ones,
and elevation touches about 5% of cells, so 95% of that was the same flat
ceiling drawn 256 times: `tools/bench.sh` read 1.043 on Level 0, which has no
raised cells at all. Greedy-meshing equal-height runs back together gives a flat
chunk its single quad back (0.986, i.e. noise) and only pays where the ceiling
actually steps. Any per-cell surface that is usually uniform wants the same
treatment — and `bench.sh` will tell you, where a screenshot will not.

**A cross-chunk lookup in the mesher pulls neighbouring chunks into existence.**
`ceilY(gi + 1, gk)` at a chunk edge calls `data()` on the neighbour and
generates it. That is correct and the floor mesher already did it, but it moved
`chunks=` in the capture banner from 40 to 45, and what a capture contains at a
given frame follows from that. If a frame gains distant geometry after a mesher
change and you cannot see why, check `chunks=` before you go looking in the
renderer. It is *not*, however, where Level 4's differing pixels in a
regression diff come from: those are its balloons, which bob on wall-clock time
and move with any frame-rate wobble.
**A guard written against absolute zero breaks the moment the floor moves.**
The trapdoor's trigger was `py > -0.05f`, meaning "you are standing at floor
level and not falling into a pit". Dishing the rotten patches 8.5 cm put the
player *below* zero while standing squarely on one, so the test stopped firing
and the trapdoor quietly stopped being a trapdoor. Nothing errored, nothing
looked wrong, and the only symptom was a capture at frame 60 that was still the
same yellow carpet it had been at frame 12. Anything comparing `py` against a
constant wants the floor height it is actually standing on.

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

**Level 4 has a noise floor of its own, and it is about 0.014%.** Its balloons
bob on `sinf(now * 0.8f + ...)` — wall clock, not frame count — so every balloon
in the frame moves with any run-to-run frame-rate wobble, and the regression
capture lands ~170 differing pixels in the middle-left bands against an
*unchanged* binary. Twice now that has been read as evidence for a change that
had nothing to do with it. The other four levels sit at single-digit pixels, so
do not carry Level 4's floor over to them, or the reverse.

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

**A FOV authored in vertical degrees only works at one aspect.** `fov` is the
camera's *vertical* angle, and raylib derives the horizontal one from it times
the aspect — so the authored 70 was only right at the 1440x850 window (about
1.69:1) every HUD number, light band and screenshot comparison was made
against. On a portrait phone (0.46:1) it became a 34 deg horizontal keyhole
that read as "the game is fine, just narrow" rather than as a projection bug.
`Game::baseFov()` now locks the horizontal view instead (pure
`fovForWindow()` for the harness), exact at the authored shape so the sweep
stays pixel-clean, clamped 58-100 vertical so square windows don't go fisheye
and phone-landscape doesn't go binoculars. Two rules to keep: the sprint/aim/
slide terms stay constant *vertical* offsets on the base — they are action,
not viewport — and `fovForWindow` must invert raylib's own cone identity
(`tan(fovy/2) = tan(fovX/2)·h/w`, the one render.cpp's culling uses), or the
lock drifts from what the camera draws. A headless harness cannot drive
`sprinting` through `updateMovement` — the flag is recomputed from the shift
key each frame and CGEvent key injection does not reach GLFW — so the sprint
composition is asserted as a target offset, not as a live flag; a check that
passes only against a value the next line overwrites is worse than none.

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
"built" — and the previous binary *used to be* still sitting there, so the next
capture ran happily and showed you the *old* behaviour. Two separate sessions of
"why is the entity missing" were this, both times from a missing `#include
<cstdio>` for a temporary `printf`. Check that the last line actually starts
with "built", or grep the output for "error".

`sandbox-build.sh` now deletes its target before compiling, so a failed build
leaves **no** binary rather than a stale one: `tools/shot.sh` fails outright
instead of capturing code you did not write. That closes the trap rather than
the habit — the compiler error still scrolls past, and a build you did not
notice failing now reads as a missing file, so it is still worth checking for
the word "built".

**Temporary test hooks must be removed by exact string, not by slicing.**
Cutting from `s.index(start)` to `s.index(end)` is dangerous when the end
anchor appears more than once — `if (shotPath && frame == shotFrame)` occurs
in three places, and slicing to the first one deletes hundreds of lines of
real code. Use a unique multi-line anchor, and `git diff --stat` afterwards.

## Architecture invariants

**Chalk is per level and per descent.** `Game::chalk` is an array indexed by
level, not one list: `applyLevel` no longer clears it, `beginDescent` does. The
marks are the only counter-play the game has to not knowing where you are, and
finding one of your own again is the good moment — clearing them at every
doorway deleted it. Two of the marks on each level were not made by you
(`ChalkMark::mine` is false, and they draw duller and yellower); they are laid
once per level per descent, on the first frame *after* arrival rather than
inside `applyLevel`, because `applyLevel` runs before the transition has moved
the player and would seed them around the last floor's position. The 128 cap is
`MAXCHALK`, per level, and eviction drops your own oldest rather than the front
of the list — the stranger's arrows are at the front, and evicting those would
quietly delete the rarest thing on the floor.

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

### Height

`int8_t ChunkData::elev` is one floor height per cell, in decimetres. The whole
engine rests on there being exactly one: `floorY` returns a scalar,
`buildOccupancy` is a byte per cell with no height in it, `lightVis` is a 2D DDA
and `pathStep` a 2D BFS. Extending height *upward* keeps all of that and still
buys stairwells, mezzanines and drops; walkable floor directly over walkable
floor is the one thing it cannot express, and nothing in the fiction needs it.

The ceiling is `World::ceilY`, not `wallH`. It follows the floor **up only**:
`max(floorY, 0) + wallH`. A raised deck has to carry its ceiling with it or its
floor comes through the slab, which is the whole reason the ceiling is per-cell.
A sunken cell is the other case and is *not* a lower storey — a pool basin and a
sunken lounge are depressions in the floor of the room they are in, and they
keep that room's ceiling. Dropping it with them hangs a soffit round every pool
in the Poolrooms 0.6 m below the tile grid, which reads as a broken mesh.
Telling a depression from a genuine lower storey needs something the cell does
not store yet.

Two rules fall out of this, and both have already been broken once:

- **Anything attached to the ceiling takes `ceilY` of the cell it is in**, not
  `wallH`: the light trays, the diffusers and sprinklers, the conduit runs, the
  Level 3 pipework, the Level 4 streamers and the ceiling crease AO strips. A
  crease left at a fixed `wallH` hangs in clear air under a ceiling that moved,
  which reads as a smear rather than a shadow.
- **A wall stands between two cells that may differ in height.** It is based on
  the lower floor and taken to the higher ceiling, and everything fixed to it —
  sill, door head, architrave, skirting, scrawl — is measured off that same
  base. Base a wall on its own cell instead and a step leaves a gap under it on
  the low side and a slot over it on the high side, both of which you see
  straight through.

What has *not* moved is the shader's light plane. `uLY` is still one constant
per level (`wallH - 0.12`), and `lightAtCPU` mirrors that constant, so a fitting
hanging in a raised bay is drawn where it is but lights the room from where the
base ceiling is. That is invisible at Level 1's 0.6-1.2 m decks and would not be
at a whole storey; it needs the panel grid to carry a height, which is a
different ticket. Move it and `lightAtCPU` moves with it.

Where two neighbouring cells' ceilings differ, the lower one draws a soffit
closing the slot. Only the lower of each pair draws it, so a shared edge is
drawn exactly once, including across a chunk seam where both sides read the same
global heights.

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
- **The rotten floor patches are a shortcut, not an accident, and three things
  say so.** `World::softDip` is the single source of the bowl's shape: the floor
  mesher builds the cell out of it and `groundAt` walks the player down the same
  curve, so what you see and what you stand in cannot drift. On top of that the
  camera sags further as `softTimer` builds, and `sndGroan` re-triggers faster
  and higher as it does. The 0.9 s grace window is unchanged — it was always the
  good part; what was missing was anything to spend it on. Subdivision is a real
  parameter here, not a detail: `MB::quad` carries one colour and one normal per
  quad, so 8 across the cell reads as a chequerboard and 12 still quilts.
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

## Projectiles and squeezing

- `Revolver::SHOT_INTERVAL` controls both firing cadence and Shoot playback.
  Keep those in sync when tuning the fire rate.
- Revolver rounds advance at 220 m/s and ray-test the full frame segment against
  solid world meshes and actor bounds. Keep nearest-hit selection shared across
  actors and walls; separate damage loops let one round hit multiple targets.
  Mesh bounds must accept a ray starting inside the box: rejecting it by the
  distance to the box exit skips nearby interior triangles.
- C / touch SQUEEZE reduces the player radius to 0.12 m at 1.1 m/s. On release,
  retain the narrow stance until the normal radius fits; expanding in a gap can
  eject the player through a wall. Match the collider's height tolerance.
- The source reload uses quarter-size live rounds as a visibility switch.
  Export those as hidden and retain full-size spent cases during ejection;
  omitting cases leaves tiny live bullets floating around the cylinder.
- Drinking PCM uses smooth envelopes at the same swallow times as drawDrinkCan.
  Test makeGulpWave directly for peaks and discontinuities; a loud white-noise
  attack sounds like a click rather than a swallow.

- Shadow lookup bias uses the geometric surface normal, not the detail-map
  normal: tile relief pushed floor samples across walls and made bright strips
  at Poolrooms corners. Occluded panels contribute no sharp reflected image.

## Health, swimming, double-tap run and the Smiler (September 2026)

- Catches no longer end the run. `Game::hurtPlayer` takes `ENTITY_HIT` (0.4)
  for a landed lunge and `PACK_BITE` (0.25) per bite, shoves the player 6 m/s
  away, and grants `HURT_GRACE` of immunity; `dieRun` only fires when health
  reaches 0. Both catch sites gate on `hurtT <= 0`, or one lunge overlapping
  the player for several frames strips the whole bar at once. After a landed
  lunge the Smiler's lunge is spent and it staggers, so it cannot re-commit
  from inside the grace. `updateHealth` regenerates after `REGEN_DELAY`.
- Swimming has one input: `updateSwimming(dt, rise)`. Not holding SPACE/JUMP
  sinks the swimmer — that is the dive — and holding it rises to and holds the
  surface float. There is no dive key; CTRL is only crouch, and is disabled in
  water.
- Double-tap W latches sprint (`wSprint`) until W is released; the second
  press must land inside `W_TAP`. Touch keeps its push-past-the-ring sprint.
- The hunter outside Level Fun is a lore Smiler: two sheets on the entity grid,
  `makeSmilerTex(false)` a fog body lit like any billboard and
  `makeSmilerTex(true)` the eyes and grin, drawn unlit at full white on top.
  Tinting the glow sheet by `lightAtCPU` like the body would put the grin out
  in exactly the dark corridors it exists to be seen in.
- Water one-shots are bubble synthesis (rising-pitch damped sines over a noise
  slap). Filtered noise alone read as static. Swim strokes have their own
  `makeSwimStroke`, not a pitched-down splash.
- The Poolrooms ceiling is 7.5 m with vaults peaking at 7.0 m; `lightMul` 1.25
  compensates for panels 2.7 m further from the floor. The regression entity
  captures search for a clear 7 m sightline rather than using a fixed spot,
  which had drifted behind a wall and was capturing an empty corridor.
