#include "world_mesh.h"
#include "mesh_builder.h"
#include "textures.h"   // FIXTURES and the vending machine's door: where each sits in the atlas
#include "util.h"
#include "vec_rl.h"
#include "core/level_rules.h"
#include <algorithm>
#include <cmath>

// Door trim: an architrave 24 mm proud of the wall face, and the threshold
// strip under the opening. Both sample the props atlas's plain metal at
// (0.375, 0.75), which is where addSolidBox looks.
static const float TRIM_T = 0.024f;
static const Color TRIM_COL = { 176, 170, 152, 254 };   // painted trim, no relief bump
static const Color SILL_COL = { 138, 136, 130, 254 };   // dulled metal threshold
// A locked door's leaf and its lock plate. Alpha 254: textured and opaque, but
// below the shader's relief threshold, because a flat painted slab with the
// world-space bump on it comes out looking like pebbledash (see AGENTS.md).
static const Color LEAF_COL = { 150, 128, 96, 254 };
static const Color LOCK_COL = { 206, 194, 140, 254 };

// Baked contact-shadow tint. The AO strip texture carries the falloff in its
// alpha channel, so wall creases and furniture shadows all share this one colour.
static const Color AO_TINT = { 10, 9, 9, 255 };

// A rounded contact shadow shared by props and pillars. Only the footprint
// is solid; the skirt samples the AO gradient down to zero at its outer edge.
static void addContactShadow(MB &ao, float pcx, float pcz, float ey, float rot,
                             float hx2, float hz2) {
    float ca = cosf(rot), sa = sinf(rot);
    const float S = 0.24f;                    // how far the falloff reaches
    const float d = S * 0.7071f;              // the corner, cut across
    const Vector3 up = { 0, 1, 0 };
    auto P = [&](float lx, float lz) {
        return Vector3{ pcx + lx * ca - lz * sa, ey + 0.006f, pcz + lx * sa + lz * ca };
    };
    // core, right under the piece: darkest end of the gradient
    ao.quad(P(-hx2,-hz2), P(hx2,-hz2), P(hx2,hz2), P(-hx2,hz2), up,
            {0,0},{1,0},{1,0},{0,0}, AO_TINT);
    // four skirts fading outward. Each inner edge runs so the quad
    // stays wound the same way round as the core.
    const float sd[4][6] = {
        {  hx2,-hz2, -hx2,-hz2,  0,  -S },
        {  hx2, hz2,  hx2,-hz2,  S,   0 },
        { -hx2, hz2,  hx2, hz2,  0,   S },
        { -hx2,-hz2, -hx2, hz2, -S,   0 },
    };
    for (auto &e : sd)
        ao.quad(P(e[0], e[1]), P(e[2], e[3]),
                P(e[2] + e[4], e[3] + e[5]), P(e[0] + e[4], e[1] + e[5]), up,
                {0,0},{1,0},{1,1},{0,1}, AO_TINT);
    // and the corners, so the skirt closes instead of leaving notches
    const float cn[4][4] = {
        {  hx2,  hz2,  1,  1 }, { -hx2,  hz2, -1,  1 },
        { -hx2, -hz2, -1, -1 }, {  hx2, -hz2,  1, -1 },
    };
    for (auto &c2 : cn) {
        Vector3 inner = P(c2[0], c2[1]);
        Vector3 pxv = P(c2[0] + c2[2] * S, c2[1]);
        Vector3 pmv = P(c2[0] + c2[2] * d, c2[1] + c2[3] * d);
        Vector3 pzv = P(c2[0], c2[1] + c2[3] * S);
        bool xFirst = (c2[2] * c2[3]) > 0;    // keeps the winding consistent
        Vector3 a1 = xFirst ? pxv : pzv, b1 = xFirst ? pzv : pxv;
        ao.tri(inner, a1, pmv, up, {0,0},{0,1},{1,1}, AO_TINT);
        ao.tri(inner, pmv, b1, up, {0,0},{1,1},{0,1}, AO_TINT);
    }
}

// The wall builder: a mesh builder that also knows how its paper maps onto
// height. tileV is how many metres of wall one texture tile spans vertically:
// 3 where the texture is a repeating pattern, Level 1's wall height because its
// concrete carries a damp band and pour joints at real heights (a 3 m repeat
// draws a second tide line under the 4.2 m slab). tallPaper, on a storeyed
// level, continues a wall that climbs past one tile from the tile's clean
// middle, a whole number of pattern repeats down, so no second baseboard runs
// round a stair shaft at 3 m; everywhere else a tall wall repeats its tile.
struct WallBuilder : MB {
    float tileV = 3.0f;
    bool tallPaper = false;
};
// The wallpaper's V at a height, for tallPaper.
static float wallV(float y, float tileV) { return y <= tileV + 1e-4f ? 1 - y / tileV : 1 - (y - tileV * 0.5f) / tileV; }
// A face over a void — an upper storey's wall seen from the shaft below it —
// stands on no floor, so it must show no baseboard at all. It takes the
// tile's clean middle, 0.75 m to 2.25 m, repeating by whole chevrons, which
// means splitting the face wherever that band wraps.
static void voidFace(WallBuilder &mb, Vector3 a0, Vector3 a1, Vector3 n, float ua, float ub, float y0, float y1, Color w) {
    auto T = [](float y) { return 0.75f + fmodf(y + 0.57f + 150.0f, 1.5f); };
    float y = y0;
    while (y < y1 - 1e-4f) {
        float next = y + (2.25f - T(y));             // where the band wraps
        float yb = std::min(y1, next);
        float va = 1 - T(y) / mb.tileV, vb = 1 - (T(y) + (yb - y)) / mb.tileV;
        mb.quad({a0.x,y,a0.z},{a1.x,y,a1.z},{a1.x,yb,a1.z},{a0.x,yb,a0.z}, n, {ua,va},{ub,va},{ub,vb},{ua,vb}, w);
        y = yb;
    }
}
// skip bits: 1 = -z face, 2 = +z face, 4 = -x face, 8 = +x face. Wall runs overlap
// their neighbours by WT so corners close, which buries the end caps inside the
// next box — and an end cap meeting the neighbour's front face at exactly the same
// depth z-fights into a vertical seam. Skipping buried caps removes the seam.
// voidFaces: which of the four faces look into a hole (same bits as skip).
static void addBoxSides(WallBuilder &mb, float x0, float y0, float z0, float x1, float y1, float z1,
                        bool bottomFace = false, int skip = 0, Color w = WHITE, int voidFaces = 0) {
    if (mb.tallPaper && voidFaces) {
        if (!(skip & 1) && (voidFaces & 1)) voidFace(mb, {x0,0,z0}, {x1,0,z0}, {0,0,-1}, x0/3, x1/3, y0, y1, w);
        if (!(skip & 2) && (voidFaces & 2)) voidFace(mb, {x1,0,z1}, {x0,0,z1}, {0,0,1}, x1/3, x0/3, y0, y1, w);
        if (!(skip & 4) && (voidFaces & 4)) voidFace(mb, {x0,0,z1}, {x0,0,z0}, {-1,0,0}, z1/3, z0/3, y0, y1, w);
        if (!(skip & 8) && (voidFaces & 8)) voidFace(mb, {x1,0,z0}, {x1,0,z1}, {1,0,0}, z0/3, z1/3, y0, y1, w);
        addBoxSides(mb, x0, y0, z0, x1, y1, z1, bottomFace, skip | voidFaces, w, 0);
        return;
    }
    if (!mb.tallPaper) {
        float va = 1 - y0 / mb.tileV, vb = 1 - y1 / mb.tileV;
        if (!(skip & 1))
            mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
        if (!(skip & 2))
            mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
        if (!(skip & 4))
            mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
        if (!(skip & 8))
            mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
        if (bottomFace)
            mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
        return;
    }
    if (y0 < mb.tileV - 0.01f && y1 > mb.tileV + 0.01f) {   // straddles the tile: split it there
        addBoxSides(mb, x0, y0, z0, x1, mb.tileV, z1, bottomFace, skip, w, 0);
        addBoxSides(mb, x0, mb.tileV, z0, x1, y1, z1, false, skip, w, 0);
        return;
    }
    float va = wallV(y0, mb.tileV), vb = wallV(y1, mb.tileV);
    if (y0 >= mb.tileV - 0.01f) { va = 1 - (y0 - mb.tileV * 0.5f) / mb.tileV; vb = 1 - (y1 - mb.tileV * 0.5f) / mb.tileV; }
    if (!(skip & 1))
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{0,0,-1},{x0/3,va},{x1/3,va},{x1/3,vb},{x0/3,vb},w);
    if (!(skip & 2))
        mb.quad({x1,y0,z1},{x0,y0,z1},{x0,y1,z1},{x1,y1,z1},{0,0,1},{x1/3,va},{x0/3,va},{x0/3,vb},{x1/3,vb},w);
    if (!(skip & 4))
        mb.quad({x0,y0,z1},{x0,y0,z0},{x0,y1,z0},{x0,y1,z1},{-1,0,0},{z1/3,va},{z0/3,va},{z0/3,vb},{z1/3,vb},w);
    if (!(skip & 8))
        mb.quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0},{1,0,0},{z0/3,va},{z1/3,va},{z1/3,vb},{z0/3,vb},w);
    if (bottomFace)
        mb.quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},{0,-1,0},{x0/3,z0/3},{x1/3,z0/3},{x1/3,z1/3},{x0/3,z1/3},w);
}

// ---- rails. A guard round an opening, or up the open side of a flight, built
// the way everything else here is: a knee wall papered like the partitions,
// under a stained timber cap. The top runs from top0 to top1 along the edge,
// so the same builder does the level rail round a hole and the balustrade that
// climbs with a stair's nosing line. `bottom` closes the underside, which is
// only ever seen looking up through the opening it guards.
static const Color RAIL_CAP = { 104, 80, 46, 254 };
static void addRailRun(WallBuilder &wa, MB &pr, float ax, float az, float bx, float bz,
                       float base, float top0, float top1, bool bottom, int voidSide = 0) {
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f) return;
    float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    const float cap = 0.055f, capW = RAIL_T + 0.02f;
    bool alongX = fabsf(ux) > 0.5f;
    auto P = [&](float a, float side, float y) { return Vector3{ ax + ux * a + nx * side, y, az + uz * a + nz * side }; };
    auto U = [&](float a) { return (alongX ? ax + ux * a : az + uz * a) / 3.0f; };
    auto V = [&](float y) { return 1.0f - y / wa.tileV; };
    float w0 = top0 - cap, w1 = top1 - cap;   // the paper stops under the cap
    for (int sg = -1; sg <= 1; sg += 2) {
        float sd = sg * RAIL_T;
        // The face over a hole is the top of the bulkhead below it, not a
        // wall standing on a floor: no baseboard, and the paper picks up
        // where the bulkhead's left off (see the soffit in bakeChunk).
        auto Vs = [&](float y) { return sg == voidSide ? 1.0f - (y + 2.0f) / wa.tileV : V(y); };
        wa.quad(P(0, sd, base), P(len, sd, base), P(len, sd, w1), P(0, sd, w0), { nx * sg, 0, nz * sg },
                { U(0), Vs(base) }, { U(len), Vs(base) }, { U(len), Vs(w1) }, { U(0), Vs(w0) }, WHITE);
    }
    for (int e = 0; e < 2; e++) {   // the two ends, square across
        float a = e ? len : 0, w = e ? w1 : w0, sg = e ? 1.0f : -1.0f;
        wa.quad(P(a, -RAIL_T, base), P(a, RAIL_T, base), P(a, RAIL_T, w), P(a, -RAIL_T, w), { ux * sg, 0, uz * sg },
                { 0, V(base) }, { 0.05f, V(base) }, { 0.05f, V(w) }, { 0, V(w) }, WHITE);
    }
    if (bottom)
        wa.quad(P(0, -RAIL_T, base), P(len, -RAIL_T, base), P(len, RAIL_T, base), P(0, RAIL_T, base), { 0, -1, 0 },
                { U(0), 0 }, { U(len), 0 }, { U(len), 0.05f }, { U(0), 0.05f }, WHITE);
    // The cap: a timber slab a little proud of both faces, sloping with the top.
    const Vector2 m = { 0.375f, 0.75f };
    float slope = (top1 - top0) / len, sn = 1.0f / sqrtf(1 + slope * slope);
    Vector3 up = { -ux * slope * sn, sn, -uz * slope * sn };
    pr.quad(P(0, -capW, top0), P(len, -capW, top1), P(len, capW, top1), P(0, capW, top0), up, m, m, m, m, RAIL_CAP);
    for (int sg = -1; sg <= 1; sg += 2) {
        float sd = sg * capW;
        pr.quad(P(0, sd, w0), P(len, sd, w1), P(len, sd, top1), P(0, sd, top0), { nx * sg, 0, nz * sg }, m, m, m, m, RAIL_CAP);
    }
    pr.quad(P(0, -capW, w0), P(len, -capW, w1), P(len, capW, w1), P(0, capW, w0), { -up.x, -up.y, -up.z }, m, m, m, m, RAIL_CAP);
    for (int e = 0; e < 2; e++) {
        float a = e ? len : 0, w = e ? w1 : w0, t = e ? top1 : top0, sg = e ? 1.0f : -1.0f;
        pr.quad(P(a, -capW, w), P(a, capW, w), P(a, capW, t), P(a, -capW, t), { ux * sg, 0, uz * sg }, m, m, m, m, RAIL_CAP);
    }
}

// ---- stairs. One step of a flight: a solid block from the floor to its tread,
// so the flight is a stair and not a ramp of paper-thin treads, with carpet on
// the tread and up the riser — Level 0's carpet goes wherever the floor goes —
// and an aluminium nosing on the edge you would catch your heel on. Built in
// the feature's local frame (u across, v along, y up) through `to`, which is
// one of four rotations, so the normals come through unmirrored.
static const Color NOSING_COL = { 168, 160, 138, 254 };
template <class ToWorld>
static void addStep(MB &fl, MB &pr, ToWorld to, float u0, float u1, float va, float vb, float y0, float y1,
                    bool risesPlusV, bool sides) {
    // va..vb along the rise; the riser is on the downhill face
    float vr = risesPlusV ? va : vb;            // where the riser stands
    float vs = risesPlusV ? -1.0f : 1.0f;       // which way it faces, in v
    auto W = [&](float u, float y, float v) { return to(u, y, v); };
    auto uvOf = [](Vector3 p) { return Vector2{ p.x / 2, p.z / 2 }; };
    Vector3 t0 = W(u0, y1, va), t1 = W(u1, y1, va), t2 = W(u1, y1, vb), t3 = W(u0, y1, vb);
    fl.quad(t0, t1, t2, t3, { 0, 1, 0 }, uvOf(t0), uvOf(t1), uvOf(t2), uvOf(t3), WHITE);
    Vector3 n0 = W(0, 0, 0), nv = W(0, 0, vs);
    Vector3 rn = { nv.x - n0.x, 0, nv.z - n0.z };
    Vector3 r0 = W(u0, y0, vr), r1 = W(u1, y0, vr), r2 = W(u1, y1, vr), r3 = W(u0, y1, vr);
    fl.quad(r0, r1, r2, r3, rn, { u0 / 2, y0 / 2 }, { u1 / 2, y0 / 2 }, { u1 / 2, y1 / 2 }, { u0 / 2, y1 / 2 }, WHITE);
    if (sides) {
        for (int e = 0; e < 2; e++) {
            float u = e ? u1 : u0;
            Vector3 nu = W(e ? 1.0f : -1.0f, 0, 0);
            Vector3 sn = { nu.x - n0.x, 0, nu.z - n0.z };
            Vector3 s0 = W(u, y0, va), s1 = W(u, y0, vb), s2 = W(u, y1, vb), s3 = W(u, y1, va);
            fl.quad(s0, s1, s2, s3, sn, { va / 2, y0 / 2 }, { vb / 2, y0 / 2 }, { vb / 2, y1 / 2 }, { va / 2, y1 / 2 }, WHITE);
        }
    }
    // the nosing: 40 mm of aluminium over the front edge of the tread
    if (y1 > 0.01f) {
        float a = vr, b = vr - vs * 0.04f;
        Vector3 q0 = W(u0 + 0.02f, y1 - 0.02f, std::min(a, b)), q1 = W(u1 - 0.02f, y1 + 0.004f, std::max(a, b));
        addSolidBox(pr, std::min(q0.x, q1.x), y1 - 0.02f, std::min(q0.z, q1.z),
                    std::max(q0.x, q1.x), y1 + 0.004f, std::max(q0.z, q1.z), NOSING_COL);
    }
}

// Where a piece of furniture goes, and the handful of numbers that make each
// one of a kind slightly different from the next.
struct PropSite {
    float cx, cz;    // cell centre, in world metres
    float floorY;    // the floor under this cell — furniture stands on it
    float rot;       // yaw, from ChunkData::propRot
    int gi, gk;      // global cell coordinates: the seed for this piece's variations
};

