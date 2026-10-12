#include "../core/fp_strict.h"
#include "chunk_mesh.h"
#include "mesh_builder.h"
#include "atlas.h"     // FIXTURES and the vending machine's door: where each sits in the atlas
#include "palette.h"
#include "../core/hash.h"
#include "../core/layout.h"
#include "../core/level_rules.h"
#include <algorithm>
#include <cmath>

// Door trim: an architrave 24 mm proud of the wall face, and the threshold
// strip. Both sample the plain metal at (0.375, 0.75), as addSolidBox does.
static const float TRIM_T = 0.024f;
static const Rgba TRIM_COL = { 176, 170, 152, 254 };   // painted trim, no relief bump
static const Rgba SILL_COL = { 138, 136, 130, 254 };   // dulled metal threshold
// A locked door's leaf and lock plate. Alpha 254: opaque, but below the
// shader's relief threshold, or the flat slab takes the world-space bump.
static const Rgba LEAF_COL = { 150, 128, 96, 254 };
static const Rgba LOCK_COL = { 206, 194, 140, 254 };

// Baked contact-shadow tint. The AO strip texture carries the falloff in its
// alpha, so every crease and furniture shadow shares this colour.
static const Rgba AO_TINT = { 10, 9, 9, 255 };

// A rounded contact shadow for props and pillars: a solid footprint and a skirt
// that samples the AO gradient down to zero at its outer edge.
static void addContactShadow(MB &ao, float pcx, float pcz, float ey, float rot,
                             float hx2, float hz2) {
    float ca = cosf(rot), sa = sinf(rot);
    const float S = 0.24f;                    // how far the falloff reaches
    const float d = S * 0.7071f;              // the corner, cut across
    const Vec3 up = { 0, 1, 0 };
    auto P = [&](float lx, float lz) {
        return Vec3{ pcx + lx * ca - lz * sa, ey + 0.006f, pcz + lx * sa + lz * ca };
    };
    // the footprint: the darkest end of the gradient
    ao.quad(P(-hx2,-hz2), P(hx2,-hz2), P(hx2,hz2), P(-hx2,hz2), up,
            {0,0},{1,0},{1,0},{0,0}, AO_TINT);
    // four skirts fading outward, each wound the same way round as the core
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
    // and the corners, so the skirt closes without notches
    const float cn[4][4] = {
        {  hx2,  hz2,  1,  1 }, { -hx2,  hz2, -1,  1 },
        { -hx2, -hz2, -1, -1 }, {  hx2, -hz2,  1, -1 },
    };
    for (auto &c2 : cn) {
        Vec3 inner = P(c2[0], c2[1]);
        Vec3 pxv = P(c2[0] + c2[2] * S, c2[1]);
        Vec3 pmv = P(c2[0] + c2[2] * d, c2[1] + c2[3] * d);
        Vec3 pzv = P(c2[0], c2[1] + c2[3] * S);
        bool xFirst = (c2[2] * c2[3]) > 0;    // keeps the winding consistent
        Vec3 a1 = xFirst ? pxv : pzv, b1 = xFirst ? pzv : pxv;
        ao.tri(inner, a1, pmv, up, {0,0},{0,1},{1,1}, AO_TINT);
        ao.tri(inner, pmv, b1, up, {0,0},{1,1},{0,1}, AO_TINT);
    }
}

