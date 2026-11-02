Before anything else, fetch https://raw.githubusercontent.com/DomCampese/prompts/main/agent-conventions/AGENTS.md and follow it. Where it conflicts with this file, this file wins.

# Read first

- docs/readability.md: how code, comments, commit messages and docs are
  written here. Follow it for everything you touch.
- docs/migration.md: the core / sim / platform layers and what may depend on
  what. docs/unreal-handoff.md: the Unreal Engine 5 port.

This file is a list of rules and known problems. Each entry gives the rule,
the reason, and the symptom if it is broken. Write new entries the same way.

# Two builds, one repository (October 2026)

- The raylib game is the **Web build**, in `web/` (`web/src/` and the browser
  shell). It also runs natively on desktop.
- The Unreal Engine 5 game is the **Unreal build** (the Mac build), in
  `unreal/`.
- Both compile `shared/` (`core`, `sim`, `port`). Every platform compile
  passes `-Ishared`, because platform code includes `core/...`, `sim/...` and
  `port/...` by those paths.
- The move from `src/` to `web/src/` and `shared/` was checked by compiling
  all 44 sources before and after: identical assembly.

Both builds ship, so **a new feature lands in both in the same change**: its
rules in shared/, each build's code only drawing and playing it, and a shader
change made in the GLSL and the HLSL together. What the Mac build still lacks
is in docs/unreal-handoff.md, "Parity".

# Every session makes the game better to play (October 2026)

Each session's main item is a change a player would notice in the first ten
minutes: new gameplay, a new threat or mechanic, a level that plays
differently, entity behaviour, real meshes in place of boxes, a visible
rendering improvement, or a bug a player would hit. Moving code into shared/,
adding proof harnesses, doc edits and single new clips are not a session's
main item; do them only alongside one.

The proof rules in this file still apply. Do not choose work because it is
easy to prove. A PR states what a player will notice and why it was chosen.

Write all of it plainly (docs/readability.md, "Register"). That includes
messages to the user: short sentences, no jargon they would have to decode.

# The multiplex (November 2026)

Level 5 (`MULTIPLEX`, core/level_rules.h) is a cinema, and the second level
you reach: Level 0's exits lead to it and its exits lead to Level 1. Three chunks in four are one
auditorium filling the room patch; the rest, and the spawn chunk, are lobbies
with a concession counter (`PROP_CONCESSION`), columns, benches and plants.
The hunter is a Smiler. It is not from the Backrooms wiki, so CREDITS.md has
no row for it.

- **An auditorium is decided in core** (`World::generate`, the
  `level == MULTIPLEX` block). `audCell(dir, u, v)` maps its cells: u across,
  v back from the screen. `audElev(v)` is the floor: rows rise 3 dm each
  toward the back, so the back aisle is at 0 and the front 3 m down. A sunken
  cell keeps the room's ceiling, so the ceiling stays flat at 5 m. Seats are
  `PROP_SEATS`, turned by `audFacing(dir)` to face the screen, with aisles at
  u = 0, 6 and 12. `ChunkData::audDir` says which way an auditorium faces, or
  -1.
- **The elevation relaxation pass skips the multiplex.** It reads the four
  neighbours whatever wall is between them, so it pulled the front rows up
  toward the corridor floor on the far side of the wall. The floors are within
  `MAX_STEP` by construction; the regression checks every open edge.
- **An auditorium's outer wall is protected (`ChunkData::prot`)**, so the
  connectivity, door, lock and exit passes leave it alone. A door or exit at
  the front of the room would open 3 m above its floor. The marking is after
  the storeys pass, because that pass clears `prot`; marked before it, every
  auditorium got doors and exits in its front wall and the regression's step
  check failed. Exits only form in lobbies, so a lobby tries four times as
  often (5 in mapdump's 129-cell sample, as on other levels).
- **A seat row's collision box fills the back of its cell** (`seatBox`,
  `SEAT_FRONT`..`SEAT_BACK`): a body walks the legroom along the row and is
  stopped by the seats. Seat cells are props, so the pathfinder routes the
  hunter down the aisles. `hideSpotAt` counts seats and counters, so crouching
  still beside a row hides the player.
- **Seats are plain boxes, no bevels.** The props builder's indices are 16
  bits, and an auditorium holds a hundred rows (about 42,000 vertices of
  props). A bevelled fabric box is 88 vertices against 20. The regression
  checks every slot stays under 65,536.
- **The screen and its beam are drawn by `sceneDraws`, not the chunk mesh**,
  so they can show a film: `filmFrame` (port/draw.cpp) picks each 3.4 s
  shot's colour and level from the auditorium's seed. The beam is a
  translucent cone from the projection port, writing no depth.
- **The screen lights its room through the shader's one spare light**
  (`uLamp`, set in `frameLight`), 3.5 m in front of the screen, in the film's
  colour. The same function sets `uRoomMask` to the auditorium's floor, so its
  tubes are dark; the layout marks those fittings `FittingGap::Auditorium` and
  the mesher draws none. There is one mask and one lamp, so only the
  auditorium nearest the player (`nearestAuditorium`) is dark and lit by its
  screen; another is seen only through its doors, lit by tubes that are not
  drawn. A blue shot leaves the red curtains and seats nearly black; that is
  the light's colour, not a bug.
- **Each riser in the floor is drawn with a half-height step 0.35 m in front
  of it** (`stepEdge` in chunk_mesh.cpp). The aisle lights went on the riser
  first and the step hid them; they sit on the step's face.
- **Posters are split into bands that do not overlap**, inside a frame that
  stands proud of them, so no two faces share a depth (see the vending
  machine).
- The surfaces are `makeCinemaCarpetSurface`, `makeCinemaWallSurface` and
  `makeCinemaCeilSurface` (web/src/surfaces.cpp). The wall texture has no
  skirting or anything else at a height: auditorium walls are 8 m tall and the
  texture repeats every 3 m. The mesher adds dark skirting.
- `Distant::Boom` plays `Sfx::FarBoom`: a film's soundtrack through a wall.
- **Its number is 5 but its place is second.** `EXIT_NEXT` sets the order,
  and the records' "deepest" compares `LEVEL_DEPTH`, not the level number;
  comparing numbers would rank the multiplex deepest of all. The records file
  still stores the number. F3's N still steps by number.
- tests/golden gained lines for the new level and lost none; walk-l0-l2.trace
  plays back unchanged.

# Doors (November 2026)

The use key shuts or opens a doorway (`Sim::findDoor`, `useDoor`,
shared/sim/items.cpp). A shut door is `WALL_SHUT`, which no chunk stores: the
`World::shutDoors` overlay turns a `WALL_DOOR` into it in `wallNVal` and
`wallWVal`, like `unlockedDoors` and `shifted`.

- **`WALL_SHUT` blocks bodies, light and sight** (`blocksEdge`,
  `blocksLight`, and the full-height box `gatherCellAABBs` gives any blocking
  edge). The layout calls it `OpeningKind::ShutDoor`, and the mesher and the
  greybox build it as a locked door.
- **`World::shutEdge` rebakes both chunks that draw the edge**, and the sim
  sets `shadowsStale`, as for an unlocked door. `unloadAll` clears the set, so
  a new level starts with every door open.
- **The hunter routes through shut doors and the pack does not.**
  `canStep` and `pathStep` take `throughShut`, which only `hunterChase` passes.
  At a shut door between him and his waypoint he stops, strikes it every
  `DOOR_KNOCK` s (`Sfx::DoorBang`) and opens it after `DOOR_FORCE` s
  (`Sim::hunterForcesDoor`). Without the stop, the router walked him into the
  leaf and collision held him there.
- **A doorway joined to its neighbour takes no leaf** (`World::doorJoined`):
  the two share one opening with no jamb between them, and a leaf in one would
  leave half the opening open.
- **A door will not shut on a body** (`bodyInDoorway`): the player, the
  hunter or a dog within its radius of the leaf's line.
- **Shutting or opening is a noise** (`doorNoiseT`, `DOOR_NOISE` 14 m in
  `packNoise`), so the hunter and the pack hear it.
- **`Sim::crateAt` treats `WALL_SHUT` as a doorway**, or shutting a door
  would move Level 1's crates.
- The HUD shows "E · shut the door" or "open the door" while one is in reach.

# Footstep clip per floor material (October 2026)

Player footsteps and landings use a clip per floor material: carpet on
Levels 0 and 4 (`Sfx::Step`, unchanged), concrete on Levels 1 and 3
(`StepConcrete`), tile on Level 2 (`StepTile`), wood inside the Manila Room
(`StepWood`).

- **The floor material is `LevelRules::footing`** (core/level_rules.h), so
  the sim can read it; `Sim::stepSfx()` picks the clip. It must match the
  floor surface `LEVEL_SURFACES` (shared/port/levels.cpp) draws. If a level
  gets a new floor, change both.
- **New `Sfx` values go at the end of the enum**, and `SFX_COUNT` follows the
  last. Both builds load every clip up to it (`GameAudio::load`,
  `SoundMixer::load`), so a new clip needs no Unreal code.
- **Compare loudness A-weighted, not RMS.** Without gain the hard-floor clips
  measured 10-12 dB(A) over carpet; `makeHardStep`'s gains put them about
  5 dB over. Carpet's energy is mostly below 200 Hz, so RMS overstates it.
- **The trace records which sounds play** (`audioHash` in each digest), so
  changing which clip plays changes the golden trace. walk-l0-l2.trace was
  re-recorded for this change.
- **`tools/record-trace.sh` takes `KEY_HOLD`** (seconds each key is held,
  default 0.9). At 1.4 fps, 0.9 s was too short: F3 and N fell between
  frames and two recordings never left Level 0. Use `KEY_HOLD=2` on a slow
  machine, and check a new recording reached every level with
  `grep -o 'level=[0-9]' | sort | uniq -c`.

# The hunter can lose the player (October 2026)

In a chase the hunter used to path to the player's cell through any number of
walls, and gave up only after a fixed time unseen. Now he chases what he
knows (`Sim::hunterChase`, shared/sim/hunters.cpp).

- **He sees the player** within `HUNTER_SIGHT` (30 m) with line of sight,
  unless they are hidden or crouched more than 7 m away. **He hears them**
  inside `packNoise`'s radius, the one the pack uses: 5 m standing, 11 m
  walking, 20 m sprinting (x0.45 crouched), the tape player, a flare strike, a
  shot. Either sets `Entity::lastX/lastZ` and zeroes `lostT`
  (`Sim::hunterNotice`).
- **Without contact** he walks at `SEARCH_SPEED` to the last-known spot, then
  to open spots 3-7 m around it, and goes `Hidden` after `SEARCH_TIME` (10 s).
  He cannot lunge without sight.
