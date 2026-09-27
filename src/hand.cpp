// The gloved hand on the revolver, and the sleeve behind it.
//
// The gun floated. Every first-person game that reads as real puts a hand on
// the weapon, and nothing reachable from here ships one we could use, so it is
// built: a signed-distance field of the hand's bones and pads, blended with a
// smooth minimum so the joints merge like flesh instead of like sausages, then
// polygonised once with surface nets and shaded with the field's own gradient.
// It wears a brown leather work glove because leather is forgiving of a hand
// built from capsules in a way bare skin is not; the sleeve is a knitted cuff.
//
// Everything is placed against the revolver's measured geometry, in the GLB's
// model space (metres; +z to the muzzle, +y up, +x the gun's right). The grip
// is 27 mm across (x ±0.0135) and rakes back: its back strap runs along
// z = -0.082 + 0.31y and its front strap along z = -0.0445 + 0.14(y + 0.04)
// below the trigger guard, whose underside is at y ≈ -0.035. The trigger is a
// 7 mm blade at z 0..0.028; the cylinder starts at z 0.006 and is ±0.025
// across; the hammer spur is at y 0.03-0.06, z -0.045..-0.004. A right hand
// on that: the web of the hand high on the back strap under the hammer, the
// thumb along the frame's left side under the cylinder, the index finger
// through the guard onto the trigger's face, the other three wrapping the front
// strap with their tips on the left flat — which is the side the camera sees.
#include "hand.h"
#include "textures.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

