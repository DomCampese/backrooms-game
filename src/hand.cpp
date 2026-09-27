// The bare hand on the revolver, and the sleeve behind it.
//
// The gun floated. Every first-person game that reads as real puts a hand on
// the weapon, and nothing reachable from here ships one we could use, so it is
// built: a signed-distance field of the hand's bones and pads, blended with a
// smooth minimum so the joints merge like flesh instead of like sausages, then
// polygonised once with surface nets and shaded with the field's own gradient.
//
// It was a leather glove first, because a glove forgives a hand built from
// capsules. It read as inhuman, and the reference it was replaced against is a
// photograph of a real hand held the same way: pale skin, a thumbnail, fine
// lines over the thumb's knuckle, a black jersey cuff. What carries skin, in
// order of what it bought:
// - colour that is not one colour: redder over the knuckles, pinker at the
//   fingertips and in the folds, blotched a little everywhere;
// - nails, as raised plates on the distal bones with their own gloss;
// - the wrinkles over each knuckle, which are a millimetre apart — finer than
//   the mesh — so they live in small texture patches laid over each joint.
//
// Everything is placed against the revolver's measured geometry, in the GLB's
// model space (metres; +z to the muzzle, +y up, +x the gun's right). The grip
// is 27 mm across (x ±0.0135) and rakes back: its back strap runs along
// z = -0.082 + 0.31y and its front strap along z = -0.0445 + 0.14(y + 0.04)
// below the trigger guard, whose underside is at y ≈ -0.035. The trigger is a
// 7 mm blade at z 0..0.028; the cylinder starts at z 0.006 and is ±0.025
// across; the hammer spur is at y 0.03-0.06, z -0.045..-0.004; the walnut
// panels reach y 0.03 at z -0.066..-0.043. A right hand on that: the web of
// the hand high on the back strap under the hammer, the thumb along the
// frame's left side under the cylinder, the index finger through the guard
// onto the trigger's face, the other three wrapping the front strap with their
// tips on the left flat — which is the side the camera sees. The back of the
// hand faces +x; the camera sees the thumb, the web, the fingertips and the
// radial (thumb) side of the wrist.
#include "hand.h"
#include "textures.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