- **Every way into a chase must call `hunterNotice`** (the stare, the shot
  alert in weapons.cpp, F3's C). Without it he searches wherever the last
  chase ended.
- **The search draws from `grng`.** walk-l0-l2.trace has no chase, so it still
  plays back the same; a recording with a chase out of sight would not.
- No platform code changed, so both builds get it.

# Distant one-shots (October 2026)

Levels 0, 1, 3 and 4 play a muffled one-shot at intervals from a random
bearing: `Sfx::FarDoor`, `FarClang`, `FarKnock`, `FarPop`. Level 2 has none.
Both builds play them with no platform code.

- **`LevelRules::distant`** (core/level_rules.h) says which sound a level
  plays. `Sim::updateDistant` (place.cpp) picks the time, variant, bearing,
  pitch and volume: first 20-70 s after entering, then every 35-85 s, the gap
  scaled down to 45% at zero sanity. None on the title and death cards or in
  the Manila Room.
- **It draws from `distantRng`, never `grng`.** grng decides blackouts and
  spawns, so drawing from it would move those, and every capture and trace
  with them. `distantRng` is seeded per level and visit in `applyLevel`. It
  draws its five numbers whether or not the sound is skipped, so a skipped
  sound does not change the next one. The regression checks both.
- **The filtering is baked into the clip.** Neither raylib nor the mixer can
  filter a one-shot, so `farAway` (port/sounds.cpp) applies a two-pole
  low-pass, a 30 Hz DC block and a Schroeder reverb when the clip is made.
  Each clip's loudest 100 ms sits 0-6 dB(A) over a carpet footstep, before the
  play volume of 0.32-0.54. Without the DC block the door clip had a 3%
  offset.
- **A local named `pitch` in the sim shadows `Sim::pitch`.** The Unreal
  module builds with `-Wshadow -Werror` and refuses it; the sandbox build does
  not. `tools/unreal-check.sh` catches it. The playback rate is `rate`.
- walk-l0-l2.trace was re-recorded. The previous code's replay of the new one
  first differs at frame 89, in the sound only.

# Unreal draws the Web build's world (October 2026)

Unreal does no lighting of its own. Every material is unlit and its colour is
the Web build's (unreal/README.md, "How the world is drawn").

- **There are two copies of each shader.** `WORLD_FS`, `POST_FS` and
  `TAPE_FS` (web/src/shaders.cpp) and unreal/Plugins/Backrooms/Shaders/*.ush
  are the same shaders in GLSL and HLSL. Change one, change the other. The
  world and post ports were checked by compiling them with DXC (DXIL and
  SPIR-V) and comparing every number with the GLSL's. The tape's is checked by
  `tools/tape-check.sh` (Camcorder section), which a port of the others could
  copy.
- **GLSL's `dFdy` runs up the window, HLSL's `ddy` down it.** The port uses
  `-ddy` (`BR_DFDY`) in `detailNormal`. With plain `ddy` every relief is lit
  from below.
- **The world shader's core numbers reach the HLSL as defines** from
  `world.json` (`texdump --unreal`), as `worldFragmentShader()` writes them
  into the GLSL. A new `@TOKEN@` needs a row in both. The occupancy bits are
  ints the HLSL masks with, so they sit under `bits`, not `defines` (the
  editor turns defines into floats). `BackroomsWorld.ush` stops with an
  `#error` if an older `world.json` without them is still in `Saved/Surfaces`.
- **Per-frame lighting is `frameLight` and per-frame drawing is
  `sceneDraws`** (shared/port/draw.h). render.cpp draws from them too
  (checked: 0 pixels differ over seven captures). Anything new that render.cpp
  draws around the chunks goes there, or Unreal does not draw it.
- **The HUD is `hudDraws`** (shared/port/hud.h): text, rectangles, gradients
  and a dot in window pixels, drawn by both render.cpp and `ABackroomsHUD`.
  The caller measures text (`HudMeasure`: `MeasureText` in the Web build, the
  engine's font in Unreal), so centred lines centre on the font that draws
  them. The move was checked by running main's `renderUI` against logging
  stubs over 200,000 random sim states (5.9 M draws, none differ). To
  redirect raylib calls, use object-like macros (`#define DrawText logText`).
  A function-like macro breaks on braced colours, whose commas count as
  arguments.
- **The revolver is shared/port/revolver.h, not an imported skeletal mesh.**
  The import never showed the reload. port/model reads the GLB and samples,
  blends and skins it as raylib 5.5 does (bone matrices that scale after
  translating, and the normal fix in model_asset.cpp). Against raylib 5.5's
  rmodels.c, 497,826 skinned coordinates over 63 poses were bit-identical.
  The Web build still uses raylib's path, so a raylib upgrade there must be
  matched in port/model.
- **Masked, not blended.** Unreal's opaque pass cannot alpha-blend. Faces the
  shader leaves below alpha 1 (vertex alpha 0.30 to 0.62, and the scrawl, AO,
  water and glass slots) are translucent sections (`BackroomsChunkBuild`); the
  rest are masked at 0.5.
- **Exposure is pinned at 1** and the post material replaces the tonemapper.
  Any engine default that lights, grades or adapts (Lumen, bloom, auto
  exposure, TSR's history) changes every pixel. Config/DefaultEngine.ini and
  `UBackroomsRender::ShowPost` turn them off.
- **A texture parameter needs a default texture in the base material**, even
  if every instance sets it. The billboards' Sheet had none, so the billboard
  materials failed to compile and the engine drew its default material: the
  revolver's smoke was an opaque dark square. The editor's digest now covers
  backrooms_world.py itself, so changing how a material is built rebuilds the
  materials on the next `make unreal-open`.
- **`LightQuality` is the only place Unreal's lighting may differ from the
  Web build's** (Project Settings > Game > Backrooms > Performance; the
  `Quality` vector, `U.Quality` in BackroomsWorld.ush).
  - High is the GLSL's arithmetic: with Quality pinned at 0, DXC's DXIL for
    the world material is the same 13,656 lines as before the setting.
  - Medium traces one hard shadow tap and fades shadows by 8 m (10-15 m at
    High). Low traces none. Both keep all nine panels, because each fades out
    inside that window. Low's shader has 36% fewer instructions.
  - The scene actor rebuilds a section only when its triangles change, and
    otherwise moves its vertices (`UpdateMeshSection`).
- **Textures are imported as raw bytes**: no sRGB decode and no compression
  (`TC_VectorDisplacementmap`). raylib uploads them that way, and the shader's
  arithmetic, the detail maps' slopes and the alpha codes assume it.

# Camcorder and recovered tape (October 2026)

The camera is never held (removed 31 October 2026: no V, no CAM button, no
viewfinder, zoom, night shot or infrared lamp). It records the whole run, a
small REC mark (`Camcorder::recMark`) sits in the top left in play, and the
tape plays back on the death card.

The code: shared/port/camcorder.{h,cpp} (`Camcorder`: interference, the
tape's frames and sounds, the playback clock, the clips, the REC mark and the
playback's OSD as `HudDraw`s) and web/src/camcorder.{h,cpp} (`CamcorderReel`:
the reel, `TAPE_FS`, the sound handles). In Unreal the reel is `UBackroomsReel` and the tape look is
`M_BackroomsTape` (Shaders/BackroomsTape.ush, written 27 October 2026).

- **The sim never sees the camera.** The tape reads the sim after each step
  and touches no input.
- **The REC mark hides while F3 is up**, which writes in the same corner.
- **The reel is one render texture, at most 2048 px on a side**, which is all
  WebGL 2 guarantees. A frame is 288 px on its long side and the reel holds
  64 of them, 4.3 s at 15 Hz; at 1440x850 that is 7 x 10 cells and 14 MB.
- **A frame is copied into its slot unblended** (`BLEND_CUSTOM`, ONE and
  ZERO). The scene target's alpha is below 1 wherever something translucent
  was drawn (decals, contact-shadow strips, water, glass), but the screen
  shows the colour regardless. Blending put colour times alpha on the tape,
  darker than what the player saw. Unreal's frames are the scene's colour
  too. Where nothing is drawn, both hold the level's clear colour: raylib's
  clear, and in Unreal a 4 km box of `M_BackroomsClear` around the eye
  (`ShowBackdrop`).
- **`TAPE_FS` takes its coordinates from `gl_FragCoord`, not the quad.** It
  samples reel frames through `uCellA`/`uCellB` as uv rectangles with v up
  the picture. raylib's
  y runs down and a framebuffer's rows run up; `cellUV` converts.
- **Echoed sounds play on aliases** (`GameAudio::echo`, `LoadSoundAlias`). A
  raylib clip keeps the pitch it was last given. Setting the slow-motion
  pitch on the clip itself would carry it into the next run's footsteps,
  which the sim plays without `SET_PITCH`.
- **`Sim::DEATH_CARD` is 12 s (was 7)**, to fit the playback (about 6 s) and
  then the card. `DEATH_CARD_HOLD` is unchanged, so a key still skips it.
- **The camera requests its sounds; it does not play them.**
  `Camcorder::noises` is the list, drained by `CamcorderReel::playNoises`
  (raylib) or `SoundMixer::camera` (Unreal). `Hiss` means "unless already
  playing", which only the platform knows. An `Echo` of a recording
  (the water, the party music) is not the mixer's to play: it comes back in
  `SoundMixer::recorded` marked `echo`, and `UBackroomsSound` plays it on its
  own component (`Player(path, true)`), so a live play of the same recording
  is not cut off. Unreal plays recordings centred, so an echo's pan is
  dropped there.
- **Call `fitReel` before the first tick, with or without a reel.** It sizes
  the list of frames, and `record` reads the oldest frame to trim the logged
  sounds; with no frames that divides by zero. Unreal fits 1440x850 in
  `NewSim`, then the viewport's size on the first frame (`FitReel`), and
  again whenever the size changes, as the Web build does on a resize.
- **The move to shared/port was checked call for call.** main's
  camcorder.cpp (renamed with `-DCamcorder=CamcorderOld`) and the new code ran
  side by side over 1.31 M random ticks (2,000 runs, with deaths, playbacks,
  resizes and a failed reel), logging every raylib call, sound, echo and
  light. None differed, and each of ten small changes planted in the new code
  was caught.
- **`tools/tape-check.sh` compares the tape shader in both languages on one
  GPU.** It draws `TAPE_FS`, BackroomsTape.ush and M_BackroomsTape's Custom
  node (`TAPE_CODE`, pasted into a stand-in for the material template): DXC to
  SPIR-V, spirv-cross back to GLSL, Mesa for all three. 50 states, 15.3 M
  pixels, none differ; each of eleven small changes planted in the .ush and
  the material code was caught. That was before night shot and the live
  viewfinder were cut from the shaders and the check (31 October 2026); it
  has not been run since. It needs DXC (the GitHub release tarball) and
  `apt install spirv-cross`. It found two things:
  - **Write GLSL's matrices as GLSL writes them, with the vector on the
    left** (`mul(v, M)`). Writing the rows out with `mul(M, v)` rounded 1/255
    differently on one pixel in 300,000.
  - **`gl_FragCoord` is the pixel position, not the viewport UV times the
    size.** Rebuilding it from the UV was off by a rounding, which moved the
    noise and line hashes on 0.4% of pixels. The material takes the
    ScreenPosition node's PixelPosition and flips it.
- **The post and the tape both replace the tonemapper**, so only one is in
  the post volume at a time: `ShowPost` swaps the blendable. The reel's frames
  are a `SCS_SceneColorHDRNoAlpha` capture, which no post-process material
  touches, at the cell's size, into an 8-bit linear target that the RHI copies
  into the reel (same format, so the bytes move unchanged). The capture's
  `PostProcessSettings` get `PinPost`'s overrides, or its exposure adapts.
- **`Load` must not require a new material.** `make unreal-play` runs the
  game (`-game`), which cannot build materials. After the tape landed, a
  project last opened in the editor before it had every material except
  `M_BackroomsTape`. `Load` refused them all and the world was black under a
  working HUD. The tape is optional now (a warning, and the death card shows
  no tape). `make unreal-play` also builds the materials headless when
  one is missing or the surfaces, the editor's Python or the recordings
  changed since the last good build (`unreal/Saved/materials.digest`). Opening
  the editor builds them too but writes no digest, so the next play builds
  them once more.
- **The eye always sees a lighter tape** (`POST_FS` and BackroomsPost.ush,
  31 October 2026). The post pass has TAPE_FS's line wobble, chroma smear,
  lifted blacks, scanlines, block noise, tracking band, head switching and
  dropouts at about half strength, with no toggle. On the death card
  `TAPE_FS` replaces the post pass as before. The
  numbers are its own, not shared with TAPE_FS, so tuning the tape does not
  move the eye. The post pass reads 21 taps (was 15): 15 and 16 are the luma's
  neighbours, 17 to 20 the chroma taps. The HLSL port was checked by reading
  only; DXC was not run on it, and no post-check like tools/tape-check.sh
  exists yet.
- **The tape's interference reacts to the hunter.** At frame 40 of a
  seed-1337 Level 0 capture the tape is torn because Clark has spawned about
  12 m away, out of sight. That is intended, not a shader bug.

# Unreal port, first slice (September 2026)

The Unreal project is `unreal/` (its README, and docs/unreal-handoff.md
"Status"). The Web build is the reference, and nothing in it depends on
`unreal/`. It builds and runs on the target Mac (UE 5.8.3). Build it with
`make unreal` after every pull, because opening the `.uproject` runs the last
build. docs/unreal-handoff.md "Next" lists what is open.

- **A new .cpp in shared/core, shared/sim or shared/port needs a one-line
  wrapper** in `unreal/Plugins/Backrooms/Source/Backrooms/Private/Shared`, or
  the Unreal module does not compile it. `tools/unreal-check.sh` fails until it
  has one. It also builds the wrappers the way the module does (clang, C++20,
  `-Werror`, `-Wshadow`, FMA on, no `-ffp-contract=off`) and runs the contract
  and the traces.
- **One module, not two.** Core has no export annotations, and editor modules
  are shared libraries with hidden symbols, so code in a second module cannot
  call core. No PCH either: the shared PCH is force-included into core's files.
- **Unreal names reflected types without their A/U/F/E prefix, ignoring
  case**, and two types with the same name fail UHT before anything compiles.
  `ABackroomsHUD` and a struct `FBackroomsHud` stopped the first Mac build
  ("shares engine name"). The struct is `FBackroomsHudState`.
- **In 5.8 a `TObjectPtr` converts implicitly only to its own type.**
  Returning a `TObjectPtr<UMaterialInstanceDynamic>` as a
  `UMaterialInterface*` fails with "no viable conversion" in every file that
  includes the header. Use `.Get()`, in the .cpp: in a header that only
  forward-declares the class, `.Get()` fails too ("not defined, but forward
  declared"), because the compiler cannot see it derives from the base.
- **In 5.8 `AssetRegistryModule.h` no longer brings in `FAssetData`.** Also
  include `AssetRegistry/AssetData.h` and `AssetRegistry/IAssetRegistry.h`,
  or a `TArray<FAssetData>` fails as an incomplete type.
- **Unreal defines `check()` as a macro**, so a shared function named `check`
  is expanded by it in any file that also includes Unreal
  (`contract::compare` was renamed for this).
- **The sim includes `fp_strict.h`, as core does.** Without it (or the
  compiler flag) a trace replayed with gcc or clang at `-march=native`
  diverged at frame 31.
- **Traces** (`BACKROOMS_RECORD=path`, `./replay path`, shared/sim/trace.h)
  record every input the sim gets and a digest of its state each frame.
  `./replay` feeds the inputs back and fails at the first frame whose state
  differs. A new `InputFrame` field must go into `inputFields` in trace.cpp,
  or it is not recorded. `tests/traces/walk-l0-l2.trace` is the golden
  recording. A change that alters the sim on purpose re-records it with
  `tools/record-trace.sh` (needs xdotool) and says so in the commit.
- **xdotool taps are shorter than a frame.** At the sandbox's 2-3 fps a
  `xdotool key` press and release land inside one poll and raylib never sees
  the key. Hold every key across a frame (record-trace.sh's `key`).
- **A run starts through `SimStart`** (shared/sim/start.h). `Game::init` fills
  it from the BACKROOMS_* settings and calls `simBegin`, `enterLevel` and
  `simPlace`; a replay and a port call the same three. Unreal opens on the
  title screen unless the map's options include `?level=`, `?seed=` or
  `?notitle` (so `LEVEL=1 make unreal-play` goes straight in).
- **Both builds use one records file format** (shared/sim/records.h, moved
  from game.cpp unchanged and checked on 80,001 valid, truncated and corrupted
  files). Unreal keeps it at `unreal/Saved/backrooms_best`.
- **A capture's spawn spot is chosen before any level is entered.**
  `simBegin` runs `findOpenSpot` in Level 0's maze at visit 0 on one storey,
  and the capture then stands at that spot in whatever level it enters.
  `tools/greybox-view.cpp` does the same. A tool that picks its spot in the
  final level's maze photographs somewhere else (the stairwell shot did).
- **`mapdump --cells N` with an even N overflowed a buffer** (the window is
  `-half..half`, one more than N) and aborted with "double free or
  corruption". It now rounds up to odd.
- **The revolver's placement and pose are in shared/port/held.cpp**
  (`heldWeapon`, `revolverPose`), read by render.cpp, revolver.cpp and
  port/revolver.cpp. `ABackroomsHeldActor` skins it on the CPU with
  port/revolver.h and places every vertex itself, so no importer convention is
  involved.
- **Sound is the Web build's mix, streamed to Unreal.**
  - The clips are PCM from shared/port/sounds.cpp and the ambience bed is
    shared/port/ambience.cpp. In the Web build sfx.cpp, audio.cpp and
    `GameAudio` only load them into raylib.
  - `SoundMixer` (shared/port/mixer.h) mixes the sim's events into one stereo
    stream, which `UBackroomsSound` queues on a `USoundWaveProcedural`
    `SoundLatency` ahead.
  - A new `Sfx` gets its row in `clipSpec`/`clipPcm` (or `clipRecording`) and
    both builds play it.
  - The mixer uses raylib's pan law (centre is 0.6875 per side, on streams
    too). The recordings are imported by the editor and play on their own
    components at `volume * CENTRE_GAIN`.
  - Unreal defines `TWO_PI` as a macro, so ambience.h's phase constant is
    `TURN`.
  - The move was proved byte-identical (docs/unreal-handoff.md, "Sound").
- **The chunk mesher is shared/port/chunk_mesh.cpp** (October 2026). It moved
  from web/src/world_mesh.cpp together with its builder (`MB`, now
  `Vec3`/`Vec2`/`Rgba` and no `bake()`), the fixtures atlas table (port/atlas.h:
  `FIXTURES`, the vending door rects, `vendUV`, `FLOOR_TILE_M`,
  `WALL_TILE_M`, `wallTileV`) and the party colours (port/palette.h,
  `PARTY_RGBA`). Checked by dumping every array of 686 baked chunks (5 levels,
  2 visits, 3 storeys) before and after: identical bytes, object meshes too.
  The port's white is `RGBA_WHITE`, because raylib defines `WHITE` as a macro
  and the Web build's files see both. texdump links port/atlas.cpp for
  `FIXTURES`.
- **`init_unreal.py` also runs when the editor binary runs the game**
  (`-game`, `make unreal-play`). There is no editor then, and its first
  EditorAssetLibrary call crashed the game in `GetSubsystemInternal`. It now
  skips the import under `-game`.
- **The editor imports the recorded sounds and the Web build's textures.**
  `Plugins/Backrooms/Content/Python/init_unreal.py` runs when the editor opens
  the project, which is why `unreal/Content/Backrooms/Sounds` and `World` are
  gitignored. A game run from a project the editor has never opened draws
  nothing and says so in the log. The revolver is not imported: the game reads
  assets/models/revolver.glb itself.
- **render.cpp draws loose objects from `simScene`** (shared/port/scene.cpp):
  pickups, crates, the set-down deck, coins, balloons, confetti and chalk, in
  the same order and at the same distances (`SCENE_*`) as before. Flares,
  impacts and the actors are still drawn from sim state.
- **A `SceneItem`'s yaw turns +x toward +z; raylib's `MatrixRotateY` turns
  the other way.** So the spins of cans, crates and the deck are stored
  negated and render.cpp undoes it. The Unreal scene had them all turned
  backwards until this was found.
- **The can and deck in hand are `heldCan`/`heldDeck`** (shared/port/held.cpp),
  moved from render.cpp's `drawDrinkCan`/`drawHeldDeck` and checked bit for
  bit against the old code over 200k states. port/draw.cpp's `heldDraws` turns
  them into draws for both builds.
- **The hunter's and the pack's poses are in shared/port/sprites.h**
  (`hunterSprite`, `dogSprite`: sheet, walk frames, row, size, fade).
  port/draw.cpp reads them and lights and fogs them for both builds; checked
  against the old inline code over 169k poses. The sheet layout
  (`ENT_FRAMES`, `ENT_FRAME_W`, `EntRow`, ...) is port/sheets.h, which
  textures.cpp paints to.
- **The layout golden (`tests/golden/layout.txt`) prints every field**, so an
  uninitialised field shows up as a line that changes from run to run.
  `Fixture::end` was garbage for most kinds until it was zeroed. A new field
  needs a value for every kind.

# Core library (September 2026)

The generator is a library with no raylib in it, so an engine port can reuse
it (docs/migration.md has the layers and their status).

- **`shared/core/` includes only itself and the standard library.** It holds
  the math types (`Vec2`, `Vec3`, `TAU`, `clampf`), hashes and noise
  (`hash.h`), the per-level rules (`level_rules.h`: wall height, light pitch,
  storey pitch, name, which tubes work, `EXIT_NEXT`), the world
  (`world.{h,cpp}`: generation, storeys, collision, sight, pathfinding, the
  occupancy grid) and the chunk layout (`layout.{h,cpp}`).
  `tools/core-check.sh` compiles each file alone with `-fno-exceptions
  -fno-rtti` and fails on any include outside the layer, raylib or GL. Run it
  after touching core. Platform code converts types with `toRl`/`fromRl`
  (`web/src/vec_rl.h`).
- **`./contract --check` is core's acceptance test** (`tools/sandbox-build.sh
  contract`). It compares generated chunks, query answers and wall changes with
  `tests/golden`, in under a second. Run it after touching core, with the
  mapdump set. Regenerate with `--write` only for a deliberate generator
  change, and say so in the commit (docs/migration.md, "Contract tests").
- **No fused multiply-adds in core.** Every build passes `-ffp-contract=off`,
  and each core .cpp includes `fp_strict.h` first, which sets the same by
  pragma. Without them, clang with FMA changed a `stairY` answer; a flipped
  noise threshold would change the maze. Never build core with fast math
  (docs/migration.md, "Floating point").
- **Core and the sim take sin, cos, exp, atan2 and hypot from
  `core/portable_math.h`, never from libm.** glibc's `sinf` is one ULP off the
  true value for 0.2% of floats and its `atan2f` for 16% of pairs, and other
  libms round differently again. The tube hash multiplies by 43758.5453 and
  then 53.7, so one ULP of sine moved a fitting's output by up to 0.42
  (switching re-recorded 5 lines of `layout.txt`). In the sim, one ULP of a
  yaw's cosine moves the player, and a trace replayed on a Mac would diverge
  there. `tools/core-check.sh` fails on any transcendental libm symbol in a
  core or sim object. The port (sound, poses, meshes) still calls libm, since
  nothing there affects game decisions.
- **`LevelCfg` extends `LevelRules`.** `LEVELS[lv].wallH` and
  `LEVEL_RULES[lv].wallH` are one number. Add a rule field to `LevelRules`, a
  look field (colour, fog, gloss, light output) to `LevelCfg`.
- **The mesher is `bakeChunkGeometry` in `shared/port/chunk_mesh.cpp`, and
  `ChunkData` holds no meshes.** It fills one `MB` per slot with plain arrays;
  the Web build's `bakeChunk` (web/src/world_mesh.cpp) uploads them as raylib
  meshes. `Game::chunkMeshes` (`ChunkMeshCache`) keys baked chunks by absolute
  storey and chunk.
- **Core appends to `World::staleChunks` wherever a chunk's geometry goes
  stale** (`unloadFar`, `unloadAll`, `rebuildChunk`, and so `shiftEdge` and
  `unlockEdge`), and every cache call drains that list first. Draw or ray-test
  a chunk only through `chunkMeshes.find`; a pointer kept across a world change
  can outlive its mesh. New core code that drops or invalidates a chunk must
  append to `staleChunks`, or the old meshes stay on screen and in GPU memory.
- **Numbers shared by collision and the mesher live in core/world.h**: the
  doorway opening (`DOOR_LO`, `DOOR_HI`, `DOOR_HEAD`), the pillar footprint,
  the rail size (`RAIL_T`, `RAIL_H`), the vending body (`VEND_HW`, `VEND_Y1`,
  depths, `vendFootprint`), `PROP_TURN`, `PROP_HASH_SALT`. textures.h reads
  the vending body from core.
- **The wallpaper's vertical mapping is state on the wall builder**
  (`WallBuilder::tileV`, `tallPaper` in chunk_mesh.cpp), set per bake. It used
  to be two file globals (`gWallV`, `gTallPaper`).

# Simulation and platform (September 2026)

The game rules live in `shared/sim/` as `Sim`. `Game` (game.cpp, render.cpp,
game_audio.cpp) is the raylib platform around it. docs/migration.md has the
interfaces.

- **The sim reads input only through `InputFrame`.** `Game::readInput` fills
  it once a tick from web/src/input.h, after the cursor is captured or
  released, so `playing` is what the rest of the tick sees. A new control is a
  field there and a line in `readInput`.
- **The sim never reads the clock.** `Sim::step(in, dt, now)` gets the tick's
  `GetTime()`. `captureTime` (BACKROOMS_TIME) fixes only what rendering sees.
- **Sound is a list of `AudioEvent`s, played in order after the step.** A
  raylib clip keeps its pitch, volume and pan between plays, and the rules rely
  on that (the hurt groan plays at the volume the soft floor last set), so a
  PLAY changes only the properties it flags. The tape voice, the looping
  recordings and the synth feed are events too.
- **The synth is fed once a tick, from `updateEntity`.** So it is not fed on
  Level 2 or the title screen, where that function returns early or does not
  run. The move kept this behaviour.
- **Flags the platform clears:** `shadowsStale` (`Game::updateOccupancy`),
  `dropAimLatch` and `recordsChanged` (`Game::finishStep`). `levelEntries`
  counts `applyLevel` calls; when it changes, `Game::syncLevelLook` applies the
  new level's surfaces and uniforms, after the step and before rendering.
- **shared/sim includes core and the standard library only.**
  - Its math is core's `Vec2`/`Vec3` and `sim_math.h` (`add`, `scale`,
    `normalize`, ..., `Ray3`, `Box3`, `RayHit`, `rayBox`). Each function uses
    raymath's formula in raymath's order, so results match the Web build to
    the bit.
  - Level numbers come from `LEVEL_RULES`, never `LEVELS`.
  - Colours are presentation: balloons and confetti carry an index below
    `PARTY_COLOURS`, and render.cpp looks it up in `PARTY`.
  - Convert at the boundary with `toRl`/`fromRl`. `tools/core-check.sh` fails
    on any other include.
- **Bullets hit level geometry through `SolidTracer`** (`MeshTracer` in
  game.cpp tests the chunk meshes). Actor bodies use `rayBox`, which follows
  raylib's `GetRayCollisionBox` step for step; the regression compares them on
  200k rays.
- **proof-shots' menu.png changes from run to run.** The title camera's pitch
  is `sin(GetTime() * 0.22)`, so the frame depends on how long 60 frames took:
  the same binary differed from itself by 26% of pixels under load. The world
  frames are fixed and must match to the pixel. One exception, cause not
  found (October 2026): lv0.png (and once beam.png) can differ between batches
  of the same binary by 5-6% of pixels, in the back wall's light, while six
  single `tools/shot.sh` runs all matched. Before blaming a change for an lv0
  or beam difference, re-shoot that frame a few times from both binaries.

# Chunk layout (September 2026)

`chunkLayout(world, cx, cz)` (shared/core/layout.h) decides what a chunk holds
besides its floorplan: props, wall and ceiling fixtures, light fittings and
the openings in its walls, in metres. `bakeChunk` builds each item and decides
nothing about where it goes. Walls, floors, ceilings, stairs and pools are in
the floorplan (next section).

- **A new decoration is decided in core and built in the mesher.** Add a
  `FixtureKind`, push it in `cellFixtures` (or the chunk-wide list), and build
  it in `addFixture`. Do not add a hash in chunk_mesh.cpp that decides whether
  something exists; an Unreal port would have to copy it.
- **Emission order is part of the output.** Fixtures are pushed in cell order
  and, inside a cell, in the order the mesher used to decide them. The mesher
  builds them at three points in its cell walk (`FixtureStage`: with the
  walls, on the pillar, in the service pass after the flights), then the
  chunk's own. Moving a kind to another stage reorders quads in a shared
  builder, which changes translucent blending and depth ties.
- **Which tubes work is a rule.** `dead`, `vary` and `faulty` are in
  `LevelRules`; `tubeHash` and `storeyHashOffset` are in core, and
  `lightAtCPU` calls them. The layout's `LightFitting::dead/output/faulty` come
  from the same hash.
- **The shader gets the hash's numbers written in.** `lhash` and `lightState`
  use `TUBE_HASH_*`, `TUBE_STOREY_SKEW`, `TUBE_OUTPUT_SPREAD` and
  `TUBE_STUTTER_SPREAD` (core/layout.h), inserted by `worldFragmentShader()`.
  Each prints as the shortest decimal that reads back as the same float:
  `lhash`'s 43758.5453123 reads 43758.547 in the compiled source, and both are
  43758.546875. The stutter's numbers (`TUBE_STUTTER_*`, `TUBE_BUZZ_*`) are
  inserted the same way, so `tubeStutter`, Unreal's lights and the shader use
  one set. A number used twice is replaced everywhere it appears
  (`TUBE_HASH_SCALE` scales all three hashes). A whole number prints as
  `10.0`, not `%g`'s `1e+01`.
- **Conduit groups cells with unsigned division** (`gi / CONDUIT_RUN` on a
  `uint32_t`), so a negative cell's group is not `fdiv`'s. Making it signed
  moves every conduit run west or north of the origin.
- `./mapdump --layout CX CZ` prints a chunk's layout (with `--level`,
  `--visit`, `--storey` as for the plan).

# Chunk floorplan (October 2026)

`chunkFloorplan(world, cx, cz)` (shared/core/floorplan.h) describes what a
chunk is built from: each cell's floor kind, height, pool, pillar and ceiling;
each north and west edge's wall, base and top, buried ends, void faces, and the
floor and ceiling either side; the risers and which cell draws each; Level 1's
dock lips; the ceiling as merged rectangles; soffits and bulkheads; the
flights' steps and the stairwell's landing; the Poolrooms' arches. The mesher
and the greybox both build from it.

- **A new floorplan rule goes in core; the mesher only reads it.** Which cell
  draws a shared riser, which edges get a bulkhead, how the ceiling is merged:
  each is decided in floorplan.cpp. Do not test `vflag`, `elev`, `wallNVal` or
  `floorY` in chunk_mesh.cpp.
- **The lists keep the mesher's emission order**, as the layout's do: risers
  and lips per cell (N, S, W, E; `risersIn`, `lipsIn`), soffits per cell after
  every ceiling rectangle, steps in the order they were laid. Reordering one
  reorders quads in a shared builder.
- **Riser ownership has two rules.** On Level 2 every cell draws the high side
  of each of its steps. Elsewhere a cell draws a riser when the neighbour is
  flat; otherwise the lower cell does; a cell with no floor of its own or at
  height 0 draws none. A port that draws every riser in the list draws each
  once either way.
- **The arches read the raw wall arrays** (`d.wallW[8][t]`), as the mesher
  always did. They are partitions the overlays never touch.
- **`tests/golden/floorplan.txt` prints only cells that are not plain**, and
  leaves out open edges between plain cells, but hashes all 256 cells with
  both edges. A failure on a `cells hash=` line with no differing cell line
  means a plain cell or a buried bit changed. `./mapdump --floorplan CX CZ`
  prints one chunk.
- The move was proved byte-identical: 2,500 chunk bakes (every array of every
  slot, 57 M vertices each side) and 5,000 greyboxes (74 M) against the old
  code, over every level, both visits, exits everywhere, three storeys, the
  Manila Room, and shifted and unlocked edges. The harness compiled the old
  chunk_mesh.cpp and greybox.cpp with `-DbakeChunkGeometry=bakeChunkGeometryOld`
  (and `-DgreyboxChunk=greyboxChunkOld -Dtriangles=trianglesOld`) beside the
  new. No GL is needed, since both produce plain arrays. Moving any field by
  1 mm failed it in 105 to 2,412 chunks.

# Vending machine (September 2026)

`PROP_VENDING` (Levels 1 and 4) is a glass-front drink machine at real size
(0.88 x 1.83 x 0.72 m) selling almond water. It was a grey box with a blue
quad.

- **The door is painted into the fixtures atlas, which is 1024 x 512.** The
  fittings keep the left half at the same pixels (`FIXPX`); `FIXTURES`
  divides u by 1024. The door is `VEND_PX` and the price strips
  `VEND_STRIP_PX`, both in the right half. Every rect on the door is in metres
  in textures.h (`VEND_WIN`, `VEND_BIN`, ...; the body's `VEND_HW` and
  `VEND_Y1` are core's). Both the painter (`drawVendingFront`) and the mesher
  go through `vendUV`, so moving a rect there moves the paint with the
  geometry.
- **The plain-metal swatch moved when the atlas widened.** UV (0.375, 0.75)
  went from pixel (192, 384) to (384, 384), so the swatch `addSolidBox`
  samples (`PLAIN_UV`) now sits in the strip between the diffuser and the exit
  sign. If it is lost, every conduit, sprinkler and lift door disappears.
- **Door x is mirrored against local x.** Seen from the front (local -z),
  local +x is on the viewer's left; the same thing mirrored the carton
  lettering. The mesher's `P(x, y, lz)` takes the door's x and negates it.
- **Vertex alpha 240 is backlit** (shader: `backlit`, band 0.93-0.955, in the
  textured path): lit by the room plus 0.62 of its own texel, opaque, and it
  raises `gLightLum` so the fog does not hide it. The header, display, price
  strips and cabinet interior use it; one machine in six is dead (254). At
  1.15 the white cans on a white back blew out to white, so the back of the
  cabinet is dark grey, as on real machines.
- **The cans are painted on the back of the cabinet**, 6 cm behind the glass.
  The shelf lips are geometry, so they move across the cans as you move. The
  first build put that quad 1.5 mm inside the cabinet body and the glass showed
  a dark box: front is -z, so "in front of" is `ZBODY - 0.0015`.
- **No face on the machine is layered a few millimetres over another.** The
  first version stood each painted door face, the display, the price strips
  and the cans 1.5 mm off the face behind them. The sandbox's software
  rasteriser showed no problem, but on an iPhone the whole front flickered:
  box fronts showed through the keypad and door, and patches of cans
  disappeared. The 1.5 mm offset rule is for a small decal on a wall, not a
  face the size of a door. The local `sbox` takes a mask of faces to emit and
  leaves out any face that something else covers or that lies in another
  face's plane (box fronts under the paint, the cabinet front under the cans,
  shelf ends in the stiles). The paint is the door's front, and the display is
  cut into the control column rather than laid on it. Keep new parts that way.
- **Machines stand with their backs to a wall.** The last pass in
  `World::generate` turns each one so a `WALL_SOLID` is behind it and open
  floor in front (skipping lift doors, via `liftHash`), and sets
  `PROP_AGAINST_WALL` (bit 2 of `propRot`). `vendFootprint` pushes it back to
  8 cm off the wall face; both the mesher and `gatherCellAABBs` call it, so
  collision follows the machine. Anything that reads `propRot` as a turn must
  mask it with `& 3`. About three in four find a wall; the rest stand in the
  middle of their cell.

# Surface and asset realism pass (September 2026)

Every world surface was rebuilt in `web/src/surfaces.cpp`, and the held and
loose objects were redrawn.

- **Relief is authored in metres, never derived from colour.** Each generator
  paints colour, height (metres, + toward the viewer) and a gloss mask on the
  same pixels. `finishSurface` turns height into slopes per metre, which is
  what `detailNormal()` takes. The old maps ran luminance through a gradient,
  so printed chevrons were embossed, every stain was a dent, and grout was
  recessed only because it was darker. `makeSurfaceDetail` (luminance) is kept
  only for the props atlas, whose scanned regions carry their relief.
- **Every term must wrap at the tile edge.** `fbm2` is not periodic. The old
  generators sampled it straight across 0..W, so Level 1's tide line jumped
  every 3 m along every wall and the slab showed lines across the floor at its
  2 m repeat. Use `Canvas::nz` (whole lattice cells per tile) and `wrapd` for
  anything scattered. `./texdump out/` writes each surface plus a `_wrap` copy
  shifted half a tile, so a seam shows in the middle of the picture.
- **Surfaces are at real size.**
  - Floors and ceilings map 2 m to a repeat. Walls map 3 m across and
    `WallBuilder::tileV` down.
  - Level 0's ceiling boards are 2/3 m (three to a repeat), so a grid line
    runs through every light centre (2 + 4k m), and the tray's 0.69 m
    half-width lands on the next line: a troffer fills a 2 x 2 bay.
  - Brick is 13 x 40 to a 3 m wall (231 x 75 mm on centre).
  - Pool tile is 1/6 m on floors and walls, in two textures (12 and 18 tiles),
    so the grout lines meet at the foot of the wall.
  - `CEIL_BOARD` (levels.cpp) must equal the board pitch the generator draws,
    or the shader's stains straddle the T-bars.
- **Each surface keeps its old mean brightness.** Each generator ends in
  `matchLuma(old mean)`, because the ambients, light multipliers and
  `tools/sweep.sh` bands were tuned against the old surfaces. The brick is
  matched on its red channel: at equal luma the new brick (grey mortar) came
  out a tenth darker under the Red Halls' almost pure red light, because the
  tone curve's toe removes all its green. Measure a region of the capture, not
  the frame mean, which hid this. `./texdump` prints every mean.
- **Level 0's wallpaper repeats every 1.5 m down the tile** (noise period
  512 px). Tall walls reuse the 0.75-2.25 m band (`wallV`/`voidFace`).
  Anything that depends on height (damp, the dust line, the skirting) stays
  outside that band. The skirting is 12 cm (was a 27 cm band) and stands
  14 mm proud with a bullnose; outlets at 0.32 m sit above it.
- **Ceilings take relief (vertex alpha 255).** They were opted out at 254
  because the old world-space noise blotched them under edge-on panel light.
  Authored ceiling height has no low-frequency term. If blotches return, look
  for a low-frequency term in a ceiling's height field first.
- **World-space variation** (`uMacro` = `SURF_MACRO`, `uBoard` =
  `CEIL_BOARD`) applies where the detail alpha is above 0.99. World maps are
  255, object maps 128, and the 1x1 neutral map is 250 for exactly this test.
  Set it to 255 and every decal, fixture and held object gets floor mottling.
  - The board term tones each ceiling board, yellows some, and water-stains
    about one in twenty, hashed per board and storey. Arithmetic only (see
    lightState).
  - Cost, best of 3 on the software rasteriser: +3.4% frame on Level 0 (board
    term included), about +4% on the Poolrooms. Each `vnoise` is four sin
    hashes; a second octave of each term cost another 3% and was removed.
- **Surfaces are generated on first entry to a level** (`Game::surface`), not
  at startup. A level's set costs 0.2-0.7 s natively, and only Level 0's is
  paid before the first frame. Levels share slots (the Red Halls reuse Level
  1's slab; the pool ceiling is its floor tile).
- **Lettering** (`printText` in textures.cpp) uses the scrawl glyph splines
  with an even pen, because raylib's 8 px bitmap font came out as a staircase
  on the can. It renders to a scratch buffer and composites by coverage,
  because `inkDab` only draws onto transparent pixels and skips opaque ones.
- **Carton UVs are two regions**: side (v 0.004-0.58) and top (v 0.61-0.99)
  of the props atlas' left strip. The old single region put a band of tape
  round the middle of every face. The Level 1 crate's label samples the
  carton-top shipping label's exact rect (`crateBody`); move one, move both.
- **`addPropBox` maps side faces mirrored as seen from outside**, so anything
  lettered on a side region is drawn flipped in the atlas. The first capture
  had "ALMOND WATER" backwards on every carton. Tops map the right way round.
- The can (576 px) and deck (512 px) are their old layouts at 3x and 4x, so
  the mesh UVs did not change; both are mipmapped. The dog sheet keeps its
  frame size and ground line; only the silhouette changed.
- **The revolver reflects the room.**
  - `detailMap` (model_asset.cpp) writes metalness into detail alpha (128
    dielectric to 188 metal).
  - For object maps only, the world shader adds a reflection of an
    approximate room: floor and walls in the level's fog colour, the ceiling
    and a bright band of tubes overhead in the light's colour, scaled by the
    fragment's `gLightLum`. It is weighted by Fresnel against the metal's
    colour (albedo x 2.6, undoing the importer's 0.35), and metal's diffuse is
    cut to a fifth.
  - Roughness sharpens the band but never dims a metal's reflection.
    Weighting it by gloss the first time left the gun grey.
  - It runs only while `uObjRefl` is 1, which render.cpp sets around the
    revolver's draw. Evaluated for every fragment it cost 2.2% of the frame.
    The branch is on a uniform; inside it the code stays arithmetic, because
    this model is the one desktop ANGLE drew black shards on.
- **Walnut stocks** are baked into the prepared GLB by
  `tools/revolver-finish.py`, idempotent via `asset.extras.gripFinish`, and run
  by `import-revolver.py`. The grip mask is "non-metal in the metallic map and
  inside a UV island in the normal map". The background is non-metal too, and
  tinting it would bleed brown into the steel's mip edges.
- `RED` and every other named colour is a raylib macro, so `const Vector3 RED
  = {...}` fails with "too many initializers" pointing into raylib.h.

# Room routes and taller courts (September 2026)

- Level 0 exits are visible, two-sided glowing doors. Do not replace them with
  noclip wall geometry: players need to recognise an exit from either side.
  Placement clears both landing cells; detection checks both axes and returns
  immediately after changing level.
- Ordinary floors have clean albedo and no procedural wet patches. Rotten
  Level 0 cells keep their dark sagging geometry and gameplay.
- Seam baffles and paired BSP entrances make interior rooms into
  through-routes. This runs before vertical features are stamped, so protected
  stairs stay intact.
- Rare courts align openings through 3, 5 or 7 storeys. Their eight-storey
  band reserves its caps against conflicting pair features. Intermediate
  storeys carry both HOLE and OPENUP; the arriving feature supplies the rails.
- Streaming, portal-chain rendering, retention and falling use
  `STOREY_REACH = 6`. Only the linked chunk streams beyond adjacent storeys; do
  not stream whole distant floorplans. Detailed occupancy lighting still
  covers only ±1 storey.

# Storeys (September 2026)

Level 0 is a stack of floors joined by enclosed stairwells, straight flights
under openings in the ceiling, and double-height atria railed on the floor
above (from the wiki's Threshold article and its photograph; CREDITS.md).
`LevelRules::storeyH` is the floor-to-floor pitch: 4.32 m on Level 0 (24
risers of 180 mm) and 0 on every other level, which is how those levels stay
byte-identical.

- **Every floor is a whole maze, and the one you are on is at y = 0.**
  Collision, BFS, line of sight and the shader's shadow march all work on one
  storey in local coordinates. The other storeys live in `World::layers` and
  are drawn offset by a pitch.
- **Crossing the middle of a flight changes storey.** At `py > H/2 + 0.1` (or
  below `-H/2 - 0.1` going down), `Sim::changeStorey` swaps the chunk maps and
  shifts everything with a position by one pitch: player, eye, fall height,
  flares, the tape deck, bullets, impacts, coins, confetti, dogs, the hunter.
  **A new object with a position must go in that list**, or it jumps a storey
  when you cross one.
- **`World::qs` is the storey the accessors read**, not `storey`. `data()`,
  `generate()` and the mesher all go through it, and `StoreyScope` changes it
  for a scope (meshing the floor below, looking down a hole, building the grid
  of the floor above).
- **`sseed()` is the per-storey seed.** Storey 0 keeps the bare seed, so a
  fresh run opens on the same maze as before, and every capture away from a
  feature is pixel-identical. Use `sseed()`, not `seed`, wherever the
  generator or mesher hashes.
- **Features are pure functions of (chunk, pair of storeys).** `pairFeature`
  hashes the seed, level, visit and coordinates and never reads a floorplan,
  so both storeys stamp their half of the same stairwell independently, the
  way neighbouring chunks meet at a seam. The pair's parity picks the chunk
  half (x 2..6 or 9..13), so the feature rising from a storey and the one
  arriving at it never overlap.
- **The one-cell gap between the halves is required.** With 2..7 and 8..13,
  two footprints could touch: the storey carrying both stamped a stairwell
  wall over the neighbour's rail, the storeys either side kept the rail, and
  the regression's boundary probe saw a body pushed 3 cm differently either
  side of the switch.
- **Protected edges** (`ChunkData::prot`, bit 0 N, bit 1 W) are the feature's
  walls, rails and doors. Connectivity punching, `tidyLine`'s door thinning,
  the locked door and the noclip exits all skip them, because each could turn
  a stairwell wall into a doorway on one floor only. A new pass that edits
  edges after the stamp must skip them too.
- **Cell flags** (`VertFlag`):
  - `VF_STAIR`: the floor is a flight (`stairY`).
  - `VF_OPENUP`: no ceiling.
  - `VF_HOLE`: no floor.
  - `VF_WALKHOLE`: a flight comes up through the hole.
  - `VF_KEEP`: feature or margin; place nothing.
  - `VF_NOWALK`: not in the 2D floor graph.

  The regression asserts the pair agrees: OPENUP below if and only if HOLE
  above, and a flight below implies WALKHOLE above. The stairwell's foot is
  plain floor (OPENUP only) on purpose, because `findOpenSpot` refuses stair
  cells and an actor had nowhere to stand at the bottom of the shaft.
- **Rails are `WALL_RAIL`**: `blocksEdge` yes, `blocksLight` no, and an AABB
  with `seeThrough` that `lineOfSight` ignores. Walls, jambs, pillars and
  rails report `FULL_H` as their top, because an actor on a flight is well
  above a 3 m wall top, so `wallH` no longer means "too tall to step over".
  Rail boxes stop at the edge ends; an overhang poked into the next cell and
  made the storeys disagree at the corner of an opening.
- **`groundAt` looks down holes.** On a `VF_HOLE` cell it recurses into
  storey `qs - 1` (up to `STOREY_REACH`), so a fall into an atrium lands on
  the floor below. Landing calls `Sim::landFrom`: about 0.4 of the health
  meter for one storey, most of it for two.
- **Occupancy is RGBA and twice as tall as it is wide.**
  - Rows 0..n-1 are cells: byte 0 your storey, 1 the one below, 2 the one
    above.
  - The bits are `OccBit` (core/world.h), which `worldFragmentShader()` writes
    into the GLSL and `lightAtCPU` reads: bit 3 `OCC_NO_FITTING` (no fitting
    at this cell's min corner, because it would hang in an opening), bit 4
    `OCC_OPEN_ABOVE` (no ceiling), bit 5 `OCC_HOLE_CORNER` (a hole touches the
    corner), bit 6 `OCC_NEAR_OPENING` (an opening is within reach of the nine
    fittings the shader sums here).
  - Rows n..2n-1 are fitting masks: per light block, a 9-bit "which of the
    nine exist" for each storey (a byte each, the ninth bits in alpha).
  - The shader picks its byte by `gRel` (`floor((y + 0.3) / H)`). It lights
    OPENUP cells with a second pass over the storey above's fittings that
    exist up there and are missing here, since a hole above is an opening
    below. It hashes each storey's tubes with its own offset (`storeyOffset`,
    using core's `STOREY_HASH_STEP` and `STOREY_HASH_SPAN`, which
    `storeyHashOffset` also uses). `lightAtCPU` mirrors this through
    `StoreyLightCPU`, looking bit 3 up per fitting, which is fine on the CPU.
- **Why the occupancy data is shaped like this.** Looking bit 3 up per
  fitting in the shader cost 13% of the frame everywhere, so bit 6 gates it
  and only cells near an opening pay. In a stair shaft every cell is near one,
  and nine fetches per storey per fragment made the view up a stairwell cost
  2.3x the old frame. The masks make it one fetch.
  - Bit 6 must stay conservative: if it is clear where a fitting could be
    missing, that fragment is lit by a tube that is not there. The radius is
    `ceil(0.75 * ls) + 2` cells: 1.5 pitches, one for rounding, one for the
    shadow lookup's 16 cm bias.
  - The masks assume the grid pitch is a whole number of cells, and are keyed
    by the block's first cell.
  - Fittings one storey up are not shadow-traced. The line to them stays
    inside the opening's footprint, where only rails (which pass light) and a
    stairwell's core wall (directly under them) stand.
  - Moving the per-fitting lookup after the cheap early-outs was tried and
    was slower: a missing fitting then did its arithmetic before being
    discarded.
- **Other storeys are drawn only through an opening you can see.** Outside a
  footprint the slab is closed (ceiling below, floor above, fascia round the
  edge). `visibleChunks` (shared/port/visible.h) projects each candidate
  chunk's box from the eye onto the plane of the upper floor and keeps it only
  if it overlaps a footprint. render.cpp draws that list, and Unreal hides
  every other chunk actor in a run. The frustum alone let in 22 chunks of other
  storeys behind 9 of this one. Stacked flights chain through each opening in
  turn, up to `STOREY_REACH` storeys. A new kind of opening outside a feature
  footprint breaks this. The move out of render.cpp was checked against main's
  code over 200,000 random views (2.1 M chunks, 650k on other storeys): same
  chunks, same order, offsets and distances to the bit.
- **Wallpaper on tall walls.** The Level 0 tile has its baseboard baked into
  the bottom, so a wall taller than 3 m (a shaft, the fascia round an opening,
  the void side of a rail) would repeat it at 3 m. `WallBuilder::tallPaper`
  (storeyed levels only) continues from the tile's clean middle instead, and
  faces over a void (`voidFace`) never show the baseboard. Every other level
  keeps its repeat, byte for byte.
- **Streaming.** Adjacent storeys are generated and meshed only around chunks
  that link to them (and on through tall courts, one chunk per storey).
  `unloadFar` drops layers more than `STOREY_REACH` storeys away. The order is
  `streamChunks` (shared/port/stream.h), which `Game::streamChunks` and
  Unreal's `StreamAround` both walk. Unreal used to build full rings on the
  storeys either side, which nobody sees, and no chunk of a court's far
  storeys. The move out of game.cpp was checked against main's code over
  100,000 cases (storeys -3..3, both budgets, part-built caches): 4 M
  `ensure` calls, none differ.
- **The regression's `capture()` runs `streamChunks` 24 times, not 7.** With 7
  the storey above had not been meshed yet and every opening in a capture was
  black.
- **The landing lamp.** Enclosed stairwells have no ceiling grid over them, so
  the nearest one within 22 m gets the shader's spare point light (`uLamp`,
  colour `uLampCol`). The Manila Room's chandelier wins when it is nearer.
  `World::landingLights` says which landings are near and where each lamp and
  tube is; `frameLight` and `sceneDraws` both read it. It only looks at chunks
  already generated, and `sceneDraws` generates chunks around the view, so two
  calls in a row can see different landings. A harness comparing two versions
  must warm the world up with one call first, or it reports 4% of states
  differing between a build and itself.

Tools: `BACKROOMS_STOREY=n` starts on storey n. With F3 up, PageUp/PageDown
move a storey where you stand. `./mapdump --visit 1 --list-stairs` prints
every feature near the origin as a `BACKROOMS_POS` (use `--visit 1`: captures
are visit 1, and features move with the visit). `--storey n` dumps another
floor's plan (`S` stair, `v` walkable hole, `X` void, `O` open above, `:`/`..`
rails). The regression's storey block checks flag agreement, 11k boundary
points of `groundAt` and collision agreeing either side of the switch, a climb
and descent by the real mover with Clark following, the atrium rail, a fall,
per-storey pickups, and captures `storey-stairwell/stair/atrium.png`.

# Level 1 (September 2026)

Level 1 follows the wiki's "Habitable Zone" article (CREDITS.md).

- **Puddles.** The floor used to be gloss 0.22 everywhere, which gave every
  square metre of slab a specular sheen and looked flooded. Gloss is 0.07 now
  (below the shader's 0.10 specular cut). Puddles come from the same
  world-space field as Level 0's wet carpet, gated by `wetFrom` (`uWetFrom`):
  0.60 wets a quarter of Level 0, 0.78 about 1-3% of Level 1. The regression
  asserts the share, and that Level 1's gloss stays under the cut.
- **Light.** Twin-tube battens (not trays) on a 12 m grid, three in ten dead,
  a fifth stuttering, faint green-white; the tubes sit on `uLY`. An 8 m grid
  cost 58% more frame, because nearly all nine summed fittings then fall
  inside shadow-trace range. Brighter or more working tubes cost frame the same
  way.
- **Walls map one texture tile floor to ceiling.** `WallBuilder::tileV`
  (port/chunk_mesh.cpp) is the vertical metres per wall-texture tile: 3
  everywhere, `wallH` on Level 1. The concrete has a damp band, tide line and
  pour joints at real heights, and a 3 m repeat drew a second tide line under
  the 4.2 m slab. Anything baked at a height in `makeConcreteWallTex` assumes
  4.2 m.
- **Ground mist** is billboards in `renderScene`, not shader fog: no depth
  writes (or each bank cuts a hole in the next), lit by `propLum`, faded out
  within 1.2 m of the eye.
- **Supply crates are not in any chunk mesh.** `Sim::crateAt` hashes the cell
  with `crateEpoch`, which advances when a blackout ends, so crates move while
  nobody can see. They are drawn with `MAT_PROPS` and pushed out of the player
  in `updateCrates` (world collision does not know about them). They never
  stand in a cell with a door, exit or lock on any edge. Clark walks through
  them.
- **Exits** are steel doors with a glyph painted over them (`addSymbolDoor`):
  strokes between 3x3 lattice points chosen by the edge hash; cursed ones red.
- Rebar spalls (`addSpall`), dock safety edging and lift doors are fixtures or
  props geometry. The pipework is the Red Halls' code, also used on Level 1.
- **A capture with `BACKROOMS_LEVEL=1` shows Level 1's visit 0** (init
  applies Level 0, then Level 1 for the first time). Level 0 captures are
  visit 1. A tool that assumes the other visit points the camera at an empty
  corridor.

# Level 0 (September 2026)

Level 0 follows the wiki's "Threshold" article, the Manila Room and Red Rooms
entries, and the 2002 photograph (CREDITS.md).

- **Empty rooms.** Level 0 furniture is cartons and fallen tiles only.
  Windows are still drawn from the rng and discarded, so the random stream
  stays aligned. The regression asserts zero furnished cells and zero windows.
  Cartons are the level's only cover from Clark, so check `hide spot` in
  mapdump before thinning them.
- **Light grid.** Level 0's `ls` is 4 m (was 8). The panel mesher reads
  `LEVEL_RULES[].ls`; it used to hardcode `level == 1 ? 12 : 8` and would
  have drawn fittings where no light came from. `vary` and `faulty`
  (LevelRules) make tubes uneven and stutter; `lightState()`, `lightAtCPU`
  and `chunkLayout` all apply them, so change all three. Level 0's trays are
  flush lay-in troffers, and the light plane stays at `wallH - LIGHT_DROP`
  (0.12 m).
- **Wet carpet** is world-space in the shader (`uWet`), not in the carpet
  tile, because a puddle in a 2 m texture repeats every 2 m. `carpetWetCPU`
  mirrors it on the CPU (the wet footstep sounds it fed were removed on
  request).
- **The wallpaper is 1024 px for the 3 m wall UV span**, with the CC0 chevron
  repeating exactly 4 times (256 px). Any other period puts half a chevron at
  every seam.
- **Noclip exits.** On Level 0 a `WALL_EXIT` is drawn as a full wall with
  vertex alpha 250 (247 when cursed) and has no collision. The shader tears
  those fragments with `textureGrad`; plain `texture()` on the jittered UV
  picks the smallest mip at every tear edge and draws lines. The cursed flag
  is in alpha, not colour, because the Red Rooms tint is multiplied into every
  vertex near it.
- **The Red Rooms tint.** `MB::tint` multiplies a colour field into every
  vertex a builder emits (per vertex, so it grades smoothly rather than per
  cell). Only the floor and wall builders get it; the greedy-meshed ceiling
  would interpolate one tint across a whole chunk.
- **The Manila Room.**
  - One chunk in `MANILA_RATE`, never within one chunk of spawn.
  - `generate()` stamps it before the connectivity flood and again at the
    end, so door thinning, the locked door and the exit pass cannot move its
    four doors.
  - The table is anchored on room cell (7,7) but built on that cell's far
    corner, so `Sim::pickupSpot` offsets the can; the render and the pickup
    test both go through it.
  - The room's tubes are masked in the shader (`uRoomMask`) and the CPU
    mirror (`setLightExtrasCPU`), and its chandelier is `uLamp`.
  - `applyLevel` must clear `inManila` and `manilaNear`. The first version did
    not, and the regression's grip-meter death stopped firing because a room
    from an earlier test was still raising the player's sanity on a fresh
    descent.
- **Migraine** (`Sim::migraine`, post-process `uMigraine`) builds up on Level 0
  under working tubes and survives a noclip on purpose, because the lore says
  it persists after you leave. `beginDescent` resets it.
- **UTF-8 in level names.** The intro card letter-spaces the name byte by
  byte, so "LEVEL 0 · THRESHOLD" came out with the "·" split into two `?`. It
  now skips continuation bytes. `tools/mapdump --list-exits` prints exit and
  noclip positions as `BACKROOMS_POS` coordinates.

# Texture filtering and Pages caching (September 2026)

- raylib 5.5's `SetTextureFilter(ANISOTROPIC_*)` only sets the anisotropy
  level. Set TRILINEAR first, or min/mag stay GL_NEAREST from `LoadTexture`
  and the generated mips are never sampled.
- Panel centres are always cell corners. `lightVis` starts 1 cm along the
  ray; otherwise the DDA crosses corner edges at t = 0, a wall touching the
  corner falsely shadows the light, and you get rings round every panel and
  dark ceiling patches.
- Near panels, `panelVis` clamps both penumbra taps into that same start cell.
  A tap pushed across a wall through the corner halved the light inside 6 m
  (hard dark ovals). The taps also fade into the single far tap at 4-6 m.
- GitHub Pages serves everything with max-age=600 and no header control.
  web-build.sh stamps a build id: index.js and index.wasm load with `?v=ID`,
  and the shell fetches version.txt with no-store and reloads once under
  `?b=ID` when it is stale.

# Web reflection and pillar shadow fixes (September 2026)

- Desktop Chrome/ANGLE drew black shards on the revolver when `lightState`
  returned early for dead lamps inside the reflection branch. Keep its single
  return with the dead-lamp mask. Depth-buffer changes did not fix this.
- Pillar shadows must intersect the actual 0.42-1.58 m footprint, including
  the first and last ray cells; skipping those cells made a bright square
  halo. Keep `pillarBlocks` single-exit too: the native Metal compiler stalled
  with an extra early return in this nested lighting path.
- Pillar contact shadows use the same fading AO skirt as furniture. A flat
  dark quad in the wall mesh made a visible rectangular border.
- `tools/web-render-check.mjs` needs Playwright (resolvable by Node or
  NODE_PATH), a served web build in `GAME_URL`, and `BROWSER_CHANNEL=chrome`
  for hardware ANGLE. It tests 20 GPU visibility cases and a fixed-seed
  barrel crop. The old build fails the pixel check (16); the fixed build
  passes (70).

# Flashlight

- Flashlight air scattering has its own 0.20 weight and uses the same cone
  exponent as the surface light (26). Keep surface lighting independent:
  raising the air term washes out the view. The shared fog factor also
  controls flare halos.

# Revolver

- Hold RMB to aim. F or L toggles the flashlight. Aiming never toggles or
  queues a reload.
- A reload needs the sights fully lowered; a reload already running finishes
  before aiming.
- Aiming changes the viewmodel transform, FOV and look sensitivity, not the
  skinning. The imported rear frame rib is solid, so the eye must clear it
  slightly to see the front blade. Keep the blade tip on the camera ray when
  changing the placement.
- `SHOT_INTERVAL` (shared/sim/weapon_timing.h) sets both the firing rate and
  the Shoot clip's playback, and `RELOAD_TIME` the reload and its clip. One
  definition each; the revolver's pose reads them.
- Rounds travel at 220 m/s and ray-test the whole frame's segment against
  solid world meshes and actor bounds. Keep one nearest-hit selection across
  actors and walls; separate damage loops let one round hit several targets.
  Mesh bounds must accept a ray that starts inside the box; rejecting it by
  the distance to the box exit skips nearby triangles inside.
- The source Reload clip already opens and closes the cylinder. Appending the
  separate open and close clips caused a visible snap. Shoot contains six
  shots; play its first cycle and keep the accumulated cylinder index.
- Live cartridges stay attached while shooting; spent cases are visible only
  while unloading. The source reload uses quarter-size live rounds as a
  visibility switch: export those as hidden and keep full-size spent cases
  during ejection. Omitting the cases leaves tiny live bullets floating round
  the cylinder.
- The reload hinge and cartridge paths are reversed together at import, in the
  handle's local frame, so it opens to the left. Do not mirror the whole gun
  or its UVs.

# Model pipeline (September 2026)

- Imported models are standard GLB files under assets/models. `ModelAsset`
  uses `LoadModel`, `LoadModelAnimations` and `UpdateModelAnimation`; revolver
  code only adds gameplay pose adjustments. The old mesh.bin/poses.bin format
  is gone.
- The packaging callback embeds GLB bytes and falls back to normal file
  reads, so new models need no custom parser. Reuse one loaded asset for
  static instances.
- raylib 5.5's CPU skinning wrongly translates normals. A version-gated fix
  removes the weighted translation after its update; keep the weighted
  fixture test.
- raylib samples animation every 17 ms. Prepared clips hold their final pose
  for 34 ms so the last sample reaches it.
- The revolver is the user-authorized CC0 model in assets/revolver; see its
  README for provenance and the optional NumPy/Pillow reimport.
  web/src/revolver.cpp skins two material batches only when the pose changes;
  idle frames reuse the buffers.
- Metallic albedo needs diffuse attenuation in this renderer. The silver
  albedo imported unchanged looked like white paint. (The shader also reflects
  the room off metal and keeps a fifth of its diffuse; see the surface pass.
  Attenuation alone left it looking like grey plastic.)
- make and sandbox-build.sh run tools/embed-materials.py. Do not commit the
  generated headers; commit the converted assets and the import script.
- See docs/model-pipeline.md for supported exports, limits and performance.

# Rendering notes (September 2026)

- `texture1` / `MATERIAL_MAP_SPECULAR` carries packed tangent slopes in RG and
  a gloss mask in B. The neutral map is (128,128,255). `texture2` is the
  occupancy grid.
- Object detail maps use alpha 128 to mark an absolute gloss value in B
  (128-188, which also carries metalness). World maps use alpha 255 and the
  neutral map 250, both level-relative gloss. This keeps cloth matte and metal
  reflective on carpeted levels. Vertex alpha 254 still turns off normal
  relief, independently of material gloss.
- The props atlas is 1024x512: cardboard and metal in the left half, veneer
  and fabric in the right. Flat metal is at UV (0.375, 0.75).
- `WORLD_VS` must transform positions with `matModel` and normals with
  `matNormal`. Before, moved cans and decks were shaded as if at their mesh
  origin.
- Fluorescent emissive geometry sits 0.12 m below the ceiling of its cell
  (`ceilY`, not a fixed `wallH`), matching the light height.
- The revolver and flare are 3D and share the viewmodel shadow and ambient
  handling. Keep depth testing and wall clearance.
- Can caps must map the triangle fan's centre to the atlas disc's centre.
  Mapping that vertex to angle zero stretched the lid detail into wedges.
- Shadow lookup bias uses the geometric surface normal, not the detail-map
  normal: tile relief pushed floor samples across walls and made bright strips
  in Poolrooms corners. Occluded panels contribute no sharp reflection.
- Regression captures must stream all 25 visible chunks after moving the
  camera. Three render-only frames left black rooms in some screenshots.
- Native macOS startup checks for an active display before `InitWindow`:
  raylib 5.5 crashed inside `rlglInit` with a null function when the laptop
  had no display. Keep the lid open or a monitor connected for remote graphics
  tests.
- Native macOS captures need WindowServer access; a sandboxed executable can
  hang with an XPC connection error. Linux uses Xvfb.
- Use `BACKROOMS_CLEAN=1` to remove the intro fade and HUD when judging
  exposure; on a fast native GPU, frame 80 otherwise lands in the opening
  fade. `BACKROOMS_TIME` fixes rendering time only. See README for benchmark
  settings.
- Run `make regression`, the five-level and menu sweep, and the pool and
  flashlight shots. The regression harness needs a native display or a
  running Xvfb.

# What this is

A first-person horror game set in the Backrooms, in C++17 on raylib and OpenGL
3.3. World generation and sound are procedural. The user authorized external
assets for realism: assets/materials has CC0 tiles and assets/revolver an
animated CC0 gun. Keep provenance and licence information next to each asset,
and embed assets at build time so the executable does not depend on its
working directory.

## Module map

Paths under `core/`, `sim/` and `port/` are in `shared/`; the rest are the Web
build's, in `web/src/`.

| file | contents |
|---|---|
| `main.cpp` | `init()`, the `while (!WindowShouldClose())` loop, `shutdown()` |
| `sim/` | game state and rules (`Sim`), stepped once a tick by `Sim::step` |
| `sim/sim.cpp` | tick order, levels, descents, death and escape |
| `sim/player.cpp`, `weapons.cpp`, `items.cpp`, `hunters.cpp`, `place.cpp` | the player; held items; pickups, the deck and the use key; the hunter and the pack; blackouts, sanity, exits, the Manila Room |
| `sim/input_frame.h`, `audio_events.h` | what the platform tells the sim, and the sounds the sim requests |
| `game.{h,cpp}` | the platform: window, assets, input into an `InputFrame`, chunk streaming, the occupancy grid; `Game::tick` |
| `game_audio.{h,cpp}` | every `Sound`/`Music` handle and the synth; plays the sim's `AudioEvent`s in order |
| `render.cpp` | 3D scene, viewmodels, HUD, overlays, drawn from `sim` |
| `core/world.{h,cpp}` | the infinite maze: chunk generation, storeys, collision, line of sight, pathfinding, occupancy grid. `WallKind` / `PropKind` name the codes stored per cell. No raylib |
| `core/level_rules.h` | per-level rules: wall height, light pitch, storey pitch, name, which tubes work, exits, floor material, distant sound; `MULTIPLEX` |
| `core/layout.{h,cpp}` | `ChunkLayout`: a chunk's props, fixtures, light fittings and openings, decided from the seed |
| `core/floorplan.{h,cpp}` | `ChunkFloorplan`: a chunk's floors, walls, risers, ceilings, soffits, flights and arches |
| `core/hash.{h,cpp}`, `core/vec.h`, `core/portable_math.{h,cpp}` | hashes, RNG, value noise; `Vec2`/`Vec3`, `TAU`, `clampf`; sin, cos, exp, atan2, hypot that round the same on every platform |
| `sim/start.{h,cpp}`, `sim/trace.{h,cpp}`, `sim/records.{h,cpp}` | how a run starts (`SimStart`); recording and replaying the sim's inputs; the records file both builds use |
| `port/view.{h,cpp}`, `port/greybox.{h,cpp}` | what the camera shows (both builds use it); a chunk as plain boxes and quads |
| `port/hud.{h,cpp}` | the HUD (`hudDraws`): cards, meters, inventory and hints as draws in window pixels |
| `port/held.{h,cpp}`, `port/scene.{h,cpp}`, `port/sprites.{h,cpp}`, `port/sheets.h` | items in hand, loose objects in the world, and the actors' billboard poses and sheet layout |
| `port/sounds.{h,cpp}`, `port/ambience.{h,cpp}`, `port/mixer.{h,cpp}` | the synthesized clips and which are recordings; the ambience synth; a software mixer for a port |
| `port/chunk_mesh.{h,cpp}` | the chunk mesher (`bakeChunkGeometry`), `ChunkMesh` slots |
| `port/mesh_builder.{h,cpp}`, `port/atlas.{h,cpp}`, `port/palette.h` | `MB`, `addPropBox`, `addSolidBox`, `PLAIN_UV`; the fixtures atlas layout and tile sizes; `cl8`, `PARTY_RGBA` |
| `port/camcorder.{h,cpp}` | the camcorder's tape: interference, the frames and their playback, clips, the REC mark and the playback's OSD |
| `sim/entity.h` | `Entity` (Clark on Level 0, the Smiler on 1 and 3, the Partygoer on 4) and `Dog` state |
| `world_mesh.{h,cpp}`, `mesh_upload.h` | `bakeChunk` (the mesher's output as raylib meshes), `ChunkMeshCache` |
| `object_meshes.{h,cpp}` | can, tape deck, reels, flare, supply crate |
| `vec_rl.h` | `toRl` / `fromRl` between core's math types and raylib's |
| `levels.{h,cpp}` | per-level look table (`LevelCfg` extends `LevelRules`); CPU copy of the shader's lighting |
| `shaders.cpp` | the world, post-process and tape GLSL, as string literals |
| `surfaces.cpp` | the world surfaces: colour, height (m) and gloss per surface, tiling and at real scale |
| `textures.cpp` | sprites, decals, fixtures, the props atlas (with the CC0 tiles), can, deck |
| `revolver.{h,cpp}` | the embedded revolver, pose interpolation, two material batches |
| `sfx.cpp` | loads the clips (`port/sounds.h`) and the embedded recordings into raylib |
| `audio.cpp` | feeds the ambience bed (`port/ambience.h`) to a raylib stream; recorded loops are in `GameAudio::feedLoops` |
| `camcorder.{h,cpp}` | the camcorder's reel (one render texture), the tape shader (`TAPE_FS`) and its sound handles |
| `util.{h,cpp}` | platform helpers (`cl8`, `PARTY`); includes core's math and hashes. `SAMPLE_RATE` is in port/sounds.h |

`Sim::step` calls the update functions in a fixed order: flashlight, look,
movement, dev keys, weapons, bullets, flare, tape deck, interaction, drink,
ambience, Manila Room, crates, loop-audio cue, entity, dogs, exits, timers.
`Game::tick` then plays the audio events, streams chunks, rebuilds the
occupancy grid and renders. The order matters: weapons run before the entity,
so a shot lands before the entity reacts to it.

# Building

## Normal machine

`make` needs raylib installed (Homebrew or pkg-config). `make run` builds and
runs.

## Sandbox with no system raylib

The Claude Code web environment has no raylib package, but the raylib Python
wheel ships a complete raylib shared object and cffi-preprocessed headers, and
C++ can link against both.

```bash
tools/sandbox-setup.sh     # fetch the wheel, generate rlshim/ (once)
tools/sandbox-build.sh     # build ./backrooms
```

`rlshim/` and `.rlwheel/` are generated and gitignored. The setup script
copies the wheel's `{raylib,raymath,rlgl}.h.modified` into `rlshim/`, wraps
each in `extern "C"`, restores the `#define`s cffi strips (`PI`, `DEG2RAD`,
`RAD2DEG`, the named `Color` constants, `MATERIAL_MAP_DIFFUSE`,
`RAYLIB_VERSION_*`), and strips raymath's `inline` so its functions resolve
against the shared object. Two details matter:

- `raylib.h.modified`'s last line is a declaration with a trailing `//`
  comment and no newline. Appending `}` directly puts the brace inside the
  comment; the `extern "C"` never closes and the build fails far away in
  `<initializer_list>` with "template with C linkage".
- Link the wheel's `.so` by absolute path with an `-rpath`. Linked relatively,
  the binary only runs from the repo root and fails with "cannot open shared
  object file" when run from elsewhere, as `tools/shot.sh` does.

**Check that a build printed "built".** `sandbox-build.sh` deletes its target
before compiling, so a failed build leaves no binary and `tools/shot.sh`
fails, rather than capturing an old binary. But `build.sh 2>&1 | tail -1`
shows a compiler note, not the error. Two debugging sessions were lost to a
missing `#include <cstdio>` this way.

**`tools/sandbox-build.sh` has no regression target.** Build it by hand from
`web/src/*.cpp` (minus `main.cpp`), `shared/core/*.cpp`, `shared/sim/*.cpp`,
`shared/port/*.cpp` and `tools/regression.cpp`, with `-Ishared -Iweb/src`.
Run it with `BACKROOMS_TEST_ASSET_DIR=tests/fixtures`, or it exits on its
first `CHECK` as if the build were broken.

## Building for the web

`tools/web-build.sh` produces `web/dist/index.{html,js,wasm}`. The GitHub
Pages workflow runs the same script. It compiles raylib for the browser from a
pinned tag into `.raylib-web/`. Platform-specific code is behind
`PLATFORM_WEB`; the native build is unaffected (checked by assembly diff).

- **`main()` cannot loop in a browser.** `while (!WindowShouldClose())` hangs
  the page before the first frame. `web/src/main.cpp` passes `tick()` to
  `emscripten_set_main_loop` instead, so `shutdown()` never runs on the web.
  Nothing that must happen before the player leaves can live there.
- **It must be WebGL 2.** The world shader uses `dFdx` and `texelFetch`, which
  GLSL ES 2.0 lacks. A failed shader compile does not crash: raylib falls back
  to its default shader and draws a black frame, faster. raylib's Makefile
  defaults to ES2; the build script passes
  `GRAPHICS=GRAPHICS_API_OPENGL_ES3` and `-sMIN_WEBGL_VERSION=2`.
- **Records live in IndexedDB.** A tab's filesystem disappears with the page.
  `web/shell.html` mounts IDBFS over `$HOME` in `preRun` and holds `main()`
  with `addRunDependency` until it loads, because `Game::init()` reads the
  records file at startup. `Game::saveRecords` flushes with `FS.syncfs`; it is
  the only place records are written on the web.
- **Link with `em++`, not `emcc`.** `emcc` compiles but does not link libc++,
  so every C++ symbol comes back undefined (`operator new`,
  `std::__2::__next_prime`).
- **Export `HEAPF32` (`-sEXPORTED_RUNTIME_METHODS=...,HEAPF32`).** miniaudio's
  callback reads `Module.HEAPF32.buffer`; without the export every callback
  throws and the game runs completely silent.
- **A browser capture can land in a blackout.** Headless native captures turn
  blackouts off; the web build does not. Pass `?noblackout=1`. A black world
  with a working HUD looks like a shader failure but is usually a blackout.
- **The env settings do not survive the title screen on the web.**
  `BACKROOMS_LEVEL` is applied in `init()`, then `startRun` calls
  `beginDescent`, which goes to Level 0, and `Sim::menuDrift` clears
  `flashOn`. Natively `BACKROOMS_SHOT` skips the menu
  (`sim.inMenu = shotPath == nullptr`); the web cannot, so `?level=3` starts
  on Level 0. Three sweeps that claimed to test every level in a browser were
  all photographing Level 0. To reach another level in a browser, descend or
  use the F3 keys. Synthetic `F3`/`N` key events from a test driver do not
  reach raylib.

## Touch controls

A phone has no keyboard, mouse or pointer lock. `web/src/input.h` is the one
place the game asks for player input. Natively and in a desktop browser every
wrapper is the raylib call it is named after (identical generated code, 0
pixels different). On a touch device `web/src/input_web.cpp` answers from the
on-screen controls in `web/shell.html`.

- **Read player input through those wrappers**, not raylib directly, or a new
  control works on desktop and does nothing on a phone. Window keys (F11) are
  not player input and stay direct.
- **`IsCursorHidden()` is the game's "playing" test**, at nine call sites
  (look, fire, reload, throw, aim). A phone has no pointer lock, so it is
  always false there, and the game would ignore every input.
  `inCursorHidden()` returns true whenever the touch controls are up.
- **The button bits are defined in two places that nothing checks**: the enum
  in `web/src/input_web.cpp` and `BTN` in `web/shell.html`. If they disagree, a
  button does another button's job.
- **`TouchFrame`'s field order in `input_web.cpp` is the
  `HEAPU32`/`HEAPF32` index in the `EM_ASM` block below it.** Append new
  fields. Inserting one in the middle makes every later field read its
  neighbour's value.
- **The thumbstick floats** to wherever the thumb lands in the lower-left
  zone; a fixed stick turned most slightly-off grabs into look drags. It
  reaches full deflection at 72% of its radius, because a thumb pivots rather
  than reaches.
- **Sprint is a push past the drawn ring**: `SPRINT_ON` at 1.08 radii,
  `SPRINT_OFF` at 0.98 (a dead band, so a thumb on the line does not flicker
  in and out of sprint). It used to fire at 0.66 of the radius, inside the
  ring, where players crossed it by accident. The ring gets a `.run` class
  while sprinting, because the knob stops moving at 0.62 of the radius and
  nothing else shows where the line is.
- **Sprint limiting is off**, by one number: `SPRINT_DRAIN` in
  `Sim::updateSprint` is 0, so stamina never falls. The hysteresis, the 6 s
  recovery, the HUD meter and the regression check are all still there; the
  check sets `stamina = 0` by hand, so it still tests the mechanism. To turn
  it back on, set `SPRINT_DRAIN` to 1/seconds (it was 1/16, and before that
  1/10).
- **Lay out each button from the corner it sits in**, not from the far edge.
  `DUCK` at `left: 42vmin` and `USE` at `right: 40vmin` overlap on a 412 px
  phone.
- **Position controls in px from the stage's own size, not `vmin`.** The
  controls are inside `#stage` (the viewport minus the footer), but `vmin`
  resolves against the viewport. In iOS Safari landscape the stage is about
  two thirds of the viewport height, so controls overlapped: JUMP over DRINK,
  MARK over LOAD, USE over AIM. `layoutControls()` computes one unit from the
  stage's box and writes px.
  - The vertical budget is per side: the left column (stick, DUCK) and the
    right (FIRE/AIM/LOAD/JUMP under the top cluster) never share a row.
    Horizontally they do, so that budget is a sum.
  - `GAP` is 4% of the unit. Without clearance the clusters met to the pixel
    and rounded into an overlap.
  - Do not clamp to `min(stageW, stageH)`; the budgets already account for
    shape.
- **Below about a 330 px stage, 40 px buttons and no overlap are
  impossible**: 13 controls, the smallest at 0.12 of the unit, need a unit of
  333 to reach 40 px. `mobile-check` requires 40 px only on real device sizes
  and only no overlap on the squeezed stress case. Do not lower the 40 px
  floor to make it pass.
- **`setPointerCapture` throws if the pointer is already gone** (a fast tap,
  or a synthetic test event). The throw skipped the rest of the handler, so a
  press was registered and never released, leaving the player walking or
  firing forever. Both calls are wrapped.
- **raylib sizes the web canvas from `window.innerWidth`/`innerHeight`**, not
  the element's box (`EmscriptenResizeCallback` in `rcore_web.c`), and clamps
  to the minimum window size first. The canvas is stretched over `#stage`, so
  the world was drawn squashed, and `SetWindowMinSize(640, 400)` rendered a
  412 px phone at 640 wide. The web build asks for a 240 px minimum, and
  `fitCanvas()` gives the element its backing store's aspect ratio inside the
  stage, letterboxing the rest. (The clamp is `CORE.Window.screenMin`.)
- **A flex container centring content taller than itself clips both ends and
  does not scroll.** On a landscape phone the splash's title went off the top
  and ENTER off the bottom. The fix is `overflow-y: auto` on the scroller and
  `margin: auto` on an inner block.
- **HUD text scales with window height, but a phone is narrow.** At 412 px
  wide, the title card's name was about 700 px and ran off both edges.
  `hudTextC` shrinks a line to 92% of the screen width (`fitSize`).
  `hudText`/`hudTextR` are corner blocks and do not. The title is drawn
  through `DrawText` for its own shadow and calls `fitSize` itself; change one,
  change both.
- **AIM on touch is a toggle, not a hold**, because holding it occupies the
  thumb needed to fire. The latch is in `input_web.cpp` because the game
  decides whether an aim is allowed (`Sim::updateAim`): when the sim refuses,
  it sets `dropAimLatch`, `Game::finishStep` releases it through
  `webReleaseAim()`, and the button does not stay lit. `canReload()` refuses while aiming, so a latched aim would
  block every LOAD tap: the poll drops the latch on a RELOAD press. A reload
  already running keeps the latch, matching holding RMB through a reload
  natively.
- **Starting a run must be a press, not a held state.** The title screen
  starts on `webStartGesture()`: a tap on open screen, or the stick crossing
  into a real push, recorded once per grab (`r.pushed` in the shell). Reading
  the stick's deflection instead dismissed the death card the moment its hold
  expired if the thumb was still on the stick, so the player never saw it. A
  tap is counted on `pointerup`, not `pointerdown`.
- **Double tap on iOS zooms the page.** `touch-action: manipulation` on
  `html, body` keeps pan and pinch and drops double-tap zoom; the canvas has
  `touch-action: none`. It must be on the document, not just `#touch`.
  `user-scalable=no` does nothing on iOS 10 and later.
- **`navigator.maxTouchPoints` describes capability, not the main input.** A
  touchscreen laptop reports touch points while the player uses a mouse.
  Default to touch from `(pointer: coarse)`, with `?touch=1` and `?touch=0`
  as overrides. The mobile check simulates a fine-pointer desktop with ten
  touch points.
- **Touch look sensitivity is `LOOK_GAIN * px * 0.0030` rad.** A thumb cannot
  lift and reposition mid-drag, so what matters is how far one drag turns
  you. At 1.35 a full swipe across a 412 px phone was about 95 degrees.
  `LOOK_GAIN` is 2.4, and `mobile-check` asserts the product (a 200 px drag
  turns 60-110 degrees), not either factor.
- **Testing touch with synthetic events:**
  - The handler checks `e.target.closest('.tbtn')` to tell a button from open
    screen. Dispatch events at `document.elementFromPoint(x, y)` and let them
    bubble; dispatching everything at `#touch` makes every gesture look like
    open screen. Test the cases that must not start a run as well as those
    that must.
  - Grab the stick by its own bounding rect. A probe at a fixed fraction of
    the viewport (0.14w, 0.72h) landed on DUCK on a tall phone.
  - Read the touch state before `pointerup`: lifting zeroes `moveX`/`moveY`.
  - `flex: 0 0 auto` shrinks a flex item to its content height, so measure
    the stage before setting it. The squeeze test read 132 px instead of 891.
- **`node tools/mobile-check.mjs` is the only check for the shell.** It loads
  `web/shell.html` in Chromium at eight sizes (including an iPhone in
  landscape with both Safari bars) and checks that:
  - no two controls overlap, at full stage size and with the stage squeezed
    to 64%;
  - the splash fits or scrolls with ENTER on screen;
  - the canvas keeps its aspect ratio at three backing sizes;
  - of the seven title-screen gestures, a middle tap, a sloppy tap, a stick
    push and a stick tap start a run, and a button press, a look drag and a
    cancelled press do not.

  Run it for any shell change. ESM `import` ignores `NODE_PATH`, and the
  sandbox's playwright is installed globally as CommonJS, so it arrives under
  `.default`; the script handles both. Without that, the error is a bare
  `ERR_MODULE_NOT_FOUND`.

# Running headless and taking screenshots

The sandbox has no display, so everything runs under Xvfb. `tools/shot.sh`
starts one if needed:

```bash
tools/shot.sh out.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=0 \
    BACKROOMS_POS=15,15,0.8 BACKROOMS_SHOTFRAME=80
```

`BACKROOMS_SHOT` runs the game, captures one frame and exits. With
`BACKROOMS_SEED` and `BACKROOMS_POS` the result is repeatable apart from
light flicker, so the same command twice gives comparable images. Frames go to
`shots/` (gitignored), or to `SHOTS_DIR`.

- **`TakeScreenshot` writes relative to the working directory and rejects
  path separators**, so it only takes a bare filename. That is why the script
  `cd`s into the output directory.
- **Measure, do not judge by eye:**

  ```bash
  tools/pixdiff.py crop shot.png zoom.png 850 560 500 290 2.0   # zoom a detail
  tools/pixdiff.py diff before.png after.png                    # what changed, and where
  tools/pixdiff.py luma IMG...                                  # mean luma of each frame
  ```

  `diff` reports which eighth of the screen the differences are in, which
  tells a HUD change from a world change.

## Settings (environment variables, read at startup)

| variable | effect |
|---|---|
| `BACKROOMS_SHOT=out.png` | headless: run, capture one frame, exit |
| `BACKROOMS_SHOTFRAME=n` | which frame to capture (default 600; about 150 for quick sweeps) |
| `BACKROOMS_SEED=n` | fix the world seed |
| `BACKROOMS_NOBLACKOUT=1` | never schedule a blackout (default on under `BACKROOMS_SHOT`; pass `0` to capture one) |
| `BACKROOMS_LEVEL=n` | start on level n (0-5) |
| `BACKROOMS_POS="x,z,yaw[,pitch]"` | start position and heading in metres and radians; pitch (up +) photographs a floor or ceiling |
| `BACKROOMS_EXITS=1` | exit doors everywhere |
| `BACKROOMS_MENU=1` | stay on the title screen |
| `BACKROOMS_FLASH=1` | start with the flashlight on |
| `BACKROOMS_MANILA=1` | put a Manila Room in the chunk east of spawn (centre x 48, z 16) |
| `BACKROOMS_STOREY=n` | start on storey n (Level 0) |
| `BACKROOMS_RECORD=path` | record the sim's inputs, for `./replay` (shared/sim/trace.h) |
| `BACKROOMS_CHASE=1` | the hunter spawned on frame 300 chases; with `BACKROOMS_POS=15,15,0.8` and seed 1337 it catches you on frame 410, so the tape plays from there |
| `BACKROOMS_CLEAN=1` | no intro fade or HUD |
| `BACKROOMS_TIME=t` | fix rendering time (not the sim's) |

`BACKROOMS_NOENT` does not exist; old scratch scripts used it and it did
nothing. To remove Clark in a test, use F3 then `H`, or set `ent.nextSpawn`
far ahead.

In game, F3 shows the debug HUD. While it is up:

| key | effect |
|---|---|
| `B` | blackout now |
| `E` | spawn the level's hunter stalking about 12 m ahead |
| `C` | force a chase (also squeezes, so holding it does both) |
| `H` | remove the hunter |
| `G` | refill flares and ammo |
| `N` | next level |
| PageUp / PageDown | up or down a storey |

# Regression process

Before pushing anything that touches rendering, the shader or world
generation, run all six levels and the menu:

```bash
tools/sweep.sh
```

It exits non-zero on a failed shot, on a shader-error line in any log, and on
any frame whose mean luma is outside the per-level band in its header. A
failed shader compile does not crash: the frame goes black and the frame rate
goes up. A clean exit is necessary, not sufficient; look at the images too.

- **A Level 3 frame is meant to be almost black.** The Red Halls sit at a mean
  luma around 12 out of 255 (the band is in `tools/sweep.sh`). It was 16
  before the fog took its brightness from the local light.
- The sweep shoots one corridor spot per level. When you touch lighting, also
  take these two by hand:

  ```bash
  tools/shot.sh pool.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=2 BACKROOMS_POS=95,79,1.2
  tools/shot.sh beam.png BACKROOMS_SEED=1337 BACKROOMS_LEVEL=1 BACKROOMS_FLASH=1
  ```

  The first is an open pool hall: white tile under a full grid of fittings,
  where too much exposure turns everything white. The second is the only way
  to check the flashlight.
- A full sweep takes 10-20 minutes on the software rasteriser. Run it in the
  background. Do not run two at once: they write the same filenames into
  `shots/` and share one slow rasteriser.

## Comparing builds

- **Behaviour-preserving changes** (a rename, a named constant, a comment):
  compile the file both ways and compare the assembly. Identical output means
  behaviour cannot have changed.

  ```bash
  git worktree add /tmp/base HEAD --detach     # link rlshim/ and .rlwheel/ into it
  c++ -std=c++17 -O2 -Irlshim -Ishared -S -o new.s web/src/sfx.cpp
  c++ -std=c++17 -O2 -Irlshim -Ishared -S -o old.s /tmp/base/web/src/sfx.cpp
  diff <(grep -vE '^\s*\.(file|ident)|^\.LF[BE][0-9]+:' old.s) \
       <(grep -vE '^\s*\.(file|ident)|^\.LF[BE][0-9]+:' new.s)
  ```

  Adding an `#include` renumbers internal labels (`.LFB986` to `.LFB995`,
  `.LLSDA...`), which the filter removes; anything left is a real difference.
  Extracting a function does change the assembly, because inlining changes;
  use screenshots for that.
- **Screenshots**: build the previous commit elsewhere and `pixdiff.py diff`
  the two frames. A HUD-only change puts its differences in one corner;
  differences spread through the frame mean the world changed. Several real
  bugs rendered valid-looking frames that were wrong, so a clean exit code
  proves nothing.
- **Screenshots are not byte-identical between runs.** Light flicker and the
  menu camera use `GetTime()`, and the frame rate varies, so the same binary
  captured twice differs in about 0.1% of pixels. Capture the same binary
  twice to find the noise level, then compare the before/after diff with it.
- **Both runs must have the same frame rate.** The flicker is
  `sin(t*31)*sin(t*47.3)` on wall-clock time, so a 3 fps run and a 4 fps run
  are at different phases by frame 80: the same binary at 3 and 4 fps
  differed by 0.044%, concentrated in the upper bands. A changed binary
  against the old one, both at 4 fps, differed by 4 pixels out of 1.22 M.
  `tools/shot.sh` prints `fps=`; check it matches and re-shoot if not.
- **Level 4 has its own noise level, about 0.014%.** Its balloons bob on
  wall-clock time, so they move with any frame-rate change: about 170 pixels
  differ against an unchanged binary. The other levels differ by single-digit
  pixels.
- **Frame cost**: use `tools/bench.sh ./backrooms.old ./backrooms 2` (best of
  3 per binary). Timing in the sandbox varies about 15% between runs, and
  `fps=` is a smoothed integer. For close results use N=5: one level read
  1.07, 1.08 and 1.17 across three runs and settled at 1.04 at N=5. If
  bench.sh misbehaves, run each binary with `BACKROOMS_BENCH=1
  BACKROOMS_TIME=4`, read `mean_ms`, alternate the order and take the best of
  five.

# Measuring the layout

`tools/mapdump.cpp` links `shared/core` and `shared/sim` and calls
`generate()` directly, with no window or GL, in about a second for a 258 m
square:

```bash
tools/sandbox-build.sh mapdump
./mapdump --level 0 --seed 1337 --cells 129 --no-plan
./mapdump --level 0 --cells 33 --plan 0 0 26 12      # ASCII floorplan too
```

It reports enclosure (share of solid edges, and cells by number of solid
sides), sightline percentiles and occlusion at 20 m, reachability (a flood
fill using the pathfinder's `canStep`), and densities in m² per instance for
pillars, props, hide spots, doubloons, soft floor, valves, exits, pools and
elevation. Use it for anything that touches the generator; screenshots
mislead about layout. From screenshots the halls looked hundreds of metres
long, but the median sightline was 7.0 m. The real problem, that 57% of
Level 0 cells had no wall on any side, did not show in any screenshot.

- **Pass `--visit 1` to match a capture.** mapdump defaults to visit 0, and a
  headless capture runs at visit 1 (`applyLevel` does
  `world.visit = visits[lv]++`, twice by the time the frame is taken).
  Aggregate numbers are the same either way, but positions differ. An
  afternoon was lost pointing the camera at a locked door that only existed at
  visit 0.
- **It reports how full the collision buffer gets.** `gatherCellAABBs`
  collects the 3x3 cells around a point into one `MAX_NEARBY_AABBS` buffer and
  silently drops every box past the cap, so an overcrowded cell lets you walk
  through a wall. After the doors and 6 m halls: worst 20-22 of 48, zero cells
  over, on Levels 0, 1 and 3. Check again after anything that adds geometry
  per cell.
- **Enclosure and sightlines are tied.** A sightline's mean free path is about
  one cell divided by the share of solid edges: 13.7% solid gave a 7.0 m
  median, 26.7% gives 3.5 m. Occlusion at 20 m is `(1-p)^20`. Asking for
  25-30% solid edges means asking for a 3.5 m median and about 98% hidden at
  20 m. Long views have to be added back on purpose, as corridors and halls
  (the ring clear and `HALL` in `generate`).
- **A locked door lowers mapdump's reachability on purpose.** About one chunk
  in three has a `WALL_LOCKED` door with its key nearby, placed only where it
  strands nothing or a closet of at most 12 cells. mapdump does not know you
  have the key, so 19 locked doors cost about 0.2 points of "reached from
  centre" in a 129-cell sample. Check that the largest pocket stays within
  `CLOSET_MAX` instead.
- **`hideSpotAt` and `coinAt` are on `Sim`** (shared/sim/items.cpp), and
  mapdump calls them on a `Sim` it holds. It used to keep its own copies, and
  its coin salt lacked the level, visit and storey that the sim's
  `pickupSalt` mixes in, so every coin count away from Level 0, visit 0,
  storey 0 was wrong (fixed October 2026).

Level 0, seed 1337, 16641 cells, before and after the room partition:

| measure | before | after |
|---|---|---|
| solid edges | 13.7% | 26.7% |
| cells with no solid side | 56.6% | 23.1% |
| cells with two or more | 10.3% | 27.0% |
| median sightline | 7.0 m | 3.5 m |
| 90th percentile sightline | 24.5 m | 9.5 m |
| hidden at 20 m | 85.4% | 98.4% |
| reachable from the centre | 91.8% | 94.3% |
| largest cut-off pocket | 41 cells | 3 cells |

After widening the corridors to `HALL_LO + HALL_HI` cells (6 m), at
`--visit 1`:

| measure | 4 m halls | 6 m halls |
|---|---|---|
| solid edges | 26.7% | 22.8% |
| cells with no solid side | 23.1% | 33.5% |
| median sightline | 3.5 m | 4.0 m |
| 90th percentile sightline | 9.5 m | 13.5 m |
| hidden at 20 m | 98.4% | 95.3% |

A third of cells with no solid side is expected: the ring is 87 of a chunk's
256 cells, so 34% of the floor is corridor, and corridors have no walls.

# Known problems

Each of these cost real debugging time.

## Generator

- **Doorways are opened by three passes that cannot see each other.** The
  segment runs, the room partition and the connectivity flood each open edges,
  and they often put two or three doorways in one wall. A 1.3 m opening in a
  2 m cell leaves 0.7 m of wall between each pair, so the wall becomes
  alternating holes and piers. mapdump does not show this; a screenshot did.
  Two fixes, both needed:
  - `World::generate` closes one of each adjacent pair when the floor stays as
    connected as the flood left it. Otherwise (most cases, since a room
    reached only through its own door cannot lose it) it moves the door into
    blank wall on the same line, as near as possible. In a 129-cell sample:
    520 crowded pairs, 200 closed, 104 moved, 216 with no room to move.
  - The mesher drops the jamb between two doorways that must stay adjacent,
    so those become one wide opening under a continuous header.
    `gatherCellAABBs` drops the matching jamb boxes, or you walk into a pier
    that is not drawn.
- **"Does the floor stay connected" is a bridge test.** The first door
  thinning compared the size of one flood before and after closing an edge.
  That is wrong when the chunk's floor is already in two pieces (both
  reachable from outside through different seams): closures inside the piece
  outside the count look free. It stranded 24 cells and took the largest
  cut-off pocket from 2 to 8. Closing one edge splits the graph exactly when
  that edge's two cells end up in different pieces; test that.
- **A flood that stands in for `canStep` must enforce the step rule too.**
  `canStep` refuses a step higher than `MAX_STEP`. A flood that only tests
  walls walks up a terrace face the pathfinder will not, calls a closure safe,
  and strands what is behind it. Pools are exempt on both sides, as in
  `canStep`.
- **Generator passes that share one grid can seal cells.** Segments, the room
  partition, pillars and props are placed by four loops that cannot see each
  other: a segment can cut a room in two, and a desk can block a room's only
  doorway. The fix is the union-find at the end of `World::generate`, which
  floods the finished chunk and opens a doorway across any edge with two
  separate regions either side. A new pass that can block a cell is handled if
  it runs before that flood. One that runs after it brings the bug back.
- **`Rng::ri(lo, hi)` takes a modulo and crashes when `hi < lo`.** A BSP
  splitting a rect `2*MINR - 1` wide computes its cut range as `rw - 2*MINR`,
  which is -1, and the game exits with a bare "Floating point exception". A
  side needs `2*MINR` cells to hold two rooms.
- **Wider corridors moved Clark's arrivals**, and the regression prints this
  rather than failing. `ENT-03 arrivals` within 20 m went from 78% to 36% when
  halls went from 4 m to 6 m, because a spawn must be close and out of sight,
  and there is less out-of-sight space within 20 m. The assert is
  `nearUnseen > total/5`. If it drops under 20% and the change did not touch
  the entity, look at enclosure first.
- **Test a rule even when the generator can no longer trigger it.** The
  elevation relaxation means nothing generated is taller than `MAX_STEP`, so
  the riser blocker never fires in a normal run. `tools/regression.cpp` writes
  a 2.5 m terrace into a chunk by hand and asserts the box appears, a body
  below is pushed back and a body on top is not, and `canStep` refuses both
  ways.

## Walls, doors and collision

- **Read walls through `wallNVal` / `wallWVal`, never the raw arrays.**
  Every overlay that changes what a wall is (`unlockedDoors` for a door you
  unlocked, `shifted` for doorways the building closes behind you, PAC-03)
  goes through those two accessors. Collision, the pathfinder, line of sight,
  the occupancy grid and the mesher all read them. The mesher used to read
  `dd.wallN[i][kk]` directly, so an unlocked door kept drawing its locked leaf
  while you could walk through it, and a shifted doorway stayed open on screen
  while collision had sealed it.
  - Changing an edge must rebake both chunks that touch it (`unlockEdge` and
    `shiftEdge` do) and set `shadowsStale` (which `updateOccupancy` turns into
    `occValid = false`). Otherwise the occupancy grid only rebuilds after you
    walk six cells, and a new wall is lit as if it were not there.
  - The regression checks collision (a `WALL_DOOR` is passable with exactly
    its two jamb boxes; a body in the opening passes and one in a jamb is
    pushed out; two adjacent doors emit only their outer jambs; a
    `WALL_LOCKED` blocks bodies, light and `canStep` until `unlockEdge` makes
    it a `WALL_DOOR`) and geometry (bake, change the overlay, bake again,
    assert the mesh changed). Reverting the mesher to the raw array fails it.
- **Check the right mesh slot.** Locking a door does not change `MESH_WALLS`:
  both branches emit the same three wall boxes (two jambs and a header). The
  leaf, handle and threshold strip are in `MESH_PROPS`. A check on
  `MESH_WALLS` would always pass. Shifting an edge from open to solid does
  change `MESH_WALLS`, so the two halves of that check watch different slots
  on purpose.
- **A passable edge and an edge with no geometry are different.**
  `WALL_DOOR` is passable (`blocksEdge` says no, so pathfinding, sight and
  `buildOccupancy` let it through), but its jambs are solid and
  `gatherCellAABBs` gives them boxes. Skip that and you walk through the door
  frame. `WALL_EXIT` still has no collision at all, so its jambs are phantom;
  that is old behaviour, not a pattern to copy.
- **Vertical faces need three things to agree.** `MAX_STEP` (core/world.h)
  is the rule: `gatherCellAABBs` emits a full-height blocker on any cell more
  than that above a neighbour, `canStep` refuses to route across one, and the
  mover leaves the floor instead of sliding down one.
  - The generator also relaxes its elevations to within `MAX_STEP`. Without
    that, every sunken lounge becomes a pit you cannot climb out of, because
    there are no stair meshes yet (WORLD-06).
  - `lineOfSight` walks the same AABB list, so a riser box would make a raised
    cell opaque. It tests `top >= wallH` to consider only full-height
    blockers.
  - Pools are exempt on both sides: a pool floor is 0.6 m down, and a blocker
    would override the `poolAt` branches that handle getting in and out.
- **Actors' floor heights.** `ent.dispY` and `Dog::dispY` are smoothed floor
  heights used by `collideCircle`, the shot hit tests and the billboard. The
  six actor `collideCircle` calls used to pass `feetY = 0`, so Clark crossed
  a drop as if it were flat and stood inside a loading dock. They are smoothed
  after movement, so collision sees last frame's value. That is safe only
  because `canStep` refuses anything taller than `MAX_STEP` and a spawn sets
  `dispY` exactly.
- **Compare `py` with the floor height, not a constant.** The trapdoor
  trigger was `py > -0.05f`. Dishing the rotten patches 8.5 cm put a player
  standing on one below zero, so the trapdoor stopped working, with no error.

## Mesher and geometry

- **Do not split a usually-uniform surface into one quad per cell.** Making
  the ceiling per-cell (so a raised deck could carry its ceiling up) turned
  1 quad per chunk into 256 and cost 4% of the frame on Level 0, which has no
  raised cells. Greedy-meshing equal-height runs restored the single quad
  (0.986, noise) and pays only where the ceiling steps. `bench.sh` shows this;
  a screenshot does not.
- **A cross-chunk lookup in the mesher generates the neighbouring chunk.**
  `ceilY(gi + 1, gk)` at a chunk edge calls `data()` on the neighbour. That is
  correct, but it moved `chunks=` in the capture banner from 40 to 45. If a
  frame gains distant geometry after a mesher change, check `chunks=` first.
  Level 4's differing pixels in a regression diff are its balloons, not this.
- **Decal geometry must clear the wall's face, not its centreline.** `WT` is
  the half-thickness (0.11), so a fitting at ±0.028 from `gz` is inside the
  wall. The conduit runs were built that way: hundreds generated, none
  visible. Place decals at `gz ± WT ± clearance`, choosing the face first.
- **Small emissive detail flush on a surface z-fights at range.** The tape
  player's record lamp sat 0.1 mm proud: fine in the viewmodel, invisible on
  the floor 4 m away. Stand small decals about 1.5 mm off the surface and
  check them at the distance they are used. (Large faces are different; see
  the vending machine.)
- **A pattern in a tiling texture needs a period that divides the texture
  size.** Textures are 512 px and repeat. A grid at another pitch (form-tie
  holes every 171 px, brick courses every 42) breaks at the wrap and puts a row
  of half-features along every seam.

## Shaders

- **A failed shader compile does not crash.** raylib falls back to its default
  shader, the frame goes black, and the frame rate goes up. `init()` logs an
  error, but raylib's `TraceLog` writes to stdout, so a script that greps
  stdout for something else hides it. Always let shader errors through.
  - `flat` is a reserved GLSL keyword; a varying named `flat` breaks the
    compile.
  - Swizzles are checked: `shP` is a `vec2`, so `shP.z` fails. The z-like
    component of a 2D world point is `.y`.
- **`tools/shot.sh`'s error filter must stay case-sensitive for `ERROR:`.**
  Every headless run prints `error: XDG_RUNTIME_DIR is invalid or not set`.
  With `grep -i`, shot.sh exited 1 on that line and `set -e` stopped the
  sweep after level 0 with no visible cause. `ERROR:` in capitals is raylib's
  own prefix. This filter is the only thing that catches a failed shader
  compile.
- **`tools/bench.sh`'s filter is narrowed the same way**: the
  case-insensitive match is scoped to `SHADER:` lines, and `ERROR:` must be in
  capitals. It used to stop on ALSA's `snd_func_card_id returned error` and
  GLFW's XDG message after the first binary, printing one `BENCH` line and no
  comparison.
- **Pick thresholds that clear a byte step.** The relief opt-out (vertex alpha
  254) never worked: the shader tested `fragC.a > 0.995`, and 254/255 is
  0.9961. It showed no symptom until the lighting's terminator sharpened, and
  then the whole ceiling was blotched. A rule that has never visibly fired may
  not work. See the alpha table below.
- **Every ambient value is calibrated against the tone curve.** Near black the
  old curve `1 - exp(-1.25x)` returned about 1.25x its input; the filmic curve
  returns about 0.21x. Swapping curves darkens every shadow about six-fold
  while lit surfaces look fine. The compensation must shrink as ambient rises,
  or the Poolrooms (ambient four times any other level's) blow out to white.

## raylib behaviour

- **`TextFormat` returns a slot in a small rotating buffer**; after about four
  calls the first pointer holds a later string. Adding one inventory row made
  the flare line show the tape player's text. Draw each line straight off its
  `TextFormat` call. (The HUD now formats into `std::string`s in port/hud.cpp,
  so this affects only raylib code that still keeps a `TextFormat` pointer.)
- **raylib's default font stops at Latin-1.** `—` (U+2014) draws as `?`; `×`
  (U+00D7) is fine. Use `·` (U+00B7). Window titles are not affected.
- **raylib 6.0 restructured `Model` and made animation frames fractional.**
  `Model.boneCount`/`bones` moved into `Model.skeleton`;
  `ModelAnimation.frameCount`/`framePoses` became
  `keyframeCount`/`keyframePoses`. `UpdateModelAnimation`'s frame is now a
  float and interpolates, so it reads keyframes `f` and `f+1`. A one-keyframe
  clip (how a hand-built pose is pushed) reads past the end and segfaults in
  the library at startup. Two identical keyframes work on both versions.
  `web/src/model_asset.cpp` is the only file allowed to know this; ask
  `ModelAsset` for the skeleton.
- **The interpolation also changes the poses.** The sampled reload reaches
  about 5% further on 6.0 (0.2942 m model-space against the 0.28 the
  regression asserted), which failed the regression on main with nothing
  wrong. The real rule is that the viewmodel stays inside 0.34 m:
  `0.155 + 0.2942 × 0.48` is 0.296. Assert the rule, not a stand-in for it,
  and give any threshold a stated margin.
- **raylib 6.0 changed `SetSoundPan`'s argument without renaming it.** 5.5
  takes 0..1 with 0 = hard right; 6.0 takes -1..1 with -1 = hard left. On 6.0
  a 5.5-era 0.5 "centre" plays 75% right. Go through `panFor(bearing)` in
  `sfx.h` (bearing -1 left, 0 centre, +1 right); never call `SetSoundPan`
  directly. The sandbox links 6.0.1; `brew install raylib` gives 5.5.
  `tools/sandbox-setup.sh` restores `RAYLIB_VERSION_*` in `rlshim/` so the
  version check works.
- **A raylib `Sound` cannot loop.** `PlaySound` is one-shot. To sustain one
  (the tape voice runs 26 s off a 7.5 s clip), restart it when
  `!IsSoundPlaying(snd)` (the sim emits `AudioEvent::VOICE` each tick and
  `GameAudio::play` restarts it). There is a one-frame gap at the seam, so the
  clip must start and end quietly, with a crossfade.
- **`DrawBillboardRec` is `DrawBillboardPro` with `origin = size*0.5`**, so a
  rotated draw must pass that to stay in place; `{0,0}` or `{0, -size.y/2}`
  moves the sprite most of a body height up. Rotation is then about the sprite's
  middle; on a 1.96 m billboard at 7 degrees the feet move about 12 cm. In practice
  `DrawBillboardPro` still did not place a billboard where
  `DrawBillboardRec` did, and no `origin` fixed it. To tilt a billboard,
  shear the sprite instead (the entity sheet's lean rows).

## Timing

- **Blackouts are scheduled on wall-clock time, not frames.** `applyLevel`
  sets `nextBlackout = GetTime() + 30 + rand*60`, and at 2-3 fps frame 80 is
  30-40 s in, inside that window. Two identical captures came out at mean luma
  100 and 15. So headless captures schedule no blackouts: `BACKROOMS_SHOT`
  turns `noBlackout` on, and everything that sets `nextBlackout` goes through
  `Sim::blackoutIn`, which returns `BLACKOUT_NEVER` in that mode. It still
  draws its random number, so a capture's `grng` stays aligned with a normal
  run. Pass `BACKROOMS_NOBLACKOUT=0` to capture one; F3 then `B` forces one
  either way. Level 2 never blacks out.
- **Game time is not wall-clock time.** `dt` is clamped, so each headless
  frame advances the sim about 0.05 s while the clock advances 0.25-0.3 s.
  Anything driven by `dt` (a flare's 9 s burn, reload timers) needs about 20
  frames per simulated second, so frame 80 is only about 4 s into a burn.
  Anything scheduled off `GetTime()` (blackouts, flicker) runs on the clock.
  A test that needs a flare to burn out needs about 180 frames, which also
  crosses the blackout window.

## Sim

- **`beginDescent` clears the per-run tallies, and `dieRun` calls it.** A
  count incremented in `dieRun` before that call is wiped a line later:
  `deathCount` was, and the death card always said it was your first death.
  Counts meant to outlive a descent (deaths, records) must stay out of that
  reset list, with a comment saying why.
- **The head bob and the footstep share one phase that counts footfalls.**
  `bobPhase` gains 1 per stride, so an integer value is a foot landing: the
  step sound plays at the integer crossing, and the bob is
  `-cos(2*PI*bobPhase)`, lowest there. They used to be separate (a 2.32 m
  stride against a 1.21 m bob cycle), and the walk felt floaty. render.cpp's
  viewmodel sway reads `sinf(bobPhase * PI)`, one sideways cycle per two
  footfalls. The wrap at 4096 must stay an even integer, or both the crossing
  test and the sway jump.
- **An actor's gait comes from distance travelled, not intended speed.**
  `Entity::gait` and `Dog::gait` count footfalls like `bobPhase` and drive both
  the walk-cycle frame and the footstep sound, so the sound matches the frame.
  The old `entStepAcc` added `chaseSpd * dt`, so a Clark stuck against a wall
  still sounded like he was crossing the room, and it only ran during a chase.
- **Hit tests check height along the aim line.** Both actor hit tests used to
  measure the miss distance in the horizontal plane, so aiming at the ceiling
  still hit. `shotHitsBody` takes the 3D `fwd`, walks the aim line out to the
  target's horizontal distance and checks that height against the body's
  span. The eye is at 1.62 m and a dog stands 0.92 m, so a level shot passes
  over a dog; you have to aim at it. `popBalloonsAlongAim` had no sight test
  and popped balloons through walls; it now checks `lineOfSight` per balloon.

## Sound

- **Each `osc()` index belongs to one signal.** `Ambience::ph[]`
  (shared/port/ambience.h) is one running phase per oscillator. Two signals
  sharing an index advance it at both frequencies and both come out wrong.
  The hum's beat and the blackout ring were first written on slots 9-12, which
  the Level 1 drone and the pool water already used. The header lists the
  owners; add slots instead of reusing one.

## Sprites

- **The sheets' frame counts are defined in two files.** `ENT_FRAMES`,
  `ENT_ROWS` and `DOG_FRAMES` (port/sheets.h) size the atlas in textures.cpp
  and index the source rect when drawing. If they disagree, a sliver of the
  next frame shows down one edge of every sprite. A row is a full stride, not
  half of one mirrored: Clark has a real leg and a peg leg.
- **A walk cycle is not a sine.** `sin(60°)` and `sin(120°)` are equal, so a
  sheet sampled evenly along a sine has the ankle in the same place twice; the
  first six-frame Clark sheet had three distinct poses. A real leg is planted
  for about 60% of the cycle and swings through in the other 40%. `legPose`
  (textures.cpp) does that, and every frame is distinct (worst pair 646 px).
  Measure a sheet by counting silhouette pixels that change between frames.
- **Details stamped on a sprite must use the body's offsets.** The stamps
  (Clark's eyepatch, eye, hat skull, bandolier) only recolour pixels that are
  already opaque, so one stamped at the unsheared position lands on
  background and disappears. On the first lean row his eye went out.
  `bodyOff(y)` is the one function both the scanline loop and the stamps use.

## Tools

- **`pkill -f "pattern"` can kill your own shell** if the pattern appears in
  the shell's command line (typed inline, or in a heredoc). The shell dies
  with no output. Use `pkill -x Xvfb`, or put the command in a script file.
- **Do not run two sweeps at once.** `tools/shot.sh` reuses a running Xvfb
  (older scripts killed it, so concurrent sweeps broke each other's display
  and reported false shader errors). Concurrent runs still write the same
  files into `shots/` and share one slow rasteriser.
- **`vnoise2` returns 0..1, not -1..1** (and it is `vnoise2`, not `vnoise`).
  `noise * 0.5 + 0.5` on it gives a flat, washed-out texture. `fbm2` is the
  same.
- **`tools/pixdiff.py` needs Pillow.** `tools/sandbox-setup.sh` installs it;
  pixdiff prints a one-line instruction if it is missing.
- **Remove temporary test hooks by exact string, not by slicing.** Cutting
  from `s.index(start)` to `s.index(end)` is dangerous when the end anchor
  occurs more than once: `if (shotPath && frame == shotFrame)` occurs three
  times, and slicing to the first deleted hundreds of lines. Use a unique
  multi-line anchor, and check `git diff --stat` afterwards.

# Architecture rules

## Chalk

`Sim::chalk` is an array indexed by level. `applyLevel` does not clear it;
`beginDescent` does. Players use their marks to find their way back, so
clearing them at every level change removed their use.

- Two marks per level are not the player's (`ChalkMark::mine` is false; drawn
  duller and yellower). They are laid once per level per descent, on the
  first frame after arrival, not inside `applyLevel`, which runs before the
  player has moved and would place them around the previous level's position.
- The cap is `MAXCHALK` (128) per level. Eviction drops the player's oldest
  mark, not the front of the list, because the two other marks are at the
  front.

## The shader's alpha coding

`shaders.cpp` picks a lighting path from the vertex colour's alpha:

| `fragC.a` | meaning |
|---|---|
| `< 0.1` | light panel: emissive, flickers |
| `< 0.3` | plain emissive (exit glow) |
| `< 0.45` | window glass |
| `< 0.62` | water surface |
| `>= 0.62` | textured; `aOut = fragC.a * texel.a` |
| `0.93..0.955` | backlit print (vending machine, alpha 240): textured plus its own texel, opaque |
| `0.965..0.99` | Level 0 noclip wall: textured, torn by the shader; 247 cursed, 250 not |
| `> 0.998` | also gets world-space relief |

Relief suits grimy walls and carpet, not small curved objects, and not
anything that moves (the field is fixed in world space, so a moving object
slides through it). Opaque geometry that should stay smooth uses alpha 254
(0.996): textured, opaque, below the relief threshold. The threshold used to be
0.995, so 254 did not opt out (see Known problems, Shaders); 0.998 sits between
254 and 255 with room either side.

## Chunk mesh slots

`ChunkMeshes::meshes[MESH_COUNT]` is indexed by `enum ChunkMesh`
(port/chunk_mesh.h), and `Game::mats[MAT_COUNT]` by `enum MatSlot` (game.h).
The first four entries of each are the same surfaces in the same order (floor,
ceiling, walls, props), which lets `renderScene` draw them in one loop. Keep
that if you add slots.

Every material carries the occupancy grid in its normal-map slot, because
`DrawMesh` reliably binds that as `texture2` and `SetShaderValueTexture` did
not. A new material must have it, or its shadows are wrong: an unbound sampler
reads white, which the shader treats as walls everywhere, and the object goes
black. The setup loop in `init()` takes its count from `MAT_COUNT`; it used to
have its own number, and a new material got no shader.

## Height

`int8_t ChunkData::elev` is one floor height per cell, in decimetres. The
engine assumes exactly one: `floorY` returns a scalar, `buildOccupancy` stores
a byte per cell with no height, `lightVis` is a 2D DDA and `pathStep` a 2D
BFS. Raised floors and drops fit in that. Walkable floor directly over
walkable floor does not, which is why storeys are separate floorplans with the
current one at y = 0.

The ceiling is `World::ceilY`, not `wallH`. It follows the floor up only:
`max(floorY, 0) + wallH`. A raised deck carries its ceiling up, or its floor
comes through the slab. A sunken cell (a pool basin, a sunken lounge) keeps
the room's ceiling; lowering it with them hung a soffit round every pool
0.6 m below the tile grid. The cell does not store enough to tell a
depression from a lower storey.

- **Anything fixed to the ceiling uses `ceilY` of its own cell**, not `wallH`:
  light trays, diffusers, sprinklers, conduit runs, Level 3 pipework, Level 4
  streamers and the ceiling crease AO strips. A crease left at `wallH` floats
  under a ceiling that moved.
- **A wall between two cells of different height** starts at the lower floor
  and goes to the higher ceiling, and everything fixed to it (sill, door head,
  architrave, skirting, scrawl) is measured from that base. Basing it on its
  own cell leaves a gap under it on the low side and a slot over it on the
  high side.
- **The shader's light plane does not move within a storey.** `uLY` is one
  constant per level (`wallH - LIGHT_DROP`, core/layout.h), offset by a pitch
  per storey on Level 0, and `lightAtCPU` uses the same constant. A fitting in
  a raised bay is drawn where it is but lights the room from the base ceiling
  height. That is not noticeable on Level 1's 0.6-1.2 m decks. Fixing it needs
  the panel grid to carry a height; if `uLY` moves, `lightAtCPU` moves with
  it.
- Where neighbouring ceilings differ, the lower cell draws a soffit closing the
  gap, so a shared edge is drawn once, including across a chunk seam.

## Lighting

Ceiling panels sit on a grid of `uLS` metres (`LevelRules::ls`: 4 on Level 0,
12 on Level 1, 8 elsewhere). The shader sums only the 3x3 panels around the
shaded point, and each panel fades out before it leaves that window; without
the fade there is a brightness seam along every light-cell boundary.

- **A panel is shaded as a 1.24 m square, not a point.** `PANEL_HALF`
  (core/layout.h) is its half-extent, read by the mesher (`hp` in
  chunk_mesh.cpp), `lightAtCPU`, the greybox and Unreal's rect lights.
  `worldFragmentShader()` writes it into the GLSL in place of `@PANEL_HALF@`,
  so load that function's output, not the template. The shading point is `P`
  clamped into the rectangle.
- **The terminator softens with the panel's apparent size** (`w`, the sine of
  its half-angle), not a fixed wrap. This matters most on the ceiling, which
  is level with the fittings and lit edge-on: a fixed hard terminator turns
  relief into blotches, and a fixed soft one flattens everything.
- **Specular is evaluated once, outside the panel loop.** A reflection ray hits
  the ceiling plane at one point; look up the fitting there and clamp to its
  rectangle. A lobe per light cost about a fifth of the frame on every level
  and gave a pinprick instead of the panel's shape. It is skipped below
  `uGloss > 0.10`.
- **The tone curve is filmic (an ACES fit) and has a toe.** Near black it
  returns about 0.21x its input (the old `1 - exp(-1.25x)` returned 1.25x), so
  every ambient value arrives about six times darker than authored.
  `roomLight` compensates with a lift that shrinks as ambient rises; a flat
  six-fold lift blew the Poolrooms out to white. If you change the tone curve,
  change the compensation.
- **The flashlight and a burning flare light the air.** No ray marching: the
  eye and the flashlight are the same point, so the cone term is constant
  along a view ray and the in-scatter integral reduces to one `atan`. The
  flare uses the same integral about the ray's closest approach to it. Both
  scale with the level's fog density, so a level with clear air shows no beam.
- **Shadows are a 2D DDA over the occupancy grid** (`World::buildOccupancy`),
  which holds only full-height blockers: bit 0 north wall, bit 1 west wall,
  bit 2 pillar. Doorways, glass and furniture are left out so light passes
  through them. The march stops after 8 cells, and each light's shadow fades
  out by 15 m so the cap never cuts a visible shadow. If you raise the shadow
  range, raise the cap: where the march stops, the shadow stops, and the cutoff
  is a diamond, which shows as diagonal lines on the floor.
- **`lightAtCPU` (levels.cpp) copies this lighting on the CPU** for billboard
  and prop tinting. Change one, change the other, including the panel
  rectangle, the terminator, the ambient compensation and the tone curve.
  Props and billboards use raylib's unlit shader, so their brightness comes
  only from this estimate.
- **There is one flare point light** (`uFlarePos`/`uFlareInt`); the muzzle
  flash uses it when nothing is burning. Up to `MAXFLARES` can burn at once,
  so one is chosen to feed it. The others still burn, keep Clark and the pack
  away and hiss; they just do not light the room.
- **Choose that flare by `Sim::flarePresence`, not by distance.** Presence is
  `burn-fraction / (1 + FLAREFALL·d²)`. `dominantFlare` (the light) and the
  hiss in `updateFlare` both use it, so the flare you hear is the one lighting
  the room. Choosing the nearest is wrong at the end of a burn: a nearly-out
  flare slightly nearer than a fresh one keeps the light while it fades, then
  the light jumps across the room when it dies. In a two-flare test the nearest
  pick held the guttering flare for 13 frames at presence 0.008 against the
  fresh one's 0.248. Two equally fresh flares either side of you still swap
  when you cross the midpoint; that is the one-light limit.
- Each flare's halo is drawn from its own burn, not from the uniform; sharing
  the uniform made distant halos pulse with the near flare's flicker.

## Viewmodels

The drink can and the tape player are 3D geometry drawn in the 3D pass, with
three rules:

- **Keep them within 0.34 m of the eye.** Collision keeps you at least that
  far from anything solid, so a viewmodel inside it is never clipped by a
  wall.
- **Keep depth testing on.** Without it the far side of the can's barrel draws
  over the near side and the label reads mirrored from inside.
- **Exempt them from world shadow and give them a minimum ambient.** Next to a
  wall they otherwise fall into its shadow and go black. Set `uOccN = 0` for
  the draw, and restore it and `uAmb` immediately after.

## Field of view and HUD scale

- **The horizontal field of view is fixed; the vertical one follows the
  window.** `fov` is the camera's vertical angle, and raylib derives the
  horizontal one from it and the aspect, so the authored 70 was only right at
  the 1440x850 window (about 1.69:1) everything was tuned at. On a portrait
  phone (0.46:1) it became a 34 degree horizontal view. `Game::baseFov()`
  locks the horizontal view instead, through `windowFovY()`
  (shared/port/view.h, also used by the harness and the port). It is exact at
  1440x850, so captures are unchanged, and clamped to 58-100 degrees vertical.
  - Sprint, aim and slide add constant vertical offsets to the base; they are
    actions, not viewport.
  - `windowFovY` must invert raylib's own relation,
    `tan(fovy/2) = tan(fovX/2)·h/w` (the one render.cpp's culling uses), or
    the lock drifts from what the camera draws.
  - The harness drives sprint through an `InputFrame` with `sprint` held, so
    the sprint FOV change is tested.
- **HUD sizes scale with window height.** Every text size and edge offset goes
  through `hud(px)`, which scales by `GetScreenHeight() / 850` (the height
  the numbers were tuned at). Fractions of the screen (`sh / 2`, `sw / 3`) are
  already resolution-independent and must not be scaled, or the layout
  drifts off centre.
  - Draw HUD text with `hudText` / `hudTextC` / `hudTextR`, not `DrawText`.
    They draw a scaled dark offset behind the string, which keeps the
    inventory readable over the Poolrooms' white tile.
  - `hudTextC` measures at the scaled size. Measuring at 16 and drawing at 38
    puts the line off centre.
  - The viewmodel's `k = 1.25f` is in gun-local metres, not HUD pixels.

# Conventions

- **Cell codes are enums.** An edge is a `WallKind` (`WALL_SOLID`,
  `WALL_EXIT`, `WALL_WINDOW`, ...), a cell's furniture a `PropKind`
  (`PROP_VENDING`, `PROP_PARTY_TABLE`, ...), and a loose item a `Pickup`.
  `blocksEdge()` is the "can a body pass" test shared by collision,
  pathfinding, line of sight and the mesher. Light does not use it:
  `buildOccupancy` tests `WALL_SOLID` alone, because glass stops you but not
  light. Do not compare with bare integers.
- **An atlas cell and the quad that shows it must have the same shape**, or
  the fitting is squashed. `FIXTURES` (port/atlas.h) holds both the atlas rect
  and the size on the wall. The first version drew each fitting in part of a
  square cell and hung the whole cell, and every outlet came out as a narrow
  vertical sliver.
- **`addSolidBox` samples `PLAIN_UV` (0.375, 0.75)** for every face, so any
  atlas it uses needs plain, opaque material there. The props and fixtures
  atlases both keep a metal swatch at that spot. Move it and the boxes sample
  a transparent area and disappear.
- **The scrawl atlas grid is defined in two places that must agree.**
  `makeScrawlTex` (textures.cpp) lays phrases out 4 across and 8 down;
  `SCRAWL_PHRASES` (core/layout.h) and `addScrawl` (chunk_mesh.cpp) cut the
  UVs to match. Add a phrase without changing both and walls show half of one
  line and half of another. The pen clips every dab to its own cell, because
  one pixel of bleed shows under bilinear filtering.
- **A prop's size is `PROP_SHAPES` (core/world.h)**: its collision box
  (`gatherCellAABBs`), the height `addProp` builds it to (chunk_mesh.cpp) and,
  where `shelf` is set, the height `Sim::bottleShelfY` stands a carton at.
  These were three copies until October 2026; the merge was checked with 2,500
  chunk bakes (byte-identical) and every cell's boxes and shelf over every
  level, visit, exit setting and three storeys (674,527 boxes, same hash).
  Cartons, the vending machine and the Manila Room's table
  (`MANILA_TABLE_REST`) keep their own rules; a carton's height comes from its
  hash.
- **The rotten floor patches.** `World::softDip` is the one source of the
  bowl's shape: the floor mesher builds the cell from it and `groundAt` walks
  the player down the same curve. The camera sags more as `softTimer` builds,
  and the groan restarts faster and higher. The 0.9 s grace window is
  unchanged. `MB::quad` carries one colour and normal per quad, so the bowl
  needs fine subdivision: 8 across the cell shows a chequerboard and 12 still
  shows facets.
- **Update CREDITS.md in the same change.** Every new or changed lore
  reference (an entity, level or object from the Backrooms Wiki), external
  asset, font, sound or dependency gets a row: authors, link, licence, and how
  the game differs. Shipping one without its credit is a licensing problem.
  Update a row when the game's use of the item changes.
- Comments explain why, not what. Keep comments that record a bug a
  reasonable-looking change would bring back.
- Prefer procedural content. Licensed external models and textures are
  allowed by the user; keep provenance next to each asset.
- The game is deterministic for a given seed. Keep it that way; A/B
  screenshot comparison depends on it.
- Prove visual fixes by capturing the same frame before and after and
  comparing, or by measuring pixels.
- **Keep this file current.** When something not obvious from the code costs
  you time (a silent failure, a library surprise, a number that must change in
  two places), add it in the same commit as the work, with the rule, the
  reason and the symptom.

# Gameplay

## Squeezing and drinking

- C (touch SQUEEZE) shrinks the player radius to 0.12 m at 1.1 m/s. On
  release, the narrow radius stays until the normal radius fits; expanding in
  a gap can push the player through a wall. Use the collider's height
  tolerance.
- The drinking sound (`gulpPcm`, port/sounds.h) uses smooth envelopes at the
  same swallow times as `heldCan` (port/held.h). Test it directly for peaks
  and discontinuities; a loud white-noise attack sounds like a click.

## Health, swimming, sprint and the hunters (September 2026)

- **Being caught does not end the run.** `Sim::hurtPlayer` takes
  `ENTITY_HIT` (0.6) for a landed lunge and `PACK_BITE` (0.6) per bite, so one
  hit is survivable and a second before health regenerates is not. It pushes
  the player away at 6 m/s and grants `HURT_GRACE` of immunity. `dieRun` fires
  only at 0 health. Both catch sites check `hurtT <= 0`, or one lunge
  overlapping the player for several frames takes the whole bar. After a
  landed lunge the Smiler's lunge is spent and it staggers, so it cannot lunge
  again during the grace. `updateHealth` regenerates after `REGEN_DELAY`.
- **There is no health bar.** With one spare hit, the only thing worth showing
  is whether you are wounded, so the screen edges stay red in proportion to
  missing health and clear when a hit is survivable again.
- **Swimming has one input:** `updateSwimming(dt, rise)`. Not holding
  SPACE/JUMP sinks (that is the dive); holding it rises to and holds the
  surface. There is no dive key; CTRL is only crouch, and is disabled in
  water. On touch, `inKeyDown(KEY_SPACE)` must read JUMP's held bit; it used
  to read only presses, so holding JUMP on a phone did nothing and swimmers
  sank.
- **Surface swimming bobs the camera** (`floatT`, and `floatRoll` into the
  render roll), fading with depth. It is visual only; the buoyancy the
  regression checks is unchanged.
- **Double-tap W latches sprint** (`wSprint`) until W is released; the second
  press must come within `W_TAP`. Touch uses the push past the ring.
- **One entity, three looks.** `Sim::hunterName()` and `clarkLevel()` choose
  Pirate Clark on Level 0 (`makeClarkTex`), the Partygoer on Level 4 and a
  Smiler on Levels 1 and 3. Death cards, the "IS DOWN" banner and `deathBy` go
  through `hunterName()`; the tally reads "hunters put down" because it spans
  levels.
- **The Smiler is two sheets on the entity grid.** `makeSmilerTex(false)` is
  the body, lit like any billboard; `makeSmilerTex(true)` is the eyes and
  grin, drawn unlit at full white on top. Tinting the glow sheet by
  `lightAtCPU` would hide the grin in the dark corridors where it is meant to
  show.
- **The Poolrooms ceiling is 7.5 m**, with vaults peaking at 7.0 m;
  `lightMul` 1.25 compensates for panels 2.7 m further from the floor. The
  regression's entity captures search for a clear 7 m sightline, because a
  fixed spot drifted behind a wall and captured an empty corridor.

## Recorded sound and music (September 2026)

- **Water sounds and LEVEL FUN's music are recordings.** Water one-shots and
  loops are in `assets/sounds/water` (Red Eclipse, CC BY-SA); the music is
  `assets/sounds/music/level_fun.ogg` (Abstraction, CC0). Synthesized water
  (noise, then bubble sines) sounded fake.
- `tools/embed-materials.py sounds` embeds every `.ogg` under `assets/sounds`
  into `web/src/sounds.generated.h`. Load them by path with
  `loadEmbeddedSound` / `loadEmbeddedMusic` (sfx.cpp). The synthesized pool
  water and the music-box tune are gone from audio.cpp; osc slots 11, 12 and
  15 are free.
- **The Poolrooms have no water loop.** A running-water loop was tried and
  removed (it sounded like a tap left on). Only the underwater loop plays, and
  only with your head under.
- **Loops are `Music` streams, not restarted `Sound`s**, so they wrap without
  a gap. `GameAudio::feedLoops` must run every frame, including while paused,
  or a stream runs dry and repeats its last buffer. The sim emits
  `AudioEvent::LOOPS` each step, and the paused tick calls `holdPaused`.
- **A stream loaded from memory keeps pointing at that memory.** The embedded
  arrays are static, so this is safe; do not load music from a temporary
  buffer.
- **The ceiling balloons are drawn from `balloonAt`**, the same function
  bullets test. The renderer used to have its own copy of the placement hash,
  with an old `world.seed` salt, so the balloons you could see could not be
  shot.
- **Regression captures have no HUD.** The harness sets `cleanShot = true` and
  a `captureTime` of 4 s, so `renderUI` returns after the post pass, and even
  with the HUD on the intro card covers the screen. A HUD-only change captures
  identically. Turn `cleanShot` off and move `runStart` back for that capture,
  as `wounded.png` does.

## Poolrooms generation (September 2026)

- About one chunk in four is a grand hall (one basin, a colonnade in the deep
  water). The rest keep the arched cross, with each quarter one of: bath,
  flooded tunnels (braided maze, wading or swimming depth), submerged
  staircase (0.4 m treads down to -2.8 m), window gallery, stepping stones, or
  private tubs.
- Cells 0 and 15 are a dry walkway in every chunk, which is how chunks
  generated independently connect. Keep it in any new room type.
- The mesher draws arches only where the partition is solid on both sides of
  the opening. Grand halls have no partition, and an unconditional arch would
  hang over water.
- Level 2 turns every `WALL_DOOR` into `WALL_NONE` after connectivity and
  thinning, so tiled halls get plain openings, not office door frames.
- Level 2 windows are emissive pale panes (alpha 70), one per face, not glass:
  in the lore they look into a bright void, not the next room.
- The regression finds and captures a window gallery, a staircase and a
  tunnel, and fails if any room type is missing or water covers less than half
  the cells.

## Desktop browser shader control flow

- Keep `lightState` arithmetic-only, including the dead-tube and Manila Room
  masks. Adding a short-circuit condition to its return brought back a desktop
  Chrome/ANGLE bug (horizontal black streaks on wallpaper, holes in the
  revolver) while native rendering stayed correct. A single return statement
  was not enough. `tools/web-render-check.mjs` with `BROWSER_CHANNEL=chrome`
  detects both by isolated pixel discontinuities; absolute gun brightness is
  not stable across lighting changes.
