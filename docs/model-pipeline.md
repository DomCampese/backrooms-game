# Standard model pipeline

Imported assets now use standard glTF 2.0 binary (`.glb`) files and Raylib's
`LoadModel`, `LoadModelAnimations`, `UpdateModelAnimation`, and resource unloading.
The revolver's bespoke mesh/pose format and vertex animation loop are removed.

## Adding a model

1. Prepare the model in Blender or another glTF exporter. Export a self-contained
   GLB with textures; use triangles, at most 65,535 vertices per primitive, one
   skeleton, up to four joint weights per vertex and no more than 256 joints.
   Apply transforms and bake animation into TRS channels. Raylib 5.5 does not
   provide a general morph-target or multi-skeleton animation pipeline.
2. Put it under `assets/models/` and record its source/license alongside it.
   `make` automatically embeds every GLB there using Raylib's file-data callback.
   The same loader also accepts ordinary filesystem GLBs for tests/development.
3. Call `ModelAsset::load("models/name.glb")`. Static objects can share one loaded
   asset and draw it repeatedly with different placement matrices. For animation,
   look up a clip by name, sample normalized progress, then call `update()` before
   drawing. Independent animated poses require separate instances or sequential
   pose updates; a global instance/cache manager is not part of this change.
4. Call `unload()` before the graphics context closes. The wrapper owns imported
   textures, meshes, animation clips, and generated shader detail maps.

The only model-specific preparation script is the existing revolver recipe,
retained to reproduce its left-opening reload, clip trim and material grading.
It outputs a standard GLB with flattened named joints and TRS animations. Future
assets do not need that script or a new custom converter. Standard normal and
metallic/roughness textures are adapted once at load time to our existing shader;
this renderer is still not full PBR. Tangent frames are reconstructed in the shader.

## Raylib 5.5 compatibility

Raylib's CPU animation routine treats normals as positions. The version-gated
compatibility block removes the erroneous weighted translation after the standard
update. Mixed static/skinned models pass only skinned meshes to that API. A fixture
with two blended bones verifies the expected deformation and unchanged normals.

A generic TRS interpolation step supplies Raylib a one-frame pose for smooth
playback. Exported clips include a short terminal hold because Raylib's 17 ms
sampling otherwise stops before the final authored keyframe. The original
revolver regression caught that seam; checks now report failure and exit instead
of aborting and generating macOS crash dialogs.

## Validation checkpoint

- Native build and regression passed: static GLB, mixed static/skinned GLB,
  blended joint weights, normal correction, all six cylinder transitions,
  reload endpoint/bounds, and 18 screenshots. Revolver/reload images inspected.
- Khronos glTF validator: zero errors; two tangent-generation warnings (our shader
  reconstructs tangent space) and buffer-view target hints.
- Interleaved best-of-three Level 0 scene timings versus commit 4f50166:
  idle 7.817 → 7.804 ms; continuous reload/shoot 6.236 → 6.422 ms (+0.186 ms, 3%).
  No GPU/frame-rate improvement is claimed; the animation overhead is small at
  this scale. Larger crowds of animated objects still require their own benchmark.
- Full five-level/menu plus pool/flashlight sweep pending at this checkpoint.
