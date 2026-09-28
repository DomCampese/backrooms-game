// Contract tests for src/core. Generates a fixed set of chunks and asks the
// world fixed questions, and compares the answers with the text files in
// tests/golden. Another engine running the same core must reproduce them
// exactly. Links src/core alone.
//
//   tools/sandbox-build.sh contract
//   ./contract --check [DIR]     # compare with DIR (default tests/golden)
//   ./contract --write [DIR]     # regenerate; only for a deliberate generator change
//
// Floats print as %.9g, which round-trips a float, so the comparison is exact.
#include "../src/core/world.h"
#include "../src/core/hash.h"
#include "../src/core/level_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr unsigned SEED = 1337;
constexpr int NVISITS = 2;
// The far chunk: off the spawn rules (spawn room, Manila exclusion, no feature
// over storey 0 at the origin).
constexpr int FAR_CX = 40, FAR_CZ = -37;
// Probes are searched for in this square of cells, z-major, then x.
constexpr int PROBE_LO = -16, PROBE_HI = 48;
constexpr float BODY_R = 0.34f;   // the player's radius

struct Golden {
    std::string name, text;
    void put(const char *fmt, ...) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        text += buf;
    }
};

// FNV-1a, 64-bit. Defined byte by byte, so any platform computes the same value.
struct Fnv {
    uint64_t h = 0xCBF29CE484222325ULL;
    void byte(uint8_t b) { h = (h ^ b) * 0x100000001B3ULL; }
    void bytes(const void *p, size_t n) {
        const uint8_t *b = (const uint8_t *)p;
        for (size_t i = 0; i < n; i++) byte(b[i]);
    }
    void i32(int32_t v) { for (int s = 0; s < 32; s += 8) byte((uint8_t)((uint32_t)v >> s)); }
};

void setup(World &w, int level, unsigned visit) {
    w.seed = SEED;
    w.level = level;
    w.visit = visit;
    w.wallH = LEVEL_RULES[level].wallH;
    w.storeyH = LEVEL_RULES[level].storeyH;
}

// ---- chunk contents

enum Enc { ENC_CHAR, ENC_HEX, ENC_DEC };   // one character, two hex digits, three decimal columns

struct GridField {
    const char *name;
    Enc enc;
    const void *(*grid)(const ChunkData &);
};
// Every ChunkData field, in the canonical order. Grids are [x][z].
const GridField GRIDS[] = {
    { "wallN",   ENC_CHAR, [](const ChunkData &d) -> const void * { return d.wallN; } },
    { "wallW",   ENC_CHAR, [](const ChunkData &d) -> const void * { return d.wallW; } },
    { "pillar",  ENC_CHAR, [](const ChunkData &d) -> const void * { return d.pillar; } },
    { "prop",    ENC_CHAR, [](const ChunkData &d) -> const void * { return d.prop; } },
    { "propRot", ENC_CHAR, [](const ChunkData &d) -> const void * { return d.propRot; } },
    { "pool",    ENC_CHAR, [](const ChunkData &d) -> const void * { return d.pool; } },
    { "elev",    ENC_DEC,  [](const ChunkData &d) -> const void * { return d.elev; } },
    { "vflag",   ENC_HEX,  [](const ChunkData &d) -> const void * { return d.vflag; } },
    { "vfeat",   ENC_CHAR, [](const ChunkData &d) -> const void * { return d.vfeat; } },
    { "prot",    ENC_CHAR, [](const ChunkData &d) -> const void * { return d.prot; } },
};

bool isSigned(const GridField &f) { return f.enc == ENC_DEC || !strcmp(f.name, "vfeat"); }
int cellValue(const GridField &f, const ChunkData &d, int x, int z) {
    const uint8_t *g = (const uint8_t *)f.grid(d);
    uint8_t b = g[x * CCELLS + z];
    return isSigned(f) ? (int)(int8_t)b : (int)b;
}

void featBytes(Fnv &h, const VertFeat &f) {
    h.byte(f.kind); h.byte(f.dir);
    h.byte((uint8_t)f.x0); h.byte((uint8_t)f.z0); h.byte((uint8_t)f.wu); h.byte((uint8_t)f.lv);
    h.byte((uint8_t)f.stairU); h.byte((uint8_t)f.wallSide); h.i32(f.lo);
}
uint64_t featsHash(const ChunkData &d) {
    Fnv h;
    h.i32(d.nfeat);
    for (int q = 0; q < d.nfeat; q++) featBytes(h, d.feats[q]);
    return h.h;
}
uint64_t lockHash(const ChunkData &d) {
    Fnv h;
    h.byte((uint8_t)d.lockI); h.byte((uint8_t)d.lockK); h.byte(d.lockWest);
    h.byte((uint8_t)d.keyI); h.byte((uint8_t)d.keyK);
    return h.h;
}
uint64_t gridHash(const GridField &f, const ChunkData &d) {
    Fnv h;
    h.bytes(f.grid(d), CCELLS * CCELLS);
    return h.h;
}

