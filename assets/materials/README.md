# Object material sources

These three tiles are derived from Poly Haven assets released under CC0 1.0.
License: https://polyhaven.com/license and https://creativecommons.org/publicdomain/zero/1.0/

| File | Source asset | Source image |
| --- | --- | --- |
| wood.jpg | https://polyhaven.com/a/wood_table_001 | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/wood_table_001/wood_table_001_diff_1k.jpg |
| metal.jpg | https://polyhaven.com/a/metal_plate | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/metal_plate/metal_plate_diff_1k.jpg |
| fabric.jpg | https://polyhaven.com/a/denim_fabric | https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/denim_fabric/denim_fabric_diff_1k.jpg |

Retrieved September 12, 2026. Original 1K diffuse images were converted to RGB,
resized to 256x256 with Lanczos filtering, and saved as optimized JPEG quality 88.
The game remaps luminance into the existing prop palette when composing its atlas.
These are color sources, not downloaded normal/roughness maps. Relief is derived
from the composed albedo; gloss is assigned by material family.

The three JPEGs total 45,083 bytes. tools/embed-materials.py embeds the compressed
bytes at build time. No network access or external asset directory is required
at runtime. The generated header is ignored by git and recreated by both build paths.
