#include "visible.h"
#include "../core/level_rules.h"
#include "../sim/sim_math.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float DEG2RAD_F = 3.14159265358979323846f / 180.0f;   // raylib's DEG2RAD

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The opening between storeys lo and lo + 1 (relative to the view's) in a chunk.
struct Portal { float x0, z0, x1, z1, y; bool up; };

} // namespace

std::vector<VisibleChunk> visibleChunks(World &w, const ChunkViewer &v,
                                        const std::function<bool(int, int, int)> &loaded) {
    const int pcx = fdiv(cellOf(v.body.x), CCELLS), pcz = fdiv(cellOf(v.body.z), CCELLS);
    std::vector<VisibleChunk> out;
    Vec3 cameraRight = normalize(cross(v.fwd, v.up));
    Vec3 cameraUp = normalize(cross(cameraRight, v.fwd));
    float tanV = tanf(v.fovY * DEG2RAD_F * 0.5f);
    float tanH = tanV * v.width / v.height;
    // Bounds include the deepest atrium, ceiling and slight wall overlap, and
    // on a storeyed level flights and shaft walls that climb to the next floor.
    float yLo = -2.5f, yHi = fmaxf(LEVEL_RULES[w.level].wallH, w.storeyH) + 1.2f;
    float halfY = (yHi - yLo) * 0.5f;
    float radius = sqrtf(CHUNK * CHUNK * 0.5f + halfY * halfY) + 0.5f;
    // A sphere against the view frustum, conservatively.
    auto sphereVisible = [&](Vec3 c, float r) {
        Vec3 delta{ c.x - v.body.x, c.y - v.body.y, c.z - v.body.z };
        float depth = dot(delta, v.fwd);
        return !(depth < -r ||
                 fabsf(dot(delta, cameraRight)) > depth * tanH + r * sqrtf(1 + tanH * tanH) ||
                 fabsf(dot(delta, cameraUp)) > depth * tanV + r * sqrtf(1 + tanV * tanV));
    };
    auto consider = [&](int storey, int cx, int cz, float yOff) {
        if (!loaded(storey, cx, cz)) return;
        for (const VisibleChunk &c : out)
            if (c.storey == storey && c.cx == cx && c.cz == cz) return;
        Vec3 c{ cx * CHUNK + CHUNK * 0.5f, yLo + halfY + yOff, cz * CHUNK + CHUNK * 0.5f };
        if (!sphereVisible(c, radius)) return;
        Vec3 d{ c.x - v.body.x, c.y - v.body.y, c.z - v.body.z };
        out.push_back({ storey, cx, cz, yOff, dot(d, d) });
    };
    auto nearer = [](const VisibleChunk &a, const VisibleChunk &b) { return a.distance2 < b.distance2; };
    for (int dx = -2; dx <= 2; ++dx) for (int dz = -2; dz <= 2; ++dz) consider(w.storey, pcx + dx, pcz + dz, 0.0f);
    std::sort(out.begin(), out.end(), nearer);

    const Vec3 eye = v.eye;
    auto portalOf = [&](int cx, int cz, int lo, Portal &p) {
        VertFeat f;
        if (!w.pairFeature(cx, cz, w.storey + lo, f)) return false;
        float ax = (float)((cx * CCELLS + f.x0) * CELL), az = (float)((cz * CCELLS + f.z0) * CELL);
        float ex = (f.dir < 2 ? f.wu : f.lv) * CELL, ez = (f.dir < 2 ? f.lv : f.wu) * CELL;
        p = { ax - 0.1f, az - 0.1f, ax + ex + 0.1f, az + ez + 0.1f, (lo + 1) * w.storeyH, lo >= 0 };
        // The prism between the two floors has to be in view at all.
        Vec3 c{ ax + ex * 0.5f, (lo + 0.5f) * w.storeyH, az + ez * 0.5f };
        return sphereVisible(c, sqrtf(ex * ex + ez * ez + w.storeyH * w.storeyH) * 0.5f + 0.5f);
    };
    // Can anything in this chunk's box (at storey offset yOff) be seen through p?
    auto through = [&](const Portal &p, int cx, int cz, float yOff) {
        // An eye at or past the plane (halfway down a flight, below the hole it
        // came through) is inside the opening: everything past it shows.
        if ((p.y > eye.y) != p.up || fabsf(p.y - eye.y) < 0.05f) return true;
        float y0 = yOff + yLo, y1 = yOff + yHi;
        if (p.up) y0 = fmaxf(y0, p.y); else y1 = fminf(y1, p.y);   // only the far side counts
        if (y0 > y1) return false;
        float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
        for (int i = 0; i < 8; i++) {
            float x = cx * CHUNK + ((i & 1) ? CHUNK : 0.0f), z = cz * CHUNK + ((i & 2) ? CHUNK : 0.0f);
            float y = (i & 4) ? y1 : y0;
            float t = (p.y - eye.y) / (y - eye.y);     // in (0, 1]: y is on the far side
            float X = eye.x + (x - eye.x) * t, Z = eye.z + (z - eye.z) * t;
            mnx = fminf(mnx, X); mxx = fmaxf(mxx, X); mnz = fminf(mnz, Z); mxz = fmaxf(mxz, Z);
        }
        return !(mxx < p.x0 || mnx > p.x1 || mxz < p.z0 || mnz > p.z1);
    };
    size_t own = out.size();
    if (w.storeyH > 0.0f)
        for (int rel = -1; rel <= 1; rel += 2)
            for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) {
                Portal p1;
                if (!portalOf(pcx + dx, pcz + dz, rel > 0 ? 0 : -1, p1)) continue;
                for (int ex = -1; ex <= 1; ++ex) for (int ez = -1; ez <= 1; ++ez) {
                    int cx = pcx + dx + ex, cz = pcz + dz + ez;
                    if (!through(p1, cx, cz, rel * w.storeyH)) continue;
                    consider(w.storey + rel, cx, cz, rel * w.storeyH);
                    // Stairs stack: a flight often lands beside the next one,
                    // whose opening shows the storey after. Only that
                    // feature's own chunk is visible, through both openings.
                    Portal chain[STOREY_REACH]; chain[0] = p1;
                    for (int depth = 2; depth <= STOREY_REACH; ++depth) {
                        if (!portalOf(cx, cz, rel > 0 ? depth - 1 : -depth, chain[depth - 1])) break;
                        bool seen = true;
                        for (int j = 0; j < depth; ++j)
                            if (!through(chain[j], cx, cz, depth * rel * w.storeyH)) { seen = false; break; }
                        if (!seen) break;
                        consider(w.storey + depth * rel, cx, cz, depth * rel * w.storeyH);
                    }
                }
            }
    std::sort(out.begin() + own, out.end(), nearer);
    return out;
}
