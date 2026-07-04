# THE BACKROOMS — Level 0

A native, procedurally-infinite backrooms horror game in a single C++ file.
No assets — every texture and every sound is synthesized at startup.

![screenshot](docs/screenshot.png)

## Build & run

Requires [raylib](https://www.raylib.com) 5.x (`brew install raylib` on macOS;
any distro package or pkg-config install works on Linux).

```sh
make run
```

## How it works

- **Infinite world** — deterministic chunk generation (32 m chunks, hashed from
  a world seed): wall runs with doorway gaps, pillars, all baked into 3 meshes
  per chunk. Chunks stream in around you and unload behind you.
- **Lighting** — ceiling lights sit on a global 8 m grid, so the fragment shader
  computes the 9 nearest fluorescents *procedurally* — zero light data. A hash
  decides which tubes are dead and which strobe. Periodic blackout events kill
  the whole floor.
- **Audio** — one continuously synthesized stream: 120 Hz fluorescent hum with
  harmonics, low room tone, and a growl that swells when something is near.
  Footsteps are generated noise-burst samples.
- **Clutter** — cardboard box stacks, filing cabinets, folding tables, and
  collapsed ceiling tiles (with the dark hole they left behind), all placed by
  the same deterministic generator and all solid.
- **PIRATE CLARK** — spawns out in the fog and stands there: tricorn hat, one
  glowing eye, a hook, a peg leg. Line-of-sight is ray-marched against real
  wall geometry. Stare too long and he chases. He is slightly slower than your
  sprint. Your sprint is finite.
- **Exits** — vanishingly rare glowing doorways carved into wall runs. Finding
  one "ends" the run. Sort of.
- **Post** — film grain, vignette, chromatic aberration, and a mains-frequency
  luma shimmer, all scaling with fear.

## Controls

| key | action |
|---|---|
| WASD | walk |
| mouse | look |
| SHIFT | sprint (stamina) |
| SPACE | jump |
| F | borderless fullscreen |
| ESC / click | release / capture mouse |
| F3 | debug HUD |

## Dev/testing knobs

- `BACKROOMS_SHOT=out.png` — run 600 frames headlessly, save a screenshot, exit.
- `BACKROOMS_EXITS=1` — exit doors everywhere (visual testing).
- `BACKROOMS_POS="x,z,yaw"` — start at a specific spot (visual testing).