void chunkLabel(Golden &g, int level, unsigned visit, int storey, int cx, int cz) {
    g.put("chunk L%d v%u s%d (%d,%d)", level, visit, storey, cx, cz);
}

// One line: the whole chunk's hash, then each field's (low 32 bits), so a
// failing check names the field.
void chunkHashLine(Golden &g, const ChunkData &d, int level, unsigned visit, int storey, int cx, int cz) {
    Fnv all;
    char fields[512] = "";
    size_t n = 0;
    for (const GridField &f : GRIDS) {
        uint64_t h = gridHash(f, d);
        all.i32((int32_t)h); all.i32((int32_t)(h >> 32));
        n += snprintf(fields + n, sizeof fields - n, " %s=%08x", f.name, (uint32_t)h);
    }
    uint64_t fh = featsHash(d), lh = lockHash(d);
    all.i32((int32_t)fh); all.i32((int32_t)(fh >> 32));
    all.i32((int32_t)lh); all.i32((int32_t)(lh >> 32));
    all.byte(d.manila);
    chunkLabel(g, level, visit, storey, cx, cz);
    g.put(" all=%016llx%s feats=%08x lock=%08x manila=%d\n", (unsigned long long)all.h, fields,
          (uint32_t)fh, (uint32_t)lh, d.manila ? 1 : 0);
}

char encodeChar(int v) {
    if (v == 0) return '.';
    if (v < 0) return '-';
    return v < 10 ? (char)('0' + v) : (char)('a' + v - 10);
}

// Every field in full, one row per z. Legend at the top of the file.
void chunkFull(Golden &g, const ChunkData &d, int level, unsigned visit, int storey, int cx, int cz) {
    chunkLabel(g, level, visit, storey, cx, cz);
    g.put(" full\n");
    g.put("  feats %d\n", d.nfeat);
    for (int q = 0; q < d.nfeat; q++) {
        const VertFeat &f = d.feats[q];
        g.put("  feat%d kind=%d dir=%d x0=%d z0=%d wu=%d lv=%d stairU=%d wallSide=%d lo=%d\n", q, f.kind, f.dir,
              f.x0, f.z0, f.wu, f.lv, f.stairU, f.wallSide, f.lo);
    }
    g.put("  lock i=%d k=%d west=%d key i=%d k=%d\n", d.lockI, d.lockK, d.lockWest, d.keyI, d.keyK);
    g.put("  manila %d\n", d.manila ? 1 : 0);
    for (const GridField &f : GRIDS)
        for (int z = 0; z < CCELLS; z++) {
            std::string row;
            char cell[8];
            for (int x = 0; x < CCELLS; x++) {
                int v = cellValue(f, d, x, z);
                if (f.enc == ENC_CHAR) {
                    if (v < -1 || v > 35) { fprintf(stderr, "%s value %d does not fit one character\n", f.name, v); exit(2); }
                    row += encodeChar(v);
                } else if (f.enc == ENC_HEX) {
                    snprintf(cell, sizeof cell, v ? " %02x" : " ..", v); row += cell;
                } else {
                    snprintf(cell, sizeof cell, "%4d", v); row += cell;
                }
            }
            g.put("  %-7s z%02d %s\n", f.name, z, row.c_str());
        }
}

struct ChunkCase { int level; unsigned visit; int storey, cx, cz; bool full; };

// The chunks round the origin and one far chunk, every level and visit, on
// storey 0; on Level 0 also both storeys of every vertical feature among them,
// and of the first feature of each kind found near the origin.
std::vector<ChunkCase> chunkCases() {
    std::vector<ChunkCase> cs;
    auto add = [&](ChunkCase c) {
        for (ChunkCase &o : cs)
            if (o.level == c.level && o.visit == c.visit && o.storey == c.storey && o.cx == c.cx && o.cz == c.cz) {
                o.full = o.full || c.full;
                return;
            }
        cs.push_back(c);
    };
    const int AROUND[10][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 },
                                { -1, 1 }, { 0, 1 }, { 1, 1 }, { FAR_CX, FAR_CZ } };
    for (int lv = 0; lv < NLEVELS; lv++)
        for (unsigned v = 0; v < NVISITS; v++) {
            World w;
            setup(w, lv, v);
            for (auto &c : AROUND) add({ lv, v, 0, c[0], c[1], c[0] == 0 && c[1] == 0 });
            if (LEVEL_RULES[lv].storeyH <= 0.0f) continue;
            for (auto &c : AROUND) {
                VertFeat fs[2];
                int n = w.featuresFor(c[0], c[1], 0, fs, 2);
                for (int q = 0; q < n; q++) {
                    add({ lv, v, fs[q].lo, c[0], c[1], false });
                    add({ lv, v, fs[q].lo + 1, c[0], c[1], false });
                }
            }
            // One of each kind (an atrium with and without a flight), both storeys,
            // in full for the capture visit.
            bool found[4][2] = {};
            for (int r = 0; r <= 6; r++)
                for (int cz = -r; cz <= r; cz++)
                    for (int cx = -r; cx <= r; cx++) {
                        if (std::max(abs(cx), abs(cz)) != r) continue;
                        VertFeat f;
                        if (!w.pairFeature(cx, cz, 0, f)) continue;
                        int flight = f.stairU >= 0 ? 1 : 0;
                        if (found[f.kind][flight]) continue;
                        found[f.kind][flight] = true;
                        add({ lv, v, 0, cx, cz, v == 1 });
                        add({ lv, v, 1, cx, cz, v == 1 });
                    }
        }
    return cs;
}

