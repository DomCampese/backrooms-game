#pragma once
// A skinned glTF binary (GLB), loaded and animated as raylib 5.5 does it,
// which is how the Web build draws the revolver (web/src/model_asset.cpp):
// clips sampled every 17 ms into poses, a pose blended between two samples,
// and the vertices skinned on the CPU with raylib's bone matrices, which scale
// after translating. The Unreal port draws the revolver from this, so a reload
// there is the Web build's reload frame for frame.
//
// It reads what assets/models holds: float positions, normals, texture
// coordinates and weights, 8 or 16-bit joints and indices, one skin, LINEAR or
// STEP channels, and images embedded in the binary chunk.
#include "../core/vec.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Quat { float x, y, z, w; };
// raylib's Transform.
struct Pose { Vec3 translation; Quat rotation; Vec3 scale; };

struct GlbPrimitive {
    std::vector<float> pos, normal, uv;    // 3, 3 and 2 a vertex
    std::vector<uint8_t> joints;           // 4 a vertex, as raylib's boneIds
    std::vector<float> weights;            // 4 a vertex
    std::vector<uint32_t> index;
    int material = -1;                     // the glTF material, or -1
};

struct GlbMaterial { int baseColour = -1, normal = -1, metalRough = -1; };   // images, or -1

struct GlbClip {
    std::string name;
    std::vector<std::vector<Pose>> frames;   // a pose per 17 ms, a Pose per bone
};

struct GlbModel {
    // In raylib's mesh order: the nodes in order, each mesh's triangle
    // primitives in order.
    std::vector<GlbPrimitive> prims;
    std::vector<GlbMaterial> materials;
    // Each image's bytes (PNG or JPEG) as offset and size in the file.
    struct Image { size_t offset = 0, size = 0; std::string mime; };
    std::vector<Image> images;
    // The skin's joints, raylib's bones.
    std::vector<std::string> bones;
    std::vector<int> parent;
    std::vector<Pose> bindPose;
    std::vector<GlbClip> clips;
};

// False with a reason on anything this loader does not read.
bool loadGlb(const uint8_t *data, size_t size, GlbModel &out, std::string &error);
int glbClip(const GlbModel &m, const char *name);
int glbBone(const GlbModel &m, const char *name);
// The pose `progress` (0..1) through a clip, blended between its two nearest
// samples (ModelAsset::sample).
void glbSample(const GlbModel &m, int clip, float progress, std::vector<Pose> &out);
// One primitive skinned to a pose, as raylib 5.5's UpdateModelAnimation
// with ModelAsset::update's correction to its normals.
void glbSkin(const GlbModel &m, const std::vector<Pose> &pose, int prim, std::vector<float> &pos,
             std::vector<float> &normal);

// The world shader's object detail map from a glTF material's normal and
// metallic-roughness images, both RGBA8 and `pixels` long: packed slopes in
// red and green, gloss in blue, metalness in alpha (128 a dielectric, 188
// fully metal). Below an alpha of 0.75 the shader reads the gloss as absolute.
void glbDetailMap(const uint8_t *normal, const uint8_t *metalRough, int pixels, uint8_t *out);

// raymath's, operation for operation.
Vec3 quatRotate(Vec3 v, Quat q);
Quat quatMultiply(Quat a, Quat b);
Quat quatFromAxisAngle(Vec3 axis, float angle);