// Build one piece of furniture into a chunk's meshes. Most of a prop is boxes
// in the props mesh; the collapsed-tile prop also cuts a hole in the ceiling
// mesh, and every piece lays a contact shadow into the AO mesh.
//
// Each piece is deliberately a handful of boxes rather than a model: everything
// in this game is generated, and furniture that reads correctly at corridor
// range is worth far more than furniture that reads correctly close up.
static void addProp(uint8_t kind, const PropSite &site, unsigned seed, int level,
                    MB &pr, MB &ce, MB &ao, MB &fx) {
    const float pcx = site.cx, pcz = site.cz, rot = site.rot, ey = site.floorY;
    uint32_t h = ih(site.gi, site.gk, seed ^ 0xB0B5u);
    float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
    // UV regions of the prop atlas
    // cardboard: a carton's side, then its top (flap seam, tape, shipping
    // label) — two regions, because a box taped round its middle on every face
    // is not how anyone packs one. makePropsTex draws them.
    const float CU0=0.01f, CV0=0.004f, CU1=0.24f, CV1=0.58f;
    const float KU0=0.01f, KV0=0.61f, KU1=0.24f, KV1=0.99f;
    const float FU0=0.26f, FV0=0.02f, FU1=0.49f, FV1=0.48f;       // cabinet front
    const float MU0=0.26f, MV0=0.52f, MU1=0.49f, MV1=0.98f;       // plain metal
    enum class Surface { Metal, Wood, Fabric, Cardboard };
    float ca = cosf(rot), sa = sinf(rot);
    // rotated sub-box placed relative to the prop centre
    auto part = [&](float ox, float oz, float hx2, float hz2, float y0, float y1,
                    Surface surface, Color tint) {
        float u0=MU0,v0=MV0,u1=MU1,v1=MV1;
        if (surface==Surface::Wood) {u0=0.51f;v0=0.02f;u1=0.99f;v1=0.48f;}
        if (surface==Surface::Fabric) {u0=0.51f;v0=0.52f;u1=0.99f;v1=0.98f;}
        float tu0=u0,tv0=v0,tu1=u1,tv1=v1;
        if (surface==Surface::Cardboard) {u0=CU0;v0=CV0;u1=CU1;v1=CV1;tu0=KU0;tv0=KV0;tu1=KU1;tv1=KV1;}
        addPropBox(pr, pcx+ox*ca-oz*sa, pcz+ox*sa+oz*ca, rot, hx2,hz2,y0,y1,
                   u0,v0,u1,v1,tu0,tv0,tu1,tv1,tint,
                   surface==Surface::Fabric ? 0.016f : surface==Surface::Wood ? 0.006f : 0.0f);
    };
    auto roundPart = [&](float ox,float oz,float r0,float r1,float y0,float y1,Color tint) {
        const Vector2 uv{0.375f,0.75f};
        float cx=pcx+ox*ca-oz*sa,cz=pcz+ox*sa+oz*ca;
        for (int i=0;i<16;++i) {
            float a=TAU*i/16,b=TAU*(i+1)/16;
            Vector3 p0{cx+r0*cosf(a),y0,cz+r0*sinf(a)},p1{cx+r0*cosf(b),y0,cz+r0*sinf(b)};
            Vector3 p2{cx+r1*cosf(b),y1,cz+r1*sinf(b)},p3{cx+r1*cosf(a),y1,cz+r1*sinf(a)};
            float ny=(r0-r1)/std::max(0.001f,y1-y0),inv=1/sqrtf(1+ny*ny);
            pr.quad(p0,p1,p2,p3,{cosf((a+b)/2)*inv,ny*inv,sinf((a+b)/2)*inv},uv,uv,uv,uv,tint);
            pr.tri({cx,y1,cz},p3,p2,{0,1,0},uv,uv,uv,tint);
        }
    };
    auto blob = [&](float hx, float hz) {
        addContactShadow(ao, pcx, pcz, ey, rot, hx, hz);
    };
    switch (kind) {
    case PROP_BOXES: {   // box stack — on LEVEL FUN they're wrapped like presents,
                // and the packing tape reads as ribbon
        blob(0.40f, 0.40f);
        float bh = 0.55f + r1 * 0.2f, bhx = 0.34f + r2 * 0.08f;
        auto wrap = [&](int rot2) {
            if (level != 4) return WHITE;
            Color c = PARTY[(h >> rot2) % 5];
            return Color{ cl8(c.r * 0.9f + 46), cl8(c.g * 0.9f + 46), cl8(c.b * 0.9f + 46), 255 };
        };
        addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f, pcz + (r1 - 0.5f) * 0.5f, rot + r2,
                   bhx, bhx, ey, ey + bh, CU0, CV0, CU1, CV1, KU0, KV0, KU1, KV1, wrap(5));
        if (r2 > 0.35f)   // second box on top, skewed
            addPropBox(pr, pcx + (r3 - 0.5f) * 0.5f + 0.06f, pcz + (r1 - 0.5f) * 0.5f - 0.05f,
                       rot + r2 + 0.5f, bhx * 0.8f, bhx * 0.8f, ey + bh, ey + bh + 0.5f,
                       CU0, CV0, CU1, CV1, KU0, KV0, KU1, KV1, wrap(9));
        if (r1 > 0.6f)    // third box beside
            addPropBox(pr, pcx + 0.62f, pcz + 0.3f, rot + r3 * 2, 0.27f, 0.27f, ey, ey + 0.5f,
                       CU0, CV0, CU1, CV1, KU0, KV0, KU1, KV1, wrap(13));
        break;
    }
    case PROP_CABINET:     // filing cabinet
        blob(0.34f, 0.42f);
        addPropBox(pr, pcx, pcz, rot, 0.26f, 0.34f, ey, ey + 1.32f,
                   FU0, FV0, FU1, FV1, MU0, MV0, MU1, MV1);
        for (int i=0;i<3;++i)
            part(0,0.343f,0.075f,0.008f,ey+0.23f+i*0.40f,ey+0.25f+i*0.40f,Surface::Metal,{187,191,186,254});
        break;
    case PROP_TABLE: {   // folding table
        blob(0.58f, 0.40f);
        float ty = 0.72f;
        addPropBox(pr, pcx, pcz, rot, 0.62f, 0.40f, ey + ty - 0.04f, ey + ty,
                   MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2) {
            float ox = lx * 0.54f, oz = lz * 0.32f;
            addPropBox(pr, pcx + ox * ca - oz * sa, pcz + ox * sa + oz * ca, rot,
                       0.03f, 0.03f, ey, ey + ty - 0.04f, MU0, MV0, MU1, MV1, MU0, MV0, MU1, MV1);
        }
        break;
    }
    case PROP_FALLEN_TILE: {   // collapsed ceiling: dark hole above, tile leaning below, debris
        // 2.994 is just under a 3 m ceiling. Only Level 0 generates this prop,
        // and Level 0 is the 3 m one — see LEVELS in levels.cpp.
        Color hole = { 12, 11, 9, 51 };
        ce.quad({pcx-0.85f,2.994f,pcz-0.85f},{pcx-0.85f,2.994f,pcz+0.85f},
                {pcx+0.85f,2.994f,pcz+0.85f},{pcx+0.85f,2.994f,pcz-0.85f},{0,-1,0},
                {0,0},{0,1},{1,1},{1,0}, hole);
        float bx0 = pcx - 0.58f * ca, bz0 = pcz - 0.58f * sa;   // base edge on floor
        float tx = pcx + 0.35f * ca, tz = pcz + 0.35f * sa;     // top edge, lifted
        Vector3 a = { bx0 - 0.58f * sa, ey + 0.02f, bz0 + 0.58f * ca };
        Vector3 b = { bx0 + 0.58f * sa, ey + 0.02f, bz0 - 0.58f * ca };
        Vector3 c2 = { tx + 0.58f * sa, ey + 0.42f, tz - 0.58f * ca };
        Vector3 dq = { tx - 0.58f * sa, ey + 0.42f, tz + 0.58f * ca };
        ce.quad(a, b, c2, dq, { -ca * 0.5f, 0.87f, -sa * 0.5f },
                {0.05f,0.45f},{0.45f,0.45f},{0.45f,0.05f},{0.05f,0.05f}, WHITE);
        Color deb = { 110, 105, 95, 255 };
        ce.quad({pcx+0.4f,ey+0.012f,pcz+0.5f},{pcx+0.75f,ey+0.012f,pcz+0.55f},
                {pcx+0.7f,ey+0.012f,pcz+0.85f},{pcx+0.38f,ey+0.012f,pcz+0.8f},{0,1,0},
                {0.1f,0.1f},{0.3f,0.1f},{0.3f,0.3f},{0.1f,0.3f}, deb);
        ce.quad({pcx-0.7f,ey+0.012f,pcz-0.35f},{pcx-0.45f,ey+0.012f,pcz-0.42f},
                {pcx-0.4f,ey+0.012f,pcz-0.2f},{pcx-0.68f,ey+0.012f,pcz-0.15f},{0,1,0},
                {0.3f,0.3f},{0.45f,0.3f},{0.45f,0.45f},{0.3f,0.45f}, deb);
        break;
    }
    case PROP_COUCH: {   // couch: mustard upholstery gone grey, facing nothing in particular
        blob(0.74f, 0.48f);
        Color uph = { 172, 152, 96, 255 };
        part(0, 0.10f, 0.78f, 0.42f, ey + 0.16f, ey + 0.44f, Surface::Fabric, uph);   // seat
        part(0, -0.36f, 0.78f, 0.14f, ey + 0.16f, ey + 0.92f, Surface::Fabric, uph);  // backrest
        part(-0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);      // arms
        part( 0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);
        part(0, 0.10f, 0.74f, 0.38f, ey, ey + 0.16f, Surface::Fabric, Color{ 120, 106, 70, 255 });
        // Three separate seat cushions and piping, inside the same collision box.
        for (int i=-1;i<=1;++i) {
            part(i*0.41f,0.12f,0.196f,0.34f,ey+0.44f,ey+0.49f,Surface::Fabric,uph);
            part(i*0.41f,0.454f,0.192f,0.006f,ey+0.452f,ey+0.461f,Surface::Fabric,{213,191,131,255});
        }
        break;
    }
    case PROP_ARMOIRE:     // armoire: a wardrobe looming where no bedroom is
        blob(0.54f, 0.46f);
        part(0, 0, 0.44f, 0.36f, ey, ey + 1.78f, Surface::Wood, Color{ 118, 82, 58, 255 });
        part(0, 0, 0.48f, 0.40f, ey + 1.78f, ey + 1.90f, Surface::Wood, Color{ 92, 63, 44, 255 });  // cornice
        part(0, 0.37f, 0.015f, 0.015f, ey + 0.85f, ey + 1.0f, Surface::Metal, Color{ 190, 170, 110, 255 }); // handles
        for (int i=-1;i<=1;i+=2)
            part(i*0.218f,0.362f,0.193f,0.003f,ey+0.15f,ey+1.67f,Surface::Wood,{155,113,79,255});
        break;
    case PROP_LAMP:     // floor lamp, shade askew, never lit
        blob(0.22f, 0.22f);
        part(0, 0, 0.14f, 0.14f, ey, ey + 0.05f, Surface::Metal, Color{ 66, 62, 60, 255 });
        part(0, 0, 0.025f, 0.025f, ey, ey + 1.34f, Surface::Metal, Color{ 66, 62, 60, 255 });
        roundPart(0.05f,0,0.17f,0.10f,ey+1.30f,ey+1.60f,{214,190,142,254});
        break;
    case PROP_NIGHTSTAND:     // nightstand, nowhere near a bed. usually.
        blob(0.36f, 0.36f);
        part(0, 0, 0.26f, 0.26f, ey, ey + 0.55f, Surface::Wood, Color{ 126, 90, 62, 255 });
        part(0, 0, 0.30f, 0.30f, ey + 0.55f, ey + 0.60f, Surface::Wood, Color{ 104, 74, 50, 255 });
        break;
    case PROP_BED: {   // bed: bare stained mattress, headboard against nothing
        blob(0.60f, 1.02f);
        Color wd = { 110, 78, 54, 255 };
        part(0, 0, 0.52f, 0.92f, ey + 0.12f, ey + 0.26f, Surface::Wood, wd);          // frame
        part(0, 0.04f, 0.48f, 0.86f, ey + 0.26f, ey + 0.46f, Surface::Fabric, Color{ 216, 208, 188, 255 }); // mattress
        part(0, -0.97f, 0.52f, 0.05f, ey, ey + 0.95f, Surface::Wood, wd);             // headboard
        break;
    }
    case PROP_PARTY_TABLE: {  // party table: paper cloth, a cake nobody cut, cups nobody drank
        blob(0.52f, 0.52f);
        float ty = 0.74f;
        uint32_t th = ih(site.gi, site.gk, seed ^ 0xCAFEu);
        Color cloth = PARTY[th % 5];
        part(0, 0, 0.55f, 0.55f, ey + ty - 0.05f, ey + ty, Surface::Fabric, cloth);
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            part(lx * 0.44f, lz * 0.44f, 0.035f, 0.035f, ey, ey + ty - 0.05f,
                 Surface::Metal, Color{ 120, 118, 112, 255 });
        part(0, 0, 0.17f, 0.17f, ey + ty, ey + ty + 0.16f, Surface::Metal, Color{ 238, 232, 220, 255 });   // cake
        part(0, 0, 0.11f, 0.11f, ey + ty + 0.16f, ey + ty + 0.26f, Surface::Metal, Color{ 232, 152, 172, 255 });
        part(0, 0, 0.013f, 0.013f, ey + ty + 0.26f, ey + ty + 0.37f, Surface::Metal, Color{ 240, 226, 172, 255 }); // candle
        {   // paper cups set out around the cake, in party colours
            int ncup = 3 + (th % 4);
            for (int c = 0; c < ncup; c++) {
                uint32_t ch = th * 2654435761u + (uint32_t)c * 40503u;
                float ang = (ch & 0xFFFF) / 65535.0f * TAU;
                float rad = 0.30f + ((ch >> 16) & 0xFF) / 255.0f * 0.15f;
                part(cosf(ang) * rad, sinf(ang) * rad, 0.04f, 0.04f,
                     ey + ty, ey + ty + 0.09f, Surface::Metal, PARTY[(ch >> 5) % 5]);
            }
        }
        {   // ...and the candle is still lit. nobody lit it. two crossed
            // emissive fins make a little flame that survives blackouts
            auto fpt = [&](float lx, float ly2, float lz) {
                return Vector3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
            };
            Color flame = { 255, 196, 110, 70 };   // alpha <0.4: raw emissive in the shader
            float fy0 = ey + ty + 0.37f, fy1 = fy0 + 0.055f;
            pr.quad(fpt(-0.022f, fy0, 0), fpt(0.022f, fy0, 0), fpt(0.013f, fy1, 0), fpt(-0.013f, fy1, 0),
                    { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, flame);
            pr.quad(fpt(0, fy0, -0.022f), fpt(0, fy0, 0.022f), fpt(0, fy1, 0.013f), fpt(0, fy1, -0.013f),
                    { ca, 0, sa }, {0,1},{1,1},{1,0},{0,0}, flame);
        }
        break;
    }
    case PROP_VENDING: {  // vending machine: still stocked, still humming, takes doubloons
        // A glass-front drink machine, built at its real size (0.88 m wide,
        // 1.83 high, 0.72 deep) from a cabinet, a door cut into the pieces
        // round its two openings, and the painted front in the fixtures atlas
        // (drawVendingFront, laid out by VEND_* in textures.h). The cans are
        // painted on the back of the cabinet, 6 cm behind the glass, and the
        // shelves in front of them are real, so they slide across the cans as
        // you walk past. Most machines are still lit: the header, the display,
        // the price strips and the inside of the cabinet take vertex alpha
        // 240, which the shader lights from behind. One in six has died.
        blob(0.50f, 0.42f);
        bool lit = (h >> 24) % 6 != 0;
        const unsigned char GLOW = lit ? 240 : 254;
        // In door coordinates x runs to the viewer's right as they face the
        // front, which is local -x: seen from the front, local +x is on the left.
        auto P = [&](float x, float y, float lz) {
            return Vector3{ pcx - x * ca - lz * sa, ey + y, pcz - x * sa + lz * ca };
        };
        const Vector3 fn = { sa, 0, -ca };                     // the front's normal
        // No two visible faces here share a plane, and nothing is layered a
        // hair in front of anything else. The first version stood every painted
        // face 1.5 mm off a door box's own front, the display 1.5 mm off the
        // paint and the cans 1.5 mm off the cabinet: fine on a desktop GPU, and
        // on a phone's depth buffer the dark box fronts flickered through the
        // keypad, the door and the cans at any range. So the boxes leave out the
        // faces something else covers (FRONT, and so on, below), the paint is
        // the front, and the display is cut into the door rather than laid on it.
        enum { FRONT = 1, BACK = 2, SIDES = 4, TOP = 8, BOTTOM = 16, ALL = 31 };
        auto sbox = [&](float x0, float x1, float y0, float y1, float z0, float z1, Color c, int faces = ALL) {
            Vector3 a = P(x0, y0, z0), b = P(x1, y1, z1);
            float wx0 = std::min(a.x, b.x), wx1 = std::max(a.x, b.x), wz0 = std::min(a.z, b.z), wz1 = std::max(a.z, b.z);
            float wy0 = y0 + ey, wy1 = y1 + ey;
            const Vector2 u = { 0.375f, 0.75f };               // the plain metal addSolidBox samples
            auto emit = [&](Vector3 q0, Vector3 q1, Vector3 q2, Vector3 q3, Vector3 n) {
                float d = n.x * fn.x + n.z * fn.z;
                int kind = n.y > 0.5f ? TOP : n.y < -0.5f ? BOTTOM : d > 0.5f ? FRONT : d < -0.5f ? BACK : SIDES;
                if (faces & kind) fx.quad(q0, q1, q2, q3, n, u, u, u, u, c);
            };
            emit({wx0,wy0,wz0},{wx1,wy0,wz0},{wx1,wy1,wz0},{wx0,wy1,wz0},{0,0,-1});
            emit({wx1,wy0,wz1},{wx0,wy0,wz1},{wx0,wy1,wz1},{wx1,wy1,wz1},{0,0,1});
            emit({wx0,wy0,wz1},{wx0,wy0,wz0},{wx0,wy1,wz0},{wx0,wy1,wz1},{-1,0,0});
            emit({wx1,wy0,wz0},{wx1,wy0,wz1},{wx1,wy1,wz1},{wx1,wy1,wz0},{1,0,0});
            emit({wx0,wy1,wz0},{wx1,wy1,wz0},{wx1,wy1,wz1},{wx0,wy1,wz1},{0,1,0});
            emit({wx0,wy0,wz1},{wx1,wy0,wz1},{wx1,wy0,wz0},{wx0,wy0,wz0},{0,-1,0});
        };
        // a painted face on the plane lz, sampling the door where it covers it
        auto face = [&](float x0, float y0, float x1, float y1, float lz, unsigned char alpha) {
            fx.quad(P(x0, y0, lz), P(x1, y0, lz), P(x1, y1, lz), P(x0, y1, lz), fn,
                    vendUV(x0, y0), vendUV(x1, y0), vendUV(x1, y1), vendUV(x0, y1),
                    Color{ 255, 255, 255, alpha });
        };
        const float ZF = -0.362f, ZBODY = -0.30f, ZBACK = VEND_DEPTH_BACK;
        // the cabinet: painted steel sides in one of the colours they come in
        static const Color SIDE_COLS[4] = { { 52, 54, 60, 254 }, { 118, 30, 28, 254 },
                                            { 34, 48, 84, 254 }, { 178, 172, 158, 254 } };
        Color side = SIDE_COLS[(h >> 20) & 3];
        const Color door = { 48, 50, 55, 254 }, black = { 18, 18, 20, 254 };
        sbox(-0.42f, 0.42f, 0.0f, VEND_Y0, -0.33f, ZBACK - 0.02f, black, ALL & ~TOP);   // plinth, set back
        // its front is covered everywhere: by the door, the cans and the flap
        sbox(-VEND_HW, VEND_HW, VEND_Y0, VEND_Y1, ZBODY, ZBACK, side, ALL & ~FRONT & ~BOTTOM);
        // the door, around its two openings (see VEND_WIN / VEND_BIN); the
        // paint is its front, and its back is against the cabinet
        const VendRect &w = VEND_WIN, &bn = VEND_BIN, &hd = VEND_HEADER;
        const int DOOR = ALL & ~FRONT & ~BACK;
        sbox(-VEND_HW, w.x0, VEND_Y0, hd.y0, ZF, ZBODY, door, DOOR);               // left stile
        sbox(w.x1, VEND_HW, VEND_Y0, hd.y0, ZF, ZBODY, door, DOOR);                // control column
        sbox(-VEND_HW, VEND_HW, hd.y0, VEND_Y1, ZF, ZBODY, door, DOOR);            // header
        sbox(w.x0, w.x1, bn.y1, w.y0, ZF, ZBODY, door, DOOR);                      // rail between
        sbox(w.x0, w.x1, VEND_Y0, bn.y0, ZF, ZBODY, door, DOOR);                   // kick rail
        const VendRect &dp = VEND_DISP;
        face(-VEND_HW, VEND_Y0, w.x0, hd.y0, ZF, 254);
        // the control column, in four pieces round the display
        face(w.x1, VEND_Y0, VEND_HW, dp.y0, ZF, 254);
        face(w.x1, dp.y1, VEND_HW, hd.y0, ZF, 254);
        face(w.x1, dp.y0, dp.x0, dp.y1, ZF, 254);
        face(dp.x1, dp.y0, VEND_HW, dp.y1, ZF, 254);
        face(dp.x0, dp.y0, dp.x1, dp.y1, ZF, GLOW);
        face(-VEND_HW, hd.y0, VEND_HW, VEND_Y1, ZF, GLOW);
        face(w.x0, bn.y1, w.x1, w.y0, ZF, 254);
        face(w.x0, VEND_Y0, w.x1, bn.y0, ZF, 254);
        // what you touch stands proud of the paint: the coin return lever, the
        // lock, and the lip of the coin cup (backs against the door, left out)
        const Color chrome = { 186, 188, 194, 254 };
        sbox(0.232f, 0.288f, 1.090f, 1.125f, ZF - 0.012f, ZF, chrome, ALL & ~BACK);
        sbox(0.385f, 0.405f, 0.545f, 0.565f, ZF - 0.016f, ZF, chrome, ALL & ~BACK);
        sbox(0.235f, 0.385f, 0.330f, 0.345f, ZF - 0.022f, ZF, chrome, ALL & ~BACK);
        // the push flap, hung 2 cm inside the opening
        face(bn.x0, bn.y0, bn.x1, bn.y1, -0.342f, 254);
        // behind the glass: the lit back of the cabinet with the cans painted
        // on it (where the cabinet's own front would be), and the six shelves in
        // front of them. A shelf is its top, its underside and its price strip:
        // its ends would lie in the stiles' faces and its back in the cans.
        const float ZLIP = -0.345f;
        face(w.x0, w.y0, w.x1, w.y1, ZBODY, GLOW);
        for (int r = 0; r < VEND_ROWS; r++) {
            float yr = VEND_ROW0 + r * VEND_ROWP;
            sbox(w.x0, w.x1, yr - 0.022f, yr + 0.004f, ZLIP, ZBODY, Color{ 58, 60, 64, 254 }, TOP | BOTTOM);
            float v0 = (VEND_STRIP_PX[1] + r * 12 + VEND_STRIP_PX[3]) / (float)FIX_ATLAS_H;
            float v1 = (VEND_STRIP_PX[1] + r * 12) / (float)FIX_ATLAS_H;
            float u0 = VEND_STRIP_PX[0] / (float)FIX_ATLAS_W, u1 = (VEND_STRIP_PX[0] + VEND_STRIP_PX[2]) / (float)FIX_ATLAS_W;
            fx.quad(P(w.x0, yr - 0.022f, ZLIP), P(w.x1, yr - 0.022f, ZLIP),
                    P(w.x1, yr + 0.004f, ZLIP), P(w.x0, yr + 0.004f, ZLIP), fn,
                    { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 }, Color{ 255, 255, 255, GLOW });
        }
        // and the glass itself, last, so it blends over everything behind it
        // (vertex alpha 100: the shader's window-glass path, a sheen at an angle)
        const Vector2 m = { 0.375f, 0.75f };
        fx.quad(P(w.x0, w.y0, -0.352f), P(w.x1, w.y0, -0.352f), P(w.x1, w.y1, -0.352f), P(w.x0, w.y1, -0.352f),
                fn, m, m, m, m, Color{ 7, 8, 9, 100 });
        break;
    }
    case PROP_DESK: {  // office desk: chair shoved back, monitor long dead. someone worked here
        blob(0.72f, 0.52f);
        Color wd = { 104, 80, 56, 255 };
        part(0, -0.12f, 0.62f, 0.34f, ey + 0.70f, ey + 0.74f, Surface::Wood, wd);      // desktop
        part(-0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);         // pedestals
        part( 0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);
        part(0.08f, -0.22f, 0.19f, 0.035f, ey + 0.76f, ey + 1.08f, Surface::Metal, Color{ 30, 30, 34, 255 }); // monitor
        part(0.08f, -0.14f, 0.06f, 0.06f, ey + 0.74f, ey + 0.77f, Surface::Metal, Color{ 38, 38, 42, 255 });  // its foot
        part(-0.30f, -0.14f, 0.11f, 0.08f, ey + 0.74f, ey + 0.765f, Surface::Metal, Color{ 200, 196, 186, 255 }); // papers
        part(0.02f + r1 * 0.1f, 0.44f, 0.20f, 0.20f, ey + 0.40f, ey + 0.46f, Surface::Fabric, Color{ 52, 50, 54, 255 }); // chair seat
        part(0.02f + r1 * 0.1f, 0.62f, 0.20f, 0.04f, ey + 0.46f, ey + 0.96f, Surface::Fabric, Color{ 52, 50, 54, 255 }); // backrest
        part(0.02f + r1 * 0.1f, 0.44f, 0.035f, 0.035f, ey, ey + 0.40f, Surface::Metal, Color{ 72, 72, 76, 255 });        // post
        break;
    }
    case PROP_SHELVING: {  // steel shelving, half-emptied in a hurry
        blob(0.68f, 0.32f);
        Color mt = { 132, 136, 142, 255 };
        for (int s2 = 0; s2 <= 3; s2++)
            part(0, 0, 0.60f, 0.24f, ey + 0.08f + s2 * 0.55f, ey + 0.12f + s2 * 0.55f, Surface::Metal, mt);
        // corner posts, not full-depth panels — side-on you see *through* the rack
        for (int ux = -1; ux <= 1; ux += 2) for (int uz = -1; uz <= 1; uz += 2)
            part(ux * 0.575f, uz * 0.215f, 0.03f, 0.03f, ey, ey + 1.80f, Surface::Metal, mt);
        part(-0.25f, 0.0f, 0.16f, 0.16f, ey + 0.12f, ey + 0.44f, Surface::Cardboard, Color{ 168, 138, 100, 255 });  // what's left
        part( 0.30f, 0.02f, 0.14f, 0.14f, ey + 0.67f, ey + 0.94f, Surface::Cardboard, Color{ 150, 122, 88, 255 });
        if (r2 > 0.4f)
            part(-0.06f, -0.02f, 0.12f, 0.12f, ey + 1.22f, ey + 1.44f, Surface::Cardboard, Color{ 174, 146, 106, 255 });
        break;
    }
    case PROP_COOLER: {  // water cooler. the water is not almond
        blob(0.30f, 0.30f);
        part(0, 0, 0.19f, 0.19f, ey, ey + 0.94f, Surface::Metal, Color{ 204, 206, 210, 255 });   // body
        roundPart(0,0,0.10f,0.125f,ey+0.94f,ey+1.24f,{150,186,214,254});
        roundPart(0,0,0.125f,0.10f,ey+1.24f,ey+1.28f,{150,186,214,254});
        for (int i=0;i<3;++i) roundPart(0,0,0.128f,0.128f,ey+1.01f+i*0.07f,ey+1.025f+i*0.07f,{179,206,223,254});
        part(0, 0.205f, 0.05f, 0.02f, ey + 0.58f, ey + 0.66f, Surface::Metal, Color{ 88, 90, 94, 255 });  // tap
        break;
    }
    case PROP_PLANT: {  // potted plant. still green. nobody waters it
        blob(0.26f, 0.26f);
        part(0, 0, 0.17f, 0.17f, ey, ey + 0.09f, Surface::Metal, Color{ 120, 70, 48, 255 });    // saucer
        roundPart(0,0,0.105f,0.145f,ey+0.02f,ey+0.30f,{146,88,58,254});
        roundPart(0,0,0.153f,0.153f,ey+0.285f,ey+0.32f,{172,104,70,254});
        part(0, 0, 0.032f, 0.032f, ey + 0.32f, ey + 0.88f, Surface::Metal, Color{ 76, 66, 44, 255 });   // stem
        for (int i=0;i<10;++i) {
            float angle=rot+i*2.39996f,yy=ey+0.58f+(i%4)*0.13f;
            float dx=cosf(angle)*0.28f,dz=sinf(angle)*0.28f;
            Vector3 base{pcx,yy,pcz},tip{pcx+dx,yy+0.17f,pcz+dz};
            Vector3 left{pcx+dx*0.55f-dz*0.20f,yy+0.14f,pcz+dz*0.55f+dx*0.20f};
            Vector3 right{pcx+dx*0.55f+dz*0.20f,yy+0.14f,pcz+dz*0.55f-dx*0.20f};
            Vector2 uv{0.375f,0.75f};
            pr.tri(base,left,tip,{0,1,0},uv,uv,uv,{64,111,53,254});
            pr.tri(base,tip,right,{0,1,0},uv,uv,uv,{48,88,39,254});
        }
        break;
    }
    }
}