void writeChunks(Golden &hashes, Golden &full) {
    hashes.put("# One line per chunk: all= hashes every field; each field's FNV-1a (low 32 bits)\n"
               "# follows. Seed %u. Chunk (cx,cz) holds cells cx*16..cx*16+15.\n", SEED);
    full.put("# Full chunk contents, seed %u. Grids are one row per z (0..15), x left to right.\n"
             "# One character: . is 0, 1-9, a-z for 10-35, - for -1. vflag: two hex digits\n"
             "# (.. is 0). elev: decimetres. wallN/wallW: WallKind; prop: PropKind; propRot:\n"
             "# quarter turns plus 4 against a wall; prot: bit 0 north, bit 1 west.\n", SEED);
    std::vector<ChunkCase> cs = chunkCases();
    // One World per level and visit, so chunks generate in the same state the
    // game's would: nothing else in the world changes a chunk's contents.
    for (int lv = 0; lv < NLEVELS; lv++)
        for (unsigned v = 0; v < NVISITS; v++) {
            World w;
            setup(w, lv, v);
            for (const ChunkCase &c : cs) {
                if (c.level != lv || c.visit != v) continue;
                StoreyScope sc(w, c.storey);
                const ChunkData &d = w.data(c.cx, c.cz);
                chunkHashLine(hashes, d, lv, v, c.storey, c.cx, c.cz);
                if (c.full) chunkFull(full, d, lv, v, c.storey, c.cx, c.cz);
            }
        }
}

// ---- queries

struct Cell { int i, k; };

Cell findCell(World &w, const char *what, const std::function<bool(int, int)> &pred) {
    for (int k = PROBE_LO; k < PROBE_HI; k++)
        for (int i = PROBE_LO; i < PROBE_HI; i++)
            if (pred(i, k)) return { i, k };
    fprintf(stderr, "contract: no %s in the probe window (L%d v%u)\n", what, w.level, w.visit);
    exit(2);
}

void putBoxes(Golden &g, World &w, const char *label, Cell c, bool props = true) {
    AABB boxes[MAX_NEARBY_AABBS];
    int n = w.gatherCellAABBs(c.i, c.k, boxes, MAX_NEARBY_AABBS, 0, props);
    g.put("gatherCellAABBs %s cell (%d,%d) props=%d -> %d\n", label, c.i, c.k, props ? 1 : 0, n);
    for (int q = 0; q < n; q++)
        g.put("  box x %.9g..%.9g z %.9g..%.9g top %.9g seeThrough=%d\n", boxes[q].minx, boxes[q].maxx,
              boxes[q].minz, boxes[q].maxz, boxes[q].top, boxes[q].seeThrough ? 1 : 0);
}
void putCollide(Golden &g, World &w, const char *label, float x, float z, float r, float feetY = 0.0f) {
    float px = x, pz = z;
    w.collideCircle(px, pz, r, feetY);
    g.put("collideCircle %s at (%.9g,%.9g) r %.9g feetY %.9g -> (%.9g,%.9g)\n", label, x, z, r, feetY, px, pz);
}
void putGround(Golden &g, World &w, const char *label, float x, float z, float feetY) {
    g.put("groundAt %s at (%.9g,%.9g) feetY %.9g -> %.9g\n", label, x, z, feetY, w.groundAt(x, z, feetY));
}
void putSight(Golden &g, World &w, const char *label, float ax, float az, float bx, float bz) {
    g.put("lineOfSight %s (%.9g,%.9g) to (%.9g,%.9g) -> %d\n", label, ax, az, bx, bz, w.lineOfSight(ax, az, bx, bz) ? 1 : 0);
}
void putStep(Golden &g, World &w, const char *label, Cell a, Cell b) {
    g.put("canStep %s (%d,%d) to (%d,%d) -> %d\n", label, a.i, a.k, b.i, b.k, w.canStep(a.i, a.k, b.i, b.k) ? 1 : 0);
}
void putWalls(Golden &g, World &w, Cell c) {
    g.put("  walls at (%d,%d): north %d west %d\n", c.i, c.k, w.wallNVal(c.i, c.k), w.wallWVal(c.i, c.k));
}
void putStale(Golden &g, World &w) {
    g.put("  staleChunks %d:", (int)w.staleChunks.size());
    for (const ChunkRef &r : w.staleChunks) g.put(" (s%d %d,%d)", r.storey, r.cx, r.cz);
    g.put("\n");
    w.staleChunks.clear();
}

