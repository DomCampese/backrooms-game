#pragma once
// What the Web build draws round the chunks each frame, decided from the sim
// so the Unreal port draws the same: the world shader's per-frame light
// (FrameLight), the loose objects drawn as meshes through the world shader,
// and the shapes and billboards raylib draws unlit, each already lit on the
// CPU the way render.cpp lights them (lightAtCPU). render.cpp draws from this,
// so the two builds cannot light or place them differently.
//
// Metres in the sim's frame: the storey the player is on, y = 0 its floor.
#include "../sim/sim.h"
#include "held.h"
#include "mesh_builder.h"
#include "view.h"
#include <vector>

// The world shader's per-frame uniforms that come from the sim.
struct FrameLight {
    float time;            // uTime
    float blackout;        // uBlackout: blackoutCur, sagging while it whispers
    float flash;           // uFlash: flashCur, flickering on a dying battery
    Vec3 flashDir;         // uFlashDir
    float flick;           // the flares' flicker this frame
    float flareInt;        // uFlareInt and uFlarePos: the dominant flare, or the
    Vec3 flarePos;         // muzzle flash when nothing burns
    Vec3 entPos;           // uEntPos: the hunter's chest
    float entDark;         // uEntDark: its pool of dead light
    float entBlock;        // uEntBlock: 1 while it can throw a shadow
    float roomMask[4];     // uRoomMask: the Manila Room's dark panels (x0, z0, x1, z1)
    float lamp[4];         // uLamp: the chandelier or a stairwell's landing tube (xyz, output)
    Vec3 lampCol;          // uLampCol
};

// The world shader's shadow grid (World::buildOccupancy, texture2): OCC_CELLS
// a side round the player, rebuilt once the player is OCC_RECENTRE cells off
// where it was built or the walls change.
constexpr int OCC_CELLS = 64;
constexpr int OCC_RECENTRE = 6;

// Also hands the room mask and lamp to lightAtCPU.
FrameLight frameLight(Sim &sim, float now);

// A model transform: p' = x * p.x + y * p.y + z * p.z + t.
struct Xform {
    Vec3 x{ 1, 0, 0 }, y{ 0, 1, 0 }, z{ 0, 0, 1 }, t{ 0, 0, 0 };
};
Xform xformOf(const HeldFrame &f);

// The loose objects drawn through the world shader (port/objects.h).
enum class ObjMesh : uint8_t { Crate, CrateLid, Can, Deck, Reel, DeckLamp, Flare, Count };
// Which material each draws with: the props atlas, the can's wrap or the deck's.
enum class ObjMat : uint8_t { Props, Can, Deck };
ObjMat objMat(ObjMesh m);
// The gloss an object draws with in place of the level's (uGloss), or -1 for
// the level's: a flare's printed tube is nearly matte, the revolver's steel is
// not.
constexpr float FLARE_GLOSS = 0.12f;
constexpr float REVOLVER_GLOSS = 0.48f;
float objGloss(ObjMesh m);

struct MeshDraw {
    ObjMesh mesh;
    Xform xf;
    float tint = 1.0f;     // colDiffuse, grey: the deck's record lamp breathes
};

// raylib's unlit shapes, at the tessellation raylib draws them with.
enum class ShapeKind : uint8_t {
    Cube,          // DrawCube: centre a, size
    Sphere,        // DrawSphere: centre a, radius r0
    Cylinder,      // DrawCylinder: base centre a, top radius r0, bottom radius r1, height h
    CylinderEx,    // DrawCylinderEx: from a (radius r0) to b (radius r1)
    Billboard,     // DrawBillboardRec: centre a, size.x by size.y, facing the camera
};
// The textures billboards are cut from (render.cpp's texParticle, the sheets).
enum class ShapeTex : uint8_t { None, Particle, Clark, Smiler, SmilerGlow, Partygoer, Dog, Count };

struct Shape {
    ShapeKind kind;
    Vec3 a{}, b{}, size{};
    float r0 = 0, r1 = 0, h = 0;
    int sides = 0;
    Rgba colour{ 255, 255, 255, 255 };
    ShapeTex tex = ShapeTex::None;
    float src[4] = { 0, 0, 0, 0 };   // the billboard's source rectangle, texels: x, y, w, h
    bool additive = false;           // BLEND_ADDITIVE
    bool depthWrite = true;          // Level 1's mist banks write no depth
};

// One draw, in render.cpp's order: what blends over what depends on it.
struct SceneDraw {
    bool isMesh;
    MeshDraw mesh;
    Shape shape;
};

// Everything render.cpp draws between the chunks and the held item: the
// stairwells' landing tubes, the sim's scene (simScene), exits burning green,
// Level 3's valves, the pack, the flares, the hunter, rounds in flight,
// Level 1's mist and the impacts. `now` is the render clock.
void sceneDraws(Sim &sim, const FrameLight &light, float now, std::vector<SceneDraw> &out);

// The can, the deck or a flare in hand (the revolver is each build's own
// model): what render.cpp draws for heldItem.
void heldDraws(const Sim &sim, const SimView &view, float now, std::vector<SceneDraw> &out);

// The muzzle flash and its smoke at `muzzle`, the revolver's muzzle in the
// world, `up` the gun's up (render.cpp's drawHeldWeapon).
void muzzleDraws(const Sim &sim, Vec3 muzzle, Vec3 up, std::vector<SceneDraw> &out);

// A shape as raylib's triangles, into `out` with its colour on every vertex.
// Billboards face the camera as raylib's do: across `camRight`, up the world's
// y. A billboard's UVs are its source rectangle over a texture texW x texH.
void shapeTriangles(const Shape &s, Vec3 camRight, int texW, int texH, MB &out);
// The camera's right, as raylib's MatrixLookAt makes it, for billboards.
Vec3 cameraRight(const SimView &view);
// A texture's size in texels, for billboard UVs.
void shapeTexSize(ShapeTex t, int &w, int &h);