struct V { float x, y, z; };
inline V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator*(V a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float len(V a) { return sqrtf(dot(a, a)); }
inline V norm(V a) { float l = len(a); return l > 1e-9f ? a * (1.0f / l) : V{ 0, 1, 0 }; }
inline V cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

// One part of the hand: a tapered capsule (a bone with its flesh) or an
// ellipsoid (a pad of muscle), and how softly it blends into the rest.
struct Prim {
    bool ell;
    V a, b;          // capsule ends, or ellipsoid centre (a) and radii (b)
    float ra, rb;    // capsule radii at a and b
    float k;         // smooth-union width, metres
    V bc; float br;  // bounding sphere, for skipping it far away
};

float primDist(const Prim &p, V q) {
    if (p.ell) {
        V d = q - p.a;
        V s = { d.x / p.b.x, d.y / p.b.y, d.z / p.b.z };
        V s2 = { d.x / (p.b.x * p.b.x), d.y / (p.b.y * p.b.y), d.z / (p.b.z * p.b.z) };
        float k0 = len(s), k1 = len(s2);
        return k1 > 1e-9f ? k0 * (k0 - 1.0f) / k1 : -std::min(p.b.x, std::min(p.b.y, p.b.z));
    }
    V ba = p.b - p.a, pa = q - p.a;
    float t = clampf(dot(pa, ba) / std::max(dot(ba, ba), 1e-12f), 0.0f, 1.0f);
    return len(pa - ba * t) - (p.ra + (p.rb - p.ra) * t);
}

inline float sstep01(float t) { t = clampf(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

inline float smin(float a, float b, float k) {
    float h = std::max(k - fabsf(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

// A finger bone, kept beside the field so the glove's seams and creases can be
// laid along it: its ends, which bone of the finger it is (0 nearest the palm),
// and which way the back of the finger faces.
struct Bone { V a, b, dorsal; float r; int idx; };

struct HandField {
    std::vector<Prim> prims;
    std::vector<Bone> bones;
    void cap(V a, V b, float ra, float rb, float k) {
        Prim p{ false, a, b, ra, rb, k, (a + b) * 0.5f, 0 };
        p.br = len(b - a) * 0.5f + std::max(ra, rb);
        prims.push_back(p);
    }
    void ell(V c, V r, float k) {
        prims.push_back({ true, c, r, 0, 0, k, c, std::max(r.x, std::max(r.y, r.z)) });
    }
    // A finger: four joints, the bones between them tapering, and a knuckle
    // standing a little proud of each joint on the back of the finger. The back
    // is worked out per bone as "away from what the finger is holding":
    // `anchor` gives the point on the grip (or frame) each bone wraps.
    template <class A> void finger(const V j[4], const float r[4], A anchor) {
        for (int i = 0; i < 3; i++) {
            cap(j[i], j[i + 1], r[i], r[i + 1], i == 0 ? 0.009f : 0.003f);
            V mid = (j[i] + j[i + 1]) * 0.5f, ax = norm(j[i + 1] - j[i]);
            V w = mid - anchor(mid);
            w = w - ax * dot(w, ax);
            V dorsal = norm(w);
            bones.push_back({ j[i], j[i + 1], dorsal, (r[i] + r[i + 1]) * 0.5f, i });
            cap(j[i] + dorsal * (r[i] * 0.22f), j[i] + dorsal * (r[i] * 0.22f), r[i] * 0.98f, r[i] * 0.98f, 0.003f);
        }
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
};

// The grip's front strap below the guard, where the three lower fingers wrap.
float frontStrap(float y) { return -0.0445f + (y + 0.04f) * 0.14f; }

HandField buildField() {
    HandField f;
    // Middle, ring and little fingers wrap the front strap: the proximal bone
    // along the grip's right side, the middle one across the front, the tip
    // back along the left flat. Heights: the middle finger tucked under the
    // trigger guard, the others a finger's width apart below it.
    struct F { float y, r, s; };
    const F fingers[3] = { { -0.0465f, 0.0094f, 1.00f }, { -0.0655f, 0.0090f, 0.96f }, { -0.0835f, 0.0079f, 0.82f } };
    for (const F &g : fingers) {
        float zf = frontStrap(g.y), s = g.s, dy = 0.0015f;
        V j[4] = {
            { 0.0275f, g.y + dy, zf - 0.028f * s },     // knuckle
            { 0.0135f, g.y, zf + 0.0062f },             // round the front-right corner
            { -0.0100f, g.y - dy * 0.5f, zf + 0.0035f },  // across the front
            { -0.0205f, g.y - dy, zf - 0.013f * s },    // tip on the left flat
        };
        float r[4] = { g.r * 1.02f, g.r * 0.97f, g.r * 0.9f, g.r * 0.82f };
        f.finger(j, r, [](V m) { return V{ 0.0f, m.y, (-0.082f + 0.31f * m.y + frontStrap(m.y)) * 0.5f }; });
    }
    // Index: through the guard, its pad on the trigger's face, the tip just
    // showing on the gun's left.
    {
        V j[4] = { { 0.028f, -0.0135f, -0.058f }, { 0.0145f, -0.0170f, -0.0165f },
                   { 0.0040f, -0.0200f, 0.0095f }, { -0.0105f, -0.0215f, 0.0165f } };
        float r[4] = { 0.0094f, 0.0087f, 0.0079f, 0.0072f };
        f.finger(j, r, [](V m) { return V{ 0.0f, m.y - 0.004f, m.z }; });
    }
    // Thumb, from its root in the heel of the hand along the frame's left side,
    // tip under the front of the recoil shield.
    {
        V j[4] = { { -0.0235f, -0.030f, -0.092f }, { -0.0290f, 0.0010f, -0.0730f },
                   { -0.0265f, 0.0150f, -0.0480f }, { -0.0245f, 0.0195f, -0.0265f } };
        float r[4] = { 0.0115f, 0.0106f, 0.0098f, 0.0088f };
        f.finger(j, r, [](V m) { return V{ 0.0f, m.y - 0.006f, m.z }; });
    }
    // The palm: the metacarpals across the grip's right side with the knuckle
    // ridge along their ends, the pad behind the back strap, the thenar under
    // the thumb, the heel of the hand, and the web between thumb and index
    // riding high on the back strap under the hammer.
    f.ell({ 0.0245f, -0.056f, -0.090f }, { 0.0135f, 0.043f, 0.029f }, 0.010f);
    f.cap({ 0.0280f, -0.0135f, -0.0600f }, { 0.0262f, -0.0835f, -0.0735f }, 0.0112f, 0.0098f, 0.010f);
    f.cap({ 0.0005f, 0.0020f, -0.0915f }, { 0.0060f, -0.0900f, -0.1225f }, 0.0105f, 0.0125f, 0.010f);
    f.ell({ -0.0210f, -0.0290f, -0.0910f }, { 0.0110f, 0.0280f, 0.0180f }, 0.010f);
    f.ell({ 0.0150f, -0.0900f, -0.1180f }, { 0.0170f, 0.0220f, 0.0180f }, 0.010f);
    // (the web between thumb and index is a fold, not a pad: thin)
    f.cap({ 0.0280f, -0.0135f, -0.0600f }, { 0.0150f, 0.0150f, -0.0800f }, 0.0105f, 0.0078f, 0.008f);
    f.cap({ 0.0150f, 0.0150f, -0.0800f }, { -0.0020f, 0.0240f, -0.0830f }, 0.0078f, 0.0072f, 0.008f);
    f.cap({ -0.0020f, 0.0240f, -0.0830f }, { -0.0290f, 0.0010f, -0.0730f }, 0.0072f, 0.0100f, 0.008f);
    // The wrist, and the glove's cuff flaring a little where the sleeve covers it.
    f.cap({ 0.0120f, -0.0920f, -0.1280f }, { 0.0300f, -0.1300f, -0.1700f }, 0.0235f, 0.0225f, 0.012f);
    f.cap({ 0.0300f, -0.1300f, -0.1700f }, { 0.0390f, -0.1490f, -0.1920f }, 0.0240f, 0.0255f, 0.006f);
    return f;
}

// Surface nets: one vertex per grid cell the surface passes through, at the
// average of where it crosses that cell's edges, and one quad per grid edge
// the surface crosses. No tables, a watertight mesh, and with normals from the
// field's gradient it shades as smoothly as the field is.
struct Soup { std::vector<float> v, n, uv; std::vector<unsigned char> c; std::vector<unsigned short> idx; };

Mesh bakeSoup(const Soup &s) {
    Mesh m = {};
    m.vertexCount = (int)(s.v.size() / 3);
    m.triangleCount = (int)(s.idx.size() / 3);
    m.vertices = (float *)MemAlloc((unsigned)(s.v.size() * sizeof(float)));
    m.normals = (float *)MemAlloc((unsigned)(s.n.size() * sizeof(float)));
    m.texcoords = (float *)MemAlloc((unsigned)(s.uv.size() * sizeof(float)));
    m.colors = (unsigned char *)MemAlloc((unsigned)s.c.size());
    m.indices = (unsigned short *)MemAlloc((unsigned)(s.idx.size() * sizeof(unsigned short)));
    memcpy(m.vertices, s.v.data(), s.v.size() * sizeof(float));
    memcpy(m.normals, s.n.data(), s.n.size() * sizeof(float));
    memcpy(m.texcoords, s.uv.data(), s.uv.size() * sizeof(float));
    memcpy(m.colors, s.c.data(), s.c.size());
    memcpy(m.indices, s.idx.data(), s.idx.size() * sizeof(unsigned short));
    UploadMesh(&m, false);
    return m;
}

Mesh buildGlove() {
    const HandField field = buildField();
    const V lo = { -0.052f, -0.176f, -0.214f }, hi = { 0.058f, 0.042f, 0.036f };
    const float h = 0.0020f;                            // 2 mm: ~28 samples round a finger
    const int nx = (int)ceilf((hi.x - lo.x) / h), ny = (int)ceilf((hi.y - lo.y) / h), nz = (int)ceilf((hi.z - lo.z) / h);
    auto at = [&](int i, int j, int k) { return ((size_t)k * (ny + 1) + j) * (nx + 1) + i; };
    auto pos = [&](float i, float j, float k) { return V{ lo.x + i * h, lo.y + j * h, lo.z + k * h }; };
    std::vector<float> d((size_t)(nx + 1) * (ny + 1) * (nz + 1));
    for (int k = 0; k <= nz; k++) for (int j = 0; j <= ny; j++) for (int i = 0; i <= nx; i++)
        d[at(i, j, k)] = field(pos((float)i, (float)j, (float)k));
    // the glove stops at its cuff: the sleeve covers the rest
    const V cuffC = { 0.036f, -0.143f, -0.185f }, cuffD = norm(V{ 0.30f, -0.63f, -0.72f });
    Soup s;
    std::vector<int> cellVert((size_t)nx * ny * nz, -1);
    auto cell = [&](int i, int j, int k) { return ((size_t)k * ny + j) * nx + i; };
    for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) {
        float c[8]; int mask = 0;
        for (int q = 0; q < 8; q++) { c[q] = d[at(i + (q & 1), j + ((q >> 1) & 1), k + ((q >> 2) & 1))]; if (c[q] < 0) mask |= 1 << q; }
        if (mask == 0 || mask == 255) continue;
        V acc = { 0, 0, 0 }; int cnt = 0;
        static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (const auto &e : E) {
            float a = c[e[0]], b = c[e[1]];
            if ((a < 0) == (b < 0)) continue;
            float t = a / (a - b);
            V pa = { (float)(e[0] & 1), (float)((e[0] >> 1) & 1), (float)((e[0] >> 2) & 1) };
            V pb = { (float)(e[1] & 1), (float)((e[1] >> 1) & 1), (float)((e[1] >> 2) & 1) };
            acc = acc + (pa + (pb - pa) * t); cnt++;
        }
        acc = acc * (1.0f / cnt);
        V p = pos(i + acc.x, j + acc.y, k + acc.z);
        if (s.v.size() / 3 >= 65535) continue;
        cellVert[cell(i, j, k)] = (int)(s.v.size() / 3);
        const float e = 0.0004f;
        V g = norm(V{ field(p + V{ e, 0, 0 }) - field(p - V{ e, 0, 0 }), field(p + V{ 0, e, 0 }) - field(p - V{ 0, e, 0 }),
                      field(p + V{ 0, 0, e }) - field(p - V{ 0, 0, e }) });
        s.v.insert(s.v.end(), { p.x, p.y, p.z });
        s.n.insert(s.n.end(), { g.x, g.y, g.z });
        // The grain is wrapped round the grip's axis — the way the fingers and
        // thumb themselves wrap it — 4 cm to a repeat. Picking a projection
        // plane per vertex instead put square seams all over the grain. The
        // one seam this has is where the angle wraps, which is on the palm's
        // far side (+x), out of view.
        float ang = atan2f(p.z + 0.07f, -p.x);
        s.uv.insert(s.uv.end(), { ang * 0.030f / 0.04f, p.y / 0.04f });
        // A shade darker in the creases the field is concave in, where a glove
        // wears and gathers dirt, found by how far the surface sits inside a
        // slightly bigger sampling of the field.
        float cav = clampf((field(p + g * 0.004f) - 0.004f) * -400.0f, 0.0f, 1.0f);
        float shade = 1.0f - 0.28f * cav;
        // What makes it a glove and not a hand: a stitched seam down each side
        // of every finger, and the leather creasing across the back of each
        // knuckle where it bends. Found from the nearest finger bone: where
        // round it this point sits (against the bone's dorsal direction) and
        // how far along it. The creases and the leather's softer wrinkles are
        // relief as well as tone: the normal is tilted by the slope of a
        // height laid out in the bone's own frame (along it, round it), so
        // wrinkles run across the finger the way a bent glove folds. Without
        // it the field shades like modelling clay however good the grain is.
        // Features are kept to 4.5 mm and up: the vertices are 2 mm apart, and
        // the first 3 mm crease aliased into a faint irregular smudge.
        V tilt = { 0, 0, 0 };
        {
            const Bone *best = nullptr; float bd = 1e9f, bt = 0; V br = { 0, 0, 0 };
            for (const Bone &bn : field.bones) {
                V ba = bn.b - bn.a;
                float t = clampf(dot(p - bn.a, ba) / std::max(dot(ba, ba), 1e-12f), 0.0f, 1.0f);
                V off = p - (bn.a + ba * t);
                float dd = len(off) - bn.r;
                if (dd < bd) { bd = dd; best = &bn; bt = t; br = norm(off); }
            }
            if (best && bd < 0.0040f) {
                V ax = norm(best->b - best->a), tg = norm(cross(ax, br));
                float c = dot(br, best->dorsal);
                float blen = len(best->b - best->a), along = bt * blen;
                float ang = atan2f(dot(br, cross(ax, best->dorsal)), c);
                float around = ang * best->r;
                float nearW = 1.0f - sstep01((bd - 0.0015f) / 0.0025f);
                // soft wrinkles, stretched along the finger so they lie across it
                const float WA = 0.00040f, SA = 0.0060f, SR = 0.016f, fe = 0.25f;
                auto wr = [&](float u, float v) { return WA * vnoise2(u / SA, v / SR, 71u + (unsigned)best->idx); };
                float w0 = wr(along, around);
                float dAl = (wr(along + fe * SA, around) - w0) / (fe * SA);
                float dAr = (wr(along, around + fe * SR) - w0) / (fe * SR);
                tilt = tilt + (ax * dAl + tg * dAr) * nearW;
                if (!(best->idx == 0 && bt < 0.25f)) {
                    if (fabsf(c) < 0.22f) shade *= 0.50f + 0.50f * sstep01(fabsf(c) / 0.22f);   // the side seams
                    if (best->idx > 0 && c > 0.20f && along < 0.0100f) {                // creases over the joint
                        const float P = 0.0045f, CA = 0.00032f;
                        float win = sstep01(c / 0.45f) * (1.0f - sstep01((along - 0.0065f) / 0.0035f));
                        float ph = along / P * TAU;
                        shade *= 1.0f - win * 0.30f * (0.5f - 0.5f * cosf(ph));
                        tilt = tilt + ax * (-CA * TAU / P * sinf(ph) * win);
                    }
                }
            }
        }
        {
            // the stretched leather over the back of the hand and the web wrinkles too,
            // isotropically, and more gently than the fingers
            float q = 0.00022f, sc = 0.0075f, fe = 0.0010f;
            auto n3 = [&](V r) { return q * (vnoise2(r.x / sc + r.z / sc * 0.61f, r.y / sc, 73u) +
                                             vnoise2(r.z / sc - r.x / sc * 0.37f, r.y / sc * 1.3f, 74u)); };
            float n0 = n3(p);
            tilt = tilt + V{ (n3(p + V{ fe, 0, 0 }) - n0) / fe, (n3(p + V{ 0, fe, 0 }) - n0) / fe, (n3(p + V{ 0, 0, fe }) - n0) / fe };
        }
        tilt = tilt - g * dot(tilt, g);                        // tangential only
        V gn = norm(g - tilt);
        s.n[s.n.size() - 3] = gn.x; s.n[s.n.size() - 2] = gn.y; s.n[s.n.size() - 1] = gn.z;
        unsigned char sh = (unsigned char)(255 * clampf(shade, 0.0f, 1.0f));
        s.c.insert(s.c.end(), { sh, sh, sh, 255 });
    }
    // One quad for every grid edge the surface crosses, between the four cells
    // that share it, wound so the front face looks out of the hand.
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
            // skip what the sleeve hides: faces past the glove's cuff
            V mid = { s.v[q[0] * 3], s.v[q[0] * 3 + 1], s.v[q[0] * 3 + 2] };
            if (dot(mid - cuffC, cuffD) > 0.012f) continue;
            if (!in) std::swap(q[1], q[3]);
            s.idx.insert(s.idx.end(), { (unsigned short)q[0], (unsigned short)q[1], (unsigned short)q[2],
                                        (unsigned short)q[0], (unsigned short)q[2], (unsigned short)q[3] });
        }
    }
    return bakeSoup(s);
}

// The jacket sleeve: a tube from the glove's cuff back along the forearm and
// off the bottom-right of the screen, fuller toward the elbow, rucked in soft
// folds the way a sleeve bunches when the arm is bent and pushed forward.
Mesh buildSleeve() {
    const V start = { 0.030f, -0.131f, -0.171f };
    const V dir = norm(V{ 0.30f, -0.63f, -0.72f });
    V side = norm(cross(dir, V{ 0, 1, 0 })), up = cross(side, dir);
    const int R = 36, L = 28;
    const float length = 0.28f;
    Soup s;
    auto radius = [&](float t, float a) {
        float r = 0.0335f + 0.016f * t + 0.004f * sinf(t * 3.1f);
        float fold = sinf(a * 3.0f + t * 11.0f) * 0.5f + sinf(a * 5.0f - t * 17.0f + 1.3f) * 0.3f;
        r *= 1.0f + 0.045f * fold * sstep01(t * 4.0f);
        if (t < 0.035f) r *= 1.0f + 0.10f * (1.0f - t / 0.035f);   // the rolled edge of the cuff
        return r;
    };
    for (int l = 0; l <= L; l++) {
        float t = (float)l / L;
        for (int k = 0; k <= R; k++) {
            float a = TAU * k / R;
            V radial = side * cosf(a) + up * sinf(a);
            float r = radius(t, a);
            V p = start + dir * (t * length) + radial * r;
            // normal from the tube's own slope: neighbouring radii round and along
            float da = 0.02f, dt = 0.01f;
            float rA = radius(t, a + da), rT = radius(std::min(1.0f, t + dt), a);
            V ta = (side * -sinf(a) + up * cosf(a)) * r + radial * ((rA - r) / da);
            V tt = dir * length + radial * ((rT - r) / dt);
            V n = norm(cross(tt, ta));
            if (dot(n, radial) < 0) n = n * -1.0f;
            s.v.insert(s.v.end(), { p.x, p.y, p.z });
            s.n.insert(s.n.end(), { n.x, n.y, n.z });
            s.uv.insert(s.uv.end(), { a * 0.04f / 0.05f * 6.0f, t * length / 0.05f });
            // the inside of the cuff is in shadow
            unsigned char sh = (unsigned char)(t < 0.02f ? 150 : 255);
            s.c.insert(s.c.end(), { sh, sh, sh, 255 });
        }
    }
    for (int l = 0; l < L; l++) for (int k = 0; k < R; k++) {
        unsigned short a = (unsigned short)(l * (R + 1) + k), b = (unsigned short)(a + 1);
        unsigned short c = (unsigned short)(a + R + 1), d = (unsigned short)(c + 1);
        s.idx.insert(s.idx.end(), { a, c, b, b, c, d });
    }
    return bakeSoup(s);
}

// ------------------------------------------------------------- materials
//
// Both surfaces are authored like the world's (surfaces.cpp): colour, height
// in metres and gloss on the same pixels, tileable, relief turned into slopes.
// Their detail maps are object maps (alpha 128: dielectric, absolute gloss).
void finishObjectSurface(int N, float metres, const std::vector<Color> &col, const std::vector<float> &ht,
                         const std::vector<float> &gl, Texture2D &albedo, Texture2D &detail) {
    Image a = GenImageColor(N, N, BLANK), dm = GenImageColor(N, N, BLANK);
    Color *pa = (Color *)a.data, *pd = (Color *)dm.data;
    float k = N / (2.0f * metres);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int i = y * N + x;
        pa[i] = col[i];
        int xl = (x + N - 1) % N, xr = (x + 1) % N, yu = (y + N - 1) % N, yd = (y + 1) % N;
        float sx = (ht[y * N + xr] - ht[y * N + xl]) * k, sy = (ht[yd * N + x] - ht[yu * N + x]) * k;
        pd[i] = { cl8(128 + 127 * clampf(sx, -1, 1)), cl8(128 + 127 * clampf(sy, -1, 1)), cl8(255 * clampf(gl[i], 0, 1)), 128 };
    }
    albedo = finishTexture(a, true);
    detail = finishTexture(dm, true);
}

// Glove leather: a pebbled grain — a jittered cell per 0.9 mm, each a low dome
// with a crease round it — in the mid brown of a worn work glove, with the
// sheen leather has on the domes and not in the creases. Near-black leather
// read as a dark blob in the building's light, not a hand; tan read as skin.
void makeLeather(Texture2D &albedo, Texture2D &detail) {
    const int N = 256, CELLS = 44;               // 4 cm to the tile
    const float metres = 0.04f;
    std::vector<Color> col(N * N);
    std::vector<float> ht(N * N), gl(N * N);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float fx = (x + 0.5f) * CELLS / N, fy = (y + 0.5f) * CELLS / N;
        int cx = (int)fx, cy = (int)fy;
        float d1 = 9, d2 = 9;
        for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
            int gx = cx + ox, gy = cy + oy;
            int wx = (gx % CELLS + CELLS) % CELLS, wy = (gy % CELLS + CELLS) % CELLS;
            float px = gx + 0.15f + 0.7f * lat(wx, wy, 0x61u), py = gy + 0.15f + 0.7f * lat(wx, wy, 0x62u);
            float dd = sqrtf((fx - px) * (fx - px) + (fy - py) * (fy - py));
            if (dd < d1) { d2 = d1; d1 = dd; } else if (dd < d2) d2 = dd;
        }
        float edge = d2 - d1;                        // 0 on a crease between two pebbles
        float dome = clampf(edge * 2.2f, 0.0f, 1.0f);
        int i = y * N + x;
        float tone = 0.88f + 0.24f * vnoise2(x * 0.03f, y * 0.03f, 0x63u);
        float v = (0.78f + 0.22f * dome) * tone;
        col[i] = { cl8(88 * v), cl8(57 * v), cl8(36 * v), 255 };
        ht[i] = 0.00006f * dome * dome;
        gl[i] = 0.18f + 0.30f * dome;
    }
    finishObjectSurface(N, metres, col, ht, gl, albedo, detail);
}

// Jacket knit: rows of V-shaped stitches, 2.5 mm a stitch, in a dark olive
// heather — the sleeve of something a wanderer would actually be wearing.
void makeKnit(Texture2D &albedo, Texture2D &detail) {
    const int N = 256;
    const float metres = 0.05f;
    const int SW = 12, SH = 10;                  // stitch cell, px: 2.3 x 2.0 mm
    std::vector<Color> col(N * N);
    std::vector<float> ht(N * N), gl(N * N);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int sx = x % SW, sy = y % SH, cxl = x / SW, cyl = y / SH;
        float u = (sx + 0.5f) / SW - 0.5f, w = (sy + 0.5f) / SH;
        // each stitch is two legs leaning into a V
        float leg = fabsf(fabsf(u) - (0.28f - 0.20f * w));
        float loop = clampf(1.0f - leg * 5.0f, 0.0f, 1.0f);
        loop *= 0.75f + 0.25f * sinf(w * 3.14159f);
        float yarn = lat(cxl * 3 + (sx > SW / 2), cyl, 0x71u);   // heather: each leg its own yarn
        float v = (0.55f + 0.45f * loop) * (0.86f + 0.26f * yarn);
        col[y * N + x] = { cl8(58 * v), cl8(60 * v), cl8(48 * v), 255 };
        ht[y * N + x] = 0.00025f * loop;
        gl[y * N + x] = 0.04f;
    }
    finishObjectSurface(N, metres, col, ht, gl, albedo, detail);
}

}   // namespace

HeldHand buildHeldHand() {
    HeldHand h;
    h.glove = buildGlove();
    h.sleeve = buildSleeve();
    makeLeather(h.leather, h.leatherDetail);
    makeKnit(h.knit, h.knitDetail);
    return h;
}

void unloadHeldHand(HeldHand &h) {
    UnloadMesh(h.glove); UnloadMesh(h.sleeve);
    UnloadTexture(h.leather); UnloadTexture(h.leatherDetail);
    UnloadTexture(h.knit); UnloadTexture(h.knitDetail);
    h = HeldHand{};
}