// Cell (i,k)'s north edge borders (i,k-1); its west edge borders (i-1,k).
Cell across(Cell c, bool west) { return west ? Cell{ c.i - 1, c.k } : Cell{ c.i, c.k - 1 }; }

struct Edge { Cell c; bool west; };
Edge findEdge(World &w, const char *what, const std::function<bool(Cell, bool)> &pred) {
    for (int k = PROBE_LO; k < PROBE_HI; k++)
        for (int i = PROBE_LO; i < PROBE_HI; i++)
            for (int west = 0; west < 2; west++)
                if (pred({ i, k }, west != 0)) return { { i, k }, west != 0 };
    fprintf(stderr, "contract: no %s in the probe window (L%d v%u)\n", what, w.level, w.visit);
    exit(2);
}
uint8_t wallAt(World &w, Cell c, bool west) { return west ? w.wallWVal(c.i, c.k) : w.wallNVal(c.i, c.k); }

// The chunk nearest the origin whose feature rising from storey qs is of this kind.
void findFeature(World &w, int kind, int &cx, int &cz, VertFeat &f) {
    for (int r = 0; r <= 6; r++)
        for (cz = -r; cz <= r; cz++)
            for (cx = -r; cx <= r; cx++)
                if (std::max(abs(cx), abs(cz)) == r && w.pairFeature(cx, cz, w.qs, f) && f.kind == kind) return;
    fprintf(stderr, "contract: no feature of kind %d near the origin (L%d v%u)\n", kind, w.level, w.visit);
    exit(2);
}
// A point on an edge, `along` metres from its low end.
void edgePoint(Cell c, bool west, float along, float &x, float &z) {
    x = c.i * CELL + (west ? 0.0f : along);
    z = c.k * CELL + (west ? along : 0.0f);
}

void queriesDoors(Golden &g, World &w) {
    g.put("\n## doors, L%d v%u\n", w.level, w.visit);
    // A lone doorway on a north edge, with plain floor either side.
    Cell door = findCell(w, "lone north doorway", [&](int i, int k) {
        return w.wallNVal(i, k) == WALL_DOOR && w.wallNVal(i - 1, k) != WALL_DOOR && w.wallNVal(i + 1, k) != WALL_DOOR &&
               !w.vflagAt(i, k) && !w.vflagAt(i, k - 1) && !w.pillarAt(i, k) && !w.propAt(i, k);
    });
    putBoxes(g, w, "door", door);
    float x, z;
    edgePoint(door, false, 1.0f, x, z);
    putCollide(g, w, "door opening", x, z, BODY_R);
    edgePoint(door, false, 0.15f, x, z);
    putCollide(g, w, "door jamb", x, z + 0.05f, BODY_R);
    putStep(g, w, "door", door, across(door, false));
    putSight(g, w, "door", x + 0.85f, z - 1.0f, x + 0.85f, z + 1.0f);

    Cell pair = findCell(w, "adjacent north doorways", [&](int i, int k) {
        return w.wallNVal(i, k) == WALL_DOOR && w.wallNVal(i + 1, k) == WALL_DOOR;
    });
    putBoxes(g, w, "door pair west", pair);
    putBoxes(g, w, "door pair east", { pair.i + 1, pair.k });

    Edge le = findEdge(w, "locked door", [&](Cell c, bool west) { return wallAt(w, c, west) == WALL_LOCKED; });
    Cell lock = le.c;
    bool west = le.west;
    g.put("locked door at cell (%d,%d) %s edge\n", lock.i, lock.k, west ? "west" : "north");
    putBoxes(g, w, "locked", lock);
    edgePoint(lock, west, 1.0f, x, z);
    putCollide(g, w, "locked door centre", x + (west ? 0.05f : 0.0f), z + (west ? 0.0f : 0.05f), BODY_R);
    putStep(g, w, "locked", lock, across(lock, west));
    putSight(g, w, "locked", west ? x - 1.0f : x, west ? z : z - 1.0f, west ? x + 1.0f : x, west ? z : z + 1.0f);
}

