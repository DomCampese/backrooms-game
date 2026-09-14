# CC0 revolver

Author: **loafbrr_1 / TheLoafbrr**. Source: [Revolver Game Asset](https://opengameart.org/content/revolver-game-asset), published December 4, 2021.

License: **Creative Commons CC0 1.0** ([legal terms](https://creativecommons.org/publicdomain/zero/1.0/legalcode)). Both the author's upload and the archive's README identify the asset as CC0. The source permits redistribution and modification, including commercial use.

Original archive: [revolver_fbx_gltf_blend_textures.zip](https://opengameart.org/sites/default/files/revolver_fbx_gltf_blend_textures.zip).
SHA-256: `d404e247c2503937d7ba8fc965e9872a8e3636aef0de6100c2ae30650112eb4d`.

## Conversion

`tools/import-revolver.py /path/to/extracted/archive` reads the original
`GLTF/RevolverExport.gltf` and `Textures/Revolver_1` / `Textures/RevolverAmmo`.
This optional reimport requires NumPy and Pillow. Ordinary builds only require
Python's standard library and the converted files committed here.

- Retains all seven gun objects and six cartridges; combines them into two material batches (2,334 vertices, 2,534 triangles). Omits loose ammunition-box/display props and duplicate spent-case alternatives.
- Preserves source UVs and normals, rigid joint assignments, and bind matrices. Rotates the source +X barrel to the game's +Z axis and centers it for the existing viewmodel transform.
- Samples the complete Reload action and the first of six shots in Shoot at approximately 30 Hz. Runtime interpolates the sampled matrices. The existing 1.8-second reload and 0.42-second firing cooldown control playback.
- Reload articulation is reversed in the handle’s local frame so the cylinder opens to the player’s left, with cartridge paths transformed together. Two reflections preserve the original closed geometry, UVs, and winding.
- The complete Reload already includes opening/closing; the separate OpenCylinder/CloseCyinder clips are not appended.
- Cartridges remain attached to the cylinder during shooting, avoiding the source's swap to omitted spent-case meshes. Reload retains individual cartridge movement. This is cosmetic ammunition animation, not a simulation of individual spent cases.
- Downsamples authored albedo, AO, normals, roughness, and metalness to 512 pixels. Albedo gets partial AO and attenuated metallic diffuse energy to fit the game's diffuse/gloss lighting; this is not a full metallic PBR shader. Source tangent normals become the existing packed RG slope representation; roughness/metalness produce B gloss, alpha 128 selects absolute object gloss.

`mesh.bin` contains two little-endian batches: uint32 vertex/index counts;
vertices of eight float32 values (position, normal, UV) plus uint32 joint;
then uint16 indices. `poses.bin` contains three clips (Default, Reload, Shoot):
uint32 frame count, float32 duration, then 17 row-major 3x4 float32 skin matrices
per frame. The coordinate conversion is baked into those matrices.

`tools/embed-materials.py revolver` creates the ignored C++ header used by
`src/revolver.cpp`. No runtime downloads, filesystem paths, glTF parser,
Blender, or Python dependencies are added to the executable.
