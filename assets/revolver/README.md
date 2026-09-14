# CC0 revolver

Author: **loafbrr_1 / TheLoafbrr**. Source: [Revolver Game Asset](https://opengameart.org/content/revolver-game-asset), published December 4, 2021.

License: **Creative Commons CC0 1.0** ([legal terms](https://creativecommons.org/publicdomain/zero/1.0/legalcode)). Both the author's upload and the archive's README identify the asset as CC0. The source permits redistribution and modification, including commercial use.

Original archive: [revolver_fbx_gltf_blend_textures.zip](https://opengameart.org/sites/default/files/revolver_fbx_gltf_blend_textures.zip).
SHA-256: `d404e247c2503937d7ba8fc965e9872a8e3636aef0de6100c2ae30650112eb4d`.

## Standard runtime asset

The runtime asset is [revolver.glb](../models/revolver.glb), loaded by Raylib's
standard model and animation APIs. The previous mesh.bin/poses.bin format is
removed. See [the reusable model workflow](../../docs/model-pipeline.md).

`tools/import-revolver.py /path/to/extracted/archive` reads the original
`GLTF/RevolverExport.gltf` and `Textures/Revolver_1` / `Textures/RevolverAmmo`.
This optional reimport requires NumPy and Pillow. It writes only the prepared GLB;
ordinary builds use Python's standard library to package committed assets.
New unrelated models can be exported directly from Blender and do not use this
revolver-specific preparation recipe.

## Artistic preparation

- Seven gun objects and six cartridges are combined into two material batches:
  2,334 vertices and 2,534 triangles. Loose props and duplicate spent-case meshes
  are omitted.
- Source geometry, UVs, and normals are preserved, with the barrel rotated from
  +X to +Z and centered for the existing viewmodel. Prepared skin transforms are
  exported as independent named joints with identity binds and standard TRS clips.
- The complete reload and first firing cycle are resampled to standard glTF keys.
  A 34 ms final-pose hold accommodates Raylib 5.5's 17 ms animation sampling.
  Gameplay still controls the original 1.8-second reload and 0.42-second shot timing.
- Reload opens left: hinge and cartridge trajectories are reversed together in
  the handle's frame. The mesh and UVs are not mirrored. Cartridges remain in the
  cylinder during firing and follow individual reload paths; spent cases do not
  have persistent physical simulation.
- Albedo is graded with partial AO and attenuated metallic diffuse energy for
  this game's lighting. Textures are 512 pixels. Standard normal and combined
  metallic/roughness maps are embedded in the GLB; the reusable renderer adapter
  converts those maps to packed slope/gloss textures at load time.

The standard GLB is 741,988 bytes and can be inspected in glTF-capable tools.
Runtime uses Raylib's parser, with generic embedded-file packaging so it remains
independent of its working directory. No Python or Blender runtime is required.