void queriesSolids(Golden &g, World &w) {
    g.put("\n## pillar and rail, L%d v%u\n", w.level, w.visit);
    Cell pil = findCell(w, "pillar", [&](int i, int k) { return w.pillarAt(i, k); });
    putBoxes(g, w, "pillar", pil);
    putCollide(g, w, "pillar west face", pil.i * CELL + 0.30f, pil.k * CELL + 1.0f, BODY_R);
    putCollide(g, w, "pillar centre", pil.i * CELL + 0.95f, pil.k * CELL + 1.1f, BODY_R);
    putStep(g, w, "into pillar", { pil.i - 1, pil.k }, pil);

    // A flight's guard (full height) and a balcony rail (a cap RAIL_H above the
    // floor, so feet above it pass over).
    auto onFlight = [&](Cell c, bool west) {
        Cell o = across(c, west);
        return ((w.vflagAt(c.i, c.k) | w.vflagAt(o.i, o.k)) & (VF_STAIR | VF_WALKHOLE)) != 0;
    };
    for (int flight = 1; flight >= 0; flight--) {
        const char *label = flight ? "flight rail" : "balcony rail";
        Edge re = findEdge(w, label, [&](Cell c, bool west) {
            return wallAt(w, c, west) == WALL_RAIL && onFlight(c, west) == (flight != 0);
        });
        Cell o = across(re.c, re.west);
        g.put("%s at cell (%d,%d) %s edge, vflag %d, across vflag %d\n", label, re.c.i, re.c.k,
              re.west ? "west" : "north", w.vflagAt(re.c.i, re.c.k), w.vflagAt(o.i, o.k));
        putBoxes(g, w, label, re.c);
        float x, z;
        edgePoint(re.c, re.west, 1.0f, x, z);
        float bx = x + (re.west ? 0.1f : 0.0f), bz = z + (re.west ? 0.0f : 0.1f);
        putCollide(g, w, "rail, standing", bx, bz, BODY_R);
        putCollide(g, w, "rail, feet over the cap", bx, bz, BODY_R, RAIL_H + 0.05f);
        putStep(g, w, "rail", re.c, o);
        putSight(g, w, "rail", re.west ? x - 1.0f : x, re.west ? z : z - 1.0f,
                 re.west ? x + 1.0f : x, re.west ? z : z + 1.0f);
    }
}

void queriesStoreys(Golden &g, World &w) {
    g.put("\n## storeys, L%d v%u\n", w.level, w.visit);
    Cell hole = findCell(w, "hole with no flight", [&](int i, int k) {
        uint8_t f = w.vflagAt(i, k);
        return (f & VF_HOLE) && !(f & VF_WALKHOLE);
    });
    g.put("hole at cell (%d,%d) vflag %d\n", hole.i, hole.k, w.vflagAt(hole.i, hole.k));
    putGround(g, w, "into a hole", hole.i * CELL + 1.0f, hole.k * CELL + 1.0f, 0.0f);
    Cell walk = findCell(w, "walkable hole", [&](int i, int k) { return (w.vflagAt(i, k) & VF_WALKHOLE) != 0; });
    g.put("walkable hole at cell (%d,%d) vflag %d\n", walk.i, walk.k, w.vflagAt(walk.i, walk.k));
    putGround(g, w, "down a walkable hole", walk.i * CELL + 1.0f, walk.k * CELL + 1.0f, 0.0f);

    // A flight of each kind rising from this storey, sampled up the middle of
    // each lane in the feature's own frame (u across, v along the rise).
    for (int kind : { VK_STAIRWELL, VK_STAIR }) {
        VertFeat f;
        int fcx, fcz;
        findFeature(w, kind, fcx, fcz, f);
        g.put("flight kind %d in chunk (%d,%d): dir %d x0 %d z0 %d wu %d lv %d\n", kind, fcx, fcz, f.dir, f.x0, f.z0, f.wu, f.lv);
        int lanes = kind == VK_STAIRWELL ? 2 : 1;
        for (int lane = 0; lane < lanes; lane++)
            for (int t = 0; t <= 16; t++) {
                float u = lane * CELL + 1.0f, v = 1.0f + t * 0.5f;
                Vec3 p = w.featureWorld(f, fcx, fcz, u, 0.0f, v);
                g.put("stairY u %.9g v %.9g at (%.9g,%.9g) -> %.9g ramp %.9g ground %.9g\n", u, v, p.x, p.z,
                      w.stairY(p.x, p.z), w.stairY(p.x, p.z, true), w.groundAt(p.x, p.z, 5.0f));
            }
    }
    g.put("stairY off a flight (1,1) -> %s\n", std::isnan(w.stairY(1.0f, 1.0f)) ? "nan" : "number");
    // ceilY under an opening and on plain floor.
    Cell open = findCell(w, "open ceiling", [&](int i, int k) { return (w.vflagAt(i, k) & VF_OPENUP) != 0; });
    g.put("ceilY open (%d,%d) -> %.9g   plain (0,0) -> %.9g\n", open.i, open.k, w.ceilY(open.i, open.k), w.ceilY(0, 0));
    for (Cell c : { Cell{ 0, 0 }, hole, walk, open }) {
        Vec2 s = w.findOpenSpot(c.i * CELL + 1.0f, c.k * CELL + 1.0f);
        g.put("findOpenSpot from cell (%d,%d) -> (%.9g,%.9g)\n", c.i, c.k, s.x, s.y);
    }
}