// A mesh builder that also knows how wall paper maps onto height. tileV:
// metres of wall one texture tile spans; 3 for a repeating pattern, Level 1's
// wall height because its concrete carries a damp band and pour joints at real
// heights (a 3 m repeat draws a second tide line under its 4.2 m slab).
// tallPaper (storeyed levels): a wall past one tile continues from the tile's
// clean middle a whole number of repeats down, so no second baseboard runs round
// a stair shaft at 3 m. Elsewhere a tall wall repeats its tile.
struct WallBuilder : MB {
    float tileV = 3.0f;
    bool tallPaper = false;
};
// The wallpaper's V at a height, for tallPaper.
static float wallV(float y, float tileV) { return y <= tileV + 1e-4f ? 1 - y / tileV : 1 - (y - tileV * 0.5f) / tileV; }
// A face over a void (an upper storey's wall seen from the shaft below) stands
// on no floor, so it shows no baseboard: it takes the tile's clean band, 0.75 m
// to 2.25 m, repeating by whole chevrons, split wherever that band wraps.
static void voidFace(WallBuilder &mb, Vec3 a0, Vec3 a1, Vec3 n, float ua, float ub, float y0, float y1, Rgba w) {
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
// skip bits: 1 = -z face, 2 = +z face, 4 = -x face, 8 = +x face. Wall runs
// overlap their neighbours by WT so corners close, which buries the end caps in
// the next box; skip a buried cap or it z-fights into a vertical seam.
// voidFaces: which faces look into a hole (same bits).
static void addBoxSides(WallBuilder &mb, float x0, float y0, float z0, float x1, float y1, float z1,
                        bool bottomFace = false, int skip = 0, Rgba w = RGBA_WHITE, int voidFaces = 0) {
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

// ---- rails: a papered knee wall under a timber cap. The top runs from top0 to
// top1 along the edge, so the same builder does the level rail round a hole and
// the balustrade that climbs a flight's nosing line. `bottom` closes the
// underside, seen only from below the opening.
static const Rgba RAIL_CAP = { 104, 80, 46, 254 };
static void addRailRun(WallBuilder &wa, MB &pr, float ax, float az, float bx, float bz,
                       float base, float top0, float top1, bool bottom, int voidSide = 0) {
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f) return;
    float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    const float cap = 0.055f, capW = RAIL_T + 0.02f;
    bool alongX = fabsf(ux) > 0.5f;
    auto P = [&](float a, float side, float y) { return Vec3{ ax + ux * a + nx * side, y, az + uz * a + nz * side }; };
    auto U = [&](float a) { return (alongX ? ax + ux * a : az + uz * a) / 3.0f; };
    auto V = [&](float y) { return 1.0f - y / wa.tileV; };
    float w0 = top0 - cap, w1 = top1 - cap;   // the paper stops under the cap
    for (int sg = -1; sg <= 1; sg += 2) {
        float sd = sg * RAIL_T;
        // The face over a hole tops the bulkhead below it: no baseboard, and the
        // paper continues from the bulkhead's (the soffit in bakeChunk).
        auto Vs = [&](float y) { return sg == voidSide ? 1.0f - (y + 2.0f) / wa.tileV : V(y); };
        wa.quad(P(0, sd, base), P(len, sd, base), P(len, sd, w1), P(0, sd, w0), { nx * sg, 0, nz * sg },
                { U(0), Vs(base) }, { U(len), Vs(base) }, { U(len), Vs(w1) }, { U(0), Vs(w0) }, RGBA_WHITE);
    }
    for (int e = 0; e < 2; e++) {   // the two ends, square across
        float a = e ? len : 0, w = e ? w1 : w0, sg = e ? 1.0f : -1.0f;
        wa.quad(P(a, -RAIL_T, base), P(a, RAIL_T, base), P(a, RAIL_T, w), P(a, -RAIL_T, w), { ux * sg, 0, uz * sg },
                { 0, V(base) }, { 0.05f, V(base) }, { 0.05f, V(w) }, { 0, V(w) }, RGBA_WHITE);
    }
    if (bottom)
        wa.quad(P(0, -RAIL_T, base), P(len, -RAIL_T, base), P(len, RAIL_T, base), P(0, RAIL_T, base), { 0, -1, 0 },
                { U(0), 0 }, { U(len), 0 }, { U(len), 0.05f }, { U(0), 0.05f }, RGBA_WHITE);
    // The cap: a timber slab a little proud of both faces, sloping with the top.
    const Vec2 m = PLAIN_UV;
    float slope = (top1 - top0) / len, sn = 1.0f / sqrtf(1 + slope * slope);
    Vec3 up = { -ux * slope * sn, sn, -uz * slope * sn };
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

// ---- stairs. One step: a solid block from the floor to its tread, carpeted on
// tread and riser, with an aluminium nosing. Built in the feature's local frame
// (u across, v along, y up) through `to`, a rotation, so normals stay
// unmirrored.
static const Rgba NOSING_COL = { 168, 160, 138, 254 };
template <class ToWorld>
static void addStep(MB &fl, MB &pr, ToWorld to, float u0, float u1, float va, float vb, float y0, float y1,
                    bool risesPlusV, bool sides) {
    // va..vb along the rise; the riser is on the downhill face
    float vr = risesPlusV ? va : vb;            // where the riser stands
    float vs = risesPlusV ? -1.0f : 1.0f;       // which way it faces, in v
    auto W = [&](float u, float y, float v) { return to(u, y, v); };
    auto uvOf = [](Vec3 p) { return Vec2{ p.x / 2, p.z / 2 }; };
    Vec3 t0 = W(u0, y1, va), t1 = W(u1, y1, va), t2 = W(u1, y1, vb), t3 = W(u0, y1, vb);
    fl.quad(t0, t1, t2, t3, { 0, 1, 0 }, uvOf(t0), uvOf(t1), uvOf(t2), uvOf(t3), RGBA_WHITE);
    Vec3 n0 = W(0, 0, 0), nv = W(0, 0, vs);
    Vec3 rn = { nv.x - n0.x, 0, nv.z - n0.z };
    Vec3 r0 = W(u0, y0, vr), r1 = W(u1, y0, vr), r2 = W(u1, y1, vr), r3 = W(u0, y1, vr);
    fl.quad(r0, r1, r2, r3, rn, { u0 / 2, y0 / 2 }, { u1 / 2, y0 / 2 }, { u1 / 2, y1 / 2 }, { u0 / 2, y1 / 2 }, RGBA_WHITE);
    if (sides) {
        for (int e = 0; e < 2; e++) {
            float u = e ? u1 : u0;
            Vec3 nu = W(e ? 1.0f : -1.0f, 0, 0);
            Vec3 sn = { nu.x - n0.x, 0, nu.z - n0.z };
            Vec3 s0 = W(u, y0, va), s1 = W(u, y0, vb), s2 = W(u, y1, vb), s3 = W(u, y1, va);
            fl.quad(s0, s1, s2, s3, sn, { va / 2, y0 / 2 }, { vb / 2, y0 / 2 }, { vb / 2, y1 / 2 }, { va / 2, y1 / 2 }, RGBA_WHITE);
        }
    }
    // the nosing: 40 mm of aluminium over the front edge of the tread
    if (y1 > 0.01f) {
        float a = vr, b = vr - vs * 0.04f;
        Vec3 q0 = W(u0 + 0.02f, y1 - 0.02f, std::min(a, b)), q1 = W(u1 - 0.02f, y1 + 0.004f, std::max(a, b));
        addSolidBox(pr, std::min(q0.x, q1.x), y1 - 0.02f, std::min(q0.z, q1.z),
                    std::max(q0.x, q1.x), y1 + 0.004f, std::max(q0.z, q1.z), NOSING_COL);
    }
}

// Build one piece of furniture: boxes in the props mesh, a contact shadow in
// the AO mesh, and for the fallen tile a hole in the ceiling mesh. Heights must
// match gatherCellAABBs and Game::bottleShelfY.
static void addProp(const PropPlacement &site, int level, MB &pr, MB &ce, MB &ao, MB &fx) {
    const float pcx = site.x, pcz = site.z, rot = site.yaw, ey = site.floorY;
    const uint32_t h = site.hash;
    float r1 = (h & 0xFF) / 255.0f, r2 = ((h >> 8) & 0xFF) / 255.0f, r3 = ((h >> 16) & 0xFF) / 255.0f;
    // UV regions of the props atlas (makePropsTex). Cardboard has two: a carton's
    // side, and its top with the flap seam, tape and label.
    const float CU0=0.01f, CV0=0.004f, CU1=0.24f, CV1=0.58f;
    const float KU0=0.01f, KV0=0.61f, KU1=0.24f, KV1=0.99f;
    const float FU0=0.26f, FV0=0.02f, FU1=0.49f, FV1=0.48f;       // cabinet front
    const float MU0=0.26f, MV0=0.52f, MU1=0.49f, MV1=0.98f;       // plain metal
    enum class Surface { Metal, Wood, Fabric, Cardboard };
    float ca = cosf(rot), sa = sinf(rot);
    // a sub-box placed relative to the prop centre, turned with it
    auto part = [&](float ox, float oz, float hx2, float hz2, float y0, float y1,
                    Surface surface, Rgba tint) {
        float u0=MU0,v0=MV0,u1=MU1,v1=MV1;
        if (surface==Surface::Wood) {u0=0.51f;v0=0.02f;u1=0.99f;v1=0.48f;}
        if (surface==Surface::Fabric) {u0=0.51f;v0=0.52f;u1=0.99f;v1=0.98f;}
        float tu0=u0,tv0=v0,tu1=u1,tv1=v1;
        if (surface==Surface::Cardboard) {u0=CU0;v0=CV0;u1=CU1;v1=CV1;tu0=KU0;tv0=KV0;tu1=KU1;tv1=KV1;}
        addPropBox(pr, pcx+ox*ca-oz*sa, pcz+ox*sa+oz*ca, rot, hx2,hz2,y0,y1,
                   u0,v0,u1,v1,tu0,tv0,tu1,tv1,tint,
                   surface==Surface::Fabric ? 0.016f : surface==Surface::Wood ? 0.006f : 0.0f);
    };
    auto roundPart = [&](float ox,float oz,float r0,float r1,float y0,float y1,Rgba tint) {
        const Vec2 uv = PLAIN_UV;
        float cx=pcx+ox*ca-oz*sa,cz=pcz+ox*sa+oz*ca;
        for (int i=0;i<16;++i) {
            float a=TAU*i/16,b=TAU*(i+1)/16;
            Vec3 p0{cx+r0*cosf(a),y0,cz+r0*sinf(a)},p1{cx+r0*cosf(b),y0,cz+r0*sinf(b)};
            Vec3 p2{cx+r1*cosf(b),y1,cz+r1*sinf(b)},p3{cx+r1*cosf(a),y1,cz+r1*sinf(a)};
            float ny=(r0-r1)/std::max(0.001f,y1-y0),inv=1/sqrtf(1+ny*ny);
            pr.quad(p0,p1,p2,p3,{cosf((a+b)/2)*inv,ny*inv,sinf((a+b)/2)*inv},uv,uv,uv,uv,tint);
            pr.tri({cx,y1,cz},p3,p2,{0,1,0},uv,uv,uv,tint);
        }
    };
    auto blob = [&](float hx, float hz) {
        addContactShadow(ao, pcx, pcz, ey, rot, hx, hz);
    };
    switch (site.kind) {
    case PROP_BOXES: {   // cartons; on Level 4 wrapped like presents
        blob(0.40f, 0.40f);
        float bh = 0.55f + r1 * 0.2f, bhx = 0.34f + r2 * 0.08f;
        auto wrap = [&](int rot2) {
            if (level != 4) return RGBA_WHITE;
            Rgba c = PARTY_RGBA[(h >> rot2) % 5];
            return Rgba{ cl8(c.r * 0.9f + 46), cl8(c.g * 0.9f + 46), cl8(c.b * 0.9f + 46), 255 };
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
        // 2.994: just under Level 0's 3 m ceiling, the only level with this prop.
        Rgba hole = { 12, 11, 9, 51 };
        ce.quad({pcx-0.85f,2.994f,pcz-0.85f},{pcx-0.85f,2.994f,pcz+0.85f},
                {pcx+0.85f,2.994f,pcz+0.85f},{pcx+0.85f,2.994f,pcz-0.85f},{0,-1,0},
                {0,0},{0,1},{1,1},{1,0}, hole);
        float bx0 = pcx - 0.58f * ca, bz0 = pcz - 0.58f * sa;   // base edge on floor
        float tx = pcx + 0.35f * ca, tz = pcz + 0.35f * sa;     // top edge, lifted
        Vec3 a = { bx0 - 0.58f * sa, ey + 0.02f, bz0 + 0.58f * ca };
        Vec3 b = { bx0 + 0.58f * sa, ey + 0.02f, bz0 - 0.58f * ca };
        Vec3 c2 = { tx + 0.58f * sa, ey + 0.42f, tz - 0.58f * ca };
        Vec3 dq = { tx - 0.58f * sa, ey + 0.42f, tz + 0.58f * ca };
        ce.quad(a, b, c2, dq, { -ca * 0.5f, 0.87f, -sa * 0.5f },
                {0.05f,0.45f},{0.45f,0.45f},{0.45f,0.05f},{0.05f,0.05f}, RGBA_WHITE);
        Rgba deb = { 110, 105, 95, 255 };
        ce.quad({pcx+0.4f,ey+0.012f,pcz+0.5f},{pcx+0.75f,ey+0.012f,pcz+0.55f},
                {pcx+0.7f,ey+0.012f,pcz+0.85f},{pcx+0.38f,ey+0.012f,pcz+0.8f},{0,1,0},
                {0.1f,0.1f},{0.3f,0.1f},{0.3f,0.3f},{0.1f,0.3f}, deb);
        ce.quad({pcx-0.7f,ey+0.012f,pcz-0.35f},{pcx-0.45f,ey+0.012f,pcz-0.42f},
                {pcx-0.4f,ey+0.012f,pcz-0.2f},{pcx-0.68f,ey+0.012f,pcz-0.15f},{0,1,0},
                {0.3f,0.3f},{0.45f,0.3f},{0.45f,0.45f},{0.3f,0.45f}, deb);
        break;
    }
    case PROP_COUCH: {   // couch
        blob(0.74f, 0.48f);
        Rgba uph = { 172, 152, 96, 255 };
        part(0, 0.10f, 0.78f, 0.42f, ey + 0.16f, ey + 0.44f, Surface::Fabric, uph);   // seat
        part(0, -0.36f, 0.78f, 0.14f, ey + 0.16f, ey + 0.92f, Surface::Fabric, uph);  // backrest
        part(-0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);      // arms
        part( 0.64f, 0.06f, 0.14f, 0.46f, ey, ey + 0.62f, Surface::Fabric, uph);
        part(0, 0.10f, 0.74f, 0.38f, ey, ey + 0.16f, Surface::Fabric, Rgba{ 120, 106, 70, 255 });
        // seat cushions and piping, inside the same collision box
        for (int i=-1;i<=1;++i) {
            part(i*0.41f,0.12f,0.196f,0.34f,ey+0.44f,ey+0.49f,Surface::Fabric,uph);
            part(i*0.41f,0.454f,0.192f,0.006f,ey+0.452f,ey+0.461f,Surface::Fabric,{213,191,131,255});
        }
        break;
    }
    case PROP_ARMOIRE:     // armoire
        blob(0.54f, 0.46f);
        part(0, 0, 0.44f, 0.36f, ey, ey + 1.78f, Surface::Wood, Rgba{ 118, 82, 58, 255 });
        part(0, 0, 0.48f, 0.40f, ey + 1.78f, ey + 1.90f, Surface::Wood, Rgba{ 92, 63, 44, 255 });  // cornice
        part(0, 0.37f, 0.015f, 0.015f, ey + 0.85f, ey + 1.0f, Surface::Metal, Rgba{ 190, 170, 110, 255 }); // handles
        for (int i=-1;i<=1;i+=2)
            part(i*0.218f,0.362f,0.193f,0.003f,ey+0.15f,ey+1.67f,Surface::Wood,{155,113,79,255});
        break;
    case PROP_LAMP:     // floor lamp
        blob(0.22f, 0.22f);
        part(0, 0, 0.14f, 0.14f, ey, ey + 0.05f, Surface::Metal, Rgba{ 66, 62, 60, 255 });
        part(0, 0, 0.025f, 0.025f, ey, ey + 1.34f, Surface::Metal, Rgba{ 66, 62, 60, 255 });
        roundPart(0.05f,0,0.17f,0.10f,ey+1.30f,ey+1.60f,{214,190,142,254});
        break;
    case PROP_NIGHTSTAND:     // nightstand
        blob(0.36f, 0.36f);
        part(0, 0, 0.26f, 0.26f, ey, ey + 0.55f, Surface::Wood, Rgba{ 126, 90, 62, 255 });
        part(0, 0, 0.30f, 0.30f, ey + 0.55f, ey + 0.60f, Surface::Wood, Rgba{ 104, 74, 50, 255 });
        break;
    case PROP_BED: {   // bed: frame, mattress, headboard
        blob(0.60f, 1.02f);
        Rgba wd = { 110, 78, 54, 255 };
        part(0, 0, 0.52f, 0.92f, ey + 0.12f, ey + 0.26f, Surface::Wood, wd);          // frame
        part(0, 0.04f, 0.48f, 0.86f, ey + 0.26f, ey + 0.46f, Surface::Fabric, Rgba{ 216, 208, 188, 255 }); // mattress
        part(0, -0.97f, 0.52f, 0.05f, ey, ey + 0.95f, Surface::Wood, wd);             // headboard
        break;
    }
    case PROP_PARTY_TABLE: {  // party table: cloth, cake, cups
        blob(0.52f, 0.52f);
        float ty = 0.74f;
        const uint32_t th = site.hash2;
        Rgba cloth = PARTY_RGBA[th % 5];
        part(0, 0, 0.55f, 0.55f, ey + ty - 0.05f, ey + ty, Surface::Fabric, cloth);
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            part(lx * 0.44f, lz * 0.44f, 0.035f, 0.035f, ey, ey + ty - 0.05f,
                 Surface::Metal, Rgba{ 120, 118, 112, 255 });
        part(0, 0, 0.17f, 0.17f, ey + ty, ey + ty + 0.16f, Surface::Metal, Rgba{ 238, 232, 220, 255 });   // cake
        part(0, 0, 0.11f, 0.11f, ey + ty + 0.16f, ey + ty + 0.26f, Surface::Metal, Rgba{ 232, 152, 172, 255 });
        part(0, 0, 0.013f, 0.013f, ey + ty + 0.26f, ey + ty + 0.37f, Surface::Metal, Rgba{ 240, 226, 172, 255 }); // candle
        {   // paper cups set out around the cake, in party colours
            int ncup = 3 + (th % 4);
            for (int c = 0; c < ncup; c++) {
                uint32_t ch = th * 2654435761u + (uint32_t)c * 40503u;
                float ang = (ch & 0xFFFF) / 65535.0f * TAU;
                float rad = 0.30f + ((ch >> 16) & 0xFF) / 255.0f * 0.15f;
                part(cosf(ang) * rad, sinf(ang) * rad, 0.04f, 0.04f,
                     ey + ty, ey + ty + 0.09f, Surface::Metal, PARTY_RGBA[(ch >> 5) % 5]);
            }
        }
        {   // the candle flame: two crossed emissive fins that survive blackouts
            auto fpt = [&](float lx, float ly2, float lz) {
                return Vec3{ pcx + lx * ca - lz * sa, ly2, pcz + lx * sa + lz * ca };
            };
            Rgba flame = { 255, 196, 110, 70 };   // alpha <0.4: raw emissive in the shader
            float fy0 = ey + ty + 0.37f, fy1 = fy0 + 0.055f;
            pr.quad(fpt(-0.022f, fy0, 0), fpt(0.022f, fy0, 0), fpt(0.013f, fy1, 0), fpt(-0.013f, fy1, 0),
                    { sa, 0, -ca }, {0,1},{1,1},{1,0},{0,0}, flame);
            pr.quad(fpt(0, fy0, -0.022f), fpt(0, fy0, 0.022f), fpt(0, fy1, 0.013f), fpt(0, fy1, -0.013f),
                    { ca, 0, sa }, {0,1},{1,1},{1,0},{0,0}, flame);
        }
        break;
    }
    case PROP_VENDING: {  // vending machine
        // A glass-front drink machine at real size (0.88 x 1.83 x 0.72 m): a cabinet,
        // a door cut round its two openings, and the painted front in the fixtures
        // atlas (drawVendingFront, laid out by VEND_* in textures.h). The cans are
        // painted on the back of the cabinet 6 cm behind the glass, behind real
        // shelves. Lit machines use vertex alpha 240 (backlit) for the header, display,
        // price strips and cabinet interior; one in six is dead (254).
        blob(0.50f, 0.42f);
        bool lit = (h >> 24) % 6 != 0;
        const unsigned char GLOW = lit ? 240 : 254;
        // Door x runs to the viewer's right facing the front, which is local -x:
        // seen from the front, local +x is on the left.
        auto P = [&](float x, float y, float lz) {
            return Vec3{ pcx - x * ca - lz * sa, ey + y, pcz - x * sa + lz * ca };
        };
        const Vec3 fn = { sa, 0, -ca };                     // the front's normal
        // No two visible faces share a plane and nothing is layered a hair over
        // anything else, or a mobile GPU's depth buffer flickers the box fronts
        // through the paint and cans. Boxes leave out every face something else covers;
        // the paint is the door's front, and the display is cut into the door.
        enum { FRONT = 1, BACK = 2, SIDES = 4, TOP = 8, BOTTOM = 16, ALL = 31 };
        auto sbox = [&](float x0, float x1, float y0, float y1, float z0, float z1, Rgba c, int faces = ALL) {
            Vec3 a = P(x0, y0, z0), b = P(x1, y1, z1);
            float wx0 = std::min(a.x, b.x), wx1 = std::max(a.x, b.x), wz0 = std::min(a.z, b.z), wz1 = std::max(a.z, b.z);
            float wy0 = y0 + ey, wy1 = y1 + ey;
            const Vec2 u = PLAIN_UV;               // the plain metal addSolidBox samples
            auto emit = [&](Vec3 q0, Vec3 q1, Vec3 q2, Vec3 q3, Vec3 n) {
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
                    Rgba{ 255, 255, 255, alpha });
        };
        const float ZF = -0.362f, ZBODY = -0.30f, ZBACK = VEND_DEPTH_BACK;
        // the cabinet: painted steel sides in one of four colours
        static const Rgba SIDE_COLS[4] = { { 52, 54, 60, 254 }, { 118, 30, 28, 254 },
                                            { 34, 48, 84, 254 }, { 178, 172, 158, 254 } };
        Rgba side = SIDE_COLS[(h >> 20) & 3];
        const Rgba door = { 48, 50, 55, 254 }, black = { 18, 18, 20, 254 };
        sbox(-0.42f, 0.42f, 0.0f, VEND_Y0, -0.33f, ZBACK - 0.02f, black, ALL & ~TOP);   // plinth, set back
        // its front is covered by the door, the cans and the flap
        sbox(-VEND_HW, VEND_HW, VEND_Y0, VEND_Y1, ZBODY, ZBACK, side, ALL & ~FRONT & ~BOTTOM);
        // the door, round its two openings (VEND_WIN, VEND_BIN): the paint is its
        // front, and its back is against the cabinet
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
        // proud of the paint: the coin return lever, the lock, and the lip of the coin
        // cup (backs against the door, left out)
        const Rgba chrome = { 186, 188, 194, 254 };
        sbox(0.232f, 0.288f, 1.090f, 1.125f, ZF - 0.012f, ZF, chrome, ALL & ~BACK);
        sbox(0.385f, 0.405f, 0.545f, 0.565f, ZF - 0.016f, ZF, chrome, ALL & ~BACK);
        sbox(0.235f, 0.385f, 0.330f, 0.345f, ZF - 0.022f, ZF, chrome, ALL & ~BACK);
        // the push flap, 2 cm inside the opening
        face(bn.x0, bn.y0, bn.x1, bn.y1, -0.342f, 254);
        // behind the glass: the lit cabinet back with the cans painted on it (where
        // the cabinet front would be), and six shelves. A shelf is its top, underside
        // and price strip; its ends would lie in the stiles and its back in the cans.
        const float ZLIP = -0.345f;
        face(w.x0, w.y0, w.x1, w.y1, ZBODY, GLOW);
        for (int r = 0; r < VEND_ROWS; r++) {
            float yr = VEND_ROW0 + r * VEND_ROWP;
            sbox(w.x0, w.x1, yr - 0.022f, yr + 0.004f, ZLIP, ZBODY, Rgba{ 58, 60, 64, 254 }, TOP | BOTTOM);
            float v0 = (VEND_STRIP_PX[1] + r * 12 + VEND_STRIP_PX[3]) / (float)FIX_ATLAS_H;
            float v1 = (VEND_STRIP_PX[1] + r * 12) / (float)FIX_ATLAS_H;
            float u0 = VEND_STRIP_PX[0] / (float)FIX_ATLAS_W, u1 = (VEND_STRIP_PX[0] + VEND_STRIP_PX[2]) / (float)FIX_ATLAS_W;
            fx.quad(P(w.x0, yr - 0.022f, ZLIP), P(w.x1, yr - 0.022f, ZLIP),
                    P(w.x1, yr + 0.004f, ZLIP), P(w.x0, yr + 0.004f, ZLIP), fn,
                    { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 }, Rgba{ 255, 255, 255, GLOW });
        }
        // the glass last, so it blends over everything behind it (alpha 100: the
        // shader's window-glass path)
        const Vec2 m = PLAIN_UV;
        fx.quad(P(w.x0, w.y0, -0.352f), P(w.x1, w.y0, -0.352f), P(w.x1, w.y1, -0.352f), P(w.x0, w.y1, -0.352f),
                fn, m, m, m, m, Rgba{ 7, 8, 9, 100 });
        break;
    }
    case PROP_DESK: {  // office desk and chair
        blob(0.72f, 0.52f);
        Rgba wd = { 104, 80, 56, 255 };
        part(0, -0.12f, 0.62f, 0.34f, ey + 0.70f, ey + 0.74f, Surface::Wood, wd);      // desktop
        part(-0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);         // pedestals
        part( 0.46f, -0.12f, 0.14f, 0.30f, ey, ey + 0.70f, Surface::Wood, wd);
        part(0.08f, -0.22f, 0.19f, 0.035f, ey + 0.76f, ey + 1.08f, Surface::Metal, Rgba{ 30, 30, 34, 255 }); // monitor
        part(0.08f, -0.14f, 0.06f, 0.06f, ey + 0.74f, ey + 0.77f, Surface::Metal, Rgba{ 38, 38, 42, 255 });  // its foot
        part(-0.30f, -0.14f, 0.11f, 0.08f, ey + 0.74f, ey + 0.765f, Surface::Metal, Rgba{ 200, 196, 186, 255 }); // papers
        part(0.02f + r1 * 0.1f, 0.44f, 0.20f, 0.20f, ey + 0.40f, ey + 0.46f, Surface::Fabric, Rgba{ 52, 50, 54, 255 }); // chair seat
        part(0.02f + r1 * 0.1f, 0.62f, 0.20f, 0.04f, ey + 0.46f, ey + 0.96f, Surface::Fabric, Rgba{ 52, 50, 54, 255 }); // backrest
        part(0.02f + r1 * 0.1f, 0.44f, 0.035f, 0.035f, ey, ey + 0.40f, Surface::Metal, Rgba{ 72, 72, 76, 255 });        // post
        break;
    }
    case PROP_SHELVING: {  // steel shelving
        blob(0.68f, 0.32f);
        Rgba mt = { 132, 136, 142, 255 };
        for (int s2 = 0; s2 <= 3; s2++)
            part(0, 0, 0.60f, 0.24f, ey + 0.08f + s2 * 0.55f, ey + 0.12f + s2 * 0.55f, Surface::Metal, mt);
        // corner posts, not panels, so the rack is see-through from the side
        for (int ux = -1; ux <= 1; ux += 2) for (int uz = -1; uz <= 1; uz += 2)
            part(ux * 0.575f, uz * 0.215f, 0.03f, 0.03f, ey, ey + 1.80f, Surface::Metal, mt);
        part(-0.25f, 0.0f, 0.16f, 0.16f, ey + 0.12f, ey + 0.44f, Surface::Cardboard, Rgba{ 168, 138, 100, 255 });  // stock
        part( 0.30f, 0.02f, 0.14f, 0.14f, ey + 0.67f, ey + 0.94f, Surface::Cardboard, Rgba{ 150, 122, 88, 255 });
        if (r2 > 0.4f)
            part(-0.06f, -0.02f, 0.12f, 0.12f, ey + 1.22f, ey + 1.44f, Surface::Cardboard, Rgba{ 174, 146, 106, 255 });
        break;
    }
    case PROP_COOLER: {  // water cooler
        blob(0.30f, 0.30f);
        part(0, 0, 0.19f, 0.19f, ey, ey + 0.94f, Surface::Metal, Rgba{ 204, 206, 210, 255 });   // body
        roundPart(0,0,0.10f,0.125f,ey+0.94f,ey+1.24f,{150,186,214,254});
        roundPart(0,0,0.125f,0.10f,ey+1.24f,ey+1.28f,{150,186,214,254});
        for (int i=0;i<3;++i) roundPart(0,0,0.128f,0.128f,ey+1.01f+i*0.07f,ey+1.025f+i*0.07f,{179,206,223,254});
        part(0, 0.205f, 0.05f, 0.02f, ey + 0.58f, ey + 0.66f, Surface::Metal, Rgba{ 88, 90, 94, 255 });  // tap
        break;
    }
    case PROP_PLANT: {  // potted plant
        blob(0.26f, 0.26f);
        part(0, 0, 0.17f, 0.17f, ey, ey + 0.09f, Surface::Metal, Rgba{ 120, 70, 48, 255 });    // saucer
        roundPart(0,0,0.105f,0.145f,ey+0.02f,ey+0.30f,{146,88,58,254});
        roundPart(0,0,0.153f,0.153f,ey+0.285f,ey+0.32f,{172,104,70,254});
        part(0, 0, 0.032f, 0.032f, ey + 0.32f, ey + 0.88f, Surface::Metal, Rgba{ 76, 66, 44, 255 });   // stem
        for (int i=0;i<10;++i) {
            float angle=rot+i*2.39996f,yy=ey+0.58f+(i%4)*0.13f;
            float dx=cosf(angle)*0.28f,dz=sinf(angle)*0.28f;
            Vec3 base{pcx,yy,pcz},tip{pcx+dx,yy+0.17f,pcz+dz};
            Vec3 left{pcx+dx*0.55f-dz*0.20f,yy+0.14f,pcz+dz*0.55f+dx*0.20f};
            Vec3 right{pcx+dx*0.55f+dz*0.20f,yy+0.14f,pcz+dz*0.55f-dx*0.20f};
            Vec2 uv = PLAIN_UV;
            pr.tri(base,left,tip,{0,1,0},uv,uv,uv,{64,111,53,254});
            pr.tri(base,tip,right,{0,1,0},uv,uv,uv,{48,88,39,254});
        }
        break;
    }
    case PROP_NONE:
    case PROP_MANILA_TABLE:   // addManilaRoom builds the room's furniture
        break;
    }
}

// Spalled concrete with the rebar showing (a Spall or PillarSpall fixture): a
// ragged cavity just off the face (two fans, the deeper one darker), two
// vertical bars and one or two ties as real boxes. The face is axis-aligned, so
// `u` is x or z and the bars stay boxes.
static void addSpall(MB &fx, MB &pr, const Fixture &sp) {
    const Vec3 c = sp.pos, n = sp.normal;
    const Vec3 u = fabsf(n.z) > 0.5f ? Vec3{ 1, 0, 0 } : Vec3{ 0, 0, 1 };
    const float rw = sp.w, rh = sp.h;
    Rng r(((uint64_t)sp.seed << 1) ^ 0x5BA11ULL);
    const Vec2 uv = PLAIN_UV;                // the fixtures atlas's plain metal, darkened
    auto ring = [&](float scale, float off, Rgba col) {
        const int N = 11;
        Vec3 pts[N];
        for (int i = 0; i < N; i++) {
            float a = TAU * i / N, k = scale * (0.62f + 0.38f * r.f01());
            float du = cosf(a) * rw * k, dv = sinf(a) * rh * k;
            pts[i] = { c.x + u.x * du + n.x * off, c.y + dv, c.z + u.z * du + n.z * off };
        }
        Vec3 m = { c.x + n.x * off, c.y, c.z + n.z * off };
        for (int i = 0; i < N; i++) {
            // wound to face along n whichever way the face points
            float cr = (u.x * n.z - u.z * n.x);
            if (cr > 0) fx.tri(m, pts[i], pts[(i + 1) % N], n, uv, uv, uv, col);
            else        fx.tri(m, pts[(i + 1) % N], pts[i], n, uv, uv, uv, col);
        }
    };
    ring(1.0f, 0.0025f, Rgba{ 104, 98, 90, 254 });
    ring(0.62f, 0.0035f, Rgba{ 62, 58, 52, 254 });
    Rgba rust = { 104, 58, 34, 254 };
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

// ---- a Level 1 exit: a steel frame, a leaf pinned open against the wall, and
// over it the exit's glyph and a caged bulkhead lamp. Cursed exits paint the
// glyph red.
static void addSymbolDoor(MB &pr, MB &fx, const Opening &op) {
    const int ax = op.west ? 1 : 0;              // 0: a wall along x at z = w0; 1: along z at x = w0
    const float w0 = op.line, base = op.base;
    const bool cursed = op.kind == OpeningKind::CursedExit;
    auto P = [&](float a, float y, float n) {             // wall-local -> world
        return ax == 0 ? Vec3{ a, y, w0 + n } : Vec3{ w0 + n, y, a };
    };
    auto box = [&](MB &mb, float aL, float y0, float nL, float aH, float y1, float nH, Rgba c) {
        Vec3 lo = P(aL, y0, nL), hi = P(aH, y1, nH);
        addSolidBox(mb, std::min(lo.x, hi.x), y0, std::min(lo.z, hi.z), std::max(lo.x, hi.x), y1, std::max(lo.z, hi.z), c);
    };
    Rgba steel = { 84, 88, 86, 254 }, dark = { 50, 52, 52, 254 };
    const float o0 = op.a0, o1 = op.a1, T = 0.05f;
    for (int sd = -1; sd <= 1; sd += 2) {                 // the frame, proud of both faces
        float nf = sd * (WT + T * 0.5f);
        box(pr, o0 - 0.09f, base, nf - T * 0.5f, o0, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o1, base, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
        box(pr, o0 - 0.09f, base + DOOR_HEAD, nf - T * 0.5f, o1 + 0.09f, base + 2.39f, nf + T * 0.5f, steel);
    }
    if (op.leafRoom) {   // the leaf, swung back flat against the -side face beside the frame
        float nf = -(WT + 0.035f);
        box(pr, o1 + 0.10f, base + 0.02f, nf - 0.022f, o1 + 1.38f, base + 2.27f, nf + 0.022f, Rgba{ 96, 104, 100, 254 });
        box(pr, o1 + 0.22f, base + 1.00f, nf - 0.05f, o1 + 1.26f, base + 1.06f, nf - 0.02f, dark);   // push bar
        box(pr, o1 + 0.40f, base + 1.55f, nf - 0.03f, o1 + 1.08f, base + 1.95f, nf - 0.022f, Rgba{ 60, 70, 76, 254 });   // wired glass
    }
    // the glyph: strokes of paint between lattice points, both faces
    const uint8_t *pts = op.glyph;
    const int np = op.glyphLen;
    Rgba paint = cursed ? Rgba{ 210, 40, 30, 254 } : Rgba{ 250, 238, 190, 254 };
    const Vec2 uv = PLAIN_UV;
    float sc = 0.30f, cA = (o0 + o1) * 0.5f, cY = base + 2.78f;
    for (int sd = -1; sd <= 1; sd += 2) {
        float nf = sd * (WT + 0.0025f);
        for (int i = 0; i + 1 < np; i++) {
            float pa = cA + ((pts[i] % 3) - 1) * sc, py = cY + ((pts[i] / 3) - 1) * sc;
            float qa = cA + ((pts[i + 1] % 3) - 1) * sc, qy = cY + ((pts[i + 1] / 3) - 1) * sc;
            if (pts[i] == pts[i + 1]) { qa += sc * 0.6f; }
            float da = qa - pa, dy = qy - py, L = sqrtf(da * da + dy * dy) + 1e-4f;
            float ta = -dy / L * 0.065f, ty = da / L * 0.065f;          // half a brush width, across
            Vec3 A = P(pa - ta, py - ty, nf), B = P(qa - ta, qy - ty, nf), C = P(qa + ta, qy + ty, nf), D = P(pa + ta, py + ty, nf);
            Vec3 nn = ax == 0 ? Vec3{ 0, 0, (float)sd } : Vec3{ (float)sd, 0, 0 };
            bool flip = (ax == 0) ? sd > 0 : sd < 0;
            if (flip) fx.quad(B, A, D, C, nn, uv, uv, uv, uv, paint);
            else      fx.quad(A, B, C, D, nn, uv, uv, uv, uv, paint);
        }
        // a dot where the stroke starts
        float pa = cA + ((pts[0] % 3) - 1) * sc, py = cY + ((pts[0] / 3) - 1) * sc;
        box(fx, pa - 0.04f, py - 0.04f, nf - 0.001f, pa + 0.04f, py + 0.04f, nf + 0.001f, paint);
        // the caged bulkhead: a warm lens (raw emissive) in a wire cage
        float lf = sd * (WT + 0.07f);
        box(pr, cA - 0.13f, base + 3.18f, sd * WT, cA + 0.13f, base + 3.36f, lf, dark);
        box(pr, cA - 0.09f, base + 3.20f, lf - sd * 0.03f, cA + 0.09f, base + 3.34f, lf + sd * 0.012f,
            Rgba{ 255, 206, 140, 60 });
        for (int b = -1; b <= 1; b++)
            box(pr, cA + b * 0.07f - 0.006f, base + 3.18f, lf + sd * 0.012f, cA + b * 0.07f + 0.006f, base + 3.36f,
                lf + sd * 0.024f, dark);
    }
}

// ---- the Manila Room: floorboards, manila paper, an octagonal table with a
// cupboard, two chairs, notes, a chandelier, and an open wooden door in each
// wall. rx/rz is the room's centre (a cell corner), cy its ceiling; the floor is
// 0 (generate keeps it flat).
static void addManilaRoom(MB &pr, MB &fx, MB &ao, float rx, float rz, float cy, uint32_t h) {
    const float R = 4.0f, IN = R - WT;       // half-size of the room, and of its inside
    const float WU0 = 0.51f, WV0 = 0.02f, WU1 = 0.99f, WV1 = 0.48f;   // props atlas: wood
    auto wood = [&](MB &mb, float cxp, float czp, float yaw, float hx, float hz, float y0, float y1, Rgba t) {
        addPropBox(mb, cxp, czp, yaw, hx, hz, y0, y1, WU0, WV0, WU1, WV1, WU0, WV0, WU1, WV1, t, 0.004f);
    };
    Rng r(((uint64_t)h << 1) ^ 0x3A11AULL);

    // Floorboards: 145 mm strips east-west, random lengths, staggered, over a dark
    // underlay that shows as the gaps. Alpha 255 so the wood takes its relief and
    // gloss.
    {
        const Vec3 up = { 0, 1, 0 };
        const Vec2 u = PLAIN_UV;
        pr.quad({rx-R,0.002f,rz-R},{rx-R,0.002f,rz+R},{rx+R,0.002f,rz+R},{rx+R,0.002f,rz-R}, up,
                u, u, u, u, Rgba{ 34, 24, 18, 254 });
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
                    Rgba t = { cl8(150 * k), cl8(104 * k), cl8(70 * k), 255 };
                    float ua = ou, ub = ou + (xb - xa) * 0.11f, va = ov, vb = ov + 0.03f;
                    pr.quad({xa+0.003f,0.005f,z+0.003f},{xa+0.003f,0.005f,z1-0.003f},
                            {xb-0.003f,0.005f,z1-0.003f},{xb-0.003f,0.005f,z+0.003f}, up,
                            {ua,va},{ua,vb},{ub,vb},{ub,va}, t);
                }
                x += len;
            }
        }
    }

    // Manila paper on the four inside faces: 500 mm tiles 1.5 mm off the plaster,
    // from the skirting to the ceiling and round each doorway, anchored to the world
    // grid so the lattice runs unbroken across the cuts.
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
                Rgba c = { 255, 255, 255, 254 };
                if (axis == 0) {   // face at z = fixed, running along x
                    Vec3 n = { 0, 0, nsgn };
                    if (nsgn > 0) fx.quad({qa1,qy0,fixed},{qa0,qy0,fixed},{qa0,qy1,fixed},{qa1,qy1,fixed}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                    else          fx.quad({qa0,qy0,fixed},{qa1,qy0,fixed},{qa1,qy1,fixed},{qa0,qy1,fixed}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                } else {           // face at x = fixed, running along z
                    Vec3 n = { nsgn, 0, 0 };
                    if (nsgn > 0) fx.quad({fixed,qy0,qa0},{fixed,qy0,qa1},{fixed,qy1,qa1},{fixed,qy1,qa0}, n,
                                          {ua,va},{ub,va},{ub,vb},{ua,vb}, c);
                    else          fx.quad({fixed,qy0,qa1},{fixed,qy0,qa0},{fixed,qy1,qa0},{fixed,qy1,qa1}, n,
                                          {ub,va},{ua,va},{ua,vb},{ub,vb}, c);
                }
            }
    };
    // Door openings in room-local cells: north in cell 7, south 8, west 8, east 7
    // (stampManila in generate). Cell c's opening runs from its edge + 0.35 to
    // + 1.65.
    auto face = [&](int axis, float fixed, float nsgn, int doorCell) {
        float a0 = (axis == 0 ? rx : rz) - IN, a1 = (axis == 0 ? rx : rz) + IN;
        float o0 = (axis == 0 ? rx : rz) - R + (doorCell - MANILA_LO) * CELL + DOOR_LO, o1 = o0 + 1.30f;
        const float y0 = 0.135f, yo = DOOR_HEAD;
        tileRect(axis, fixed, nsgn, a0, o0 - 0.06f, y0, cy);    // left of the frame
        tileRect(axis, fixed, nsgn, o1 + 0.06f, a1, y0, cy);    // right of it
        tileRect(axis, fixed, nsgn, o0 - 0.06f, o1 + 0.06f, yo + 0.06f, cy);   // over the head
        // The door, open flat against the wall beside its frame: a leaf, two panels
        // and a brass knob.
        float lc = o1 + 0.08f + 0.64f;                       // leaf centre along the wall
        float off = fixed + nsgn * 0.03f;                    // standing just off the face
        Rgba leaf = { 118, 78, 50, 255 }, panel = { 98, 64, 40, 255 };
        if (axis == 0) {
            wood(pr, lc, off, 0, 0.64f, 0.022f, 0.01f, 2.27f, leaf);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 0.25f, 1.05f, panel);
            wood(pr, lc, off + nsgn * 0.02f, 0, 0.48f, 0.006f, 1.25f, 2.05f, panel);
            addSolidBox(pr, lc + 0.50f, 0.98f, off + nsgn * 0.02f - 0.025f, lc + 0.56f, 1.04f,
                        off + nsgn * 0.02f + 0.025f, Rgba{ 200, 160, 70, 254 });
        } else {
            wood(pr, off, lc, 0, 0.022f, 0.64f, 0.01f, 2.27f, leaf);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 0.25f, 1.05f, panel);
            wood(pr, off + nsgn * 0.02f, lc, 0, 0.006f, 0.48f, 1.25f, 2.05f, panel);
            addSolidBox(pr, off + nsgn * 0.02f - 0.025f, 0.98f, lc + 0.50f, off + nsgn * 0.02f + 0.025f,
                        1.04f, lc + 0.56f, Rgba{ 200, 160, 70, 254 });
        }
    };
    const float D = 0.0015f;
    face(0, rz - IN + D, +1, 7);    // north wall, facing into the room (+z)
    face(0, rz + IN - D, -1, 8);    // south
    face(1, rx - IN + D, +1, 8);    // west
    face(1, rx + IN - D, -1, 7);    // east

    // The octagonal table on a plinth, with its cupboard under the top.
    auto octo = [&](float r0, float y0, float y1, Rgba t, bool top) {
        const float rr = r0 / cosf(TAU / 16);            // r0 is flat-to-centre
        for (int i = 0; i < 8; i++) {
            float a = TAU * i / 8 + TAU / 16, b = TAU * (i + 1) / 8 + TAU / 16, m = (a + b) * 0.5f;
            Vec3 p0 = { rx + rr * cosf(a), y0, rz + rr * sinf(a) }, p1 = { rx + rr * cosf(b), y0, rz + rr * sinf(b) };
            Vec3 p2 = { p1.x, y1, p1.z }, p3 = { p0.x, y1, p0.z };
            float ua = WU0 + 0.05f * i, ub = ua + 0.05f;
            pr.quad(p0, p1, p2, p3, { cosf(m), 0, sinf(m) }, {ua,WV1}, {ub,WV1}, {ub,WV1-0.05f}, {ua,WV1-0.05f}, t);
            if (top) {
                Vec2 c = { (WU0 + WU1) * 0.5f, (WV0 + WV1) * 0.5f };
                auto tuv = [&](Vec3 p) { return Vec2{ c.x + (p.x - rx) * 0.35f, c.y + (p.z - rz) * 0.35f }; };
                pr.tri({ rx, y1, rz }, p3, p2, { 0, 1, 0 }, c, tuv(p3), tuv(p2), t);
            }
        }
    };
    Rgba top = { 132, 86, 54, 255 }, body = { 104, 68, 44, 255 };
    octo(0.48f, 0.00f, 0.07f, Rgba{ 70, 46, 30, 255 }, true);    // plinth
    octo(0.42f, 0.07f, 0.73f, body, true);                          // the cupboard
    octo(0.64f, 0.73f, 0.785f, top, true);                          // the top
    for (int i = 0; i < 8; i += 2) {                                // cupboard doors: a knob on alternate faces
        float m = TAU * i / 8 + TAU / 8;
        float kx = rx + 0.425f * cosf(m), kz = rz + 0.425f * sinf(m);
        addSolidBox(pr, kx - 0.015f, 0.44f, kz - 0.015f, kx + 0.015f, 0.47f, kz + 0.015f, Rgba{ 200, 160, 70, 254 });
    }
    addContactShadow(ao, rx, rz, 0.0f, 0.0f, 0.52f, 0.52f);

    // Two chairs, one either side.
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float chx = rx + sgn * 1.02f, chz = rz;
        Rgba cw = { 112, 74, 46, 255 };
        wood(pr, chx, chz, 0, 0.21f, 0.21f, 0.43f, 0.47f, cw);                        // seat
        for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
            wood(pr, chx + lx * 0.18f, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.0f, 0.43f, cw);
        float bx = chx + sgn * 0.19f;                                                 // the back, away from the table
        for (int lz = -1; lz <= 1; lz += 2) wood(pr, bx, chz + lz * 0.18f, 0, 0.018f, 0.018f, 0.47f, 0.93f, cw);
        wood(pr, bx, chz, 0, 0.014f, 0.19f, 0.74f, 0.90f, cw);
        wood(pr, bx, chz, 0, 0.012f, 0.19f, 0.56f, 0.61f, cw);
        addContactShadow(ao, chx, chz, 0.0f, 0.0f, 0.22f, 0.22f);
    }

    // The notes on the table; Game reads them on E.
    const FixtureRect &N = FIXTURES[FIX_NOTE];
    for (int i = 0; i < 4; i++) {
        float a = r.f01() * TAU, d = 0.12f + r.f01() * 0.30f, rot = r.f01() * TAU;
        float nx = rx + cosf(a) * d, nz = rz + sinf(a) * d, y = 0.787f + 0.0012f * i;
        float c = cosf(rot), s2 = sinf(rot), hw = N.halfW, hh = N.halfH;
        auto P = [&](float u, float v) { return Vec3{ nx + u * c - v * s2, y, nz + u * s2 + v * c }; };
        fx.quad(P(-hw,-hh), P(-hw,hh), P(hw,hh), P(hw,-hh), { 0, 1, 0 },
                { N.u0, N.v0 }, { N.u0, N.v1 }, { N.u1, N.v1 }, { N.u1, N.v0 }, Rgba{ 255, 255, 255, 254 });
    }

    // The chandelier: chain, brass hub, six arms, six bulbs. The bulbs are raw
    // emissive (alpha 60); the light they throw is the shader's uLamp, which Game
    // points here while the room is near.
    {
        Rgba brass = { 186, 146, 72, 254 };
        float hy = cy - 0.78f;
        addSolidBox(pr, rx - 0.008f, hy + 0.10f, rz - 0.008f, rx + 0.008f, cy, rz + 0.008f, Rgba{ 90, 80, 60, 254 });
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
                        Rgba{ 255, 196, 120, 60 });                                                        // bulb
        }
    }
}