// Spalled concrete with the rebar showing — "exposed rebar" is in the first
// line of Level 1's description, and a warehouse of poured concrete gets it
// wherever water has got at the steel and the cover has blown off. A ragged
// cavity pressed just off the face (two fans, the deeper one darker), and the
// bars themselves as real geometry standing proud of it: two verticals and a
// tie or two across, rust-coloured. The face is axis-aligned, so `u` (the
// horizontal tangent) is always x or z and the bars stay boxes.
static void addSpall(MB &fx, MB &pr, Vector3 c, Vector3 n, Vector3 u, float rw, float rh, uint32_t h) {
    Rng r(((uint64_t)h << 1) ^ 0x5BA11ULL);
    const Vector2 uv = { 0.375f, 0.75f };                // the fixtures atlas's plain metal, darkened
    auto ring = [&](float scale, float off, Color col) {
        const int N = 11;
        Vector3 pts[N];
        for (int i = 0; i < N; i++) {
            float a = TAU * i / N, k = scale * (0.62f + 0.38f * r.f01());
            float du = cosf(a) * rw * k, dv = sinf(a) * rh * k;
            pts[i] = { c.x + u.x * du + n.x * off, c.y + dv, c.z + u.z * du + n.z * off };
        }
        Vector3 m = { c.x + n.x * off, c.y, c.z + n.z * off };
        for (int i = 0; i < N; i++) {
            // wound to face along n whichever way the face points
            float cr = (u.x * n.z - u.z * n.x);
            if (cr > 0) fx.tri(m, pts[i], pts[(i + 1) % N], n, uv, uv, uv, col);
            else        fx.tri(m, pts[(i + 1) % N], pts[i], n, uv, uv, uv, col);
        }
    };
    ring(1.0f, 0.0025f, Color{ 104, 98, 90, 254 });
    ring(0.62f, 0.0035f, Color{ 62, 58, 52, 254 });
    Color rust = { 104, 58, 34, 254 };
    const float R = 0.0085f, out = 0.014f;
    for (int b = -1; b <= 1; b += 2) {                   // two verticals
        float du = b * rw * (0.25f + 0.12f * r.f01());
        float x0 = c.x + u.x * du + n.x * out, z0 = c.z + u.z * du + n.z * out;
        addSolidBox(pr, x0 - R, c.y - rh * 0.75f, z0 - R, x0 + R, c.y + rh * 0.8f, z0 + R, rust);
    }
    int ties = 1 + (int)(r.f01() * 2);
    for (int t = 0; t < ties; t++) {                      // and the ties across them
        float y = c.y + (t - 0.5f * (ties - 1)) * rh * 0.55f;
        float ax = c.x - u.x * rw * 0.55f + n.x * (out + 0.012f), az = c.z - u.z * rw * 0.55f + n.z * (out + 0.012f);
        float bx = c.x + u.x * rw * 0.55f + n.x * (out + 0.012f), bz = c.z + u.z * rw * 0.55f + n.z * (out + 0.012f);
        addSolidBox(pr, std::min(ax, bx) - R, y - R, std::min(az, bz) - R,
                    std::max(ax, bx) + R, y + R, std::max(az, bz) + R, rust);
    }
}

// ---- a Level 1 exit. The article lists Level 1's ways out as doors, and the
// first of them is "doors with unique symbols at the end of corridors with
// fluorescent lights". So an exit here is a steel door — frame, a heavy leaf
// pinned open against the wall, the glow of wherever it goes in the opening —
// with a symbol painted over it that no other door has (strokes between
// points on a 3x3 lattice, chosen by the edge's hash, so every one differs),
// and a caged bulkhead lamp above that. Cursed ones get their symbol in red.
// `ax` is 0 for a wall running along x at z = w0, 1 for one along z at x = w0;
// `a0` is the cell's low corner along the wall and `base` the floor under it.
static void addSymbolDoor(MB &pr, MB &fx, int ax, float a0, float w0, float base, bool cursed, uint32_t h,
                          bool leafRoom) {
    auto P = [&](float a, float y, float n) {             // wall-local -> world
        return ax == 0 ? Vector3{ a, y, w0 + n } : Vector3{ w0 + n, y, a };
    };
    auto box = [&](MB &mb, float aL, float y0, float nL, float aH, float y1, float nH, Color c) {
        Vector3 lo = P(aL, y0, nL), hi = P(aH, y1, nH);
        addSolidBox(mb, std::min(lo.x, hi.x), y0, std::min(lo.z, hi.z), std::max(lo.x, hi.x), y1, std::max(lo.z, hi.z), c);
    };
    Color steel = { 84, 88, 86, 254 }, dark = { 50, 52, 52, 254 };
    const float o0 = a0 + 0.35f, o1 = a0 + 1.65f, T = 0.05f;
    for (int sd = -1; sd <= 1; sd += 2) {                 // the frame, proud of both faces
        float nf = sd * (WT + T * 0.5f);
        box(pr, o0 - 0.09f, base, nf - T * 0.5f, o0, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o1, base, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o0 - 0.09f, base + 2.30f, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
    }
    if (leafRoom) {   // the leaf, swung back flat against the -side face beside the frame
        float nf = -(WT + 0.035f);
        box(pr, o1 + 0.10f, base + 0.02f, nf - 0.022f, o1 + 1.38f, base + 2.27f, nf + 0.022f, Color{ 96, 104, 100, 254 });
        box(pr, o1 + 0.22f, base + 1.00f, nf - 0.05f, o1 + 1.26f, base + 1.06f, nf - 0.02f, dark);   // push bar
        box(pr, o1 + 0.40f, base + 1.55f, nf - 0.03f, o1 + 1.08f, base + 1.95f, nf - 0.022f, Color{ 60, 70, 76, 254 });   // wired glass
    }
    // the symbol: strokes of paint between lattice points, both faces
    Rng r(((uint64_t)h << 1) ^ 0x51B01ULL);
    int pts[6], np = 4 + r.ri(0, 2);
    for (int i = 0; i < np; i++) pts[i] = r.ri(0, 8);
    Color paint = cursed ? Color{ 210, 40, 30, 254 } : Color{ 250, 238, 190, 254 };
    const Vector2 uv = { 0.375f, 0.75f };
    float sc = 0.30f, cA = (o0 + o1) * 0.5f, cY = base + 2.78f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float nf = sd * (WT + 0.0025f);
        for (int i = 0; i + 1 < np; i++) {
            float pa = cA + ((pts[i] % 3) - 1) * sc, py = cY + ((pts[i] / 3) - 1) * sc;
            float qa = cA + ((pts[i + 1] % 3) - 1) * sc, qy = cY + ((pts[i + 1] / 3) - 1) * sc;
            if (pts[i] == pts[i + 1]) { qa += sc * 0.6f; }
            float da = qa - pa, dy = qy - py, L = sqrtf(da * da + dy * dy) + 1e-4f;
            float ta = -dy / L * 0.065f, ty = da / L * 0.065f;          // half a brush width, across
            Vector3 A = P(pa - ta, py - ty, nf), B = P(qa - ta, qy - ty, nf), C = P(qa + ta, qy + ty, nf), D = P(pa + ta, py + ty, nf);
            Vector3 nn = ax == 0 ? Vector3{ 0, 0, (float)sd } : Vector3{ (float)sd, 0, 0 };
            bool flip = (ax == 0) ? sd > 0 : sd < 0;
            if (flip) fx.quad(B, A, D, C, nn, uv, uv, uv, uv, paint);
            else      fx.quad(A, B, C, D, nn, uv, uv, uv, uv, paint);
        }
        // a dot where the stroke started: whoever painted it began there
        float pa = cA + ((pts[0] % 3) - 1) * sc, py = cY + ((pts[0] / 3) - 1) * sc;
        box(fx, pa - 0.04f, py - 0.04f, nf - 0.001f, pa + 0.04f, py + 0.04f, nf + 0.001f, paint);
        // the caged bulkhead over it all: a warm glass lens (raw emissive) in a wire cage
        float lf = sd * (WT + 0.07f);
        box(pr, cA - 0.13f, base + 3.18f, sd * WT, cA + 0.13f, base + 3.36f, lf, dark);
        box(pr, cA - 0.09f, base + 3.20f, lf - sd * 0.03f, cA + 0.09f, base + 3.34f, lf + sd * 0.012f,
            Color{ 255, 206, 140, 60 });
        for (int b = -1; b <= 1; b++)
            box(pr, cA + b * 0.07f - 0.006f, base + 3.18f, lf + sd * 0.012f, cA + b * 0.07f + 0.006f, base + 3.36f,
                lf + sd * 0.024f, dark);
    }
}