struct V { float x, y, z; };
inline V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator*(V a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline V mul(V a, V b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
inline V mix(V a, V b, float t) { return a + (b - a) * t; }
inline float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float len(V a) { return sqrtf(dot(a, a)); }
inline V norm(V a) { float l = len(a); return l > 1e-9f ? a * (1.0f / l) : V{ 0, 1, 0 }; }
inline V cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline V perpTo(V v, V n) { return norm(v - n * dot(v, n)); }
inline float sstep01(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

inline float smin(float a, float b, float k) {
    float h = std::max(k - fabsf(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// One part of the hand: a tapered capsule (a bone with its flesh), flattened
// along one direction because fingers are wider than they are deep, or an
// oriented ellipsoid (a pad of muscle, a nail), and how softly it blends.
struct Prim {
    bool ell;
    V a, b;              // capsule ends, or ellipsoid centre (a)
    V ex, ey, ez;        // ellipsoid axes; a capsule's ex is the direction it is flattened in
    V rad;               // ellipsoid radii along ex, ey, ez
    float ra, rb, sd;    // capsule radii at a and b; its depth along ex, as a fraction of them
    float k;             // smooth-union width, metres
    V bc; float br;      // bounding sphere, for skipping it far away
};

float primDist(const Prim &p, V q) {
    if (p.ell) {
        V d = q - p.a;
        V l = { dot(d, p.ex), dot(d, p.ey), dot(d, p.ez) };
        V s = { l.x / p.rad.x, l.y / p.rad.y, l.z / p.rad.z };
        V s2 = { l.x / (p.rad.x * p.rad.x), l.y / (p.rad.y * p.rad.y), l.z / (p.rad.z * p.rad.z) };
        float k0 = len(s), k1 = len(s2);
        return k1 > 1e-9f ? k0 * (k0 - 1.0f) / k1 : -std::min(p.rad.x, std::min(p.rad.y, p.rad.z));
    }
    V pa = q - p.a, ba = p.b - p.a;
    float t = clampf(dot(pa, ba) / std::max(dot(ba, ba), 1e-12f), 0.0f, 1.0f);
    V off = pa - ba * t;
    float r = p.ra + (p.rb - p.ra) * t;
    if (p.sd > 0.999f) return len(off) - r;
    // squash space along ex: the level set is exact, the distance a little
    // under-estimated across it, which the smooth union does not mind
    float od = dot(off, p.ex);
    V perp = off - p.ex * od;
    return (sqrtf(dot(perp, perp) + (od / p.sd) * (od / p.sd)) - r) * p.sd;
}

// A finger bone: its ends and radii, which bone of the digit it is (0 nearest
// the palm), which digit (0 thumb, 1 index .. 4 little), which way its back
// faces, how far it bends from the bone before it, and its nail prim if any.
struct Bone { V a, b, dorsal; float ra, rb, sd; int idx, digit; float flex; int nail; };
// A joint the skin wrinkles over: where, the axis of the bone beyond it, its
// back, its radius and how bent it is (straight joints wrinkle, bent ones go taut).
struct Joint { V c, ax, dorsal; float r, flex; int digit, idx; };

struct HandField {
    std::vector<Prim> prims;
    std::vector<Bone> bones;
    std::vector<Joint> joints;
    void cap(V a, V b, float ra, float rb, float k, V flat = { 0, 0, 0 }, float sd = 1.0f) {
        Prim p{};
        p.ell = false; p.a = a; p.b = b; p.ra = ra; p.rb = rb; p.k = k;
        p.ex = flat; p.sd = len(flat) > 0.5f ? sd : 1.0f;
        p.bc = (a + b) * 0.5f; p.br = len(b - a) * 0.5f + std::max(ra, rb);
        prims.push_back(p);
    }
    void ell(V c, V r, float k, V ex = { 1, 0, 0 }, V ey = { 0, 1, 0 }, V ez = { 0, 0, 1 }) {
        Prim p{};
        p.ell = true; p.a = c; p.rad = r; p.k = k; p.ex = ex; p.ey = ey; p.ez = ez;
        p.bc = c; p.br = std::max(r.x, std::max(r.y, r.z));
        prims.push_back(p);
    }
    // A digit: four joints, the bones between them tapering and flattened, a
    // knuckle standing a little proud of each joint on the back, and a nail on
    // the last bone. The back is worked out per bone as "away from what the
    // finger is holding": `anchor` gives the point on the grip or frame each
    // bone wraps.
    template <class A> void finger(int digit, const V j[4], const float r[4], float sd, float k0, A anchor) {
        V prevAx = { 0, 0, 0 };
        for (int i = 0; i < 3; i++) {
            V ax = norm(j[i + 1] - j[i]), mid = (j[i] + j[i + 1]) * 0.5f;
            V dorsal = perpTo(mid - anchor(mid), ax);
            float flex = i > 0 ? acosf(clampf(dot(prevAx, ax), -1.0f, 1.0f)) : 0.9f;
            cap(j[i], j[i + 1], r[i], r[i + 1], i == 0 ? k0 : 0.003f, dorsal, sd);
            V kn = j[i] + dorsal * (r[i] * 0.16f);
            cap(kn, kn, r[i] * 0.93f, r[i] * 0.93f, 0.003f, dorsal, sd);
            bones.push_back({ j[i], j[i + 1], dorsal, r[i], r[i + 1], sd, i, digit, flex, -1 });
            if (digit > 0 || i > 0) joints.push_back({ j[i], ax, dorsal, r[i], flex, digit, i });
            prevAx = ax;
        }
        // The nail: a thin plate over the far 70% of the last bone, standing
        // a third of a millimetre proud, curving down into the skin at its
        // sides and base the way the nail folds hold it.
        Bone &d = bones.back();
        V ax = norm(d.b - d.a), lat = norm(cross(ax, d.dorsal));
        float L = len(d.b - d.a), rr = d.ra + (d.rb - d.ra) * 0.62f;
        const float hn = 0.0011f;
        V c = d.a + ax * (L * 0.62f) + d.dorsal * (rr * sd - hn + 0.00035f);
        ell(c, V{ hn, 0.62f * rr, 0.36f * L }, 0.0007f, d.dorsal, lat, ax);
        d.nail = (int)prims.size() - 1;
    }
    float operator()(V q) const {
        float d = 1e9f;
        for (const Prim &p : prims) {
            float lb = len(q - p.bc) - p.br;             // nothing of it is nearer than this
            if (lb > d + p.k) continue;
            d = smin(d, primDist(p, q), p.k);
        }
        return d;
    }
    V grad(V p, float e = 0.0003f) const {
        const HandField &f = *this;
        return norm(V{ f(p + V{ e, 0, 0 }) - f(p - V{ e, 0, 0 }), f(p + V{ 0, e, 0 }) - f(p - V{ 0, e, 0 }),
                       f(p + V{ 0, 0, e }) - f(p - V{ 0, 0, e }) });
    }
};

// The grip's front strap below the guard, where the three lower fingers wrap.
// Pale skin, as albedo. The first try (0.88, 0.70, 0.63) was the colour of
// the photograph's lit side and came out as a mannequin in the building's light.
const V SKIN = { 0.80f, 0.61f, 0.53f };

float frontStrap(float y) { return -0.0445f + (y + 0.04f) * 0.14f; }
float backStrap(float y) { return -0.082f + 0.31f * y; }

// The forearm: its axis (from the wrist toward the elbow, off the bottom right
// of the screen), the wrist crease on it, where the sleeve begins, and its
// frame — "radial" is the thumb side (up here), "dorsal" the back of the arm.
const V ARM = { 0.29924f, -0.62841f, -0.71818f };             // norm(0.30, -0.63, -0.72)
const V WRIST = { 0.018f, -0.104f, -0.140f };
const V SLEEVE0 = { 0.030f, -0.131f, -0.171f };
inline V armRadial() { return perpTo(V{ 0, 1, 0 }, ARM); }
inline V armDorsal() { return norm(cross(armRadial(), ARM)) * -1.0f; }
inline float alongArm(V p) { return dot(p - WRIST, ARM); }
const float SLEEVE_AT = 0.0433f;                               // alongArm(SLEEVE0)

HandField buildField() {
    HandField f;
    // Middle, ring and little fingers wrap the front strap: the proximal bone
    // along the grip's right side, the middle one across the front, the tip
    // back along the left flat. Heights: the middle finger tucked under the
    // trigger guard, the others a finger's width apart below it.
    struct F { float y, r, s; };
    const F fingers[3] = { { -0.0465f, 0.0094f, 1.00f }, { -0.0655f, 0.0090f, 0.96f }, { -0.0835f, 0.0079f, 0.82f } };
    for (int n = 0; n < 3; n++) {
        const F &g = fingers[n];
        float zf = frontStrap(g.y), s = g.s, dy = 0.0015f;
        V j[4] = {
            { 0.0275f, g.y + dy, zf - 0.028f * s },     // knuckle
            { 0.0135f, g.y, zf + 0.0062f },             // round the front-right corner
            { -0.0100f, g.y - dy * 0.5f, zf + 0.0035f },  // across the front
            { -0.0205f, g.y - dy, zf - 0.013f * s },    // tip on the left flat
        };
        float r[4] = { g.r * 1.02f, g.r * 0.97f, g.r * 0.9f, g.r * 0.82f };
        f.finger(2 + n, j, r, 0.88f, 0.009f, [](V m) { return V{ 0.0f, m.y, (backStrap(m.y) + frontStrap(m.y)) * 0.5f }; });
    }
    // Index: through the guard, its pad on the trigger's face, the tip just
    // showing on the gun's left.
    {
        V j[4] = { { 0.028f, -0.0135f, -0.058f }, { 0.0145f, -0.0170f, -0.0165f },
                   { 0.0040f, -0.0200f, 0.0095f }, { -0.0105f, -0.0215f, 0.0165f } };
        float r[4] = { 0.0094f, 0.0087f, 0.0079f, 0.0072f };
        f.finger(1, j, r, 0.88f, 0.009f, [](V m) { return V{ 0.0f, m.y - 0.004f, m.z }; });
    }
    // Thumb, from its root in the heel of the hand along the frame's left
    // side, the nail facing out at the camera. A thumb is broad and shallow
    // at the end, more so than a finger.
    {
        V j[4] = { { -0.0235f, -0.030f, -0.092f }, { -0.0290f, 0.0010f, -0.0730f },
                   { -0.0265f, 0.0150f, -0.0480f }, { -0.0245f, 0.0195f, -0.0265f } };
        float r[4] = { 0.0115f, 0.0106f, 0.0100f, 0.0092f };
        f.finger(0, j, r, 0.82f, 0.016f, [](V m) { return V{ 0.0f, m.y - 0.006f, m.z }; });
    }
    // The palm: the metacarpals across the grip's right side with the knuckle
    // ridge along their ends, the pad behind the back strap, the thenar under
    // the thumb, the heel of the hand, and the web between thumb and index
    // riding high on the back strap under the hammer.
    // One wide blend (18 mm) runs them together into a single mass, as a hand
    // is: with the narrow one the fingers use, the pad behind the back strap
    // stood between the thenar and the back of the hand as a third finger,
    // with a groove down each side of it.
    const float PALM_K = 0.018f;
    f.ell({ 0.0240f, -0.056f, -0.090f }, { 0.0120f, 0.041f, 0.027f }, PALM_K);
    f.cap({ 0.0280f, -0.0135f, -0.0600f }, { 0.0262f, -0.0835f, -0.0735f }, 0.0105f, 0.0092f, 0.010f);
    f.cap({ 0.0010f, 0.0130f, -0.0875f }, { 0.0070f, -0.0900f, -0.1205f }, 0.0078f, 0.0105f, PALM_K);
    f.ell({ -0.0195f, -0.0290f, -0.0900f }, { 0.0095f, 0.0260f, 0.0165f }, PALM_K);
    // the muscle between the thumb and the palm, behind the top of the grip:
    // without it that corner was a pit, which read as a dark hole under the web
    f.ell({ -0.0095f, 0.0040f, -0.0895f }, { 0.0090f, 0.0120f, 0.0085f }, PALM_K);
    f.ell({ 0.0150f, -0.0900f, -0.1180f }, { 0.0150f, 0.0200f, 0.0160f }, PALM_K);
    // (the web between thumb and index is a fold, not a pad: thin)
    f.cap({ 0.0270f, -0.0150f, -0.0620f }, { 0.0160f, 0.0100f, -0.0790f }, 0.0080f, 0.0070f, 0.010f);
    f.cap({ 0.0160f, 0.0100f, -0.0790f }, { -0.0020f, 0.0210f, -0.0830f }, 0.0070f, 0.0065f, 0.010f);
    f.cap({ -0.0020f, 0.0210f, -0.0830f }, { -0.0290f, 0.0010f, -0.0730f }, 0.0065f, 0.0100f, 0.010f);
    // The back of the hand under the web: a flat plate from the web down to
    // the palm on the grip's right. Without it the web was an arch over a
    // tunnel, and from the camera the arch read as a ball on top of the hand
    // with a dark fold under it.
    f.cap({ 0.0170f, 0.0080f, -0.0810f }, { 0.0250f, -0.0520f, -0.0930f }, 0.0090f, 0.0135f, PALM_K, V{ 1, 0, 0 }, 0.65f);
    // The wrist and forearm, oval (a wrist is half again as wide as it is
    // deep), running on into the sleeve.
    V thick = armDorsal();
    f.cap({ 0.0120f, -0.0920f, -0.1280f }, SLEEVE0 + V{ 0.0f, 0.001f, 0.001f }, 0.0235f, 0.0255f, PALM_K, thick, 0.80f);
    f.cap(SLEEVE0 + V{ 0.0f, 0.001f, 0.001f }, SLEEVE0 + ARM * 0.10f, 0.0265f, 0.0290f, 0.010f, thick, 0.76f);
    return f;
}

// ------------------------------------------------------------- the skin

// Surface nets: one vertex per grid cell the surface passes through, at the
// average of where it crosses that cell's edges, and one quad per grid edge
// the surface crosses. No tables, a watertight mesh, and with normals from the
// field's gradient it shades as smoothly as the field is. The field is sampled
// on a coarse grid first and only evaluated finely near the surface: the
// forearm made the box large and most of it is air.
struct Surface { std::vector<V> p; std::vector<uint32_t> tri; };

Surface polygonise(const HandField &field, V lo, V hi, float h) {
    const int nx = (int)ceilf((hi.x - lo.x) / h), ny = (int)ceilf((hi.y - lo.y) / h), nz = (int)ceilf((hi.z - lo.z) / h);
    auto at = [&](int i, int j, int k) { return ((size_t)k * (ny + 1) + j) * (nx + 1) + i; };
    auto pos = [&](float i, float j, float k) { return V{ lo.x + i * h, lo.y + j * h, lo.z + k * h }; };
    const int C = 4;
    const int cx = nx / C + 1, cy = ny / C + 1, cz = nz / C + 1;
    std::vector<float> coarse((size_t)(cx + 1) * (cy + 1) * (cz + 1));
    auto cat = [&](int i, int j, int k) { return ((size_t)k * (cy + 1) + j) * (cx + 1) + i; };
    for (int k = 0; k <= cz; k++) for (int j = 0; j <= cy; j++) for (int i = 0; i <= cx; i++)
        coarse[cat(i, j, k)] = field(pos((float)(i * C), (float)(j * C), (float)(k * C)));
    std::vector<float> d((size_t)(nx + 1) * (ny + 1) * (nz + 1));
    const float far = C * h * 1.8f;
    for (int k = 0; k <= nz; k++) for (int j = 0; j <= ny; j++) for (int i = 0; i <= nx; i++) {
        float c = coarse[cat((i + C / 2) / C, (j + C / 2) / C, (k + C / 2) / C)];
        d[at(i, j, k)] = fabsf(c) > far ? c : field(pos((float)i, (float)j, (float)k));
    }
    Surface s;
    std::vector<int> cellVert((size_t)nx * ny * nz, -1);
    auto cell = [&](int i, int j, int k) { return ((size_t)k * ny + j) * nx + i; };
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
        float c[8]; int mask = 0;
        for (int q = 0; q < 8; q++) { c[q] = d[at(i + (q & 1), j + ((q >> 1) & 1), k + ((q >> 2) & 1))]; if (c[q] < 0) mask |= 1 << q; }
        if (mask == 0 || mask == 255) continue;
        V acc = { 0, 0, 0 }; int cnt = 0;
        for (const auto &e : E) {
            float a = c[e[0]], b = c[e[1]];
            if ((a < 0) == (b < 0)) continue;
            float t = a / (a - b);
            V pa = { (float)(e[0] & 1), (float)((e[0] >> 1) & 1), (float)((e[0] >> 2) & 1) };
            V pb = { (float)(e[1] & 1), (float)((e[1] >> 1) & 1), (float)((e[1] >> 2) & 1) };
            acc = acc + (pa + (pb - pa) * t); cnt++;
        }
        acc = acc * (1.0f / cnt);
        cellVert[cell(i, j, k)] = (int)s.p.size();
        s.p.push_back(pos(i + acc.x, j + acc.y, k + acc.z));
    }
    for (int k = 1; k < nz; k++) for (int j = 1; j < ny; j++) for (int i = 1; i < nx; i++) {
        bool in = d[at(i, j, k)] < 0;
        for (int axis = 0; axis < 3; axis++) {
            int i2 = i + (axis == 0), j2 = j + (axis == 1), k2 = k + (axis == 2);
            if (i2 > nx || j2 > ny || k2 > nz) continue;
            if ((d[at(i2, j2, k2)] < 0) == in) continue;
            int q[4];
            if (axis == 0) { q[0] = cellVert[cell(i, j - 1, k - 1)]; q[1] = cellVert[cell(i, j, k - 1)]; q[2] = cellVert[cell(i, j, k)]; q[3] = cellVert[cell(i, j - 1, k)]; }
            else if (axis == 1) { q[0] = cellVert[cell(i - 1, j, k - 1)]; q[1] = cellVert[cell(i - 1, j, k)]; q[2] = cellVert[cell(i, j, k)]; q[3] = cellVert[cell(i, j, k - 1)]; }
            else { q[0] = cellVert[cell(i - 1, j - 1, k)]; q[1] = cellVert[cell(i, j - 1, k)]; q[2] = cellVert[cell(i, j, k)]; q[3] = cellVert[cell(i - 1, j, k)]; }
            if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) continue;
            if (!in) std::swap(q[1], q[3]);
            s.tri.insert(s.tri.end(), { (uint32_t)q[0], (uint32_t)q[1], (uint32_t)q[2], (uint32_t)q[0], (uint32_t)q[2], (uint32_t)q[3] });
        }
    }
    return s;
}

// What nobody can see: skin pressed inside the grip (the palm and the finger
// pads overlap it; the gun is solid there), and the forearm up the sleeve.
bool hidden(V c) {
    if (alongArm(c) > SLEEVE_AT + 0.015f) return true;
    if (fabsf(c.x) > 0.0122f) return false;
    if (c.y < -0.036f && c.y > -0.099f) return c.z > backStrap(c.y) + 0.0012f && c.z < frontStrap(c.y) - 0.0012f;
    if (c.y >= -0.036f && c.y < 0.026f) return c.z > backStrap(c.y) + 0.0012f && c.z < -0.046f;
    return false;
}

// Colour of the skin at a point, as linear-ish sRGB albedo: one pale base,
// blotched, redder over knuckles and in folds, pinker at the tips; and the
// nails, bed, half-moon and free edge.
V skinColour(const HandField &f, V p, V g, float &nailW) {
    V col = SKIN;
    // blotching, a couple of centimetres across, and a finer freckle-scale one
    float m = vnoise2((p.x * 0.7f + p.z) * 55.0f, p.y * 55.0f + p.x * 20.0f, 81u);
    float m2 = vnoise2((p.z - p.x * 0.4f) * 240.0f, p.y * 240.0f, 82u);
    col = mul(col, V{ 1.0f, 1.0f - 0.07f * m, 1.0f - 0.07f * m });
    col = col * (0.975f + 0.05f * m2);
    // folds: redder and darker where the field is concave
    float cav = clampf((f(p + g * 0.004f) - 0.004f) * -400.0f, 0.0f, 1.0f);
    col = mul(col, mix(V{ 1, 1, 1 }, V{ 0.90f, 0.78f, 0.76f }, cav * 0.6f));
    // knuckles
    for (const Joint &j : f.joints) {
        V o = p - j.c;
        float d2 = dot(o, o) / (j.r * j.r * 0.6f);
        if (d2 > 6) continue;
        float back = clampf(dot(perpTo(o, j.ax), j.dorsal) * 1.4f, 0.0f, 1.0f);
        float red = expf(-d2) * back * (j.idx == 0 ? 0.55f : 0.8f);
        col = mul(col, mix(V{ 1, 1, 1 }, V{ 0.97f, 0.83f, 0.82f }, red));
    }
    // fingertips, and the nails
    nailW = 0;
    for (const Bone &b : f.bones) {
        if (b.idx != 2) continue;
        V o = p - b.b;
        float tip = expf(-dot(o, o) / (b.rb * b.rb * 1.3f));
        col = mul(col, mix(V{ 1, 1, 1 }, V{ 0.98f, 0.84f, 0.83f }, tip * 0.7f));
        if (b.nail < 0) continue;
        const Prim &nl = f.prims[b.nail];
        V q = p - nl.a;
        float u = dot(q, nl.ez) / nl.rad.z;                   // -1 at the base .. +1 at the free edge
        float w = dot(q, nl.ey) / nl.rad.y;
        float dn = primDist(nl, p);
        float onPlate = (1.0f - sstep01((dn + 0.00005f) / 0.0005f)) * sstep01((dot(g, nl.ex) - 0.15f) / 0.3f);
        if (onPlate > nailW) {
            nailW = onPlate;
            V nail = { 0.90f, 0.71f, 0.69f };                                 // pink over the bed
            nail = mix(nail, V{ 0.93f, 0.82f, 0.79f }, sstep01((-u - 0.45f) / 0.25f));   // the half-moon
            nail = mix(nail, V{ 0.95f, 0.90f, 0.84f }, sstep01((u - 0.70f) / 0.12f));    // free edge
            nail = nail * (1.0f - 0.10f * sstep01((fabsf(w) - 0.75f) / 0.25f));
            col = mix(col, nail, onPlate);
        }
        // the cuticle fold, just off the plate at its base
        if (dn > 0 && dn < 0.0012f && u < -0.55f && dot(g, nl.ex) > 0.2f)
            col = mul(col, mix(V{ 1, 1, 1 }, V{ 0.93f, 0.80f, 0.78f }, 1.0f - dn / 0.0012f));
    }
    return col;
}

// ------------------------------------------------------------- mesh assembly

struct Out { std::vector<float> v, n, uv; std::vector<unsigned char> c; std::vector<uint32_t> idx; };

Mesh bakeOne(const Out &o, size_t v0, size_t vcount, const std::vector<unsigned short> &idx) {
    Mesh m = {};
    m.vertexCount = (int)vcount;
    m.triangleCount = (int)(idx.size() / 3);
    m.vertices = (float *)MemAlloc((unsigned)(vcount * 3 * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned)(vcount * 3 * sizeof(float)));
    m.texcoords = (float *)MemAlloc((unsigned)(vcount * 2 * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned)(vcount * 4));
    m.indices = (unsigned short *)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
    memcpy(m.vertices, o.v.data() + v0 * 3, vcount * 3 * sizeof(float));
    memcpy(m.normals, o.n.data() + v0 * 3, vcount * 3 * sizeof(float));
    memcpy(m.texcoords, o.uv.data() + v0 * 2, vcount * 2 * sizeof(float));
    memcpy(m.colors, o.c.data() + v0 * 4, vcount * 4);
    memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
    UploadMesh(&m, false);
    return m;
}

// raylib meshes have 16-bit indices: past 65535 vertices they wrap silently.
// Split by triangles into meshes that each stay under the cap.
std::vector<Mesh> bakeSplit(const Out &o) {
    std::vector<Mesh> res;
    const size_t total = o.v.size() / 3;
    std::vector<int> remap(total, -1);
    Out cur; std::vector<uint32_t> used; std::vector<unsigned short> idx;
    auto flush = [&]() {
        if (!idx.empty()) res.push_back(bakeOne(cur, 0, cur.v.size() / 3, idx));
        for (uint32_t u : used) remap[u] = -1;
        used.clear(); idx.clear(); cur = Out{};
    };
    for (size_t t = 0; t + 2 < o.idx.size(); t += 3) {
        if (cur.v.size() / 3 + 3 > 65000) flush();
        for (int c = 0; c < 3; c++) {
            uint32_t v = o.idx[t + c];
            if (remap[v] < 0) {
                remap[v] = (int)(cur.v.size() / 3);
                used.push_back(v);
                cur.v.insert(cur.v.end(), o.v.begin() + v * 3, o.v.begin() + v * 3 + 3);
                cur.n.insert(cur.n.end(), o.n.begin() + v * 3, o.n.begin() + v * 3 + 3);
                cur.uv.insert(cur.uv.end(), o.uv.begin() + v * 2, o.uv.begin() + v * 2 + 2);
                cur.c.insert(cur.c.end(), o.c.begin() + v * 4, o.c.begin() + v * 4 + 4);
            }
            idx.push_back((unsigned short)remap[v]);
        }
    }
    flush();
    return res;
}

struct Groups { Out skin, knuckles, nails; };

// The knuckle patches: a square 2.8 radii across on the back of each joint,
// in the joint's own frame (v along the bone, u across it), into one of four
// cells of the wrinkle atlas — two straight-joint variants (deep lines) and
// two bent-joint ones (taut, faint).
const float PATCH = 2.8f;

void buildSkin(const HandField &f, Groups &g) {
    const V lo = { -0.052f, -0.232f, -0.285f }, hi = { 0.095f, 0.042f, 0.036f };
    Surface s = polygonise(f, lo, hi, 0.0018f);
    const size_t nv = s.p.size();
    std::vector<V> nrm(nv), col(nv);
    std::vector<float> nailW(nv);
    std::vector<float> uvs(nv * 2);
    for (size_t i = 0; i < nv; i++) {
        V p = s.p[i];
        V gr = f.grad(p);
        nrm[i] = gr;
        col[i] = skinColour(f, p, gr, nailW[i]);
        // Tiled skin UV, 3 cm to the tile: wrapped round the grip's axis on
        // the hand (four tiles round, so the wrap is seamless) and round the
        // arm's past the wrist.
        float u, v;
        if (alongArm(p) < 0.0f) {
            float ang = atan2f(p.z + 0.07f, -p.x);
            u = ang * (0.12f / TAU) / 0.03f; v = p.y / 0.03f;
        } else {
            V q = p - WRIST;
            float ang = atan2f(dot(q, armDorsal()), dot(q, armRadial()));
            u = ang * (0.12f / TAU) / 0.03f; v = alongArm(p) / 0.03f;
        }
        uvs[i * 2] = u; uvs[i * 2 + 1] = v;
    }
    // Vertices go to the group's output the first time a triangle of that group
    // uses them; knuckle vertices are keyed by patch too, since each patch has
    // its own UVs.
    std::vector<int> skinMap(nv, -1), nailMap(nv, -1);
    std::unordered_map<uint64_t, int> knMap;
    auto push = [&](Out &o, size_t i, float u, float v) {
        V p = s.p[i], n = nrm[i], c = col[i];
        o.v.insert(o.v.end(), { p.x, p.y, p.z });
        o.n.insert(o.n.end(), { n.x, n.y, n.z });
        o.uv.insert(o.uv.end(), { u, v });
        o.c.insert(o.c.end(), { cl8(255 * c.x), cl8(255 * c.y), cl8(255 * c.z), 255 });
        return (uint32_t)(o.v.size() / 3 - 1);
    };
    for (size_t t = 0; t + 2 < s.tri.size(); t += 3) {
        uint32_t a = s.tri[t], b = s.tri[t + 1], c = s.tri[t + 2];
        V cen = (s.p[a] + s.p[b] + s.p[c]) * (1.0f / 3.0f);
        if (hidden(cen)) continue;
        V tn = norm(cross(s.p[b] - s.p[a], s.p[c] - s.p[a]));
        float nw = (nailW[a] + nailW[b] + nailW[c]) / 3.0f;
        if (nw > 0.5f) {
            for (uint32_t v : { a, b, c }) {
                if (nailMap[v] < 0) nailMap[v] = (int)push(g.nails, v, 0.5f, 0.5f);
                g.nails.idx.push_back((uint32_t)nailMap[v]);
            }
            continue;
        }
        int best = -1; float bestD = 1e9f;
        for (int j = 0; j < (int)f.joints.size(); j++) {
            const Joint &jt = f.joints[j];
            V o = cen - jt.c, lat = norm(cross(jt.ax, jt.dorsal));
            float S = PATCH * jt.r;
            float pa = dot(o, jt.ax), pl = dot(o, lat);
            if (fabsf(pa) > S * 0.46f || fabsf(pl) > S * 0.46f) continue;
            if (dot(tn, jt.dorsal) < 0.25f || dot(o, jt.dorsal) < 0) continue;
            float dd = (pa * pa + pl * pl) / (S * S);
            if (dd < bestD) { bestD = dd; best = j; }
        }
        if (best >= 0) {
            const Joint &jt = f.joints[best];
            V lat = norm(cross(jt.ax, jt.dorsal));
            float S = PATCH * jt.r;
            int cellX = (jt.digit + jt.idx) & 1, cellY = jt.flex < 0.45f ? 0 : 1;
            for (uint32_t v : { a, b, c }) {
                uint64_t key = (uint64_t)v * 64u + (uint64_t)best;
                auto it = knMap.find(key);
                int id;
                if (it == knMap.end()) {
                    V o = s.p[v] - jt.c;
                    float u = clampf(0.5f + dot(o, lat) / S, 0.01f, 0.99f), w = clampf(0.5f + dot(o, jt.ax) / S, 0.01f, 0.99f);
                    id = (int)push(g.knuckles, v, (cellX + u) * 0.5f, (cellY + w) * 0.5f);
                    knMap[key] = id;
                } else id = it->second;
                g.knuckles.idx.push_back((uint32_t)id);
            }
            continue;
        }
        for (uint32_t v : { a, b, c }) {
            if (skinMap[v] < 0) skinMap[v] = (int)push(g.skin, v, uvs[v * 2], uvs[v * 2 + 1]);
            g.skin.idx.push_back((uint32_t)skinMap[v]);
        }
    }
}

// ------------------------------------------------------------- the sleeve

// A black jersey sleeve: a tube from just past the wrist back along the
// forearm and off the bottom-right of the screen, fuller toward the elbow,
// rucked in soft folds the way a sleeve bunches when the arm is pushed
// forward, with a turned hem at the cuff.
Out buildSleeve() {
    const V dir = ARM;
    V side = norm(cross(dir, V{ 0, 1, 0 })), up = cross(side, dir);
    const int R = 40, L = 28;
    const float length = 0.28f;
    Out s;
    auto radius = [&](float t, float a) {
        float r = 0.0310f + 0.018f * t + 0.004f * sinf(t * 3.1f);
        float fold = sinf(a * 3.0f + t * 11.0f) * 0.5f + sinf(a * 5.0f - t * 17.0f + 1.3f) * 0.3f;
        r *= 1.0f + 0.045f * fold * sstep01(t * 4.0f);
        if (t < 0.03f) r *= 1.0f + 0.05f * sinf(t / 0.03f * 3.14159f);   // the hem
        return r;
    };
    for (int l = 0; l <= L; l++) {
        float t = (float)l / L;
        t = t * t * 0.4f + t * 0.6f;                           // rows closer at the cuff, where it shows
        for (int k = 0; k <= R; k++) {
            float a = TAU * k / R;
            V radial = side * cosf(a) + up * sinf(a);
            float r = radius(t, a);
            V p = SLEEVE0 + dir * (t * length) + radial * r;
            float da = 0.02f, dt = 0.01f;
            float rA = radius(t, a + da), rT = radius(std::min(1.0f, t + dt), a);
            V ta = (side * -sinf(a) + up * cosf(a)) * r + radial * ((rA - r) / da);
            V tt = dir * length + radial * ((rT - r) / dt);
            V n = norm(cross(tt, ta));
            if (dot(n, radial) < 0) n = n * -1.0f;
            s.v.insert(s.v.end(), { p.x, p.y, p.z });
            s.n.insert(s.n.end(), { n.x, n.y, n.z });
            s.uv.insert(s.uv.end(), { a * r / 0.04f, t * length / 0.04f });
            unsigned char sh = (unsigned char)(t < 0.012f ? 170 : 255);   // the inside of the hem in shadow
            s.c.insert(s.c.end(), { sh, sh, sh, 255 });
        }
    }
    for (int l = 0; l < L; l++) for (int k = 0; k < R; k++) {
        uint32_t a = (uint32_t)(l * (R + 1) + k), b = a + 1;
        uint32_t c = a + R + 1, d = c + 1;
        s.idx.insert(s.idx.end(), { a, c, b, b, c, d });
    }
    return s;
}

// ------------------------------------------------------------- materials
//
// Authored like the world's surfaces (surfaces.cpp): colour, height in metres
// and gloss on the same pixels, relief turned into slopes. Their detail maps
// are object maps (alpha 128: dielectric, absolute gloss). The skin textures
// are near white: the tone is in the vertices, so the plain-white nails and
// the textured skin agree where they meet.
void finishObjectSurface(int W, int H, float metresW, const std::vector<Color> &col, const std::vector<float> &ht,
                         const std::vector<float> &gl, bool tiled, Texture2D &albedo, Texture2D &detail) {
    Image a = GenImageColor(W, H, BLANK), dm = GenImageColor(W, H, BLANK);
    Color *pa = (Color *)a.data, *pd = (Color *)dm.data;
    float k = W / (2.0f * metresW);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int i = y * W + x;
        pa[i] = col[i];
        int xl = (x + W - 1) % W, xr = (x + 1) % W, yu = (y + H - 1) % H, yd = (y + 1) % H;
        float sx = (ht[y * W + xr] - ht[y * W + xl]) * k, sy = (ht[yd * W + x] - ht[yu * W + x]) * k;
        pd[i] = { cl8(128 + 127 * clampf(sx, -1, 1)), cl8(128 + 127 * clampf(sy, -1, 1)), cl8(255 * clampf(gl[i], 0, 1)), 128 };
    }
    albedo = finishTexture(a, tiled);
    detail = finishTexture(dm, tiled);
}

// Skin's microrelief at a point, in tiles of 3 cm (periodic in 1): the
// criss-cross of fine lines every half millimetre at ±35 degrees, and pores.
void skinMicro(float u, float v, float &alb, float &h, float &gl) {
    float warp = 0.30f * sinf(TAU * (u * 3 + v * 2)) + 0.25f * sinf(TAU * (-u * 2 + v * 5));
    float g1 = 0.5f + 0.5f * cosf(TAU * (u * 48 + v * 34 + warp));
    float g2 = 0.5f + 0.5f * cosf(TAU * (u * 48 - v * 34 - warp * 0.8f));
    float lines = powf(g1, 14.0f) * (0.5f + 0.5f * sinf(TAU * (u * 5 + v * 7))) + powf(g2, 14.0f) * 0.8f;
    int px = ((int)floorf(u * 256) % 256 + 256) % 256, py = ((int)floorf(v * 256) % 256 + 256) % 256;
    float pore = lat(px, py, 0x91u) > 0.94f ? 1.0f : 0.0f;
    alb = 0.97f - 0.035f * lines - 0.03f * pore;
    h = -0.000012f * lines - 0.000008f * pore;
    gl = 0.30f - 0.08f * lines - 0.1f * pore;
}

void makeSkin(Texture2D &albedo, Texture2D &detail) {
    const int N = 256;
    std::vector<Color> col(N * N);
    std::vector<float> ht(N * N), gl(N * N);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float a, h, g;
        skinMicro((x + 0.5f) / N, (y + 0.5f) / N, a, h, g);
        // a few faint freckles, on a periodic lattice so the tile still wraps
        int cx = x / 16, cy = y / 16;
        float fx = cx * 16 + 16 * lat(cx, cy, 0x92u), fy = cy * 16 + 16 * lat(cx, cy, 0x93u);
        float fr = lat(cx, cy, 0x94u) > 0.8f ? expf(-((x - fx) * (x - fx) + (y - fy) * (y - fy)) / 5.0f) : 0.0f;
        int i = y * N + x;
        col[i] = { cl8(255 * a * (1 - 0.05f * fr)), cl8(255 * a * (1 - 0.10f * fr)), cl8(255 * a * (1 - 0.13f * fr)), 255 };
        ht[i] = h; gl[i] = g;
    }
    finishObjectSurface(N, N, 0.03f, col, ht, gl, true, albedo, detail);
}

// The wrinkles over a knuckle: four 256 px cells, each one patch (2.8 joint
// radii, ~2 cm). Lines across the finger, bowed round the joint, broken and
// uneven, deepest in the middle and fading out before the cell's edge so the
// patch meets the tiled skin with nothing to show where. The top row is for
// straight joints (the thumb's, lying along the frame), deep and many, as in
// the reference photograph; the bottom row for bent ones, where the skin is
// pulled taut and the lines go faint.
void makeKnuckles(Texture2D &albedo, Texture2D &detail) {
    const int N = 256, W = 2 * N;
    const float S = 0.022f;
    std::vector<Color> col(W * W);
    std::vector<float> ht(W * W), gl(W * W);
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) {
        int cxl = x / N, cyl = y / N;
        float u = ((x % N) + 0.5f) / N, v = ((y % N) + 0.5f) / N;
        float a, h, g;
        skinMicro(u * S / 0.03f + cxl * 0.37f, v * S / 0.03f + cyl * 0.61f, a, h, g);
        bool straight = cyl == 0;
        // bowed lines: concentric ellipses round a point beyond the joint
        float du = (u - 0.5f) / 1.7f, dv = v - (0.5f + 0.55f);
        float rr = sqrtf(du * du + dv * dv);
        float spacing = straight ? 0.042f : 0.060f;
        float ph = (rr - 0.55f) / spacing + 0.35f * vnoise2(u * 9 + cxl * 3.1f, v * 9, 0x95u);
        float line = expf(-powf((ph - floorf(ph + 0.5f)) * spacing / 0.0055f, 2.0f));
        float along = vnoise2(atan2f(du, dv) * 14.0f + floorf(ph + 0.5f) * 5.3f + cxl * 7.0f, floorf(ph + 0.5f) * 1.7f, 0x96u);
        line *= sstep01((along - 0.25f) / 0.3f);                   // broken, not ruled
        float fade = 1.0f - sstep01((sqrtf((u - 0.5f) * (u - 0.5f) / 0.9f + (v - 0.5f) * (v - 0.5f) * 1.3f) - 0.16f) / 0.22f);
        float wr = line * fade * (straight ? 1.0f : 0.45f);
        int i = y * W + x;
        float alb = a * (1.0f - 0.20f * wr);
        col[i] = { cl8(255 * alb), cl8(255 * alb * 0.985f), cl8(255 * alb * 0.98f), 255 };
        ht[i] = h - 0.000045f * wr;
        gl[i] = g * (1.0f - 0.4f * wr);
    }
    finishObjectSurface(W, W, S * 2, col, ht, gl, true, albedo, detail);
}

void makeFlat(float gloss, Texture2D &albedo, Texture2D &detail) {
    const int N = 4;
    std::vector<Color> col(N * N, WHITE);
    std::vector<float> ht(N * N, 0.0f), gl(N * N, gloss);
    finishObjectSurface(N, N, 0.01f, col, ht, gl, true, albedo, detail);
}

// Black cotton jersey: columns of V-shaped stitches a millimetre apart, the
// yarn not quite black, with a little lint on it.
void makeKnit(Texture2D &albedo, Texture2D &detail) {
    const int N = 256;
    const float metres = 0.04f;
    const int SW = 7, SH = 5;
    std::vector<Color> col(N * N);
    std::vector<float> ht(N * N), gl(N * N);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int sx = x % SW, sy = y % SH, cxl = x / SW, cyl = y / SH;
        float u = (sx + 0.5f) / SW - 0.5f, w = (sy + 0.5f) / SH;
        float leg = fabsf(fabsf(u) - (0.28f - 0.20f * w));
        float loop = clampf(1.0f - leg * 5.0f, 0.0f, 1.0f) * (0.75f + 0.25f * sinf(w * 3.14159f));
        float yarn = lat(cxl * 3 + (sx > SW / 2), cyl, 0x71u);
        float lint = lat(x, y, 0x72u) > 0.985f ? 1.0f : 0.0f;
        float v = (0.55f + 0.45f * loop) * (0.85f + 0.3f * yarn) + 0.8f * lint;
        col[y * N + x] = { cl8(30 * v), cl8(30 * v), cl8(33 * v), 255 };
        ht[y * N + x] = 0.00015f * loop;
        gl[y * N + x] = 0.06f;
    }
    finishObjectSurface(N, N, metres, col, ht, gl, true, albedo, detail);
}