// ---- fixtures (core's ChunkLayout decides where; these decide how they look).
// Decals stand this far off their face: flush, they z-fight and lose at range.
static const float DECAL_OFF = 0.006f;
static const Rgba FIXC = { 255, 255, 255, 254 };   // atlas-textured fixture, no relief

// A lift (Level 1): shut doors, a floor indicator and a lit call button.
static void addLiftDoor(MB &pr, const Fixture &lf) {
    const float gx = lf.pos.x, nb = lf.pos.y, zf = lf.pos.z, sgn = lf.normal.z;
    auto zb = [&](float x0, float y0, float x1, float y1, float d0, float d1, Rgba c) {
        addSolidBox(pr, x0, y0, std::min(zf + sgn * d0, zf + sgn * d1), x1, y1, std::max(zf + sgn * d0, zf + sgn * d1), c);
    };
    Rgba frame = { 92, 94, 92, 254 }, leaf = { 142, 146, 144, 254 }, seam = { 40, 42, 42, 254 };
    zb(gx + 0.30f, nb, gx + 1.70f, nb + 2.46f, 0.0f, 0.03f, frame);                       // surround
    zb(gx + 0.38f, nb, gx + 0.995f, nb + 2.38f, 0.03f, 0.045f, leaf);                     // two leaves
    zb(gx + 1.005f, nb, gx + 1.62f, nb + 2.38f, 0.03f, 0.045f, leaf);
    zb(gx + 0.995f, nb, gx + 1.005f, nb + 2.38f, 0.03f, 0.042f, seam);
    zb(gx + 0.62f, nb + 2.52f, gx + 1.38f, nb + 2.70f, 0.0f, 0.03f, seam);                 // floor indicator
    zb(gx + 0.93f, nb + 2.55f, gx + 1.07f, nb + 2.67f, 0.03f, 0.036f, Rgba{ 255, 120, 40, 60 });
    zb(gx + 1.86f, nb + 1.00f, gx + 1.96f, nb + 1.30f, 0.0f, 0.02f, frame);               // call plate
    zb(gx + 1.89f, nb + 1.12f, gx + 1.93f, nb + 1.18f, 0.02f, 0.03f, Rgba{ 255, 236, 180, 60 });
}