// ---- the Manila Room, built. Everything here follows the wiki entry and the
// renders made from it: wooden floorboards, walls papered the colour of a
// manila folder, "one octagonal table and two chairs", cupboards under the
// table, "a wooden entrance door on each wall", and — from the Level 0 article
// — "a table illuminated by a lone chandelier". rx/rz is the room's centre
// (a cell corner), cy its ceiling. Floor is 0: generate() keeps it flat.
static void addManilaRoom(MB &pr, MB &fx, MB &ao, float rx, float rz, float cy, uint32_t h) {
    const float R = 4.0f, IN = R - WT;       // half-size of the room, and of its inside
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;   // props atlas: wood
    auto wood = [&](MB &mb, float cxp, float czp, float yaw, float hx, float hz, float y0, float y1, Color t) {
        addPropBox(mb, cxp, czp, yaw, hx, hz, y0, y1, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1, t, 0.004f);
    };
    Rng r(((uint64_t)h << 1) ^ 0x3A11AULL);

    // Floorboards: 145 mm strips running east-west, broken at random lengths
    // and staggered row to row, over a dark underlay that shows as the gaps.
    // Alpha 255, so the props detail map gives the grain its relief, and the
    // wood's own gloss mask gives the varnish a sheen off the chandelier.
    {
        const Vector3 up = { 0, 1, 0 };
        const Vector2 u = { 0.375f, 0.75f };
        pr.quad({rx-R,0.002f,rz-R},{rx-R,0.002f,rz+R},{rx+R,0.002f,rz+R},{rx+R,0.002f,rz-R}, up,
                u, u, u, u, Color{ 34, 24, 18, 254 });
        const float PW = 0.145f;
        for (float z = rz - R; z < rz + R - 0.01f; z += PW) {
            float z1 = std::min(z + PW, rz + R);
            float x = rx - R - r.f01() * 1.2f;
            while (x < rx + R) {
                float len = 0.9f + r.f01() * 1.6f;
                float xa = std::max(x, rx - R), xb = std::min(x + len, rx + R);
                if (xb - xa > 0.05f) {
                    float ou = WU0 + r.f01() * (WU1 - WU0 - 0.26f), ov = WV0 + r.f01() * (WV1 - WV0 - 0.05f);
                    float k = 0.80f + 0.34f * r.f01();
                    Color t = { cl8(150 * k), cl8(104 * k), cl8(70 * k), 255 };
                    float ua = ou, ub = ou + (xb - xa) * 0.11f, va = ov, vb = ov + 0.03f;
                    pr.quad({xa+0.003f,0.005f,z+0.003f},{xa+0.003f,0.005f,z1-0.003f},
                            {xb-0.003f,0.005f,z1-0.003f},{xb-0.003f,0.005f,z+0.003f}, up,
                            {ua,va},{ua,vb},{ub,vb},{ub,va}, t);
                }
                x += len;
            }
        }
    }

    // Manila paper on the four inside faces, as 500 mm tiles pressed 1.5 mm
    // off the plaster, from the top of the skirting to the ceiling and round
    // each doorway. Tiles are anchored to the world grid so the lattice runs
    // unbroken across the cuts.
    const FixtureRect &M = FIXTURES[FIX_MANILA];
    auto tileRect = [&](int axis, float fixed, float nsgn, float a0, float a1, float y0, float y1) {
        const float T = 0.5f;
        for (float a = floorf(a0 / T) * T; a < a1 - 1e-4f; a += T)
            for (float y = floorf(y0 / T) * T; y < y1 - 1e-4f; y += T) {
                float qa0 = std::max(a, a0), qa1 = std::min(a + T, a1);
                float qy0 = std::max(y, y0), qy1 = std::min(y + T, y1);
                if (qa1 - qa0 < 1e-3f || qy1 - qy0 < 1e-3f) continue;
                float ua = M.u0 + (M.u1 - M.u0) * (qa0 - a) / T, ub = M.u0 + (M.u1 - M.u0) * (qa1 - a) / T;
                float va = M.v1 - (M.v1 - M.v0) * (qy0 - y) / T, vb = M.v1 - (M.v1 - M.v0) * (qy1 - y) / T;
                Color c = { 255, 255, 255, 254 };
                if (axis == 0) {   // face at z = fixed, running along x
                    Vector3 n = { 0, 0, nsgn };
                    if (nsgn > 0) fx.quad({qa1,qy0,fixed},{qa0,qy0,fixed},{qa0,qy1,fixed},{qa1,qy1,fixed}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                    else          fx.quad({qa0,qy0,fixed},{qa1,qy0,fixed},{qa1,qy1,fixed},{qa0,qy1,fixed}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                } else {           // face at x = fixed, running along z
                    Vector3 n = { nsgn, 0, 0 };
                    if (nsgn > 0) fx.quad({fixed,qy0,qa0},{fixed,qy0,qa1},{fixed,qy1,qa1},{fixed,qy1,qa0}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                    else          fx.quad({fixed,qy0,qa1},{fixed,qy0,qa0},{fixed,qy1,qa0},{fixed,qy1,qa1}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                }
            }
    };
    // Door openings, in room-local cells: north wall's doorway is in cell 7,
    // south's in 8, west's in 8, east's in 7 (see stampManila in generate).
    // Cell c's opening runs from its west/north edge + 0.35 to + 1.65.
    auto face = [&](int axis, float fixed, float nsgn, int doorCell) {
        float a0 = (axis == 0 ? rx : rz) - IN, a1 = (axis == 0 ? rx : rz) + IN;
        float o0 = (axis == 0 ? rx : rz) - R + (doorCell - MANILA_LO) * CELL + 0.35f, o1 = o0 + 1.30f;
        const float y0 = 0.135f, yo = 2.30f;
        tileRect(axis, fixed, nsgn, a0, o0 - 0.06f, y0, cy);    // left of the frame
        tileRect(axis, fixed, nsgn, o1 + 0.06f, a1, y0, cy);    // right of it
        tileRect(axis, fixed, nsgn, o0 - 0.06f, o1 + 0.06f, yo + 0.06f, cy);   // over the head
        // The door itself, opened flat back against the wall beside its frame:
        // a wooden leaf, two panels, and a brass knob. It is what the lore
        // means by "a wooden entrance door on each wall", and it is open
        // because this is the one place down here you are meant to walk into.
        float lc = o1 + 0.08f + 0.64f;                       // leaf centre along the wall
        float off = fixed + nsgn * 0.03f;                    // standing just off the face
        Color leaf = { 118, 78, 50, 255 }, panel = { 98, 64, 40, 255 };
        if (axis == 0) {
            wood(pr, lc, off, 0, 0.64f, 0.022f, 0.01f, 2.27f, leaf);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 0.25f, 1.05f, panel);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 1.25f, 2.05f, panel);
            addSolidBox(pr, lc + 0.50f, 0.98f, off + nsgn * 0.02f - 0.025f, lc + 0.56f, 1.04f,
                        off + nsgn * 0.02f + 0.025f, Color{ 200, 160, 70, 254 });
        } else {
            wood(pr, off, lc, 0, 0.022f, 0.64f, 0.01f, 2.27f, leaf);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 0.25f, 1.05f, panel);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 1.25f, 2.05f, panel);
            addSolidBox(pr, off + nsgn * 0.02f - 0.025f, 0.98f, lc + 0.50f, off + nsgn * 0.02f + 0.025f,
                        1.04f, lc + 0.56f, Color{ 200, 160, 70, 254 });
        }
    };
    const float D = 0.0015f;
    face(0, rz - IN + D, +1, 7);    // north wall, facing into the room (+z)
    face(0, rz + IN - D, -1, 8);    // south
    face(1, rx - IN + D, +1, 8);    // west
    face(1, rx + IN - D, -1, 7);    // east

    // The octagonal table, with its cupboard under the top (the lore keeps
    // "food, water, and more documents" in there), on a plinth.
    auto octo = [&](float r0, float y0, float y1, Color t, bool top) {
        const float rr = r0 / cosf(TAU / 16);            // r0 is flat-to-centre
        for (int i = 0; i < 8; i++) {
            float a = TAU * i / 8 + TAU / 16, b = TAU * (i + 1) / 8 + TAU / 16, m = (a + b) * 0.5f;
            Vector3 p0 = { rx + rr * cosf(a), y0, rz + rr * sinf(a) }, p1 = { rx + rr * cosf(b), y0, rz + rr * sinf(b) };
            Vector3 p2 = { p1.x, y1, p1.z }, p3 = { p0.x, y1, p0.z };
            float ua = WU0 + 0.05f * i, ub = ua + 0.05f;
            pr.quad(p0, p1, p2, p3, { cosf(m), 0, sinf(m) }, {ua,WV1}, {ub,WV1}, {ub,WV1-0.05f}, {ua,WV1-0.05f}, t);
            if (top) {
                Vector2 c = { (WU0 + WU1) * 0.5f, (WV0 + WV1) * 0.5f };
                auto tuv = [&](Vector3 p) { return Vector2{ c.x + (p.x - rx) * 0.35f, c.y + (p.z - rz) * 0.35f }; };
                pr.tri({ rx, y1, rz }, p3, p2, { 0, 1, 0 }, c, tuv(p3), tuv(p2), t);
            }
        }
    };
    Color top = { 132, 86, 54, 255 }, body = { 104, 68, 44, 255 };
    octo(0.48f, 0.00f, 0.07f, Color{ 70, 46, 30, 255 }, true);    // plinth
    octo(0.42f, 0.07f, 0.73f, body, true);                          // the cupboard
    octo(0.64f, 0.73f, 0.785f, top, true);                          // the top
    for (int i = 0; i < 8; i += 2) {                                // cupboard doors: a knob on alternate faces
        float m = TAU * i / 8 + TAU / 8;
        float kx = rx + 0.425f * cosf(m), kz = rz + 0.425f * sinf(m);
        addSolidBox(pr, kx - 0.015f, 0.44f, kz - 0.015f, kx + 0.015f, 0.47f, kz + 0.015f, Color{ 200, 160, 70, 254 });
    }
    addContactShadow(ao, rx, rz, 0.0f, 0.0f, 0.52f, 0.52f);

    // Two chairs, one either side, pulled up to it as if two people had sat
    // down to talk — the only place in Level 0 anyone ever can.
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float chx = rx + sgn * 1.02f, chz = rz;
        Color cw = { 112, 74, 46, 255 };
        wood(pr, chx, chz, 0, 0.21f, 0.21f, 0.43f, 0.47f, cw);                        // seat
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            wood(pr, chx + lx * 0.18f, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.0f, 0.43f, cw);
        float bx = chx + sgn * 0.19f;                                                 // the back, away from the table
        for (int lz = -1; lz <= 1; lz += 2) wood(pr, bx, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.47f, 0.93f, cw);
        wood(pr, bx, chz, 0, 0.014f, 0.19f, 0.74f, 0.90f, cw);
        wood(pr, bx, chz, 0, 0.012f, 0.19f, 0.56f, 0.61f, cw);
        addContactShadow(ao, chx, chz, 0.0f, 0.0f, 0.22f, 0.22f);
    }

    // The notes. "Notes have been left on the table containing information
    // about the Backrooms and guides to no-clipping." E reads them (Game).
    const FixtureRect &N = FIXTURES[FIX_NOTE];
    for (int i = 0; i < 4; i++) {
        float a = r.f01() * TAU, d = 0.12f + r.f01() * 0.30f, rot = r.f01() * TAU;
        float nx = rx + cosf(a) * d, nz = rz + sinf(a) * d, y = 0.787f + 0.0012f * i;
        float c = cosf(rot), s2 = sinf(rot), hw = N.halfW, hh = N.halfH;
        auto P = [&](float u, float v) { return Vector3{ nx + u * c - v * s2, y, nz + u * s2 + v * c }; };
        fx.quad(P(-hw,-hh), P(-hw,hh), P(hw,hh), P(hw,-hh), { 0, 1, 0 },
                { N.u0, N.v0 }, { N.u0, N.v1 }, { N.u1, N.v1 }, { N.u1, N.v0 }, Color{ 255, 255, 255, 254 });
    }

    // The chandelier: a chain from the ceiling, a brass hub, six arms and six
    // warm bulbs. The bulbs are raw emissive (alpha 60); the light they throw
    // is the shader's uLamp, which Game points at this room while you are near.
    {
        Color brass = { 186, 146, 72, 254 };
        float hy = cy - 0.78f;
        addSolidBox(pr, rx - 0.008f, hy + 0.10f, rz - 0.008f, rx + 0.008f, cy, rz + 0.008f, Color{ 90, 80, 60, 254 });
        addSolidBox(pr, rx - 0.06f, hy - 0.05f, rz - 0.06f, rx + 0.06f, hy + 0.10f, rz + 0.06f, brass);
        addSolidBox(pr, rx - 0.10f, cy - 0.03f, rz - 0.10f, rx + 0.10f, cy, rz + 0.10f, brass);   // ceiling rose
        for (int i = 0; i < 6; i++) {
            float a = TAU * i / 6;
            float ex = rx + cosf(a) * 0.34f, ez = rz + sinf(a) * 0.34f;
            for (int sgm = 0; sgm < 4; sgm++) {                 // the arm, as a few short boxes out and up
                float t0 = sgm / 4.0f, t1 = (sgm + 1) / 4.0f;
                float ax = rx + cosf(a) * 0.34f * t1, az = rz + sinf(a) * 0.34f * t1;
                float ay = hy + 0.02f * sinf(t0 * 3.1416f) - 0.03f * t1;
                addSolidBox(pr, ax - 0.012f, ay - 0.012f, az - 0.012f, ax + 0.012f, ay + 0.012f, az + 0.012f, brass);
            }
            addSolidBox(pr, ex - 0.03f, hy - 0.05f, ez - 0.03f, ex + 0.03f, hy - 0.01f, ez + 0.03f, brass);   // cup
            addSolidBox(pr, ex - 0.018f, hy - 0.01f, ez - 0.018f, ex + 0.018f, hy + 0.07f, ez + 0.018f,
                        Color{ 255, 196, 120, 60 });                                                        // bulb
        }
    }
}