Mesh bakeSingle(const Out &o) {
    std::vector<Mesh> m = bakeSplit(o);
    for (size_t i = 1; i < m.size(); i++) UnloadMesh(m[i]);
    return m.empty() ? Mesh{} : m[0];
}

}   // namespace

HeldHand buildHeldHand() {
    HeldHand h;
    HandField f = buildField();
    Groups g;
    buildSkin(f, g);
    h.skin = bakeSplit(g.skin);
    h.knuckles = bakeSplit(g.knuckles);
    h.nails = bakeSplit(g.nails);
    h.sleeve = bakeSingle(buildSleeve());
    makeSkin(h.skinTex, h.skinDetail);
    makeKnuckles(h.knuckleTex, h.knuckleDetail);
    makeFlat(0.55f, h.white, h.nailDetail);
    makeKnit(h.knit, h.knitDetail);
    return h;
}

void unloadHeldHand(HeldHand &h) {
    for (Mesh &m : h.skin) UnloadMesh(m);
    for (Mesh &m : h.knuckles) UnloadMesh(m);
    for (Mesh &m : h.nails) UnloadMesh(m);
    UnloadMesh(h.sleeve);
    for (Texture2D t : { h.skinTex, h.skinDetail, h.knuckleTex, h.knuckleDetail, h.white, h.nailDetail,
                         h.knit, h.knitDetail }) UnloadTexture(t);
    h = HeldHand{};
}