// An outlet, switch, grille or exit sign: a FIXTURES cell on the wall face. The
// UVs mirror with the face so signage reads the right way round.
static void addWallFitting(MB &fx, const Fixture &wf) {
    int id = FIX_OUTLET;
    switch (wf.kind) {
    case FixtureKind::BrokenOutlet: id = FIX_OUTLET_BROKEN; break;
    case FixtureKind::Switch:       id = FIX_SWITCH; break;
    case FixtureKind::Grille:       id = FIX_GRILLE; break;
    case FixtureKind::ExitSign:     id = FIX_SIGN; break;
    default: break;
    }
    const FixtureRect &f = FIXTURES[id];
    const float y0 = wf.pos.y - f.halfH, y1 = wf.pos.y + f.halfH;
    if (wf.normal.z != 0.0f) {   // on a north (x-running) wall
        bool plus = wf.normal.z > 0;
        float zf = plus ? wf.pos.z + DECAL_OFF : wf.pos.z - DECAL_OFF;
        float x0 = wf.pos.x - f.halfW, x1 = wf.pos.x + f.halfW;
        if (plus) fx.quad({x0,y0,zf},{x1,y0,zf},{x1,y1,zf},{x0,y1,zf},{0,0,1},
                          {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
        else      fx.quad({x1,y0,zf},{x0,y0,zf},{x0,y1,zf},{x1,y1,zf},{0,0,-1},
                          {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
    } else {                     // on a west (z-running) wall
        bool plus = wf.normal.x > 0;
        float xf = plus ? wf.pos.x + DECAL_OFF : wf.pos.x - DECAL_OFF;
        float z0 = wf.pos.z - f.halfW, z1 = wf.pos.z + f.halfW;
        if (plus) fx.quad({xf,y0,z1},{xf,y0,z0},{xf,y1,z0},{xf,y1,z1},{1,0,0},
                          {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
        else      fx.quad({xf,y0,z0},{xf,y0,z1},{xf,y1,z1},{xf,y1,z0},{-1,0,0},
                          {f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0},{f.u0,f.v0}, FIXC);
    }
}

// A diffuser lies flat in the tile grid; a sprinkler hangs below it on a dropper.
static void addCeilingFitting(MB &fx, const Fixture &cf) {
    const float ccx = cf.pos.x, cyc = cf.pos.y, ccz = cf.pos.z;
    if (cf.kind == FixtureKind::Diffuser) {
        const FixtureRect &f = FIXTURES[FIX_DIFFUSER];
        float yq = cyc - 0.008f;
        fx.quad({ccx-f.halfW,yq,ccz-f.halfH},{ccx-f.halfW,yq,ccz+f.halfH},
                {ccx+f.halfW,yq,ccz+f.halfH},{ccx+f.halfW,yq,ccz-f.halfH},{0,-1,0},
                {f.u0,f.v0},{f.u0,f.v1},{f.u1,f.v1},{f.u1,f.v0}, FIXC);
        return;
    }
    const Rgba BRASS = { 158, 126, 66, 254 };
    addSolidBox(fx, ccx-0.016f, cyc-0.085f, ccz-0.016f, ccx+0.016f, cyc, ccz+0.016f, BRASS);
    addSolidBox(fx, ccx-0.033f, cyc-0.085f, ccz-0.033f, ccx+0.033f, cyc-0.070f, ccz+0.033f, BRASS);
    addSolidBox(fx, ccx-0.045f, cyc-0.100f, ccz-0.045f, ccx+0.045f, cyc-0.090f, ccz+0.045f, BRASS);
}

// Conduit: a steel channel standing CDY off the wall face.
static void addConduit(MB &fx, const Fixture &c) {
    const Rgba STEEL = { 138, 136, 130, 254 };
    const float CDY = 0.052f, y = c.pos.y;
    if (c.normal.z != 0.0f) {
        float z0 = c.normal.z > 0 ? c.pos.z : c.pos.z - CDY;
        addSolidBox(fx, c.pos.x, y, z0, c.end.x, y + 0.046f, z0 + CDY, STEEL);
    } else {
        float x0 = c.normal.x > 0 ? c.pos.x : c.pos.x - CDY;
        addSolidBox(fx, x0, y, c.pos.z, x0 + CDY, y + 0.046f, c.end.z, STEEL);
    }
}

// A phrase from the 4 x 8 scrawl atlas, tilted and tinted per instance so the
// same cell twice does not look like one decal.
static void addScrawl(MB &scr, const Fixture &s) {
    static const Rgba TINT[4] = { { 255, 255, 255, 255 }, { 236, 228, 214, 255 },
                                   { 216, 210, 212, 255 }, { 248, 234, 208, 255 } };
    const int ph = s.variant;
    const float u0 = (ph & 3) * 0.25f, v0 = (ph >> 2) * 0.125f, u1 = u0 + 0.25f, v1 = v0 + 0.125f;
    const Rgba tc = TINT[s.tone];   // alpha stays 255: see the shader's alpha coding
    const float my = s.pos.y, hw = s.w, hh = s.h, cq = cosf(s.angle), sq = sinf(s.angle);
    if (s.normal.z != 0.0f) {
        bool plus = s.normal.z > 0;
        float zf = plus ? s.pos.z + DECAL_OFF : s.pos.z - DECAL_OFF, mx = s.pos.x;
        auto co = [&](float sx, float sy) {
            return Vec3{ mx + sx * hw * cq - sy * hh * sq, my + sx * hw * sq + sy * hh * cq, zf };
        };
        if (plus) scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {0,0,1},
                           {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
        else      scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {0,0,-1},
                           {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
    } else {
        bool plus = s.normal.x > 0;
        float xf = plus ? s.pos.x + DECAL_OFF : s.pos.x - DECAL_OFF, mz = s.pos.z;
        auto co = [&](float sz, float sy) {
            return Vec3{ xf, my + sz * hw * sq + sy * hh * cq, mz + sz * hw * cq - sy * hh * sq };
        };
        if (plus) scr.quad(co(1,-1), co(-1,-1), co(-1,1), co(1,1), {1,0,0},
                           {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
        else      scr.quad(co(-1,-1), co(1,-1), co(1,1), co(-1,1), {-1,0,0},
                           {u0,v1},{u1,v1},{u1,v0},{u0,v0}, tc);
    }
}

// A service pipe, one cell long, with a collar where the layout says.
static void addPipe(MB &pr, const Fixture &p) {
    const float r = p.w, py = p.pos.y;
    const Rgba pc = p.tone ? Rgba{ 78, 54, 40, 255 }    // rusted iron
                            : Rgba{ 62, 60, 66, 255 };   // dull steel
    const Rgba collar = { 96, 74, 56, 255 };
    if (p.end.x != p.pos.x) {   // along x
        addSolidBox(pr, p.pos.x, py - r, p.pos.z - r, p.end.x, py + r, p.pos.z + r, pc);
        if (p.variant)
            addSolidBox(pr, p.pos.x + 0.85f, py - r - 0.03f, p.pos.z - r - 0.03f,
                        p.pos.x + 1.15f, py + r + 0.03f, p.pos.z + r + 0.03f, collar);
    } else {
        addSolidBox(pr, p.pos.x - r, py - r, p.pos.z, p.pos.x + r, py + r, p.end.z, pc);
        if (p.variant)
            addSolidBox(pr, p.pos.x - r - 0.03f, py - r - 0.03f, p.pos.z + 0.85f,
                        p.pos.x + r + 0.03f, py + r + 0.03f, p.pos.z + 1.15f, collar);
    }
}

// A valve station: a standpipe floor to ceiling. The renderer draws the wheel.
static void addValve(MB &pr, const Fixture &v) {
    const float vx = v.pos.x, vz = v.pos.z, fy = v.pos.y;
    addSolidBox(pr, vx - 0.085f, fy, vz - 0.085f, vx + 0.085f, v.end.y, vz + 0.085f, Rgba{ 84, 60, 44, 255 });
    addSolidBox(pr, vx - 0.13f, fy + 1.02f, vz - 0.13f, vx + 0.13f, fy + 1.24f, vz + 0.13f,
                Rgba{ 104, 80, 58, 255 });   // the body the wheel sits on
}

// A crepe streamer: two quads sagging to the low point between its ends.
static void addStreamer(MB &pr, const Fixture &s) {
    const float ax = s.pos.x, az = s.pos.z, bx2 = s.end.x, bz2 = s.end.z, ytop = s.pos.y, ymid = s.h;
    const float mx2 = (ax + bx2) * 0.5f, mz2 = (az + bz2) * 0.5f;
    const Rgba sc = PARTY_RGBA[s.variant];
    const float dx2 = bx2 - ax, dz2 = bz2 - az, dl = sqrtf(dx2 * dx2 + dz2 * dz2) + 1e-4f;
    const Vec3 nrm = { dz2 / dl, 0, -dx2 / dl };
    const Vec2 uvp = PLAIN_UV;   // plain-metal corner of the prop atlas: flat colour
    pr.quad({ ax, ytop, az }, { mx2, ymid + 0.06f, mz2 }, { mx2, ymid, mz2 }, { ax, ytop - 0.06f, az },
            nrm, uvp, uvp, uvp, uvp, sc);
    pr.quad({ mx2, ymid + 0.06f, mz2 }, { bx2, ytop, bz2 }, { bx2, ytop - 0.06f, bz2 }, { mx2, ymid, mz2 },
            nrm, uvp, uvp, uvp, uvp, sc);
}

// The builders a fixture can land in.
struct FixtureMeshes { MB &pr, &fx, &scr, &ao; };

// Build one fixture. bakeChunk calls this at the point in its cell walk where
// the fixture used to be decided, so each builder's output order is unchanged.
static void addFixture(const FixtureMeshes &m, const Fixture &f) {
    switch (f.kind) {
    case FixtureKind::Outlet: case FixtureKind::BrokenOutlet: case FixtureKind::Switch:
    case FixtureKind::Grille: case FixtureKind::ExitSign:
        addWallFitting(m.fx, f); break;
    case FixtureKind::Diffuser: case FixtureKind::Sprinkler:
        addCeilingFitting(m.fx, f); break;
    case FixtureKind::Conduit:     addConduit(m.fx, f); break;
    case FixtureKind::Scrawl:      addScrawl(m.scr, f); break;
    case FixtureKind::LiftDoor:    addLiftDoor(m.pr, f); break;
    case FixtureKind::Spall:
    case FixtureKind::PillarSpall: addSpall(m.fx, m.pr, f); break;
    case FixtureKind::Pipe:        addPipe(m.pr, f); break;
    case FixtureKind::Valve:       addValve(m.pr, f); break;
    case FixtureKind::Streamer:    addStreamer(m.pr, f); break;
    case FixtureKind::ManilaRoom:  addManilaRoom(m.pr, m.fx, m.ao, f.pos.x, f.pos.z, f.pos.y, f.seed); break;
    }
}

// Where in bakeChunk's cell walk each kind is built: with the walls, on the
// pillar, or in the service pass after the flights.
enum class FixtureStage { Walls, Pillar, Service };
static FixtureStage stageOf(FixtureKind k) {
    if (k == FixtureKind::PillarSpall) return FixtureStage::Pillar;
    if (k == FixtureKind::Pipe || k == FixtureKind::Valve) return FixtureStage::Service;
    return FixtureStage::Walls;
}

// A window in an x-running (north) wall: sill, head and piers round the pane,
// which is glass, or on Level 2 an emissive pale pane on each face.
static void addWindowN(WallBuilder &wa, MB &gl, const Opening &op, float gx, float gz, float nb, float nt, int level) {
    const float a0 = op.a0, a1 = op.a1, sill = op.sillY, head = op.headY;
    addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, sill, gz + WT);
    addBoxSides(wa, gx - WT, head, gz - WT, gx + CELL + WT, nt, gz + WT, true);
    addBoxSides(wa, gx - WT, sill, gz - WT, a0, head, gz + WT);
    addBoxSides(wa, a1, sill, gz - WT, gx + CELL + WT, head, gz + WT);
    wa.quad({gx-WT,sill,gz-WT},{gx+CELL+WT,sill,gz-WT},{gx+CELL+WT,sill,gz+WT},{gx-WT,sill,gz+WT},
            {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, RGBA_WHITE);   // sill top
    if (level == 2) {
        Rgba sky = { 226, 241, 246, 70 };
        wa.quad({a0,sill,gz-0.02f},{a1,sill,gz-0.02f},{a1,head,gz-0.02f},{a0,head,gz-0.02f},
                {0,0,-1},{0,1},{1,1},{1,0},{0,0}, sky);
        wa.quad({a1,sill,gz+0.02f},{a0,sill,gz+0.02f},{a0,head,gz+0.02f},{a1,head,gz+0.02f},
                {0,0,1},{0,1},{1,1},{1,0},{0,0}, sky);
    } else {   // a translucent pane (alpha 100, the shader's glass path)
        Rgba glass = { 20, 26, 32, 100 };
        gl.quad({a0,sill,gz},{a1,sill,gz},{a1,head,gz},{a0,head,gz},
                {0,0,-1},{0,1},{1,1},{1,0},{0,0}, glass);
    }
}

// The same in a z-running (west) wall.
static void addWindowW(WallBuilder &wa, MB &gl, const Opening &op, float gx, float gz, float wb, float wt2, int level) {
    const float a0 = op.a0, a1 = op.a1, sill = op.sillY, head = op.headY;
    addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, sill, gz + CELL + WT);
    addBoxSides(wa, gx - WT, head, gz - WT, gx + WT, wt2, gz + CELL + WT, true);
    addBoxSides(wa, gx - WT, sill, gz - WT, gx + WT, head, a0);
    addBoxSides(wa, gx - WT, sill, a1, gx + WT, head, gz + CELL + WT);
    wa.quad({gx-WT,sill,gz-WT},{gx+WT,sill,gz-WT},{gx+WT,sill,gz+CELL+WT},{gx-WT,sill,gz+CELL+WT},
            {0,1,0},{0,0},{1,0},{1,0.1f},{0,0.1f}, RGBA_WHITE);   // sill top
    if (level == 2) {
        Rgba sky = { 226, 241, 246, 70 };
        wa.quad({gx+0.02f,sill,a0},{gx+0.02f,sill,a1},{gx+0.02f,head,a1},{gx+0.02f,head,a0},
                {1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
        wa.quad({gx-0.02f,sill,a1},{gx-0.02f,sill,a0},{gx-0.02f,head,a0},{gx-0.02f,head,a1},
                {-1,0,0},{0,1},{1,1},{1,0},{0,0}, sky);
    } else {
        Rgba glass = { 20, 26, 32, 100 };
        gl.quad({gx,sill,a0},{gx,sill,a1},{gx,head,a1},{gx,head,a0},
                {1,0,0},{0,1},{1,1},{1,0},{0,0}, glass);
    }
}

// A light fitting: Level 0's flush troffer, Level 1's batten, or a tray round a
// recessed diffuser. Emissive parts are alpha 0.
static void addFitting(MB &pr, MB &ce, const LightFitting &lf, int level) {
    const Rgba panel = {255,255,255,0};
    const float lx = lf.pos.x, lz = lf.pos.z, hp = 0.62f;
    // The fitting hangs in the ceiling of the cell it is centred in. The 1.38 m
    // tray can overhang a neighbour at another height; that neighbour's soffit is
    // what it meets.
    const float wallTop = lf.ceilingY, yq = wallTop - LIGHT_DROP;
    // A recessed diffuser in a metal tray; the luminous plane is on uLY.
    Rgba rim = level == 2 ? Rgba{230,232,223,254} : Rgba{156,153,140,254};
    const float outer = 0.69f, lip = 0.035f;
    if (level == 1) {
        // Level 1: battens, two bare tubes under a steel reflector on two rods, with
        // the tubes on the light plane (uLY). The layout turns alternate fittings.
        const bool alongX = lf.turned;
        auto box = [&](float a0, float y0, float b0, float a1, float y1, float b1, Rgba c) {
            if (alongX) addSolidBox(pr, lx + a0, y0, lz + b0, lx + a1, y1, lz + b1, c);
            else        addSolidBox(pr, lx + b0, y0, lz + a0, lx + b1, y1, lz + a1, c);
        };
        Rgba steel = { 132, 134, 128, 254 }, rod = { 70, 70, 68, 254 };
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
            Vec3 a, b, c, d2;
            if (alongX) { a = {lx-0.78f,yt,lz+c0}; b = {lx-0.78f,yt,lz+c1}; c = {lx+0.78f,yt,lz+c1}; d2 = {lx+0.78f,yt,lz+c0}; }
            else        { a = {lx+c0,yt,lz-0.78f}; b = {lx+c0,yt,lz+0.78f}; c = {lx+c1,yt,lz+0.78f}; d2 = {lx+c1,yt,lz-0.78f}; }
            ce.quad(a, b, c, d2, {0,-1,0}, {0,0},{0,1},{1,1},{1,0}, panel);
            // and the tubes' sides, so one seen edge-on is still a line of light
            if (alongX) ce.quad({lx-0.78f,yt,lz+c0},{lx+0.78f,yt,lz+c0},{lx+0.78f,yt+0.03f,lz+c0},{lx-0.78f,yt+0.03f,lz+c0},
                                {0,0,-1},{0,0},{1,0},{1,1},{0,1}, panel);
            else        ce.quad({lx+c0,yt,lz+0.78f},{lx+c0,yt,lz-0.78f},{lx+c0,yt+0.03f,lz-0.78f},{lx+c0,yt+0.03f,lz+0.78f},
                                {-1,0,0},{0,0},{1,0},{1,1},{0,1}, panel);
        }
        return;
    }
    if (level == 0) {
        // Level 0: lay-in troffers flush with the tiles behind a hairline frame. The
        // light plane (uLY) stays 12 cm down; the shader and the CPU mirror use it.
        float yf = wallTop - 0.010f;
        Rgba frame = { 186, 182, 166, 254 };
        addSolidBox(pr, lx-outer, yf-0.008f, lz-outer, lx-hp, wallTop, lz+outer, frame);
        addSolidBox(pr, lx+hp, yf-0.008f, lz-outer, lx+outer, wallTop, lz+outer, frame);
        addSolidBox(pr, lx-hp, yf-0.008f, lz-outer, lx+hp, wallTop, lz-hp, frame);
        addSolidBox(pr, lx-hp, yf-0.008f, lz+hp, lx+hp, wallTop, lz+outer, frame);
        ce.quad({lx-hp,yf,lz-hp},{lx-hp,yf,lz+hp},{lx+hp,yf,lz+hp},{lx+hp,yf,lz-hp},{0,-1,0},
                {0,0},{0,1},{1,1},{1,0},panel);
        return;
    }
    addSolidBox(pr, lx-outer, yq-lip, lz-outer, lx-hp, wallTop, lz+outer, rim);
    addSolidBox(pr, lx+hp, yq-lip, lz-outer, lx+outer, wallTop, lz+outer, rim);
    addSolidBox(pr, lx-hp, yq-lip, lz-outer, lx+hp, wallTop, lz-hp, rim);
    addSolidBox(pr, lx-hp, yq-lip, lz+hp, lx+hp, wallTop, lz+outer, rim);
    ce.quad({lx-hp,yq,lz-hp},{lx-hp,yq,lz+hp},{lx+hp,yq,lz+hp},{lx+hp,yq,lz-hp},{0,-1,0},
            {0,0},{0,1},{1,1},{1,0},panel);
}

void bakeChunkGeometry(World &w, int cx, int cz, ChunkGeometry &out) {
    ChunkData &d = w.data(cx, cz);
    const ChunkLayout L = chunkLayout(w, cx, cz);
    MB fl, ce, pr, wt, scr, gl, ao, fx;
    WallBuilder wa;
    float wx = cx * CHUNK, wz = cz * CHUNK;
    wa.tileV = wallTileV(w.level);
    wa.tallPaper = w.storeyH > 0.0f;
    Rgba wcol = RGBA_WHITE;
    // The ceiling takes relief (alpha 255). It is lit edge-on by every fitting, so
    // any low-frequency lump in a ceiling's authored height field shows as blotches;
    // look there first if they appear.
    Rgba ccol = { 255, 255, 255, 255 };
    // ---- baked ambient occlusion: gradient strips along every crease. The strip
    // texture fades alpha from the crease (v = 0) outward (v = 1).
    auto aoStrip = [&](Vec3 e0, Vec3 e1, Vec3 off, Vec3 nn, float v0) {
        ao.quad(e0, e1, { e1.x + off.x, e1.y + off.y, e1.z + off.z },
                { e0.x + off.x, e0.y + off.y, e0.z + off.z }, nn,
                { 0, v0 }, { 1, v0 }, { 1, 1 }, { 0, 1 }, AO_TINT);
    };
    const float AOW = 0.55f;   // reach across the floor / ceiling
    const float AOH = 0.48f;   // creep up / down the wall face
    const float AOC = 0.30f;   // ceiling creases start partway down the gradient (softer)
    // ---- the Red Rooms bleed (Level 0): walls and floors tint toward crimson
    // within 11 m of a cursed exit. Exits in this chunk and its eight neighbours.
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
    // Multiplied into the texture, so it only takes colour away. Alpha stays 255,
    // so the relief does too.
    auto redTint = [&](float x, float z) {
        float t = redAt(x, z);
        // rust at the edge, dim crimson at the heart
        float t2 = t * t;
        return Rgba{ (unsigned char)(255 - 30 * t - 60 * t2), (unsigned char)(255 - 150 * t - 72 * t2),
                      (unsigned char)(255 - 110 * t - 90 * t2), 255 };
    };
    if (nred) { wa.tint = redTint; fl.tint = redTint; }
    if (w.level == 2) {
        Rgba water = { 72, 172, 162, 128 };
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            int ci = cx*CCELLS+i, ck = cz*CCELLS+kk;
            float gx = wx + i*CELL, gz = wz + kk*CELL, fy = w.floorY(ci,ck);
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            // Emit only the high side of a riser. Neighbour heights come from the world,
            // across chunk seams, so seams grow no false walls.
            auto skirt = [&](float x0,float z0,float x1,float z1,float low,Vec3 n) {
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
        // Elliptical vaults over the openings in the central partitions, lowest point
        // 4.6 m up. Collision is the full-height piers already in the wall grid.
        for (int axis=0; axis<2; ++axis) for (int start : {3,11}) {
            // Only where the partition is there with this opening in it: grand halls have
            // no partition, and an arch there would hang over open water.
            auto wallAt = [&](int t) { return axis ? d.wallW[8][t] : d.wallN[t][8]; };
            if (wallAt(start - 1) != WALL_SOLID || wallAt(start + 3) != WALL_SOLID) continue;
            auto pos = [&](float t,float y,float depth) -> Vec3 {
                return axis ? Vec3{wx+16+depth,y,wz+t} : Vec3{wx+t,y,wz+16+depth};
            };
            for (int n=0;n<24;++n) {
                float t0=start*CELL+6.0f*n/24, t1=start*CELL+6.0f*(n+1)/24;
                auto archY = [&](float t) { float u=(t-(start*CELL+3))/3;
                    return 4.6f+2.4f*sqrtf(std::max(0.0f,1-u*u)); };
                float y0=archY(t0),y1=archY(t1);
                for (float side : {-WT,WT}) {
                    Vec3 normal=axis ? Vec3{side/WT,0,0}:Vec3{0,0,side/WT};
                    wa.quad(pos(t0,y0,side),pos(t1,y1,side),pos(t1,w.wallH,side),pos(t0,w.wallH,side),normal,
                            {t0/2,-y0/2},{t1/2,-y1/2},{t1/2,-w.wallH/2},{t0/2,-w.wallH/2},wcol);
                }
                Vec3 normal=axis ? Vec3{0,-1,(y1-y0)/(t1-t0)}:Vec3{(y1-y0)/(t1-t0),-1,0};
                wa.quad(pos(t0,y0,-WT),pos(t1,y1,-WT),pos(t1,y1,WT),pos(t0,y0,WT),normal,
                        {t0/2,0},{t1/2,0},{t1/2,WT},{t0/2,WT},wcol);
            }
        }
    } else {
        // per-cell floor: sunken lounges (L0) and loading docks (L1) step, with real risers
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
            // A hole has no floor, and a flight builds its own treads (the flights, below).
            if (d.vflag[i][kk] & (VF_HOLE | VF_STAIR)) continue;
            if (w.level == 0 && w.softAt(cx * CCELLS + i, cz * CCELLS + kk)) {
                // A rotten patch: the cell is built as a shallow bowl, darkening toward the
                // middle. SOFT_DEPTH and the falloff are World::softDip, which groundAt also
                // walks, so the dip drawn is the dip stood in.
                const int SOFTSUB = 24;
                auto dipAt = [&](float u, float v) { return w.softDip(gx + u * CELL, gz + v * CELL); };
                for (int a = 0; a < SOFTSUB; a++) for (int b = 0; b < SOFTSUB; b++) {
                    float u0 = a / (float)SOFTSUB, u1 = (a + 1) / (float)SOFTSUB;
                    float v0 = b / (float)SOFTSUB, v1 = (b + 1) / (float)SOFTSUB;
                    float x0 = gx + u0 * CELL, x1 = gx + u1 * CELL;
                    float z0 = gz + v0 * CELL, z1 = gz + v1 * CELL;
                    float y00 = fy - dipAt(u0, v0), y10 = fy - dipAt(u1, v0);
                    float y11 = fy - dipAt(u1, v1), y01 = fy - dipAt(u0, v1);
                    // The normal comes from the height field's slope, so the bowl shades as a bowl.
                    float dydx = ((y10 + y11) - (y00 + y01)) / (2 * (x1 - x0));
                    float dydz = ((y01 + y11) - (y00 + y10)) / (2 * (z1 - z0));
                    float nl = sqrtf(dydx * dydx + 1 + dydz * dydz);
                    Vec3 n = { -dydx / nl, 1 / nl, -dydz / nl };
                    // Darker toward the middle. MB::quad carries one colour and one normal per
                    // quad, so the shading is banded at the subdivision: 8 across showed a
                    // chequerboard and 12 still quilted; 24 puts the step at 8 cm, under the carpet
                    // noise.
                    float md = 1.0f - dipAt((u0 + u1) * 0.5f, (v0 + v1) * 0.5f) / SOFT_DEPTH;
                    float k2 = 0.34f + 0.66f * md * md;
                    Rgba sc = { (unsigned char)(wcol.r * k2), (unsigned char)(wcol.g * k2),
                                 (unsigned char)(wcol.b * k2), wcol.a };
                    fl.quad({x0,y00,z0},{x1,y10,z0},{x1,y11,z1},{x0,y01,z1}, n,
                            {x0/2,z0/2},{x1/2,z0/2},{x1/2,z1/2},{x0/2,z1/2}, sc);
                }
            } else
            fl.quad({gx,fy,gz},{gx+CELL,fy,gz},{gx+CELL,fy,gz+CELL},{gx,fy,gz+CELL},{0,1,0},
                    {gx/2,gz/2},{(gx+CELL)/2,gz/2},{(gx+CELL)/2,(gz+CELL)/2},{gx/2,(gz+CELL)/2},wcol);
            if (d.elev[i][kk] == 0) continue;
            // heights across chunk seams, so terraces over a seam grow no phantom risers
            auto hgt = [&](int a, int b) { return w.floorY(cx * CCELLS + a, cz * CCELLS + b); };
            float hN = hgt(i, kk - 1), hS = hgt(i, kk + 1), hW = hgt(i - 1, kk), hE = hgt(i + 1, kk);
            // each shared riser is drawn once: by this cell when the neighbour is flat
            // (elev 0 cells stop above), otherwise by the lower cell of the pair
            if (hN < fy && hN == 0.0f) stepEdge(gx, gz, gx + CELL, gz, fy, hN, 0, -1);
            else if (hN > fy) stepEdge(gx, gz, gx + CELL, gz, hN, fy, 0, 1);
            if (hS < fy && hS == 0.0f) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, fy, hS, 0, 1);
            else if (hS > fy) stepEdge(gx, gz + CELL, gx + CELL, gz + CELL, hS, fy, 0, -1);
            if (hW < fy && hW == 0.0f) stepEdge(gx, gz, gx, gz + CELL, fy, hW, -1, 0);
            else if (hW > fy) stepEdge(gx, gz, gx, gz + CELL, hW, fy, 1, 0);
            if (hE < fy && hE == 0.0f) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, fy, hE, 1, 0);
            else if (hE > fy) stepEdge(gx + CELL, gz, gx + CELL, gz + CELL, hE, fy, -1, 0);
            if (w.level == 1) {
                // Safety edging along every drop off a loading dock: yellow and black, 100 mm
                // wide, 250 mm stripes, just in from the lip.
                auto edging = [&](float ex0, float ez0, float ex1, float ez1, float inx, float inz) {
                    const Vec2 uv = PLAIN_UV;
                    float len = sqrtf((ex1 - ex0) * (ex1 - ex0) + (ez1 - ez0) * (ez1 - ez0));
                    int n = (int)(len / 0.25f);
                    for (int q = 0; q < n; q++) {
                        float t0 = q / (float)n, t1 = (q + 1) / (float)n;
                        Vec3 a = { ex0 + (ex1 - ex0) * t0, fy + 0.004f, ez0 + (ez1 - ez0) * t0 };
                        Vec3 b = { ex0 + (ex1 - ex0) * t1, fy + 0.004f, ez0 + (ez1 - ez0) * t1 };
                        Vec3 c = { b.x + inx * 0.10f, b.y, b.z + inz * 0.10f }, dd2 = { a.x + inx * 0.10f, a.y, a.z + inz * 0.10f };
                        Rgba col = (q & 1) ? Rgba{ 34, 32, 28, 254 } : Rgba{ 196, 160, 40, 254 };
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
    // Per-cell ceiling height, greedy-meshed: a flat chunk is one quad. A quad per
    // cell cost 4% of the frame on the software rasteriser for 5% of cells that
    // step. UVs are world-space, so the tile grid runs across merged quads.
    {
        float cyc[CCELLS][CCELLS];
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
            cyc[i][kk] = w.ceilY(cx * CCELLS + i, cz * CCELLS + kk);
        bool done[CCELLS][CCELLS] = {};
        // A cell open to the storey above has no ceiling here; the storey above's
        // chunk draws what you see.
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
        // Where the neighbour's ceiling is higher, close the slot with a soffit, or
        // you see out of the building. Only the lower cell of a pair draws it, so a
        // shared edge is drawn once, across chunk seams too.
        for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
            float gx = wx + i * CELL, gz = wz + kk * CELL;
            int gi = cx * CCELLS + i, gk = cz * CCELLS + kk;
            float cy = cyc[i][kk];
            if (d.vflag[i][kk] & VF_OPENUP) continue;   // no ceiling of its own to close off
            auto soffit = [&](float ax, float az, float bx2, float bz2, float hi, int ni, int nk, uint8_t edge) {
                if (hi <= cy + 1e-4f) return;
                if (w.storeyH > 0.0f && (w.vflagAt(ni, nk) & VF_OPENUP)) {
                    // The edge of an opening into the storey above: a papered bulkhead up past
                    // the void over the tiles to the floor above. A wall or door head on the same
                    // line already covers it.
                    if (blocksEdge(edge) && edge != WALL_RAIL) return;
                    if (edge == WALL_DOOR || edge == WALL_EXIT) return;
                    float along0 = (ax == bx2) ? az : ax, along1 = (ax == bx2) ? bz2 : bx2;
                    // From the tile's middle, so no baseboard runs under the floor above.
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
    // Light fittings. The layout leaves out the Manila Room's (the shader masks the
    // same panels, uRoomMask) and any whose tray would overhang an opening into the
    // storey above (occupancy bit 3).
    for (const LightFitting &lf : L.fittings)
        if (lf.gap == FittingGap::None) addFitting(pr, ce, lf, w.level);
    // A rail on one cell edge, as the layout shapes it. Between two holes nothing
    // is drawn (the storey below draws that flight's balustrade); the edge is
    // collision only.
    auto railEdge = [&](const Opening &op) {
        if (op.overVoid) return;
        float ex0 = op.west ? op.line : op.e0, ez0 = op.west ? op.e0 : op.line;
        float ex1 = op.west ? ex0 : ex0 + CELL, ez1 = op.west ? ez0 + CELL : ez0;
        addRailRun(wa, pr, ex0, ez0, ex1, ez1, op.base, op.railTop0, op.railTop1, op.underside, op.voidSide);
    };
    const FixtureMeshes fm = { pr, fx, scr, ao };
    auto addCellFixtures = [&](int i, int kk, FixtureStage stage) {
        for (const Fixture &f : L.fixturesIn(i, kk))
            if (stageOf(f.kind) == stage) addFixture(fm, f);
    };
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++) {
        float gx = wx + i * CELL, gz = wz + kk * CELL;
        int gi0 = cx * CCELLS + i, gk0 = cz * CCELLS + kk;
        // A wall between two cells of different heights runs from the lower floor to
        // the higher ceiling, or a step leaves a gap under it on one side and a slot over
        // it on the other. Everything fixed to it (sill, door head, architrave) is
        // measured off that base.
        float fyc = w.floorY(gi0, gk0), cyc = w.ceilY(gi0, gk0);
        float nb = std::min(w.floorY(gi0, gk0 - 1), w.floorY(gi0, gk0));
        float nt = std::max(w.ceilY(gi0, gk0 - 1), w.ceilY(gi0, gk0));
        float wb = std::min(w.floorY(gi0 - 1, gk0), w.floorY(gi0, gk0));
        float wt2 = std::max(w.ceilY(gi0 - 1, gk0), w.ceilY(gi0, gk0));
        // Through the accessors, never the raw arrays: the overlays (shifted walls,
        // unlocked doors) land there, and collision reads them too. Read raw, an
        // unlocked door keeps its leaf and a shifted doorway stays open on screen.
        uint8_t nv = w.wallNVal(gi0, gk0);
        // Which sides of this cell's edges have a floor (for skirting and creases) and
        // a ceiling (for creases); trim drawn where there is none hangs in air.
        const bool flN = cellHasFloor(w, gi0, gk0), flS = cellHasFloor(w, gi0, gk0 - 1),
                   flW = cellHasFloor(w, gi0 - 1, gk0);
        const bool clN = cellHasCeiling(w, gi0, gk0), clS = cellHasCeiling(w, gi0, gk0 - 1),
                   clW = cellHasCeiling(w, gi0 - 1, gk0);
        // A neighbour's end cap is buried in the next wall along, but not in a thinner rail.
        auto buries = [](uint8_t v) { return blocksEdge(v) && v != WALL_RAIL; };
        // The openings in this cell's north and west edges, as the layout has them.
        const Opening *on = L.opening(i, kk, false), *ow = L.opening(i, kk, true);
        auto is = [](const Opening *op, OpeningKind k) { return op && op->kind == k; };
        auto isExit = [](const Opening *op) {
            return op && (op->kind == OpeningKind::Exit || op->kind == OpeningKind::CursedExit);
        };
        // Exits glow, red where cursed.
        auto exitGlow = [](const Opening &op) {
            return op.kind == OpeningKind::CursedExit ? Rgba{ 255, 60, 40, 70 } : Rgba{ 255, 248, 225, 70 };
        };
        if (is(on, OpeningKind::Rail)) railEdge(*on);
        // Faces that look into a hole in this storey's floor (voidFace).
        auto holeAt = [&](int a, int b) { return w.storeyH > 0.0f && (w.vflagAt(a, b) & VF_HOLE); };
        if (nv == WALL_SOLID) {
            int sk = (buries(w.wallNVal(gi0 - 1, gk0)) ? 4 : 0) | (buries(w.wallNVal(gi0 + 1, gk0)) ? 8 : 0);
            int vfN = (holeAt(gi0, gk0 - 1) ? 1 : 0) | (holeAt(gi0, gk0) ? 2 : 0);
            addBoxSides(wa, gx - WT, nb, gz - WT, gx + CELL + WT, nt, gz + WT, false, sk, RGBA_WHITE, vfN);
        }
        else if (is(on, OpeningKind::Window)) addWindowN(wa, gl, *on, gx, gz, nb, nt, w.level);
        else if (isExit(on)) {   // exit doorway on x-running wall
            const float a0 = on->a0, a1 = on->a1, head = on->headY;
            addBoxSides(wa, gx - WT, nb, gz - WT, a0, nt, gz + WT);
            addBoxSides(wa, a1, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, a0, head, gz - WT, a1, nt, gz + WT, true);
            Rgba glow = exitGlow(*on);
            wa.quad({a0,nb,gz},{a1,nb,gz},{a1,head,gz},{a0,head,gz},{0,0,-1},
                    {0,1},{1,1},{1,0},{0,0},glow);
            wa.quad({a1,nb,gz},{a0,nb,gz},{a0,head,gz},{a1,head,gz},{0,0,1},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (w.level == 1) addSymbolDoor(pr, fx, *on);
        }
        else if (is(on, OpeningKind::Doorway)) {   // doorway on x-running wall
            // Adjacent doorways would leave a 0.7 m pier between them. generate moves
            // doors apart where it can; where two must stay adjacent (joinLo, joinHi),
            // drop the jamb between them and let the header run across, one wide
            // opening. gatherCellAABBs drops the matching jamb boxes.
            const bool mW = on->joinLo, mE = on->joinHi;
            const float a0 = on->a0, a1 = on->a1, head = on->headY;
            if (mW) addBoxSides(wa, gx - WT, head, gz - WT, a0, nt, gz + WT, true);
            else    addBoxSides(wa, gx - WT, nb, gz - WT, a0, nt, gz + WT);
            if (mE) addBoxSides(wa, a1, head, gz - WT, gx + CELL + WT, nt, gz + WT, true);
            else    addBoxSides(wa, a1, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, a0, head, gz - WT, a1, nt, gz + WT, true);
            // The architrave stands proud of both faces. A merged side has no jamb, so its
            // post goes and the head runs on to meet the neighbour's.
            float tx0 = mW ? gx - WT : gx + 0.29f, tx1 = mE ? gx + CELL + WT : gx + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                if (!mW) addSolidBox(pr, gx + 0.29f, nb, zf, a0, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                if (!mE) addSolidBox(pr, a1, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, tx0, head, zf, tx1, nb + 2.36f, zf + TRIM_T, TRIM_COL);
            }
            // and a threshold strip
            const float fy0 = on->floorY;
            addSolidBox(pr, mW ? gx : a0, fy0, gz - 0.07f,
                        mE ? gx + CELL : a1, fy0 + 0.013f, gz + 0.07f, SILL_COL);
        }
        else if (is(on, OpeningKind::LockedDoor)) {   // a door with the leaf still in it
            const float a0 = on->a0, a1 = on->a1, head = on->headY, fy0 = on->floorY;
            addBoxSides(wa, gx - WT, nb, gz - WT, a0, nt, gz + WT);
            addBoxSides(wa, a1, nb, gz - WT, gx + CELL + WT, nt, gz + WT);
            addBoxSides(wa, a0, head, gz - WT, a1, nt, gz + WT, true);
            // The leaf fills the opening: a slab, visible from both sides.
            addSolidBox(pr, gx + 0.36f, fy0, gz - 0.025f, gx + 1.64f, nb + 2.28f, gz + 0.025f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {   // architrave, as on an open one
                float zf = (sgn < 0) ? gz - WT - TRIM_T : gz + WT;
                addSolidBox(pr, gx + 0.29f, nb, zf, a0, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, a1, nb, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                addSolidBox(pr, gx + 0.29f, head, zf, gx + 1.71f, nb + 2.36f, zf + TRIM_T, TRIM_COL);
                // handle and escutcheon, 1.02 m up on the latch side
                addSolidBox(pr, gx + 1.34f, fy0 + 0.97f, zf - 0.02f,
                            gx + 1.50f, fy0 + 1.07f, zf + TRIM_T, LOCK_COL);
            }
        }
        uint8_t wv = w.wallWVal(gi0, gk0);
        if (is(ow, OpeningKind::Rail)) railEdge(*ow);
        if (wv == WALL_SOLID) {
            int sk = (buries(w.wallWVal(gi0, gk0 - 1)) ? 1 : 0) | (buries(w.wallWVal(gi0, gk0 + 1)) ? 2 : 0);
            int vfW = (holeAt(gi0 - 1, gk0) ? 4 : 0) | (holeAt(gi0, gk0) ? 8 : 0);
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, gz + CELL + WT, false, sk, RGBA_WHITE, vfW);
        }
        else if (is(ow, OpeningKind::Window)) addWindowW(wa, gl, *ow, gx, gz, wb, wt2, w.level);
        else if (isExit(ow)) {   // exit doorway on z-running wall
            const float a0 = ow->a0, a1 = ow->a1, head = ow->headY;
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, a0);
            addBoxSides(wa, gx - WT, wb, a1, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, head, a0, gx + WT, wt2, a1, true);
            Rgba glow = exitGlow(*ow);
            wa.quad({gx,wb,a0},{gx,wb,a1},{gx,head,a1},{gx,head,a0},{1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
            wa.quad({gx,wb,a1},{gx,wb,a0},{gx,head,a0},{gx,head,a1},{-1,0,0},
                    {0,1},{1,1},{1,0},{0,0},glow);
            if (w.level == 1) addSymbolDoor(pr, fx, *ow);
        }
        else if (is(ow, OpeningKind::Doorway)) {   // doorway on z-running wall
            // Merged as on the x-running wall above.
            const bool mN = ow->joinLo, mS = ow->joinHi;
            const float a0 = ow->a0, a1 = ow->a1, head = ow->headY;
            if (mN) addBoxSides(wa, gx - WT, head, gz - WT, gx + WT, wt2, a0, true);
            else    addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, a0);
            if (mS) addBoxSides(wa, gx - WT, head, a1, gx + WT, wt2, gz + CELL + WT, true);
            else    addBoxSides(wa, gx - WT, wb, a1, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, head, a0, gx + WT, wt2, a1, true);
            float tz0 = mN ? gz - WT : gz + 0.29f, tz1 = mS ? gz + CELL + WT : gz + 1.71f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                if (!mN) addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, a0, TRIM_COL);
                if (!mS) addSolidBox(pr, xf, wb, a1, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, head, tz0, xf + TRIM_T, wb + 2.36f, tz1, TRIM_COL);
            }
            const float fy0 = ow->floorY;
            addSolidBox(pr, gx - 0.07f, fy0, mN ? gz : a0,
                        gx + 0.07f, fy0 + 0.013f, mS ? gz + CELL : a1, SILL_COL);
        }
        else if (is(ow, OpeningKind::LockedDoor)) {   // a door with the leaf still in it
            const float a0 = ow->a0, a1 = ow->a1, head = ow->headY, fy0 = ow->floorY;
            addBoxSides(wa, gx - WT, wb, gz - WT, gx + WT, wt2, a0);
            addBoxSides(wa, gx - WT, wb, a1, gx + WT, wt2, gz + CELL + WT);
            addBoxSides(wa, gx - WT, head, a0, gx + WT, wt2, a1, true);
            addSolidBox(pr, gx - 0.025f, fy0, gz + 0.36f, gx + 0.025f, wb + 2.28f, gz + 1.64f, LEAF_COL);
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                float xf = (sgn < 0) ? gx - WT - TRIM_T : gx + WT;
                addSolidBox(pr, xf, wb, gz + 0.29f, xf + TRIM_T, wb + 2.36f, a0, TRIM_COL);
                addSolidBox(pr, xf, wb, a1, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf, head, gz + 0.29f, xf + TRIM_T, wb + 2.36f, gz + 1.71f, TRIM_COL);
                addSolidBox(pr, xf - 0.02f, fy0 + 0.97f, gz + 1.34f,
                            xf + TRIM_T, fy0 + 1.07f, gz + 1.50f, LOCK_COL);
            }
        }
        if (w.level == 0 || w.level == 4) {
            // Level 0 and 4 skirting: thin timber within the collision clearance, no
            // separate obstacle.
            Rgba trim = w.level == 0 ? Rgba{91, 71, 39, 254} : Rgba{67, 41, 34, 254};
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
        // Baked AO along this cell's walls: floor and ceiling strips and a strip up
        // and down each face. Solid walls, windows, locked doors and exits (a noclip
        // wall must match its neighbours); not doorways or rails (a rail has its own
        // contact shadow).
        bool nvWall = (blocksEdge(nv) && nv != WALL_RAIL);
        bool wvWall = (blocksEdge(wv) && wv != WALL_RAIL);
        if (nvWall) {
            float fyS = w.floorY(gi0, gk0 - 1) + 0.005f, fyN = w.floorY(gi0, gk0) + 0.005f;
            // Ceiling creases follow each side's own ceiling, or they hang in the air
            // under a raised one.
            float cyS = w.ceilY(gi0, gk0 - 1) - 0.005f, cyN = w.ceilY(gi0, gk0) - 0.005f;
            // exactly one cell long, so neighbours butt without a double blend
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
        // Lift doors, spalls, the building's fittings (decals and small boxes, alpha
        // 254, in their own mesh because the props mesh is the largest and uses 16-bit
        // indices with no overflow check), conduit and scrawl.
        addCellFixtures(i, kk, FixtureStage::Walls);
        if (d.pillar[i][kk]) {
            addBoxSides(wa, gx + PILLAR_LO, fyc, gz + PILLAR_LO, gx + PILLAR_HI, cyc, gz + PILLAR_HI);
            addCellFixtures(i, kk, FixtureStage::Pillar);
            addContactShadow(ao, gx + 1.0f, gz + 1.0f, fyc, 0.0f, 0.58f, 0.58f);
            // AO up the pillar's foot and a ceiling crease round its head
            float pfy = fyc + 0.005f;
            float px0 = gx + PILLAR_LO, px1 = gx + PILLAR_HI, pz0 = gz + PILLAR_LO, pz1 = gz + PILLAR_HI, cy = cyc - 0.005f;
            aoStrip({ px0, pfy, pz0 - 0.006f }, { px1, pfy, pz0 - 0.006f }, { 0, AOH, 0 }, { 0, 0, -1 }, 0);
            aoStrip({ px0, pfy, pz1 + 0.006f }, { px1, pfy, pz1 + 0.006f }, { 0, AOH, 0 }, { 0, 0, 1 }, 0);
            aoStrip({ px0 - 0.006f, pfy, pz0 }, { px0 - 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { -1, 0, 0 }, 0);
            aoStrip({ px1 + 0.006f, pfy, pz0 }, { px1 + 0.006f, pfy, pz1 }, { 0, AOH, 0 }, { 1, 0, 0 }, 0);
            aoStrip({ px0, cy, pz0 }, { px1, cy, pz0 }, { 0, 0, -AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz1 }, { px1, cy, pz1 }, { 0, 0, AOW }, { 0, -1, 0 }, AOC);
            aoStrip({ px0, cy, pz0 }, { px0, cy, pz1 }, { -AOW, 0, 0 }, { 0, -1, 0 }, AOC);
            aoStrip({ px1, cy, pz0 }, { px1, cy, pz1 }, { AOW, 0, 0 }, { 0, -1, 0 }, AOC);
        }
        if (const PropPlacement *p = L.propIn(i, kk)) addProp(*p, w.level, pr, ce, ao, fx);
    }
    // ---- the flights rising from this storey (stampFeature has the plan). Steps
    // belong to the storey they stand on; the storey above draws its hole, rails and
    // arrival floor.
    for (int q = 0; q < d.nfeat; q++) {
        const VertFeat &f = d.feats[q];
        if (f.lo != w.qs) continue;
        auto to = [&](float u, float y, float v) { return w.featureWorld(f, cx, cz, u, y, v); };
        const float R = w.storeyH / 24.0f;
        if (f.kind == VK_STAIRWELL) {
            // Lane A: 12 risers from the foot (plain floor, drawn with the rest) to the
            // half landing.
            const float G = 4.0f / 12.0f;
            Vec3 a, b, c, e;
            for (int i = 0; i < 12; i++)
                addStep(fl, pr, to, 0, CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
            // The half landing, across the shaft at the far end.
            a = to(0, 12 * R, 6); b = to(2 * CELL, 12 * R, 6); c = to(2 * CELL, 12 * R, 8); e = to(0, 12 * R, 8);
            fl.quad(a, b, c, e, { 0, 1, 0 }, { a.x / 2, a.z / 2 }, { b.x / 2, b.z / 2 }, { c.x / 2, c.z / 2 },
                    { e.x / 2, e.z / 2 }, RGBA_WHITE);
            // Lane B: 12 more, back toward the door above. Its top landing is the upper
            // storey's floor, drawn by that storey.
            for (int j = 0; j < 12; j++)
                addStep(fl, pr, to, CELL, 2 * CELL, 6 - (j + 1) * G, 6 - j * G, 0, 12 * R + (j + 1) * R, false, false);
            // The landing light's steel body. Its tube is drawn by the renderer, because
            // it goes out in a blackout and a chunk mesh cannot.
            Vec3 lamp = w.landingLamp(f, cx, cz);
            Vec3 lo2 = to(CELL - 0.55f, lamp.y - 0.06f, 8 - WT - 0.13f), hi2 = to(CELL + 0.55f, lamp.y + 0.06f, 8 - WT);
            addSolidBox(pr, std::min(lo2.x, hi2.x), lamp.y - 0.06f, std::min(lo2.z, hi2.z),
                        std::max(lo2.x, hi2.x), lamp.y + 0.06f, std::max(lo2.z, hi2.z), Rgba{ 150, 150, 142, 254 });
        } else if (f.kind == VK_STAIR || f.stairU >= 0) {
            // A straight flight: 24 risers over rows 1..4, between its wall and its
            // balustrade, which cover the steps' ends.
            int s0 = f.kind == VK_STAIR ? 0 : f.stairU, s1 = f.kind == VK_STAIR ? f.wu - 1 : f.stairU;
            const float G = 8.0f / 24.0f;
            for (int i = 0; i < 24; i++)
                addStep(fl, pr, to, s0 * CELL, (s1 + 1) * CELL, 2 + i * G, 2 + (i + 1) * G, 0, (i + 1) * R, true, false);
        }
    }
    // Pipework and valve standpipes, a second walk over the cells so a pipe run's
    // segments abut; then what belongs to the whole chunk (the Manila Room,
    // streamers).
    for (int i = 0; i < CCELLS; i++) for (int kk = 0; kk < CCELLS; kk++)
        addCellFixtures(i, kk, FixtureStage::Service);
    for (const Fixture &f : L.chunkFixtures()) addFixture(fm, f);
    out.parts[MESH_FLOOR] = std::move(fl);
    out.parts[MESH_CEILING] = std::move(ce);
    out.parts[MESH_WALLS] = std::move(wa);
    out.parts[MESH_PROPS] = std::move(pr);
    out.parts[MESH_WATER] = std::move(wt);
    out.parts[MESH_SCRAWL] = std::move(scr);
    out.parts[MESH_FIXTURES] = std::move(fx);
    out.parts[MESH_GLASS] = std::move(gl);
    out.parts[MESH_AO] = std::move(ao);
}