void queriesVending(Golden &g, World &w) {
    g.put("\n## vending, L%d v%u\n", w.level, w.visit);
    for (int against = 1; against >= 0; against--) {
        Cell v = findCell(w, against ? "vending machine against a wall" : "free-standing vending machine", [&](int i, int k) {
            return w.propAt(i, k) == PROP_VENDING && ((w.propRotAt(i, k) & PROP_AGAINST_WALL) != 0) == (against != 0);
        });
        g.put("vending at cell (%d,%d) propRot %d\n", v.i, v.k, w.propRotAt(v.i, v.k));
        putBoxes(g, w, "vending", v);
        putBoxes(g, w, "vending, no props", v, false);
        float cx, cz, x0, z0, x1, z1;
        vendFootprint(w.propRotAt(v.i, v.k), v.i * CELL + 1.0f, v.k * CELL + 1.0f, cx, cz, x0, z0, x1, z1);
        for (int t = 0; t < 4; t++) {
            const float DX[4] = { 0.0f, 0.7f, 0.0f, -0.7f }, DZ[4] = { -0.7f, 0.0f, 0.7f, 0.0f };
            putCollide(g, w, "near vending", cx + DX[t], cz + DZ[t], BODY_R);
        }
        putCollide(g, w, "standing on vending", cx, cz, BODY_R, VEND_Y1);
        putGround(g, w, "on vending top", cx, cz, VEND_Y1 + 0.01f);
    }
}

void queriesSampled(Golden &g, World &w) {
    g.put("\n## sampled, L%d v%u\n", w.level, w.visit);
    Rng rng(0xC0417AC7u ^ (uint64_t)w.level * 131u ^ w.visit);
    for (int t = 0; t < 24; t++) {
        float ax = PROBE_LO * CELL + rng.f01() * (PROBE_HI - PROBE_LO) * CELL;
        float az = PROBE_LO * CELL + rng.f01() * (PROBE_HI - PROBE_LO) * CELL;
        // No libm here: the answers must not depend on the harness's sinf.
        float bx = ax + (rng.f01() * 2.0f - 1.0f) * 16.0f, bz = az + (rng.f01() * 2.0f - 1.0f) * 16.0f;
        putSight(g, w, "sample", ax, az, bx, bz);
    }
    for (int t = 0; t < 24; t++) {
        Cell c = { rng.ri(PROBE_LO, PROBE_HI - 1), rng.ri(PROBE_LO, PROBE_HI - 1) };
        g.put("canStep from (%d,%d) north south west east -> %d%d%d%d\n", c.i, c.k,
              w.canStep(c.i, c.k, c.i, c.k - 1), w.canStep(c.i, c.k, c.i, c.k + 1),
              w.canStep(c.i, c.k, c.i - 1, c.k), w.canStep(c.i, c.k, c.i + 1, c.k));
    }
    for (int t = 0; t < 12; t++) {
        Cell s = { rng.ri(PROBE_LO, PROBE_HI - 1), rng.ri(PROBE_LO, PROBE_HI - 1) };
        Cell e = { s.i + rng.ri(-12, 12), s.k + rng.ri(-12, 12) };
        int oi = 0, ok = 0;
        bool found = w.pathStep(s.i, s.k, e.i, e.k, oi, ok);
        if (found) g.put("pathStep (%d,%d) to (%d,%d) -> (%d,%d)\n", s.i, s.k, e.i, e.k, oi, ok);
        else       g.put("pathStep (%d,%d) to (%d,%d) -> none\n", s.i, s.k, e.i, e.k);
    }
    for (int t = 0; t < 12; t++) {
        float x = PROBE_LO * CELL + rng.f01() * (PROBE_HI - PROBE_LO) * CELL;
        float z = PROBE_LO * CELL + rng.f01() * (PROBE_HI - PROBE_LO) * CELL;
        putGround(g, w, "sample", x, z, 0.5f);
        putCollide(g, w, "sample", x, z, BODY_R);
        Vec2 s = w.findOpenSpot(x, z);
        g.put("findOpenSpot (%.9g,%.9g) -> (%.9g,%.9g)\n", x, z, s.x, s.y);
    }
    for (int t = 0; t < 8; t++) {
        Cell c = { rng.ri(PROBE_LO, PROBE_HI - 1), rng.ri(PROBE_LO, PROBE_HI - 1) };
        g.put("cell (%d,%d) floorY %.9g ceilY %.9g pool %d soft %d valve %d key %d cursed %d manila %d\n", c.i, c.k,
              w.floorY(c.i, c.k), w.ceilY(c.i, c.k), w.poolAt(c.i, c.k), w.softAt(c.i, c.k), w.valveAt(c.i, c.k),
              w.keyAt(c.i, c.k), w.cursedExit(c.i, c.k), w.manilaAt(c.i, c.k));
    }
}