void bakeChunk(World &w, int cx, int cz, ChunkMeshes &out) {
    ChunkData &d = w.data(cx, cz);
    MB fl, ce, pr, wt, scr, gl, ao, fx;
    WallBuilder wa;
    float wx = cx * CHUNK, wz = cz * CHUNK;
    wa.tileV = w.level == 1 ? w.wallH : 3.0f;
    wa.tallPaper = w.storeyH > 0.0f;
    Color wcol = WHITE;
    // The ceiling takes its relief (alpha 255) again. It hangs level with the
    // light fittings, so every panel lights it edge-on, and a bump under raking
    // light swings the terminator far harder than the same bump lit head-on:
    // when relief was a world-space noise field, that turned the whole ceiling
    // into dark mould-like blotches roughly a tile across, and it was opted out
    // at 254. Relief is authored per surface now (surfaces.cpp) and a ceiling's
    // has no lumps in it — fine fissures, pinholes, the T-bar standing a
    // millimetre proud with a rolled edge, a formwork fin — which under raking
    // light is exactly what a real ceiling shows. If blotches come back, look
    // for a low-frequency term in a ceiling's height field before you look here.
    Color ccol = { 255, 255, 255, 255 };
    // ---- baked ambient occlusion: gradient decals hugging every crease where
    // geometry meets. The strip texture fades alpha from the crease (v=0)
    // outward (v=1), so walls sit *in* the room instead of on top of it.
    auto aoStrip = [&](Vector3 e0, Vector3 e1, Vector3 off, Vector3 nn, float v0) {
        ao.quad(e0, e1, { e1.x + off.x, e1.y + off.y, e1.z + off.z },
                { e0.x + off.x, e0.y + off.y, e0.z + off.z }, nn,
                { 0, v0 }, { 1, v0 }, { 1, 1 }, { 0, 1 }, AO_TINT);
    };
    const float AOW = 0.55f;   // reach across the floor / ceiling
    const float AOH = 0.48f;   // creep up / down the wall face
    const float AOC = 0.30f;   // ceiling creases start partway down the gradient (softer)
    // ---- the Red Rooms, bleeding through. On Level 0 a cursed noclip wall
    // leads to the red places, and the lore's warning signs are that the
    // colour shifts toward red and the paper starts peeling to crimson as you
    // get near one. So collect the cursed exits in this chunk and its eight
    // neighbours (an exit's pull reaches ~10 m, less than a chunk) and tint
    // every wall and floor cell by how close it stands to the nearest.
    struct RedSrc { float x, z; };
    RedSrc reds[16]; int nred = 0;
    if (w.level == 0) {
        for (int ncx = cx - 1; ncx <= cx + 1; ncx++) for (int ncz = cz - 1; ncz <= cz + 1; ncz++) {
            ChunkData &nd = w.data(ncx, ncz);
            for (int a = 0; a < CCELLS; a++) for (int b = 0; b < CCELLS; b++) {
                int gi = ncx * CCELLS + a, gk = ncz * CCELLS + b;
                bool n = nd.wallN[a][b] == WALL_EXIT, west = nd.wallW[a][b] == WALL_EXIT;
                if ((!n && !west) || !w.cursedExit(gi, gk) || nred >= 16) continue;
                reds[nred++] = { gi * CELL + (n ? 1.0f : 0.0f), gk * CELL + (n ? 0.0f : 1.0f) };
            }
        }
    }
    auto redAt = [&](float x, float z) {
        float best = 0;
        for (int r = 0; r < nred; r++) {
            float dx = x - reds[r].x, dz = z - reds[r].z;
            float t = 1.0f - sqrtf(dx * dx + dz * dz) / 11.0f;
            if (t > best) best = t;
        }
        return best * best * (3 - 2 * best);
    };
    // Multiplied into the texture, so it can only take colour away: the
    // yellow ground loses green and blue and goes to rust, then to the dull
    // crimson the Red Rooms are papered in. Alpha stays 255 so the relief does.
    auto redTint = [&](float x, float z) {
        float t = redAt(x, z);
        // rust at the edge of the pull, crimson and dim at its heart
        float t2 = t * t;
        return Color{ (unsigned char)(255 - 30 * t - 60 * t2), (unsigned char)(255 - 150 * t - 72 * t2),
                      (unsigned char)(255 - 110 * t - 90 * t2), 255 };
    };
    if (nred) { wa.tint = redTint; fl.tint = redTint; }
    if (w.level == 2) {
        Color water = { 72, 172, 162, 128 };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            int ci = cx*CCELLS+i, ck = cz*CCELLS+kk;
            float gx = wx + i*CELL, gz = wz + kk*CELL, fy = w.floorY(ci,ck);
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            // Emit only the high side of a riser. Cross-chunk lookups avoid
            // false walls at seams; physics reads these same terrace heights.
            auto skirt = [&](float x0,float z0,float x1,float z1,float low,Vector3 n) {
                if (low >= fy) return;
                fl.quad({x0,fy,z0},{x1,fy,z1},{x1,low,z1},{x0,low,z0},n,
                        {(x0+z0)/2,-fy/2},{(x1+z1)/2,-fy/2},
                        {(x1+z1)/2,-low/2},{(x0+z0)/2,-low/2},wcol);
            };
            skirt(gx,gz,gx+CELL,gz,w.floorY(ci,ck-1),{0,0,-1});
            skirt(gx+CELL,gz+CELL,gx,gz+CELL,w.floorY(ci,ck+1),{0,0,1});
            skirt(gx,gz+CELL,gx,gz,w.floorY(ci-1,ck),{-1,0,0});
            skirt(gx+CELL,gz,gx+CELL,gz+CELL,w.floorY(ci+1,ck),{1,0,0});
            if (d.pool[i][kk])
                wt.quad({gx,WATER_Y,gz},{gx+CELL,WATER_Y,gz},{gx+CELL,WATER_Y,gz+CELL},{gx,WATER_Y,gz+CELL},{0,1,0},
                        {0,0},{1,0},{1,1},{0,1},water);
        }
        // Elliptical vaults spanning the openings in the central partitions.
        // The lowest point is 4.6 m above the deck: all collision lives in the
        // full-height piers already represented by the wall grid.
        for (int axis=0; axis<2; ++axis) for (int start : {3,11}) {
            // Only where the partition is actually there with this opening in
            // it: grand halls have no partition, and an arch left hanging over
            // open water reads as a bug, not a ruin.
            auto wallAt = [&](int t) { return axis ? d.wallW[8][t] : d.wallN[t][8]; };
            if (wallAt(start - 1) != WALL_SOLID || wallAt(start + 3) != WALL_SOLID) continue;
            auto pos = [&](float t,float y,float depth) -> Vector3 {
                return axis ? Vector3{wx+16+depth,y,wz+t} : Vector3{wx+t,y,wz+16+depth};
            };
            for (int n=0;n<24;++n) {
                float t0=start*CELL+6.0f*n/24, t1=start*CELL+6.0f*(n+1)/24;
                auto archY = [&](float t) { float u=(t-(start*CELL+3))/3;
                    return 4.6f+2.4f*sqrtf(std::max(0.0f,1-u*u)); };
                float y0=archY(t0),y1=archY(t1);
                for (float side : {-WT,WT}) {
                    Vector3 normal=axis ? Vector3{side/WT,0,0}:Vector3{0,0,side/WT};
                    wa.quad(pos(t0,y0,side),pos(t1,y1,side),pos(t1,w.wallH,side),pos(t0,w.wallH,side),normal,
                            {t0/2,-y0/2},{t1/2,-y1/2},{t1/2,-w.wallH/2},{t0/2,-w.wallH/2},wcol);
                }
                Vector3 normal=axis ? Vector3{0,-1,(y1-y0)/(t1-t0)}:Vector3{(y1-y0)/(t1-t0),-1,0};
                wa.quad(pos(t0,y0,-WT),pos(t1,y1,-WT),pos(t1,y1,WT),pos(t0,y0,WT),normal,
                        {t0/2,0},{t1/2,0},{t1/2,WT},{t0/2,WT},wcol);
            }
        }
    } else {
        // per-cell floor: sunken lounges (L0) and loading docks (L1) change height, with real steps
        auto stepEdge = [&](float bx0, float bz0, float bx1, float bz1, float hi, float lo, float nx, float nz) {
            fl.quad({bx0,hi,bz0},{bx1,hi,bz1},{bx1,lo,bz1},{bx0,lo,bz0},{nx,0,nz},
                    {(bx0+bz0)/2,0},{(bx1+bz1)/2,0},{(bx1+bz1)/2,(hi-lo)/2},{(bx0+bz0)/2,(hi-lo)/2},wcol);
            float mid = (hi + lo) * 0.5f, ox = nx * 0.35f, oz = nz * 0.35f;
            fl.quad({bx0,mid,bz0},{bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx0+ox,mid,bz0+oz},{0,1,0},
                    {0,0},{1,0},{1,0.18f},{0,0.18f},wcol);
            fl.quad({bx0+ox,mid,bz0+oz},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx0+ox,lo,bz0+oz},{nx,0,nz},
                    {0,0},{1,0},{1,(mid-lo)/2},{0,(mid-lo)/2},wcol);
            fl.quad({bx0,mid,bz0},{bx0+ox,mid,bz0+oz},{bx0+ox,lo,bz0+oz},{bx0,lo,bz0},{bz1-bz0,0,bx0-bx1},
                    {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);   // end caps
            fl.quad({bx1,mid,bz1},{bx1+ox,mid,bz1+oz},{bx1+ox,lo,bz1+oz},{bx1,lo,bz1},{bz0-bz1,0,bx1-bx0},
                    {0,0},{0.18f,0},{0.18f,(mid-lo)/2},{0,(mid-lo)/2},wcol);
        };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            float fy = d.elev[i][kk] * ELEV_UNIT;
            // the Manila Room lays its own floorboards (addManilaRoom)
            if (d.manila && i >= MANILA_LO && i <= MANILA_HI && kk >= MANILA_LO && kk <= MANILA_HI) continue;
            // A hole has no floor, and a flight builds its own treads and
            // landings (buildFlights, below).
            if (d.vflag[i][kk] & (VF_HOLE | VF_STAIR)) continue;
            if (w.level == 0 && w.softAt(cx * CCELLS + i, cz * CCELLS + kk)) {
                // A rotten patch, and the one thing on Level 0 that will drop you
                // a floor. It used to be two flat decal quads: a black rectangle
                // with a blacker rectangle inside it, which you can certainly see
                // but which reads as a hole already there, or as a rug — not as a
                // floor that is about to stop being one.
                //
                // So it is geometry. The cell is built as a shallow bowl and the
                // dip catches the ceiling light along its far rim the way a real
                // sag does; the darkening is damp carpet over a backing that has
                // gone, not a painted-on square. It still reads across a room.
                //
                // SOFT_DEPTH and the falloff live in World::softDip, because
                // groundAt walks the player down the same bowl and the two must
                // not drift.
                const int SOFTSUB = 24;
                auto dipAt = [&](float u, float v) { return w.softDip(gx + u * CELL, gz + v * CELL); };
                for (int a = 0; a < SOFTSUB; a++) for (int b = 0; b < SOFTSUB; b++) {
                    float u0 = a / (float)SOFTSUB, u1 = (a + 1) / (float)SOFTSUB;
                    float v0 = b / (float)SOFTSUB, v1 = (b + 1) / (float)SOFTSUB;
                    float x0 = gx + u0 * CELL, x1 = gx + u1 * CELL;
                    float z0 = gz + v0 * CELL, z1 = gz + v1 * CELL;
                    float y00 = fy - dipAt(u0, v0), y10 = fy - dipAt(u1, v0);
                    float y11 = fy - dipAt(u1, v1), y01 = fy - dipAt(u0, v1);
                    // The bowl has to shade as a bowl, so take the normal from the
                    // height field's own slope across this patch rather than
                    // leaving every quad pointing at (0,1,0).
                    float dydx = ((y10 + y11) - (y00 + y01)) / (2 * (x1 - x0));
                    float dydz = ((y01 + y11) - (y00 + y10)) / (2 * (z1 - z0));
                    float nl = sqrtf(dydx * dydx + 1 + dydz * dydz);
                    Vector3 n = { -dydx / nl, 1 / nl, -dydz / nl };
                    // Damp and dark toward the middle, where the backing has gone.
                    // MB::quad carries one colour and one normal per quad, so both
                    // the tint ramp and the bowl's shading are banded at the
                    // subdivision. 8 across two metres came out as a visible
                    // chequerboard — worse than the flat decal it replaced — and
                    // 12 still quilted. 24 puts the step at 8 cm, under the noise
                    // in the carpet, at 576 quads on a patch that occurs once per
                    // 2660 m2.
                    float md = 1.0f - dipAt((u0 + u1) * 0.5f, (v0 + v1) * 0.5f) / SOFT_DEPTH;
                    float k2 = 0.34f + 0.66f * md * md;
                    Color sc = { (unsigned char)(wcol.r * k2), (unsigned char)(wcol.g * k2),
                                 (unsigned char)(wcol.b * k2), wcol.a };
                    fl.quad({x0,y00,z0},{x1,y10,z0},{x1,y11,z1},{x0,y01,z1}, n,
                            {x0/2,z0/2},{x1/2,z0/2},{x1/2,z1/2},{x0/2,z1/2}, sc);
                }
            } else
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            if (d.elev[i][kk] == 0) continue;
            // true cross-chunk heights, so terraces spanning a chunk border don't
            // grow phantom risers (the old lookup assumed 0 beyond the edge)
            auto hgt = [&](int a, int b) { return w.floorY(cx * CCELLS + a, cz * CCELLS + b); };
            float hN = hgt(i, kk - 1), hS = hgt(i, kk + 1), hW = hgt(i - 1, kk), hE = hgt(i + 1, kk);
            // each shared riser is drawn once: by this cell when the neighbour is
            // flat ground (elev 0 cells skip out above), otherwise by the lower
            // cell of the pair — terraced atria would double-draw it otherwise
            if (hN < fy && hN == 0.0f) stepEdge(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
            else if (hN > fy) stepEdge(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
            if (hS < fy && hS == 0.0f) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
            else if (hS > fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
            if (hW < fy && hW == 0.0f) stepEdge(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
            else if (hW > fy) stepEdge(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
            if (hE < fy && hE == 0.0f) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
            else if (hE > fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
            if (w.level == 1) {
                // Safety edging along every drop off a loading dock: yellow and
                // black, 100 mm wide, 250 mm to a stripe, painted just in from
                // the lip. A warehouse marks its edges; and in the fog, with the
                // floor the colour of the risers, it is the only thing that does.
                auto edging = [&](float ex0, float ez0, float ex1, float ez1, float inx, float inz) {
                    const Vector2 uv = { 0.375f, 0.75f };
                    float len = sqrtf((ex1 - ex0) * (ex1 - ex0) + (ez1 - ez0) * (ez1 - ez0));
                    int n = (int)(len / 0.25f);
                    for (int q = 0; q < n; q++) {
                        float t0 = q / (float)n, t1 = (q + 1) / (float)n;
                        Vector3 a = { ex0 + (ex1 - ex0) * t0, fy + 0.004f, ez0 + (ez1 - ez0) * t0 };
                        Vector3 b = { ex0 + (ex1 - ex0) * t1, fy + 0.004f, ez0 + (ez1 - ez0) * t1 };
                        Vector3 c = { b.x + inx * 0.10f, b.y, b.z + inz * 0.10f }, dd2 = { a.x + inx * 0.10f, a.y, a.z + inz * 0.10f };
                        Color col = (q & 1) ? Color{ 34, 32, 28, 254 } : Color{ 196, 160, 40, 254 };
                        // wound up whichever way round the edge runs
                        if ((ex1 - ex0) * inz - (ez1 - ez0) * inx < 0) fx.quad(a, b, c, dd2, {0,1,0}, uv, uv, uv, uv, col);
                        else                                           fx.quad(dd2, c, b, a, {0,1,0}, uv, uv, uv, uv, col);
                    }
                };
                if (hN < fy) edging(gx, gz, gx + CELL, gz, 0, 1);
                if (hS < fy) edging(gx, gz + CELL, gx + CELL, gz + CELL, 0, -1);
                if (hW < fy) edging(gx, gz, gx, gz + CELL, 1, 0);
                if (hE < fy) edging(gx + CELL, gz, gx + CELL, gz + CELL, -1, 0);
            }
        }
    }
    // Per-cell ceiling at this cell's own floor plus a wall height, in place of
    // one flat slab per chunk at a fixed y. The slab is what stopped the world
    // going upward: a raised deck pushed its floor through it and its walls
    // started below it. UVs stay world-space, so the tile grid runs across the
    // cell seams exactly as it did when this was one quad.
    //
    // Emitted by greedy meshing rather than one quad per cell. Elevation touches
    // about 5% of cells, so a naive per-cell ceiling replaces one quad per chunk
    // with 256 identical coplanar ones and costs 4% of the frame on the software
    // rasteriser for nothing; merging equal-height runs gives a flat chunk its
    // single quad back and only pays where the ceiling actually steps.
    {
        float cyc[CCELLS][CCELLS];
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
            cyc[i][kk] = w.ceilY(cx * CCELLS + i, cz * CCELLS + kk);
        bool done[CCELLS][CCELLS] = {};
        // A cell open to the storey above has no ceiling here: the ceiling you
        // see from it is the next storey's, drawn by that storey's chunk.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
            if (d.vflag[i][kk] & VF_OPENUP) done[i][kk] = true;
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            if (done[i][kk]) continue;
            float cy = cyc[i][kk];
            int i1x = i;                                  // grow along x first
            while (i1x + 1 < CCELLS && !done[i1x + 1][kk] && cyc[i1x + 1][kk] == cy) i1x++;
            int k1z = kk;                                 // then take whole rows down z
            while (k1z + 1 < CCELLS) {
                bool ok = true;
                for (int a = i; a <= i1x && ok; a++) ok = !done[a][k1z + 1] && cyc[a][k1z + 1] == cy;
                if (!ok) break;
                k1z++;
            }
            for (int a = i; a <= i1x; a++) for (int b = kk; b <= k1z; b++) done[a][b] = true;
            float x0 = wx + i * CELL, x1 = wx + (i1x + 1) * CELL;
            float z0 = wz + kk * CELL, z1 = wz + (k1z + 1) * CELL;
            ce.quad({x0,cy,z0},{x0,cy,z1},{x1,cy,z1},{x1,cy,z0},{0,-1,0},
                    {x0/2,z0/2},{x0/2,z1/2},{x1/2,z1/2},{x1/2,z0/2},ccol);
        }
        // Where the neighbour's ceiling is higher, close the slot with a soffit.
        // Leave it out and you look straight along the gap and out of the
        // building. Only the *lower* cell of a pair draws it, so a shared edge
        // is drawn exactly once — including across a chunk seam, where both
        // sides read the same global heights.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            float cy = cyc[i][kk];
            if (d.vflag[i][kk] & VF_OPENUP) continue;   // no ceiling of its own to close off
            auto soffit = [&](float ax, float az, float bx2, float bz2, float hi, int ni, int nk, uint8_t edge) {
                if (hi <= cy + 1e-4f) return;
                if (w.storeyH > 0.0f && (w.vflagAt(ni, nk) & VF_OPENUP)) {
                    // The edge of an opening into the storey above: the ceiling
                    // stops and a bulkhead runs up past the metre of dark void
                    // over the tiles to the floor above. Papered like the walls,
                    // because that is what it is. A wall or a door head on the
                    // same line already climbs that high and hides it.
                    if (blocksEdge(edge) && edge != WALL_RAIL) return;
                    if (edge == WALL_DOOR || edge == WALL_EXIT) return;
                    float along0 = (ax == bx2) ? az : ax, along1 = (ax == bx2) ? bz2 : bx2;
                    // From the tile's middle rather than its foot, so no
                    // baseboard runs round the underside of the floor above.
                    auto fv = [&](float y) { return 1 - (y - 2.25f) / wa.tileV; };
                    wa.quad({ax,cy,az},{bx2,cy,bz2},{bx2,hi,bz2},{ax,hi,az},
                            {(bz2-az)/CELL,0,(ax-bx2)/CELL},
                            {along0/3,fv(cy)},{along1/3,fv(cy)},{along1/3,fv(hi)},{along0/3,fv(hi)},wcol);
                    return;
                }
                ce.quad({ax,cy,az},{bx2,cy,bz2},{bx2,hi,bz2},{ax,hi,az},
                        {(bz2-az)/CELL,0,(ax-bx2)/CELL},
                        {(ax+az)/2,0},{(bx2+bz2)/2,0},{(bx2+bz2)/2,(hi-cy)/2},{(ax+az)/2,(hi-cy)/2},ccol);
            };
            soffit(gx, gz, gx + CELL, gz, w.ceilY(gi, gk - 1), gi, gk - 1, w.wallNVal(gi, gk));
            soffit(gx + CELL, gz + CELL, gx, gz + CELL, w.ceilY(gi, gk + 1), gi, gk + 1, w.wallNVal(gi, gk + 1));
            soffit(gx, gz + CELL, gx, gz, w.ceilY(gi - 1, gk), gi - 1, gk, w.wallWVal(gi, gk));
            soffit(gx + CELL, gz, gx + CELL, gz + CELL, w.ceilY(gi + 1, gk), gi + 1, gk, w.wallWVal(gi + 1, gk));
        }
    }
    // light panels on the global grid (emissive: alpha=0); spacing varies per level
    Color panel = {255,255,255,0};
    float ls = LEVEL_RULES[w.level].ls;   // the same grid the shader lights from (uLS)
    int g0x = (int)floorf(wx / ls), g1x = (int)floorf((wx + CHUNK) / ls);
    int g0z = (int)floorf(wz / ls), g1z = (int)floorf((wz + CHUNK) / ls);
    for (int gx = g0x; gx <= g1x; gx++)
        for (int gz = g0z; gz <= g1z; gz++) {
            float lx = gx * ls + ls * 0.5f, lz = gz * ls + ls * 0.5f, hp = 0.62f;
            if (lx < wx || lx >= wx + CHUNK || lz < wz || lz >= wz + CHUNK) continue;
            // No tubes in the Manila Room: it is lit by its chandelier, and the
            // shader masks these same panels dark through uRoomMask.
            if (d.manila) {
                float mx = wx + (MANILA_HI + 1 - 2) * CELL, mz = wz + (MANILA_HI + 1 - 2) * CELL;
                if (fabsf(lx - mx) < 4.0f && fabsf(lz - mz) < 4.0f) continue;
            }
            // No fitting where there is no ceiling to hang it in: a panel whose
            // tray would overhang an opening into the storey above is left out,
            // and buildOccupancy tells the shader the same (bit 3), so no light
            // comes out of the air where a panel is not.
            if (w.storeyH > 0.0f) {
                int pci2 = (int)floorf(lx / CELL + 0.5f), pck2 = (int)floorf(lz / CELL + 0.5f);   // the corner it is centred on
                if ((w.vflagAt(pci2, pck2) | w.vflagAt(pci2 - 1, pck2) | w.vflagAt(pci2, pck2 - 1) | w.vflagAt(pci2 - 1, pck2 - 1))
                    & VF_OPENUP) continue;
            }
            // The fitting hangs in the ceiling, so it goes wherever the ceiling
            // of the cell it is centred in went. The tray is 1.38 m across and a
            // cell is 2 m, so it can overhang a neighbour at another height;
            // that neighbour's soffit is what it meets, which is what a real
            // bulkhead beside a light does.
            float wallTop = w.ceilY((int)floorf(lx / CELL), (int)floorf(lz / CELL));
            float yq = wallTop - 0.12f;
            // Recessed diffuser inside a real metal tray. The luminous plane
            // now matches uLY instead of floating 10 cm above its own light.
            Color rim = w.level == 2 ? Color{230,232,223,254} : Color{156,153,140,254};
            const float outer = 0.69f, lip = 0.035f;
            if (w.level == 1) {
                // Level 1's fittings are warehouse battens, not office trays:
                // two bare tubes under a steel reflector, hung off the slab on
                // two rods. The tubes sit on the light plane (uLY), so the
                // light still comes from where the glow is; the shader shades
                // the fitting as its usual square, which at this pitch nobody
                // can tell apart. Every other one is turned a quarter, so the
                // grid does not read as rows of identical strips.
                bool alongX = (ih((int)floorf(lx / ls), (int)floorf(lz / ls), w.sseed() ^ 0xBA77u) & 1) != 0;
                auto box = [&](float a0, float y0, float b0, float a1, float y1, float b1, Color c) {
                    if (alongX) addSolidBox(pr, lx + a0, y0, lz + b0, lx + a1, y1, lz + b1, c);
                    else        addSolidBox(pr, lx + b0, y0, lz + a0, lx + b1, y1, lz + a1, c);
                };
                Color steel = { 132, 134, 128, 254 }, rod = { 70, 70, 68, 254 };
                float yt = yq;                                   // the tubes' underside
                box(-0.82f, yt + 0.035f, -0.16f, 0.82f, yt + 0.075f, 0.16f, steel);   // reflector
                box(-0.82f, yt - 0.005f, -0.165f, 0.82f, yt + 0.075f, -0.145f, steel); // its lips
                box(-0.82f, yt - 0.005f, 0.145f, 0.82f, yt + 0.075f, 0.165f, steel);
                box(-0.84f, yt - 0.01f, -0.16f, -0.78f, yt + 0.075f, 0.16f, rod);      // end caps
                box(0.78f, yt - 0.01f, -0.16f, 0.84f, yt + 0.075f, 0.16f, rod);
                box(-0.55f, yt + 0.075f, -0.008f, -0.534f, wallTop, 0.008f, rod);     // hanger rods
                box(0.534f, yt + 0.075f, -0.008f, 0.55f, wallTop, 0.008f, rod);
                for (int t = -1; t <= 1; t += 2) {                                     // the two tubes
                    float c0 = t * 0.07f - 0.028f, c1 = t * 0.07f + 0.028f;
                    Vector3 a, b, c, d2;
                    if (alongX) { a = {lx-0.78f,yt,lz+c0}; b = {lx-0.78f,yt,lz+c1}; c = {lx+0.78f,yt,lz+c1}; d2 = {lx+0.78f,yt,lz+c0}; }
                    else        { a = {lx+c0,yt,lz-0.78f}; b = {lx+c0,yt,lz+0.78f}; c = {lx+c1,yt,lz+0.78f}; d2 = {lx+c1,yt,lz-0.78f}; }
                    ce.quad(a, b, c, d2, {0,-1,0}, {0,0},{0,1},{1,1},{1,0}, panel);
                    // and their sides, so a tube seen edge-on down a long hall
                    // is still a line of light and not nothing
                    if (alongX) ce.quad({lx-0.78f,yt,lz+c0},{lx+0.78f,yt,lz+c0},{lx+0.78f,yt+0.03f,lz+c0},{lx-0.78f,yt+0.03f,lz+c0},
                                        {0,0,-1},{0,0},{1,0},{1,1},{0,1}, panel);
                    else        ce.quad({lx+c0,yt,lz+0.78f},{lx+c0,yt,lz-0.78f},{lx+c0,yt+0.03f,lz-0.78f},{lx+c0,yt+0.03f,lz+0.78f},
                                        {-1,0,0},{0,0},{1,0},{1,1},{0,1}, panel);
                }
                continue;
            }
            if (w.level == 0) {
                // Level 0's fittings are lay-in troffers, dropped into the tile
                // grid the way they are in the photograph: the diffuser sits
                // flush with the ceiling behind a hairline frame, not in a
                // box hanging under it. The light plane (uLY) stays 12 cm
                // down, which nobody can see and every light calculation in
                // both shader and CPU mirror already agrees on.
                float yf = wallTop - 0.010f;
                Color frame = { 186, 182, 166, 254 };
                addSolidBox(pr, lx-outer, yf-0.008f, lz-outer, lx-hp, wallTop, lz+outer, frame);
                addSolidBox(pr, lx+hp, yf-0.008f, lz-outer, lx+outer, wallTop, lz+outer, frame);
                addSolidBox(pr, lx-hp, yf-0.008f, lz-outer, lx+hp, wallTop, lz-hp, frame);
                addSolidBox(pr, lx-hp, yf-0.008f, lz+hp, lx+hp, wallTop, lz+outer, frame);
                ce.quad({lx-hp,yf,lz-hp},{lx-hp,yf,lz+hp},{lx+hp,yf,lz+hp},{lx+hp,yf,lz-hp},{0,-1,0},
                        {0,0},{0,1},{1,1},{1,0},panel);
                continue;
            }
            addSolidBox(pr, lx-outer, yq-lip, lz-outer, lx-hp, wallTop, lz+outer, rim);
            addSolidBox(pr, lx+hp, yq-lip, lz-outer, lx+outer, wallTop, lz+outer, rim);
            addSolidBox(pr, lx-hp, yq-lip, lz-outer, lx+hp, wallTop, lz-hp, rim);
            addSolidBox(pr, lx-hp, yq-lip, lz+hp, lx+hp, wallTop, lz+outer, rim);
            ce.quad({lx-hp,yq,lz-hp},{lx-hp,yq,lz+hp},{lx+hp,yq,lz+hp},{lx+hp,yq,lz-hp},{0,-1,0},
                    {0,0},{0,1},{1,1},{1,0},panel);
        }
    ChunkData &dd = d;
    // A rail on one cell edge. Its top follows whatever you would be standing
    // on beside it, whichever side is higher: level round an opening, and up
    // the nosing line beside a flight. Between two holes there is nothing to
    // stand on here — the flight that needs guarding is the storey below's,
    // and so is the balustrade you see — and the edge is collision only.
    auto railEdge = [&](int a, int b, bool west) {
        float ex0 = a * CELL, ez0 = b * CELL;
        float ex1 = west ? ex0 : ex0 + CELL, ez1 = west ? ez0 + CELL : ez0;
        int oa = west ? a - 1 : a, ob = west ? b : b - 1;          // the cell across the edge
        float inx = west ? 0.05f : 0.0f, inz = west ? 0.0f : 0.05f;   // a step into cell (a,b)
        bool any = false, hole = false;
        int voidSide = 0;
        float base = 1e9f;
        auto side = [&](int ca, int cb, float x, float z) {
            uint8_t f = w.vflagAt(ca, cb);
            if (f & VF_HOLE) {
                hole = true;
                // which face of the run looks into it (addRailRun's normal is
                // +z for a north edge and -x for a west one)
                bool own = ca == a && cb == b;
                voidSide = west ? (own ? -1 : 1) : (own ? 1 : -1);
                return -1e9f;
            }
            any = true;
            base = std::min(base, w.floorY(ca, cb));
            return (f & VF_STAIR) ? w.stairY(x, z, true) : w.floorY(ca, cb);
        };
        auto topAt = [&](float x, float z) {
            return std::max(side(a, b, x + inx, z + inz), side(oa, ob, x - inx, z - inz));
        };
        float t0 = topAt(ex0 + (ex1 - ex0) * 0.01f, ez0 + (ez1 - ez0) * 0.01f);
        float t1 = topAt(ex0 + (ex1 - ex0) * 0.99f, ez0 + (ez1 - ez0) * 0.99f);
        if (!any) return;
        addRailRun(wa, pr, ex0, ez0, ex1, ez1, base, t0 + RAIL_H, t1 + RAIL_H, hole, voidSide);
    };
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        float gx = wx + i * CELL, gz = wz + kk * CELL;
        int gi0 = cx * CCELLS + i, gk0 = cz * CCELLS + kk;
        // A wall stands between two cells that may be at different heights. Base
        // it on the lower floor and take it to the higher ceiling: base it on
        // its own cell's floor instead and a step leaves a gap under the wall on
        // the low side and a slot over it on the high side, both of which you
        // see straight through. Everything fixed to the wall — sill, door head,
        // architrave — is measured off that same base, so a doorway in a step
        // has its head where a real one would.
        float fyc = w.floorY(gi0, gk0), cyc = w.ceilY(gi0, gk0);
        float nb = std::min(w.floorY(gi0, gk0 - 1), w.floorY(gi0, gk0));
        float nt = std::max(w.ceilY(gi0, gk0 - 1), w.ceilY(gi0, gk0));
        float wb = std::min(w.floorY(gi0 - 1, gk0), w.floorY(gi0, gk0));
        float wt2 = std::max(w.ceilY(gi0 - 1, gk0), w.ceilY(gi0, gk0));
        // Through the accessors, not out of the array. Every overlay that
        // changes what a wall IS lands in wallNVal/wallWVal — `shifted` for the
        // doorways the building closes behind you, `unlockedDoors` for a door
        // you have turned a key in — and reading the raw array here meant the
        // geometry was the one system that never saw them. Both ways round:
        // an unlocked door went on drawing its leaf while collision let you
        // walk through it, and a shifted doorway kept its opening on screen
        // while collision had already sealed it. AGENTS.md said the mesher came
        // through here; it did not, until now.
        uint8_t nv = w.wallNVal(gi0, gk0);
        // Storeys: which side of this cell's two edges has a floor to put a
        // skirting board, a crease and an outlet against, and which has a
        // ceiling to crease into. A hole has no floor, a flight buries the foot
        // of its walls, and an opening has no ceiling — trim or a shadow strip
        // drawn at floor level beside a hole hangs in the air over it.
        auto hasFloor = [&](int a, int b) { return w.storeyH <= 0.0f || !(w.vflagAt(a, b) & (VF_HOLE | VF_STAIR)); };
        auto hasCeil  = [&](int a, int b) { return w.storeyH <= 0.0f || !(w.vflagAt(a, b) & VF_OPENUP); };
        const bool flN = hasFloor(gi0, gk0), flS = hasFloor(gi0, gk0 - 1), flW = hasFloor(gi0 - 1, gk0);
        const bool clN = hasCeil(gi0, gk0),  clS = hasCeil(gi0, gk0 - 1),  clW = hasCeil(gi0 - 1, gk0);
        // A neighbour's end cap is buried in the next wall along, but not in a
        // rail, which is thinner than the wall it meets.
        auto buries = [](uint8_t v) { return blocksEdge(v) && v != WALL_RAIL; };
        if (nv == WALL_RAIL) railEdge(gi0, gk0, false);
        // Faces that look into a hole in this storey's floor (see voidFace).
        auto holeAt = [&](int a, int b) { return w.storeyH > 0.0f && (w.vflagAt(a, b) & VF_HOLE); };
        if (nv == WALL_SOLID) {
            int sk = (buries(w.wallNVal(gi0 - 1, gk0)) ? 4 : 0) | (buries(w.wallNVal(gi0 + 1, gk0)) ? 8 : 0);
            int vfN = (holeAt(gi0, gk0 - 1) ? 1 : 0) | (holeAt(gi0, gk0) ? 2 : 0);
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nt, gz + WT, false, sk, WHITE, vfN);
        }
        else if (nv == WALL_WINDOW) {   // window on x-running wall; behind the glass, nothing
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nb + 1.0f, gz + WT);
            addBoxSides(wa, gx - WT, nb + 2.1f, gz - WT, gx + CELL + WT, nt, gz + WT, true);
            addBoxSides(wa, gx - WT, nb + 1.0f, gz - WT, gx + 0.45f, nb + 2.1f, gz + WT);
            addBoxSides(wa, gx + 1.55f, nb + 1.0f, gz - WT, gx + CELL + WT, nb + 2.1f, gz + WT);
            wa.quad({gx-WT,nb+1.0f,gz-WT},{gx+CELL+WT,nb+1.0f,gz-WT},{gx+CELL+WT,nb+1.0f,gz+WT},{gx-WT,nb+1.0f,gz+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            if (w.level == 2) {
                // Level 37's windows look out into a light void: an emissive
                // pale pane (alpha 70 -> raw emissive), one per face.
                Color sky = { 226, 241, 246, 70 };
                wa.quad({gx+0.45f,nb+1.0f,gz-0.02f},{gx+1.55f,nb+1.0f,gz-0.02f},{gx+1.55f,nb+2.1f,gz-0.02f},{gx+0.45f,nb+2.1f,gz-0.02f},
                        {0,0,-1},{0,1},{1,1},{1,0},{0,0}, sky);
                wa.quad({gx+1.55f,nb+1.0f,gz+0.02f},{gx+0.45f,nb+1.0f,gz+0.02f},{gx+0.45f,nb+2.1f,gz+0.02f},{gx+1.55f,nb+2.1f,gz+0.02f},
                        {0,0,1},{0,1},{1,1},{1,0},{0,0}, sky);
            } else {
            // real glass now: translucent pane (alpha 100 -> glass branch), see the room beyond
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx+0.45f,nb+1.0f,gz},{gx+1.55f,nb+1.0f,gz},{gx+1.55f,nb+2.1f,gz},{gx+0.45f,nb+2.1f,gz},
                    {0,0,-1},{0,1},{1,1},{1,0},{0,0}, glass);
            }
        }
        else if (nv == WALL_EXIT) {   // exit doorway on x-running wall
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            // cursed exits glow red — they don't lead deeper, they lead to the Red Halls
            bool crs = w.cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx+0.35f,nb,gz},{gx+1.65f,nb,gz},{gx+1.65f,nb+2.3f,gz},{gx+0.35f,nb+2.3f,gz},{0,0,-1},
                    {0,1},{1,1},{1,0},{0,0},glow);
            wa.quad({gx+1.65f,nb,gz},{gx+0.35f,nb,gz},{gx+0.35f,nb+2.3f,gz},{gx+1.65f,nb+2.3f,gz},{0,0,1},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (w.level == 1)
                addSymbolDoor(pr, fx, 0, gx, gz, nb, crs, ih(gi0, gk0, w.sseed() ^ 0x51B0u),
                              w.wallNVal(gi0 + 1, gk0) == WALL_SOLID);
        }
        else if (nv == WALL_DOOR) {   // doorway on x-running wall
            // Two doorways in neighbouring cells leave 0.35 m of jamb each side
            // of the line between them: a 0.7 m sliver of plasterboard between
            // two 1.3 m holes, which is the thing that reads as unfinished
            // geometry rather than as a building. World::generate spaces
            // doorways out where the floorplan allows it, but a room reached
            // only through its own door cannot have that door moved — so where
            // two must stay adjacent, take the sliver out and let them be one
            // wide opening. The header still crosses it, so the wall above is
            // unbroken and the opening reads as deliberate.
            bool mW = w.wallNVal(gi0 - 1, gk0) == WALL_DOOR;
            bool mE = w.wallNVal(gi0 + 1, gk0) == WALL_DOOR;
            if (mW) addBoxSides(wa, gx - WT, nb + 2.3f, gz - WT, gx + 0.35f, nt, gz + WT, true);
            else    addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            if (mE) addBoxSides(wa, gx + 1.65f, nb + 2.3f, gz - WT, gx + CELL + WT, nt, gz + WT, true);
            else    addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            // The architrave is what makes it read as a door rather than a hole:
            // it stands proud of both faces, so you can see it is a way through
            // from either side and at a glancing angle. A merged side has no
            // jamb to trim, so its post goes and the head runs on to meet the
            // neighbour's.
            float tx0 = mW ? gx - WT : gx + 0.29f, tx1 = mE ? gx + CELL + WT : gx + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                if (!mW) addSolidBox(pr, gx + 0.29f, nb, zf, gx + 0.35f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                if (!mE) addSolidBox(pr, gx + 1.65f, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, tx0, nb + 2.30f, zf, tx1, nb + 2.36f, zf + TRIM_T, TRIM_COL);
            }
            // and a threshold strip underfoot, worn by whoever came through
            float fy0 = w.floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, mW ? gx : gx + 0.35f, fy0, gz - 0.07f,
                        mE ? gx + CELL : gx + 1.65f, fy0 + 0.013f, gz + 0.07f, SILL_COL);
        }
        else if (nv == WALL_LOCKED) {   // a door with the leaf still in it
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + 0.35f, nt, gz + WT);
            addBoxSides(wa, gx + 1.65f, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, gx + 0.35f, nb + 2.3f, gz - WT, gx + 1.65f, nt, gz + WT, true);
            float fy0 = w.floorY(cx * CCELLS + i, cz * CCELLS + kk);
            // The leaf fills the opening. It is what tells you at a glance that
            // this one is different from the hundred empty frames behind you,
            // so it is a slab you can see from both sides, not a decal.
            addSolidBox(pr, gx + 0.36f, fy0, gz - 0.025f, gx + 1.64f, nb + 2.28f, gz + 0.025f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {   // architrave, as on an open one
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                addSolidBox(pr, gx + 0.29f, nb, zf, gx + 0.35f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, gx + 1.65f, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, gx + 0.29f, nb + 2.30f, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                // handle and escutcheon, at 1.02 m on the latch side
                addSolidBox(pr, gx + 1.34f, fy0 + 0.97f, zf - 0.02f,
                            gx + 1.50f, fy0 + 1.07f, zf + TRIM_T, LOCK_COL);
            }
        }
        uint8_t wv = w.wallWVal(gi0, gk0);
        if (wv == WALL_RAIL) railEdge(gi0, gk0, true);
        if (wv == WALL_SOLID) {
            int sk = (buries(w.wallWVal(gi0, gk0 - 1)) ? 1 : 0) | (buries(w.wallWVal(gi0, gk0 + 1)) ? 2 : 0);
            int vfW = (holeAt(gi0 - 1, gk0) ? 4 : 0) | (holeAt(gi0, gk0) ? 8 : 0);
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + CELL + WT, false, sk, WHITE, vfW);
        }
        else if (wv == WALL_WINDOW) {   // window on z-running wall
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wb + 1.0f, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.1f, gz - WT, gx + WT, wt2, gz + CELL + WT, true);
            addBoxSides(wa, gx - WT, wb + 1.0f, gz - WT, gx + WT, wb + 2.1f, gz + 0.45f);
            addBoxSides(wa, gx - WT, wb + 1.0f, gz + 1.55f, gx + WT, wb + 2.1f, gz + CELL + WT);
            wa.quad({gx-WT,wb+1.0f,gz-WT},{gx+WT,wb+1.0f,gz-WT},{gx+WT,wb+1.0f,gz+CELL+WT},{gx-WT,wb+1.0f,gz+CELL+WT},
                    {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, WHITE);   // sill top
            if (w.level == 2) {   // see the x-running case: light void, both faces
                Color sky = { 226, 241, 246, 70 };
                wa.quad({gx+0.02f,wb+1.0f,gz+0.45f},{gx+0.02f,wb+1.0f,gz+1.55f},{gx+0.02f,wb+2.1f,gz+1.55f},{gx+0.02f,wb+2.1f,gz+0.45f},
                        {1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
                wa.quad({gx-0.02f,wb+1.0f,gz+1.55f},{gx-0.02f,wb+1.0f,gz+0.45f},{gx-0.02f,wb+2.1f,gz+0.45f},{gx-0.02f,wb+2.1f,gz+1.55f},
                        {-1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
            } else {
            Color glass = { 20, 26, 32, 100 };
            gl.quad({gx,wb+1.0f,gz+0.45f},{gx,wb+1.0f,gz+1.55f},{gx,wb+2.1f,gz+1.55f},{gx,wb+2.1f,gz+0.45f},
                    {1,0,0},{0,1},{1,1},{1,0},{0,0}, glass);
            }
        }
        else if (wv == WALL_EXIT) {   // exit doorway on z-running wall
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            bool crs = w.cursedExit(cx * CCELLS + i, cz * CCELLS + kk);
            Color glow = crs ? Color{ 255, 60, 40, 70 } : Color{ 255, 248, 225, 70 };
            wa.quad({gx,wb,gz+0.35f},{gx,wb,gz+1.65f},{gx,wb+2.3f,gz+1.65f},{gx,wb+2.3f,gz+0.35f},{1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
            wa.quad({gx,wb,gz+1.65f},{gx,wb,gz+0.35f},{gx,wb+2.3f,gz+0.35f},{gx,wb+2.3f,gz+1.65f},{-1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (w.level == 1)
                addSymbolDoor(pr, fx, 1, gz, gx, wb, crs, ih(gi0, gk0, w.sseed() ^ 0x51B1u),
                              w.wallWVal(gi0, gk0 + 1) == WALL_SOLID);
        }
        else if (wv == WALL_DOOR) {   // doorway on z-running wall
            // Merged the same way its x-running twin above is — see there.
            bool mN = w.wallWVal(gi0, gk0 - 1) == WALL_DOOR;
            bool mS = w.wallWVal(gi0, gk0 + 1) == WALL_DOOR;
            if (mN) addBoxSides(wa, gx - WT, wb + 2.3f, gz - WT, gx + WT, wt2, gz + 0.35f, true);
            else    addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            if (mS) addBoxSides(wa, gx - WT, wb + 2.3f, gz + 1.65f, gx + WT, wt2, gz + CELL + WT, true);
            else    addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            float tz0 = mN ? gz - WT : gz + 0.29f, tz1 = mS ? gz + CELL + WT : gz + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                if (!mN) addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 0.35f, TRIM_COL);
                if (!mS) addSolidBox(pr, xf, wb, gz + 1.65f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, wb + 2.30f, tz0, xf + TRIM_T, wb + 2.36f, tz1, TRIM_COL);
            }
            float fy0 = w.floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, gx - 0.07f, fy0, mN ? gz : gz + 0.35f,
                        gx + 0.07f, fy0 + 0.013f, mS ? gz + CELL : gz + 1.65f, SILL_COL);
        }
        else if (wv == WALL_LOCKED) {   // a door with the leaf still in it
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + 0.35f);
            addBoxSides(wa, gx - WT, wb, gz + 1.65f, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, wb + 2.3f, gz + 0.35f, gx + WT, wt2, gz + 1.65f, true);
            float fy0 = w.floorY(cx * CCELLS + i, cz * CCELLS + kk);
            addSolidBox(pr, gx - 0.025f, fy0, gz + 0.36f, gx + 0.025f, wb + 2.28f, gz + 1.64f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 0.35f, TRIM_COL);
                addSolidBox(pr, xf, wb, gz + 1.65f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, wb + 2.30f, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf - 0.02f, fy0 + 0.97f, gz + 1.34f,
                            xf + TRIM_T, fy0 + 1.07f, gz + 1.50f, LOCK_COL);
            }
        }
        if (w.level == 0 || w.level == 4) {
            // Thin timber trim catches grazing light. Keep the extrusion within
            // the collision clearance; no separate obstacle or draw call.
            Color trim = w.level == 0 ? Color{91, 71, 39, 254} : Color{67, 41, 34, 254};
            if (nv == WALL_SOLID) {
                float tS = w.floorY(gi0, gk0 - 1), tN = w.floorY(gi0, gk0);
                if (flS) addSolidBox(pr, gx, tS, gz-WT-0.025f, gx+CELL, tS + 0.13f, gz-WT, trim);
                if (flN) addSolidBox(pr, gx, tN, gz+WT, gx+CELL, tN + 0.13f, gz+WT+0.025f, trim);
            }
            if (wv == WALL_SOLID) {
                float tW = w.floorY(gi0 - 1, gk0), tE = w.floorY(gi0, gk0);
                if (flW) addSolidBox(pr, gx-WT-0.025f, tW, gz, gx-WT, tW + 0.13f, gz+CELL, trim);
                if (flN) addSolidBox(pr, gx+WT, tE, gz, gx+WT+0.025f, tE + 0.13f, gz+CELL, trim);
            }
        }
        // baked AO around this cell's walls: floor strip, ceiling strip, and a
        // wall-face strip on both sides (solid walls and windows; doorways stay clean)
        // A noclip wall has to be indistinguishable from its neighbours by
        // everything except the glitch, so it gets their creases too.
        // A rail is not a wall to crease against: it has its own contact shadow.
        bool nvWall = (blocksEdge(nv) && nv != WALL_RAIL);
        bool wvWall = (blocksEdge(wv) && wv != WALL_RAIL);
        if (nvWall) {
            float fyS = w.floorY(gi0, gk0 - 1) + 0.005f, fyN = w.floorY(gi0, gk0) + 0.005f;
            // Ceiling creases follow each side's own ceiling. Pinned to a fixed
            // wallH they detach the moment the floor moves, and a crease hanging
            // in clear air under a ceiling reads as a smear, not a shadow.
            float cyS = w.ceilY(gi0, gk0 - 1) - 0.005f, cyN = w.ceilY(gi0, gk0) - 0.005f;
            // span exactly one cell — neighbours butt up seamlessly, no double-blend overlap
            float x0 = gx, x1 = gx + CELL;
            if (flS) aoStrip({ x0, fyS, gz - WT }, { x1, fyS, gz - WT }, { 0, 0, -AOW }, { 0, 1, 0 }, 0);          // floor, -z side
            if (flN) aoStrip({ x0, fyN, gz + WT }, { x1, fyN, gz + WT }, { 0, 0, AOW }, { 0, 1, 0 }, 0);           // floor, +z side
            if (clS) aoStrip({ x0, cyS, gz - WT }, { x1, cyS, gz - WT }, { 0, 0, -AOW }, { 0, -1, 0 }, AOC);       // ceiling creases
            if (clN) aoStrip({ x0, cyN, gz + WT }, { x1, cyN, gz + WT }, { 0, 0, AOW }, { 0, -1, 0 }, AOC);
            if (flS) aoStrip({ x0, fyS, gz - WT - 0.006f }, { x1, fyS, gz - WT - 0.006f }, { 0, AOH, 0 }, { 0, 0, -1 }, 0);   // skirting shadow up the faces
            if (flN) aoStrip({ x0, fyN, gz + WT + 0.006f }, { x1, fyN, gz + WT + 0.006f }, { 0, AOH, 0 }, { 0, 0, 1 }, 0);
            if (clS) aoStrip({ x0, cyS + 0.005f, gz - WT - 0.006f }, { x1, cyS + 0.005f, gz - WT - 0.006f }, { 0, -AOH, 0 }, { 0, 0, -1 }, AOC);   // and down from the ceiling
            if (clN) aoStrip({ x0, cyN + 0.005f, gz + WT + 0.006f }, { x1, cyN + 0.005f, gz + WT + 0.006f }, { 0, -AOH, 0 }, { 0, 0, 1 }, AOC);
        }
        if (wvWall) {
            float fyW = w.floorY(gi0 - 1, gk0) + 0.005f, fyE = w.floorY(gi0, gk0) + 0.005f;
            float cyW = w.ceilY(gi0 - 1, gk0) - 0.005f, cyE = w.ceilY(gi0, gk0) - 0.005f;
            float z0 = gz, z1 = gz + CELL;
            if (flW) aoStrip({ gx - WT, fyW, z0 }, { gx - WT, fyW, z1 }, { -AOW, 0, 0 }, { 0, 1, 0 }, 0);
            if (flN) aoStrip({ gx + WT, fyE, z0 }, { gx + WT, fyE, z1 }, { AOW, 0, 0 }, { 0, 1, 0 }, 0);
            if (clW) aoStrip({ gx - WT, cyW, z0 }, { gx - WT, cyW, z1 }, { -AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            if (clN) aoStrip({ gx + WT, cyE, z0 }, { gx + WT, cyE, z1 }, { AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            if (flW) aoStrip({ gx - WT - 0.006f, fyW, z0 }, { gx - WT - 0.006f, fyW, z1 }, { 0, AOH, 0 }, { -1, 0, 0 }, 0);
            if (flN) aoStrip({ gx + WT + 0.006f, fyE, z0 }, { gx + WT + 0.006f, fyE, z1 }, { 0, AOH, 0 }, { 1, 0, 0 }, 0);
            if (clW) aoStrip({ gx - WT - 0.006f, cyW + 0.005f, z0 }, { gx - WT - 0.006f, cyW + 0.005f, z1 }, { 0, -AOH, 0 }, { -1, 0, 0 }, AOC);
            if (clN) aoStrip({ gx + WT + 0.006f, cyE + 0.005f, z1 }, { gx + WT + 0.006f, cyE + 0.005f, z0 }, { 0, -AOH, 0 }, { 1, 0, 0 }, AOC);
        }
        if (w.level == 1 && nv == WALL_SOLID && liftHash(gi0, gk0, w.sseed()) &&
            w.wallNVal(gi0 - 1, gk0) == WALL_SOLID && w.wallNVal(gi0 + 1, gk0) == WALL_SOLID) {
            // A lift. The article gives Level 1 "staircases, elevators, isolated
            // rooms, and hallways"; the doors are shut and nobody has found the
            // car, but the call button is lit, which is worse than if it were not.
            float sgn = (ih(gi0, gk0, w.sseed() ^ 0xE1E8u) & 1) ? 1.0f : -1.0f;
            float zf = gz + sgn * WT;
            auto zb = [&](float x0, float y0, float x1, float y1, float d0, float d1, Color c) {
                addSolidBox(pr, x0, y0, std::min(zf + sgn * d0, zf + sgn * d1), x1, y1, std::max(zf + sgn * d0, zf + sgn * d1), c);
            };
            Color frame = { 92, 94, 92, 254 }, leaf = { 142, 146, 144, 254 }, seam = { 40, 42, 42, 254 };
            zb(gx + 0.30f, nb, gx + 1.70f, nb + 2.46f, 0.0f, 0.03f, frame);                       // surround
            zb(gx + 0.38f, nb, gx + 0.995f, nb + 2.38f, 0.03f, 0.045f, leaf);                     // two leaves
            zb(gx + 1.005f, nb, gx + 1.62f, nb + 2.38f, 0.03f, 0.045f, leaf);
            zb(gx + 0.995f, nb, gx + 1.005f, nb + 2.38f, 0.03f, 0.042f, seam);
            zb(gx + 0.62f, nb + 2.52f, gx + 1.38f, nb + 2.70f, 0.0f, 0.03f, seam);                 // floor indicator
            zb(gx + 0.93f, nb + 2.55f, gx + 1.07f, nb + 2.67f, 0.03f, 0.036f, Color{ 255, 120, 40, 60 });
            zb(gx + 1.86f, nb + 1.00f, gx + 1.96f, nb + 1.30f, 0.0f, 0.02f, frame);               // call plate
            zb(gx + 1.89f, nb + 1.12f, gx + 1.93f, nb + 1.18f, 0.02f, 0.03f, Color{ 255, 236, 180, 60 });
        }
        if (w.level == 1) {   // spalls on the walls too, rarer than on the columns
            uint32_t sn = ih(gi0, gk0, w.sseed() ^ 0x5BA2u), sw2 = ih(gi0, gk0, w.sseed() ^ 0x5BA3u);
            if (nv == WALL_SOLID && sn % 13 == 0) {
                float sgn = (sn >> 4) & 1 ? 1.0f : -1.0f;
                addSpall(fx, pr, { gx + 0.5f + ((sn >> 5) & 7) * 0.14f, nb + 0.5f + ((sn >> 8) & 15) * 0.16f, gz + sgn * WT },
                         { 0, 0, sgn }, { 1, 0, 0 }, 0.30f, 0.34f, sn);
            }
            if (wv == WALL_SOLID && sw2 % 13 == 0) {
                float sgn = (sw2 >> 4) & 1 ? 1.0f : -1.0f;
                addSpall(fx, pr, { gx + sgn * WT, wb + 0.5f + ((sw2 >> 8) & 15) * 0.16f, gz + 0.5f + ((sw2 >> 5) & 7) * 0.14f },
                         { sgn, 0, 0 }, { 0, 0, 1 }, 0.30f, 0.34f, sw2);
            }
        }
        // ---- the building's fittings. Decals pressed off the wall and ceiling
        // faces, plus real (if tiny) geometry for the conduit and sprinklers.
        //
        // All of it is alpha 254: textured and opaque, but below the relief
        // threshold. A faceplate is flat moulded plastic — giving it the
        // world-space relief bump would ripple it like the wall behind it.
        //
        // These sit in their own mesh rather than in the props mesh, which is
        // already the biggest one in a chunk and is indexed with 16-bit indices
        // that nothing checks for overflow.
        {
            uint32_t gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            const Color FIXC = { 255, 255, 255, 254 };
            // A decal on an x-running (north) wall, and on a z-running (west)
            // one. `plus` picks which of the two faces it hangs on; the UVs
            // mirror with it, so signage reads the right way round from the
            // room that can actually see it. Size comes from FIXTURES, which is
            // also what drew the cell — see textures.h.
            auto decalN = [&](float xc, float yc, float zf, bool plus, int id) {
                const FixtureRect &f = FIXTURES[id];
                float x0 = xc - f.halfW, x1 = xc + f.halfW, y0 = yc - f.halfH, y1 = yc + f.halfH;
                if (plus) fx.quad({x0,y0,zf},{x1,y0,zf},{x1,y1,zf},{x0,y1,zf},{0,0,1},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
                else      fx.quad({x1,y0,zf},{x0,y0,zf},{x0,y1,zf},{x1,y1,zf},{0,0,-1},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
            };
            auto decalW = [&](float zc, float yc, float xf, bool plus, int id) {
                const FixtureRect &f = FIXTURES[id];
                float z0 = zc - f.halfW, z1 = zc + f.halfW, y0 = yc - f.halfH, y1 = yc + f.halfH;
                if (plus) fx.quad({xf,y0,z1},{xf,y0,z0},{xf,y1,z0},{xf,y1,z1},{1,0,0},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
                else      fx.quad({xf,y0,z0},{xf,y0,z1},{xf,y1,z1},{xf,y1,z0},{-1,0,0},
                                  {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
            };
            // Which fitting this wall edge carries, if any. Checked in order of
            // rarity, so a wall that qualifies for two gets the rarer one — an
            // exit sign beats a grille beats a switch beats an outlet, rather
            // than a pile of fittings on the one unlucky wall. The heights are
            // the ones a building actually uses: outlets at the skirting, a
            // switch at the handle, a return grille up near the ceiling.
            auto pick = [&](uint32_t h, float &yc, int &id) {
                if (w.level != 2 && h % EXITSIGN_RATE == 0) { yc = 2.44f; id = FIX_SIGN;   return true; }
                if (w.level != 2 && h % GRILLE_RATE == 0)   { yc = 2.10f; id = FIX_GRILLE; return true; }
                if (w.level != 2 && h % SWITCH_RATE == 0)   { yc = 1.22f; id = FIX_SWITCH; return true; }
                // A pool hall does not have mains sockets at ankle height, and
                // Level 2 is the one level meant to read as still maintained.
                if (w.level != 2 && h % OUTLET_RATE == 0) {
                    yc = 0.32f;
                    id = ((h >> 11) % OUTLET_BROKEN == 0) ? FIX_OUTLET_BROKEN : FIX_OUTLET;
                    return true;
                }
                return false;
            };
            if (nv == WALL_SOLID) {
                uint32_t h = ih(gi, gk, w.sseed() ^ 0x71F0u);
                float yc; int id;
                if (pick(h, yc, id) && ((h & 16) ? flN : flS)) {   // not on a face over a hole or a flight
                    bool plus = (h & 16) != 0;
                    float zf = plus ? gz + WT + 0.006f : gz - WT - 0.006f;
                    decalN(gx + 0.45f + ((h >> 7) & 7) * 0.155f, yc, zf, plus, id);
                }
            }
            if (wv == WALL_SOLID) {
                uint32_t h = ih(gi, gk, w.sseed() ^ 0x71F9u);
                float yc; int id;
                if (pick(h, yc, id) && ((h & 16) ? flN : flW)) {
                    bool plus = (h & 16) != 0;
                    float xf = plus ? gx + WT + 0.006f : gx - WT - 0.006f;
                    decalW(gz + 0.45f + ((h >> 7) & 7) * 0.155f, yc, xf, plus, id);
                }
            }
            // Ceiling: a supply diffuser lies flat in the tile grid, while a
            // sprinkler hangs below it on a dropper — flat-on-the-ceiling is
            // exactly wrong for a sprinkler, which you almost always see from
            // underneath and off to one side.
            uint32_t hc = ih(gi, gk, w.sseed() ^ 0x71E3u);
            float ccx = gx + CELL * 0.5f, ccz = gz + CELL * 0.5f;
            if (!clN) {
                // no ceiling here to put a diffuser or a sprinkler in
            } else if (w.level != 2 && hc % DIFFUSER_RATE == 0) {
                const FixtureRect &f = FIXTURES[FIX_DIFFUSER];
                float yq = cyc - 0.008f;
                fx.quad({ccx-f.halfW,yq,ccz-f.halfH},{ccx-f.halfW,yq,ccz+f.halfH},
                        {ccx+f.halfW,yq,ccz+f.halfH},{ccx+f.halfW,yq,ccz-f.halfH},{0,-1,0},
                        {f.u0,f.v0},{f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0}, FIXC);
            } else if (w.level != 2 && hc % SPRINK_RATE == 0) {
                const Color BRASS = { 158, 126, 66, 254 };
                addSolidBox(fx, ccx-0.016f, cyc-0.085f, ccz-0.016f, ccx+0.016f, cyc, ccz+0.016f, BRASS);
                addSolidBox(fx, ccx-0.033f, cyc-0.085f, ccz-0.033f, ccx+0.033f, cyc-0.070f, ccz+0.033f, BRASS);
                addSolidBox(fx, ccx-0.045f, cyc-0.100f, ccz-0.045f, ccx+0.045f, cyc-0.090f, ccz+0.045f, BRASS);
            }
            // Conduit runs along the top of a wall. Keyed on a bucket of cells
            // rather than a single one, so it comes out as a run of six with a
            // beginning and an end instead of a dotted line of stubs.
            // Conduit stands on a wall *face*, not inside the wall. The first
            // version ran it about the wall centreline, ±28 mm on a wall whose
            // half-thickness is 110, so every run in the building was sealed
            // inside the plasterboard and nothing was ever drawn. The run hash
            // picks the face as well as the run, so a run does not change sides
            // halfway along.
            const Color STEEL = { 138, 136, 130, 254 };
            const float CDY = 0.052f;          // how far it stands off the wall
            // Conduit runs under a ceiling; a wall climbing an opening has none.
            if (nv == WALL_SOLID && clN && clS) {
                float cy = nt - 0.155f;
                uint32_t hr = ih(gi / CONDUIT_RUN, gk, w.sseed() ^ 0x71C5u);
                if (hr % 7 == 0) {
                    float z0 = (hr & 32) ? gz + WT : gz - WT - CDY;
                    addSolidBox(fx, gx - WT, cy, z0, gx + CELL + WT, cy + 0.046f, z0 + CDY, STEEL);
                }
            }
            if (wv == WALL_SOLID && clN && clW) {
                float cy = wt2 - 0.155f;
                uint32_t hr = ih(gi, gk / CONDUIT_RUN, w.sseed() ^ 0x71CBu);
                if (hr % 7 == 0) {
                    float x0 = (hr & 32) ? gx + WT : gx - WT - CDY;
                    addSolidBox(fx, x0, cy, gz - WT, x0 + CDY, cy + 0.046f, gz + CELL + WT, STEEL);
                }
            }
        }
        // wall scrawl: rarely, a solid wall carries a phrase left by an earlier
        // wanderer. one of thirty-two, from the 4x8 scrawl atlas, drawn as a
        // decal pressed just off the wall face (level 2 is pristine tile — no
        // scrawl).
        //
        // SCRAWL_RATE is a rarity, not a decoration: at the old one-in-seven
        // every corridor had writing on it and the same eight phrases came back
        // within sight of each other, which reads as wallpaper. One in forty,
        // across thirty-two phrases, means seeing one is an event and seeing the
        // same one twice means something.
        if (w.level != 2) {
            uint32_t gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            auto uvOf = [](int ph, float &u0, float &v0, float &u1, float &v1) {
                u0 = (ph & 3) * 0.25f; v0 = (ph >> 2) * 0.125f; u1 = u0 + 0.25f; v1 = v0 + 0.125f;
            };
            // Nobody writes on a wall straight, and no two people picked up the
            // same pen. A small rotation and a tint per instance, so the same
            // atlas cell twice does not read as the same decal twice.
            auto tintOf = [](uint32_t hs) {
                static const Color T[4] = { { 255, 255, 255, 255 }, { 236, 228, 214, 255 },
                                            { 216, 210, 212, 255 }, { 248, 234, 208, 255 } };
                return T[(hs >> 17) & 3];   // alpha stays 255: see the shader's alpha coding
            };
            auto tiltOf = [](uint32_t hs) { return ((int)((hs >> 12) & 15) - 7.5f) * 0.0085f; };
            if (nv == WALL_SOLID) {
                uint32_t hs = ih(gi, gk, w.sseed() ^ 0x5C1Bu);
                if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flS)) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % SCRAWL_PHRASES, u0, v0, u1, v1);
                    float y0 = nb + 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float x0 = gx + 0.28f, x1 = gx + 1.72f;
                    float zf = (hs & 8) ? gz + WT + 0.006f : gz - WT - 0.006f;
                    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
                    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
                    float an = tiltOf(hs), cq = cosf(an), sq = sinf(an);
                    auto co = [&](float sx, float sy) {
                        return Vector3{ mx + sx * hw * cq - sy * hh * sq, my + sx * hw * sq + sy * hh * cq, zf };
                    };
                    Color tc = tintOf(hs);
                    if (hs & 8) scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {0,0,1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                    else        scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {0,0,-1},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                }
            }
            if (wv == WALL_SOLID) {
                uint32_t hs = ih(gi, gk, w.sseed() ^ 0x5C2Du);
                if (hs % SCRAWL_RATE == 0 && ((hs & 8) ? flN : flW)) {
                    float u0, v0, u1, v1; uvOf((hs >> 5) % SCRAWL_PHRASES, u0, v0, u1, v1);
                    float y0 = wb + 0.95f + ((hs >> 9) & 3) * 0.12f, y1 = y0 + 0.66f;
                    float z0 = gz + 0.28f, z1 = gz + 1.72f;
                    float xf = (hs & 8) ? gx + WT + 0.006f : gx - WT - 0.006f;
                    float mz = (z0 + z1) * 0.5f, my = (y0 + y1) * 0.5f;
                    float hd = (z1 - z0) * 0.5f, hh = (y1 - y0) * 0.5f;
                    float an = tiltOf(hs), cq = cosf(an), sq = sinf(an);
                    auto co = [&](float sz, float sy) {
                        return Vector3{ xf, my + sz * hd * sq + sy * hh * cq, mz + sz * hd * cq - sy * hh * sq };
                    };
                    Color tc = tintOf(hs);
                    if (hs & 8) scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                    else        scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {-1,0,0},
                                        {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
                }
            }
        }
        if (dd.pillar[i][kk]) {
            addBoxSides(wa, gx + 0.42f, fyc, gz + 0.42f, gx + 1.58f, cyc, gz + 1.58f);
            uint32_t sh = ih(gi0, gk0, w.sseed() ^ 0x5BA1u);
            if (w.level == 1 && sh % 3 == 0) {   // a column with its cover blown off
                int f = (int)((sh >> 3) & 3);
                const Vector3 NS[4] = { {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0} };
                Vector3 n = NS[f], u = (f < 2) ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 };
                float off = ((sh >> 6) & 1) ? 0.28f : -0.28f;   // toward a corner, where it goes first
                Vector3 c = { gx + 1.0f + n.x * 0.58f + u.x * off, fyc + 0.7f + ((sh >> 8) & 15) / 15.0f * 2.0f,
                              gz + 1.0f + n.z * 0.58f + u.z * off };
                addSpall(fx, pr, c, n, u, 0.24f, 0.30f + ((sh >> 12) & 7) * 0.03f, sh);
            }
            addContactShadow(ao, gx + 1.0f, gz + 1.0f, fyc, 0.0f, 0.58f, 0.58f);
            // AO up the pillar's feet and a ceiling crease around its head
            float pfy = fyc + 0.005f;
            float px0 = gx + 0.42f, px1 = gx + 1.58f, pz0 = gz + 0.42f, pz1 = gz + 1.58f, cy = cyc - 0.005f;
            aoStrip({ px0, pfy, pz0 - 0.006f }, { px1, pfy, pz0 - 0.006f }, { 0, AOH, 0 }, { 0, 0, -1 }, 0);
            aoStrip({ px0, pfy, pz1 + 0.006f }, { px1, pfy, pz1 + 0.006f }, { 0, AOH, 0 }, { 0, 0, 1 }, 0);
            aoStrip({ px0 - 0.006f, pfy, pz0 }, { px0 - 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { -1, 0, 0 }, 0);
            aoStrip({ px1 + 0.006f, pfy, pz0 }, { px1 + 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { 1, 0, 0 }, 0);
            aoStrip({ px0, cy, pz0 }, { px1, cy, pz0 }, { 0, 0, -AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz1 }, { px1, cy, pz1 }, { 0, 0, AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz0 }, { px0, cy, pz1 }, { -AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            aoStrip({ px1, cy, pz0 }, { px1, cy, pz1 }, { AOW, 0, 0 }, { 0, -1, 0 }, AOC);
        }
        if (dd.prop[i][kk] != PROP_NONE) {
            PropSite site = { gx + 1.0f, gz + 1.0f,
                              dd.elev[i][kk] * ELEV_UNIT,     // furniture sits on the local floor
                              (dd.propRot[i][kk] & 3) * 1.5708f,   // a quarter turn at a time
                              cx * CCELLS + i, cz * CCELLS + kk };
            if (dd.prop[i][kk] == PROP_VENDING) {
                float bx0, bz0, bx1, bz1;
                vendFootprint(dd.propRot[i][kk], gx + 1.0f, gz + 1.0f, site.cx, site.cz, bx0, bz0, bx1, bz1);
            }
            addProp(dd.prop[i][kk], site, w.sseed(), w.level, pr, ce, ao, fx);
        }
    }
    // ---- the flights that rise from this storey (stampFeature has the plan).
    // Steps belong to the storey they stand on; the storey above draws only
    // its hole, its rails and the floor you arrive on.
    for (int q = 0; q < d.nfeat; q++) {
        const VertFeat &f = d.feats[q];
        if (f.lo != w.qs) continue;
        auto to = [&](float u, float y, float v) { return toRl(w.featureWorld(f, cx, cz, u, y, v)); };
        const float R = w.storeyH / 24.0f;
        if (f.kind == VK_STAIRWELL) {
            // Lane A: 12 risers from the foot of the stair (plain floor, drawn
            // with the rest) to the half landing.
            const float G = 4.0f / 12.0f;
            Vector3 a, b, c, e;
            for (int i = 0; i < 12; i++)
                addStep(fl, pr, to, 0, CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
            // The half landing, right across the shaft at the far end.
            a = to(0, 12 * R, 6); b = to(2 * CELL, 12 * R, 6); c = to(2 * CELL, 12 * R, 8); e = to(0, 12 * R, 8);
            fl.quad(a, b, c, e, { 0, 1, 0 }, { a.x / 2, a.z / 2 }, { b.x / 2, b.z / 2 }, { c.x / 2, c.z / 2 },
                    { e.x / 2, e.z / 2 }, WHITE);
            // Lane B: 12 more, climbing back toward the door above. Its top
            // landing is the upper storey's floor, drawn by that storey.
            for (int j = 0; j < 12; j++)
                addStep(fl, pr, to, CELL, 2 * CELL, 6 - (j + 1) * G, 6 - j * G, 0, 12 * R + (j + 1) * R, false, false);
            // The landing light: a batten on the end wall, 2.25 m over the half
            // landing, where it lights both flights. Its steel body is here;
            // its tube is drawn by the renderer, because it goes out in a
            // blackout and a chunk mesh cannot. It is the shaft's only light:
            // the tray grid has no fitting over an opening.
            Vector3 lamp = toRl(w.landingLamp(f, cx, cz));
            Vector3 lo2 = to(CELL - 0.55f, lamp.y - 0.06f, 8 - WT - 0.13f), hi2 = to(CELL + 0.55f, lamp.y + 0.06f, 8 - WT);
            addSolidBox(pr, std::min(lo2.x, hi2.x), lamp.y - 0.06f, std::min(lo2.z, hi2.z),
                        std::max(lo2.x, hi2.x), lamp.y + 0.06f, std::max(lo2.z, hi2.z), Color{ 150, 150, 142, 254 });
        } else if (f.kind == VK_STAIR || f.stairU >= 0) {
            // A straight flight: 24 risers over rows 1..4, between its wall and
            // its balustrade (both of which cover the steps' ends).
            int s0 = f.kind == VK_STAIR ? 0 : f.stairU, s1 = f.kind == VK_STAIR ? f.wu - 1 : f.stairU;
            const float G = 8.0f / 24.0f;
            for (int i = 0; i < 24; i++)
                addStep(fl, pr, to, s0 * CELL, (s1 + 1) * CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
        }
    }
    if (d.manila) {
        float mx = wx + (MANILA_HI + 1 - 2) * CELL, mz = wz + (MANILA_HI + 1 - 2) * CELL;
        addManilaRoom(pr, fx, ao, mx, mz, w.ceilY(cellOf(mx), cellOf(mz)), ih(cx, cz, w.sseed() ^ 0x3A11u));
    }
    if (w.level == 3 || w.level == 1) {
        // Service pipework — the Red Halls' plumbing, and Level 1's: a warehouse
        // with "a consistent supply of water and electricity" has to carry both
        // somewhere, and the ceiling is where it does. Runs are decided per *row* rather than per cell, so a
        // pipe follows a whole corridor the way a real service run does instead of
        // appearing in patches. Consecutive cells emit abutting segments, so the
        // run reads as one continuous pipe.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            // pipes hug the walls they run beside
            // Through the accessors, like the wall geometry above: a pipe hugs
            // a wall, so an edge the building has closed behind you should grow
            // one on the rebake rather than stay bare.
            if (w.wallNVal(gi, gk) == WALL_SOLID) {
                uint32_t rh = ih(gk, 7717, w.sseed() ^ 0x9191u);
                if (rh % 4 == 0) {
                    float py = w.ceilY(gi, gk) - 0.22f - ((rh >> 5) & 3) * 0.09f;
                    float side = ((rh >> 9) & 1) ? 0.34f : -0.34f;
                    float r = 0.065f + ((rh >> 11) & 3) * 0.012f;
                    Color pc = ((rh >> 13) & 1) ? Color{ 78, 54, 40, 255 }   // rusted iron
                                                : Color{ 62, 60, 66, 255 };  // dull steel
                    addSolidBox(pr, gx, py - r, gz + side - r, gx + CELL, py + r, gz + side + r, pc);
                    if (((gi * 2654435761u) & 3) == 0)   // a collar every few metres
                        addSolidBox(pr, gx + 0.85f, py - r - 0.03f, gz + side - r - 0.03f,
                                    gx + 1.15f, py + r + 0.03f, gz + side + r + 0.03f,
                                    Color{ 96, 74, 56, 255 });
                }
            }
            if (w.wallWVal(gi, gk) == WALL_SOLID) {
                uint32_t rh = ih(gi, 3313, w.sseed() ^ 0x9292u);
                if (rh % 4 == 0) {
                    float py = w.ceilY(gi, gk) - 0.22f - ((rh >> 5) & 3) * 0.09f;
                    float side = ((rh >> 9) & 1) ? 0.34f : -0.34f;
                    float r = 0.065f + ((rh >> 11) & 3) * 0.012f;
                    Color pc = ((rh >> 13) & 1) ? Color{ 78, 54, 40, 255 }
                                                : Color{ 62, 60, 66, 255 };
                    addSolidBox(pr, gx + side - r, py - r, gz, gx + side + r, py + r, gz + CELL, pc);
                    if (((gk * 2654435761u) & 3) == 0)
                        addSolidBox(pr, gx + side - r - 0.03f, py - r - 0.03f, gz + 0.85f,
                                    gx + side + r + 0.03f, py + r + 0.03f, gz + 1.15f,
                                    Color{ 96, 74, 56, 255 });
                }
            }
            // valve station: a standpipe floor to ceiling, wheel drawn by the renderer
            if (w.valveAt(gi, gk)) {
                float vx = gx + 1.0f, vz = gz + 1.0f, fy = dd.elev[i][kk] * ELEV_UNIT;
                addSolidBox(pr, vx - 0.085f, fy, vz - 0.085f, vx + 0.085f, w.ceilY(gi, gk), vz + 0.085f,
                            Color{ 84, 60, 44, 255 });
                addSolidBox(pr, vx - 0.13f, fy + 1.02f, vz - 0.13f, vx + 0.13f, fy + 1.24f, vz + 0.13f,
                            Color{ 104, 80, 58, 255 });   // the body the wheel sits on
            }
        }
    }
    if (w.level == 4) {   // crepe streamers sag from the ceiling, in pairs of quads
        Rng srng(hash64(World::key(cx, cz) ^ 0xFE57AULL ^ (uint64_t)w.sseed()));
        int ns = 3 + srng.ri(0, 3);
        for (int s = 0; s < ns; s++) {
            float ax = wx + srng.f01() * CHUNK, az = wz + srng.f01() * CHUNK;
            float bx2 = ax + (srng.f01() - 0.5f) * 9, bz2 = az + (srng.f01() - 0.5f) * 9;
            float mx2 = (ax + bx2) * 0.5f, mz2 = (az + bz2) * 0.5f;
            float cA = w.ceilY((int)floorf(ax / CELL), (int)floorf(az / CELL));
            float cB = w.ceilY((int)floorf(bx2 / CELL), (int)floorf(bz2 / CELL));
            float ytop = std::min(cA, cB) - 0.03f, ymid = ytop - 0.52f - srng.f01() * 0.35f;
            Color sc = PARTY[srng.ri(0, 4)];
            float dx2 = bx2 - ax, dz2 = bz2 - az, dl = sqrtf(dx2 * dx2 + dz2 * dz2) + 1e-4f;
            Vector3 nrm = { dz2 / dl, 0, -dx2 / dl };
            Vector2 uvp = { 0.375f, 0.75f };   // plain-metal corner of the prop atlas: flat colour
            pr.quad({ ax, ytop, az }, { mx2, ymid + 0.06f, mz2 }, { mx2, ymid, mz2 }, { ax, ytop - 0.06f, az },
                    nrm, uvp, uvp, uvp, uvp, sc);
            pr.quad({ mx2, ymid + 0.06f, mz2 }, { bx2, ytop, bz2 }, { bx2, ytop - 0.06f, bz2 }, { mx2, ymid, mz2 },
                    nrm, uvp, uvp, uvp, uvp, sc);
        }
    }
    out.meshes[MESH_FLOOR]    = fl.bake();
    out.meshes[MESH_CEILING]  = ce.bake();
    out.meshes[MESH_WALLS]    = wa.bake();
    out.meshes[MESH_PROPS]    = pr.bake();
    out.meshes[MESH_WATER]    = wt.bake();
    out.meshes[MESH_SCRAWL]   = scr.bake();
    out.meshes[MESH_FIXTURES] = fx.bake();
    out.meshes[MESH_GLASS]    = gl.bake();
    out.meshes[MESH_AO]       = ao.bake();
}

static void unloadMeshes(ChunkMeshes &c) {
    for (Mesh &m : c.meshes)
        if (m.vertexCount > 0) UnloadMesh(m);
}

void ChunkMeshCache::drain(World &w) {
    for (const ChunkRef &r : w.staleChunks) {
        auto s = baked.find(r.storey);
        if (s == baked.end()) continue;
        auto it = s->second.find(World::key(r.cx, r.cz));
        if (it == s->second.end()) continue;
        unloadMeshes(it->second);
        s->second.erase(it);
    }
    w.staleChunks.clear();
}

bool ChunkMeshCache::ensure(World &w, int cx, int cz) {
    drain(w);
    w.data(cx, cz);
    auto &m = baked[w.qs];
    uint64_t k = World::key(cx, cz);
    if (m.count(k)) return false;
    bakeChunk(w, cx, cz, m[k]);
    return true;
}

const ChunkMeshes *ChunkMeshCache::find(World &w, int s, int cx, int cz) {
    drain(w);
    auto st = baked.find(s);
    if (st == baked.end()) return nullptr;
    auto it = st->second.find(World::key(cx, cz));
    return it == st->second.end() ? nullptr : &it->second;
}

ChunkMeshes &ChunkMeshCache::slot(int s, int cx, int cz) { return baked[s][World::key(cx, cz)]; }
