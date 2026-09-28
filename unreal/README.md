# The Backrooms on Unreal Engine 5

The port described in docs/unreal-handoff.md. The raylib build in the rest of
this repository stays the reference until the port reaches M5; nothing here
replaces it, and nothing in the raylib build depends on this folder.

Nothing in this folder has been compiled by Unreal yet: it was written where no
engine was available. `tools/unreal-check.sh` checks what can be checked without
one (below). Expect the first build on a Mac to need small fixes to the Unreal
side; the shared code compiles as it does for the raylib build.

## Layout

| path | what |
|---|---|
| `BackroomsGame.uproject` | the project; its only module is a stub, the game lives in the plugin |
| `Plugins/Backrooms/` | one Runtime module, `Backrooms` |
| `.../Private/Shared/*.cpp` | one file per source in `src/core`, `src/sim`, `src/port` and `tools/contract_lib.cpp`, each a single `#include`: the module compiles the repository's files, not copies |
| `.../Public/BackroomsCoords.h` | the one conversion between core (metres, y up) and Unreal (centimetres, z up) |
| `.../BackroomsWorldSubsystem` | owns core's `World`, streams greybox chunk actors round the camera |
| `.../BackroomsChunkActor` | a chunk as a procedural mesh, one section per surface |
| `.../BackroomsGameMode` | starts a level from `?level=&seed=&visit=` and spawns a free camera |
| `.../Private/Tests/BackroomsTests.cpp` | automation tests: contract, trace replay, coordinates, greybox |

Why one module, not the core and sim modules the plan first described: core has
no export annotations, and editor builds link each module as a shared library
with hidden symbols, so a second module could not call core. Why no PCH: a
shared PCH is force-included into every file of the module, core's included,
and core must see nothing but itself and the standard library.

A new `.cpp` in `src/core`, `src/sim` or `src/port` needs a wrapper in
`Private/Shared`. `tools/unreal-check.sh` fails until it has one.

## Opening it

1. Install Unreal Engine 5 (record the version in docs/unreal-handoff.md).
2. Right-click `BackroomsGame.uproject` > Switch Unreal Engine version, or
   generate project files, then build the `BackroomsGameEditor` target.
3. Open the project and press Play. The default map is the engine's empty Entry
   map; the game mode generates the level round the engine's DefaultPawn free
   camera (WASD and the mouse). A point light follows the camera.

Other levels: open the Entry map with options, for example from the console,
`open /Engine/Maps/Entry?level=1?seed=1337`.

## Tests (M1)

Session Frontend > Automation, filter `Backrooms`, or:

```bash
UnrealEditor-Cmd BackroomsGame.uproject -ExecCmds="Automation RunTests Backrooms; Quit" -unattended -nullrhi
```

| test | passes when |
|---|---|
| `Backrooms.Core.Contract` | every line of `tests/golden` matches (docs/migration.md, "Contract tests") |
| `Backrooms.Sim.Replay` | every trace in `tests/traces` replays with every digest matching (src/sim/trace.h) |
| `Backrooms.Port.Coords` | the conversion scales, swaps y and z, and does not mirror (forward and right land on Unreal's) |
| `Backrooms.Port.Greybox` | every level's origin chunk has geometry and valid indices |

The contract and the replay run core and the sim as Unreal's compiler and the
Mac's libm build them. What to expect if they fail is in docs/unreal-handoff.md,
"Status".

## Without Unreal

```bash
tools/unreal-check.sh
```

Checks that every shared source has a wrapper, compiles each wrapper alone with
the module's settings (C++20, no exceptions, no RTTI, warnings as errors), links
them with clang and FMA enabled but without `-ffp-contract=off` and runs the
contract and the traces, and parses the `.uproject` and `.uplugin`. The Unreal
classes themselves need the engine.