// The grid over a window, with the storeys above and below loaded over the
// same window first: buildOccupancy reads only chunks already loaded there.
void queriesOccupancy(Golden &g, World &w) {
    const int ORIGIN = -24, N = 48;
    if (w.storeyH > 0.0f)
        for (int rel = -1; rel <= 1; rel += 2) {
            StoreyScope sc(w, w.storey + rel);
            for (int cz = fdiv(ORIGIN, CCELLS); cz <= fdiv(ORIGIN + N - 1, CCELLS); cz++)
                for (int cx = fdiv(ORIGIN, CCELLS); cx <= fdiv(ORIGIN + N - 1, CCELLS); cx++) w.data(cx, cz);
        }
    std::vector<unsigned char> out((size_t)N * N * 4 * 2);
    w.buildOccupancy(ORIGIN, ORIGIN, N, out.data());
    Fnv cells, masks;
    cells.bytes(out.data(), (size_t)N * N * 4);
    masks.bytes(out.data() + (size_t)N * N * 4, (size_t)N * N * 4);
    g.put("\n## buildOccupancy L%d v%u origin (%d,%d) n %d\n", w.level, w.visit, ORIGIN, ORIGIN, N);
    g.put("occupancy cells=%016llx masks=%016llx\n",
          (unsigned long long)cells.h, (unsigned long long)masks.h);
    // How many cells set each bit, per byte, so a failure says which bit moved.
    for (int ch = 0; ch < 3; ch++) {
        g.put("  byte%d", ch);
        for (int b = 0; b < 7; b++) {
            int n = 0;
            for (int c = 0; c < N * N; c++) n += (out[c * 4 + ch] >> b) & 1;
            g.put(" bit%d=%d", b, n);
        }
        g.put("\n");
    }
}

void queriesVendFootprint(Golden &g) {
    g.put("\n## vendFootprint at cell centre (1,1)\n");
    for (int flag = 0; flag <= PROP_AGAINST_WALL; flag += PROP_AGAINST_WALL)
        for (int rot = 0; rot < 4; rot++) {
            float px, pz, x0, z0, x1, z1;
            vendFootprint((uint8_t)(rot | flag), 1.0f, 1.0f, px, pz, x0, z0, x1, z1);
            g.put("rot %d against %d -> centre (%.9g,%.9g) box x %.9g..%.9g z %.9g..%.9g\n", rot, flag ? 1 : 0,
                  px, pz, x0, x1, z0, z1);
        }
}

// ---- mutations: the wall overlays and the stale-chunk list.
void mutations(Golden &g, World &w) {
    g.put("## mutations, L%d v%u\n", w.level, w.visit);
    // An open west edge on the chunk seam at x = 0, so the edge is drawn by two chunks.
    Cell seam = findCell(w, "open seam edge", [&](int i, int k) {
        return i == 0 && w.wallWVal(i, k) == WALL_NONE && !w.pillarAt(i, k) && !w.pillarAt(i - 1, k) &&
               !w.propAt(i, k) && !w.propAt(i - 1, k) && !w.vflagAt(i, k) && !w.vflagAt(i - 1, k);
    });
    w.staleChunks.clear();
    g.put("shiftEdge west edge of (%d,%d)\n", seam.i, seam.k);
    float x = seam.i * CELL, z = seam.k * CELL + 1.0f;
    putWalls(g, w, seam);
    putStep(g, w, "before", seam, across(seam, true));
    putSight(g, w, "before", x - 1.0f, z, x + 1.0f, z);
    w.shiftEdge(seam.i, seam.k, true);
    putWalls(g, w, seam);
    putStale(g, w);
    putStep(g, w, "after", seam, across(seam, true));
    putSight(g, w, "after", x - 1.0f, z, x + 1.0f, z);
    putBoxes(g, w, "shifted", seam, false);
    g.put("shiftEdge again\n");
    w.shiftEdge(seam.i, seam.k, true);
    putStale(g, w);
    {
        // The same edge on the storey below is a different edge.
        StoreyScope sc(w, w.storey - 1);
        g.put("storey %d: west edge of (%d,%d) is %d\n", w.qs, seam.i, seam.k, w.wallWVal(seam.i, seam.k));
    }

    Edge le = findEdge(w, "locked door", [&](Cell c, bool west) { return wallAt(w, c, west) == WALL_LOCKED; });
    Cell lock = le.c;
    bool west = le.west;
    int cx = fdiv(lock.i, CCELLS), cz = fdiv(lock.k, CCELLS);
    const ChunkData &d = w.data(cx, cz);
    Cell key = { cx * CCELLS + d.keyI, cz * CCELLS + d.keyK };
    g.put("unlockEdge %s edge of (%d,%d), key at (%d,%d)\n", west ? "west" : "north", lock.i, lock.k, key.i, key.k);
    putWalls(g, w, lock);
    g.put("  keyAt %d\n", w.keyAt(key.i, key.k));
    putStep(g, w, "before", lock, across(lock, west));
    w.unlockEdge(lock.i, lock.k, west);
    putWalls(g, w, lock);
    putStale(g, w);
    g.put("  keyAt %d\n", w.keyAt(key.i, key.k));
    putStep(g, w, "after", lock, across(lock, west));
    putBoxes(g, w, "unlocked", lock, false);
    g.put("unlockEdge again\n");
    w.unlockEdge(lock.i, lock.k, west);
    putStale(g, w);

    // An unloaded chunk reports nothing stale.
    w.rebuildChunk(1000, 1000);
    g.put("rebuildChunk unloaded (1000,1000)\n");
    putStale(g, w);
}

