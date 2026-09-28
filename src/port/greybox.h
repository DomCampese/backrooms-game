#pragma once
// A chunk as plain boxes and quads, for a port's first milestone (M2 in
// docs/unreal-handoff.md): floors, ceilings, walls with their openings, steps,
// stairs, pillars, props as their collision boxes, water and light panels.
// Built from core's accessors and chunkLayout, so it shows the world collision
// and the rules see, and nothing of how the raylib build dresses it.
//
// Engine-neutral: core and the standard library only. Positions are metres in
// the storey's own frame (y up, y = 0 its floor), as everywhere in core. Every
// face is emitted twice, once per winding, so an engine's front-face
// convention and its handedness cannot hide a face.
#include "../core/layout.h"
#include <cstdint>
#include <vector>

enum class GreySurface : uint8_t {
    Floor, Ceiling, Wall, Step, Stair, Pillar, Prop, Door, Exit, CursedExit, Glass, Rail, Water, Light, DeadLight,
    Count,
};

struct GreyboxMesh {
    // One section per surface, so a port can give each its own material.
    struct Section {
        std::vector<Vec3> pos, normal;
        std::vector<uint32_t> index;   // triangles
    };
    Section sections[(int)GreySurface::Count];
    int storey = 0, cx = 0, cz = 0;

    size_t triangles() const;
};

// Chunk (cx, cz) of storey w.qs, generating it and its neighbours as needed.
// `meshedProps` has bit k set for each PropKind k the port draws with a mesh of
// its own (from chunkLayout's props); those props get no box here.
GreyboxMesh greyboxChunk(World &w, int cx, int cz, uint32_t meshedProps = 0);