std::vector<Golden> produce() {
    std::vector<Golden> gs(4);
    gs[0].name = "chunks.txt";
    gs[1].name = "chunks_full.txt";
    gs[2].name = "queries.txt";
    gs[3].name = "mutations.txt";
    writeChunks(gs[0], gs[1]);

    Golden &q = gs[2];
    q.put("# Query answers, seed %u, wallH and storeyH from LEVEL_RULES. Coordinates are\n"
          "# world metres, cells are global (i,k). Visit 1 is what a Level 0 capture shows.\n", SEED);
    queriesVendFootprint(q);
    {
        World w; setup(w, 0, 1);
        queriesDoors(q, w); queriesSolids(q, w); queriesStoreys(q, w); queriesOccupancy(q, w);
    }
    {
        World w; setup(w, 1, 0);
        queriesDoors(q, w); queriesVending(q, w); queriesOccupancy(q, w);
    }
    {
        World w; setup(w, 4, 1);
        queriesVending(q, w);
    }
    for (int lv = 0; lv < NLEVELS; lv++) {
        World w; setup(w, lv, 1);
        queriesSampled(q, w);
    }
    {
        World w; setup(w, 0, 1);
        mutations(gs[3], w);
    }
    return gs;
}

// ---- files

std::vector<std::string> lines(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) out.push_back(l);
    return out;
}

std::vector<std::string> tokens(const std::string &s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

// Name what differs: the name=value fields of a hash line, or the field and
// row of a grid row.
std::string whatDiffers(const std::string &want, const std::string &got) {
    std::vector<std::string> a = tokens(want), b = tokens(got);
    std::string names;
    if (a.size() == b.size())
        for (size_t t = 0; t < a.size(); t++) {
            size_t eq = a[t].find('=');
            if (a[t] != b[t] && eq != std::string::npos && eq > 0 && a[t].compare(0, eq, "all") != 0)
                names += " " + a[t].substr(0, eq);
        }
    if (!names.empty()) return "fields" + names;
    if (a.size() >= 2 && a[1].size() == 3 && a[1][0] == 'z') return "field " + a[0] + " row " + a[1];
    return a.empty() ? "line" : a[0];
}

int check(const std::string &dir, const std::vector<Golden> &gs) {
    int bad = 0;
    for (const Golden &g : gs) {
        std::ifstream f(dir + "/" + g.name);
        if (!f) { printf("FAIL %s/%s: missing\n", dir.c_str(), g.name.c_str()); bad++; continue; }
        std::stringstream ss;
        ss << f.rdbuf();
        std::vector<std::string> want = lines(ss.str()), got = lines(g.text);
        int shown = 0, diffs = 0;
        std::string context;
        for (size_t l = 0; l < std::max(want.size(), got.size()); l++) {
            const std::string &w = l < want.size() ? want[l] : std::string();
            const std::string &h = l < got.size() ? got[l] : std::string();
            if (w.rfind("chunk ", 0) == 0 || w.rfind("## ", 0) == 0) context = w.substr(0, w.find(" all="));
            if (w == h) continue;
            diffs++;
            if (shown++ >= 12) continue;
            printf("FAIL %s:%zu [%s] %s\n  want: %s\n  got:  %s\n", g.name.c_str(), l + 1, context.c_str(),
                   whatDiffers(w, h).c_str(), w.c_str(), h.c_str());
        }
        if (diffs) { printf("FAIL %s: %d lines differ\n", g.name.c_str(), diffs); bad++; }
        else printf("ok   %s (%zu lines)\n", g.name.c_str(), got.size());
    }
    return bad;
}

const char *usage = "usage: contract [--check|--write] [DIR]   (default --check tests/golden)\n";

}  // namespace

int main(int argc, char **argv) {
    bool write = false;
    std::string dir = "tests/golden";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--write") write = true;
        else if (a == "--check") write = false;
        else if (a[0] != '-') dir = a;
        else { fputs(usage, stderr); return 2; }
    }
    std::vector<Golden> gs = produce();
    if (write) {
        for (const Golden &g : gs) {
            std::string path = dir + "/" + g.name;
            FILE *f = fopen(path.c_str(), "wb");
            if (!f) { fprintf(stderr, "contract: cannot write %s\n", path.c_str()); return 2; }
            fwrite(g.text.data(), 1, g.text.size(), f);
            fclose(f);
            printf("wrote %s (%zu bytes)\n", path.c_str(), g.text.size());
        }
        return 0;
    }
    int bad = check(dir, gs);
    printf(bad ? "contract: FAILED\n" : "contract: passed\n");
    return bad ? 1 : 0;
}
