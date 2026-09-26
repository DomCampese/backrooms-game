// A measuring instrument for the generator. No window, no GL, no Xvfb: it links
// the game's own world.cpp and calls generate() directly, so every number below
// comes from exactly the code the game ships.
//
// This exists because screenshots were actively misleading about the layout.
// From captures alone the halls looked like they ran for hundreds of metres;
// measuring gave a median sightline of 7 m and 83% occlusion at 20 m. The real
// defect was enclosure — more than half the cells had no wall on any side —
// which no screenshot makes obvious. Anyone changing the generator needs to see
// the numbers move, not the pictures.
//
//   tools/sandbox-build.sh mapdump     # builds ./mapdump
//   ./mapdump --level 0 --seed 1337 --cells 129 --plan 0 0 48 32
//
#include "../src/world.h"
#include "../src/util.h"
#include "../src/levels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Game::hideSpotAt and Game::coinAt live on Game, which would drag the whole
// renderer in here. They are pure functions of world state, so they are
// mirrored — if either rule changes in game.cpp, change it here too or this
// harness will quietly report the old world.
bool hideSpotAt(World &w, int a, int b) {
    switch (w.propAt(a, b)) {
    case PROP_BOXES: case PROP_CABINET: case PROP_COUCH: case PROP_ARMOIRE:
    case PROP_NIGHTSTAND: case PROP_BED: case PROP_PARTY_TABLE: case PROP_DESK:
    case PROP_SHELVING:
        return true;
    default:
        return false;
    }
}
bool coinAt(World &w, int a, int b) {
    if (w.pillarAt(a, b) || w.propAt(a, b) || w.poolAt(a, b)) return false;
    return ih(a, b, (uint32_t)w.seed ^ 0xC01Du) % 449 == 0;
}

struct Args {
    int level = 0, cells = 129, samples = 4000, storey = 0;
    unsigned visit = 0;
    unsigned seed = 1337;
    bool plan = true, listExits = false, listStairs = false;
    int px = 0, pz = 0, pw = 48, ph = 32;
};

// A cell's glyph. Props win over pools because a prop standing in water is
// still the thing you walk into.
char cellGlyph(World &w, int a, int b) {
    if (w.pillarAt(a, b)) return '#';
    uint8_t p = w.propAt(a, b);
    if (p) {
        switch (p) {
        case PROP_VENDING:     return 'V';
        case PROP_FALLEN_TILE: return '%';
        case PROP_PLANT:       return '*';
        case PROP_LAMP:        return 'i';
        case PROP_COOLER:      return 'c';
        default:               return hideSpotAt(w, a, b) ? 'H' : 'p';
        }
    }
    if (w.valveAt(a, b)) return 'Y';
    if (w.softAt(a, b))  return '~';
    return ' ';   // coins and water are decided by the caller, which knows both
}

const char *usage =
    "usage: mapdump [--level N] [--seed S] [--storey N] [--cells N] [--samples N]\n"
    "               [--plan X Z W H] [--no-plan] [--list-exits] [--list-stairs]\n";

}  // namespace

int main(int argc, char **argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto num = [&](int def) { return (i + 1 < argc) ? atoi(argv[++i]) : def; };
        if      (k == "--level")   a.level = num(0);
        else if (k == "--seed")    a.seed = (unsigned)num(1337);
        else if (k == "--visit")   a.visit = (unsigned)num(0);
        else if (k == "--storey")  a.storey = num(0);
        else if (k == "--cells")   a.cells = num(129);
        else if (k == "--samples") a.samples = num(4000);
        else if (k == "--no-plan") a.plan = false;
        else if (k == "--list-exits") a.listExits = true;
        else if (k == "--list-stairs") a.listStairs = true;
        else if (k == "--plan")    { a.px = num(0); a.pz = num(0); a.pw = num(48); a.ph = num(32); }
        else { fputs(usage, stderr); return 2; }
    }

    World w;
    w.seed = a.seed;
    w.level = a.level;
    w.visit = a.visit;
    // wallH would normally come from the level table via applyLevel; only
    // gatherCellAABBs uses it and nothing here reads the height.
    w.wallH = 3.0f;
    // The storey pitch does come from the table: it is what turns the stairs
    // and openings on, and a harness that left it at zero would describe a
    // one-floor Level 0 that the game no longer ships.
    w.storeyH = (a.level >= 0 && a.level < NLEVELS) ? LEVELS[a.level].storeyH : 0.0f;
    w.setStorey(a.storey);

    const int N = a.cells, half = N / 2;
    printf("mapdump  level %d  seed %u  storey %d  %dx%d cells (%.0f x %.0f m)\n",
           a.level, a.seed, a.storey, N, N, N * CELL, N * CELL);
    if (a.visit) printf("  (visit %u)\n", a.visit);

    // ---- floorplan. Each cell is two characters wide so the west edge has
    // somewhere to live; the row above carries the north edges.
    if (a.plan) {
        printf("\nfloorplan  x %d..%d  z %d..%d"
               "   | - wall   , doorway   L locked door   = window   E exit   # pillar\n"
               "                                 H hide spot   p prop   V vending"
               "   Y valve   ~ soft floor   o coin   k key   w water   digits: floor height\n\n",
               a.px, a.px + a.pw - 1, a.pz, a.pz + a.ph - 1);
        for (int b = a.pz; b < a.pz + a.ph; b++) {
            std::string top, mid;
            for (int x = a.px; x < a.px + a.pw; x++) {
                uint8_t nv = w.wallNVal(x, b);
                top += '+';
                top += (nv == WALL_SOLID) ? "--" : (nv == WALL_DOOR) ? " ," :
                       (nv == WALL_LOCKED) ? "LL" : (nv == WALL_RAIL) ? ".." :
                       (nv == WALL_WINDOW) ? "==" : (nv == WALL_EXIT) ? "EE" : "  ";
                uint8_t wv = w.wallWVal(x, b);
                mid += (wv == WALL_SOLID) ? '|' : (wv == WALL_DOOR) ? ',' :
                       (wv == WALL_LOCKED) ? 'L' : (wv == WALL_RAIL) ? ':' :
                       (wv == WALL_WINDOW) ? '=' : (wv == WALL_EXIT) ? 'E' : ' ';
                char g = cellGlyph(w, x, b);
                if (g == ' ' && w.keyAt(x, b))   g = 'k';
                if (g == ' ' && coinAt(w, x, b)) g = 'o';
                if (g == ' ' && w.poolAt(x, b))  g = 'w';
                float fy = w.floorY(x, b);
                char h = (fabsf(fy) < 0.05f) ? ' ' : (fy > 0 ? '^' : '_');
                // storeys: S a flight, O open to the storey above, v a hole
                // down one you can walk (a flight comes up it), X a drop
                uint8_t vf = w.vflagAt(x, b);
                if (g == ' ') g = (vf & VF_STAIR) ? 'S' : (vf & VF_HOLE) ? ((vf & VF_WALKHOLE) ? 'v' : 'X') : ' ';
                if (h == ' ' && (vf & VF_OPENUP)) h = 'O';
                mid += g;
                mid += h;
            }
            printf("%s+\n%s\n", top.c_str(), mid.c_str());
        }
    }

    // ---- enclosure. The headline number: how much of this is actually rooms.
    long edges = 0, solidEdges = 0, doorEdges = 0, windowEdges = 0, exitEdges = 0;
    long lockedEdges = 0, keysFound = 0;
    struct Lock { float dx, dz, kx, kz; bool west; };
    std::vector<Lock> locks;
    long sides[5] = { 0, 0, 0, 0, 0 };   // cells with 0,1,2,3,4 solid sides
    long cells = 0, pillars = 0, props = 0, hides = 0, coins = 0, soft = 0, valves = 0, pools = 0;
    long raised = 0, sunk = 0;
    for (int b = -half; b <= half; b++) for (int x = -half; x <= half; x++) {
        cells++;
        // Each cell owns its north and west edge; counting only those counts
        // every edge in the grid exactly once.
        uint8_t nv = w.wallNVal(x, b), wv = w.wallWVal(x, b);
        for (uint8_t v : { nv, wv }) {
            edges++;
            if (v == WALL_SOLID) solidEdges++;
            else if (v == WALL_DOOR) doorEdges++;
            else if (v == WALL_WINDOW) windowEdges++;
            else if (v == WALL_EXIT) exitEdges++;
            else if (v == WALL_LOCKED) lockedEdges++;
        }
        int s = (w.wallNVal(x, b) == WALL_SOLID) + (w.wallWVal(x, b) == WALL_SOLID) +
                (w.wallNVal(x, b + 1) == WALL_SOLID) + (w.wallWVal(x + 1, b) == WALL_SOLID);
        sides[s]++;
        if (w.keyAt(x, b)) keysFound++;
        for (int west = 0; west < 2; west++) {
            if ((west ? w.wallWVal(x, b) : w.wallNVal(x, b)) != WALL_LOCKED) continue;
            int cx = fdiv(x, CCELLS), cz = fdiv(b, CCELLS);
            ChunkData &cd = w.data(cx, cz);
            locks.push_back({ x * CELL + (west ? 0.0f : 1.0f), b * CELL + (west ? 1.0f : 0.0f),
                              (cx * CCELLS + cd.keyI) * CELL + 1.0f,
                              (cz * CCELLS + cd.keyK) * CELL + 1.0f, west != 0 });
        }
        if (w.pillarAt(x, b)) pillars++;
        if (w.propAt(x, b)) props++;
        if (hideSpotAt(w, x, b)) hides++;
        if (coinAt(w, x, b)) coins++;
        if (w.softAt(x, b)) soft++;
        if (w.valveAt(x, b)) valves++;
        if (w.poolAt(x, b)) pools++;
        float fy = w.floorY(x, b);
        if (fy > 0.05f) raised++;
        else if (fy < -0.05f) sunk++;
    }
    double area = (double)cells * CELL * CELL;
    printf("\nenclosure over %ld cells (%.0f m2)\n", cells, area);
    printf("  edges                 %ld\n", edges);
    printf("  solid wall            %6.2f%%\n", 100.0 * solidEdges / edges);
    printf("  doorway               %6.2f%%   (%ld)\n", 100.0 * doorEdges / edges, doorEdges);
    printf("  window                %6.2f%%   (%ld)\n", 100.0 * windowEdges / edges, windowEdges);
    printf("  exit                  %6.2f%%   (%ld)\n", 100.0 * exitEdges / edges, exitEdges);
    // A locked door is meant to shut a closet off from the flood below, so a
    // few cells per one of these are SUPPOSED to read as unreachable here.
    // mapdump measures the floor, and does not know the player has a key.
    printf("  locked door           %6.2f%%   (%ld)\n", 100.0 * lockedEdges / edges, lockedEdges);
    printf("  key                             (%ld)\n", keysFound);
    // How full the collision scratch gets. gatherCellAABBs accumulates over the
    // 3x3 around a point into one MAX_NEARBY_AABBS buffer and silently DROPS
    // every box past the cap — so a cell that overflows is a cell you walk
    // through a wall in, with nothing anywhere saying so.
    {
        int worst = 0, over = 0, wx = 0, wz = 0;
        AABB boxes[512];
        for (int x = -half; x <= half; ++x) for (int b = -half; b <= half; ++b) {
            int cnt = 0;
            for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz)
                cnt = w.gatherCellAABBs(x + dx, b + dz, boxes, 512, cnt);
            if (cnt > worst) { worst = cnt; wx = x; wz = b; }
            if (cnt > MAX_NEARBY_AABBS) over++;
        }
        printf("\ncollision scratch (MAX_NEARBY_AABBS = %d)\n", MAX_NEARBY_AABBS);
        printf("  worst 3x3 box count   %d  at x %d z %d\n", worst, wx, wz);
        printf("  cells over the cap    %d%s\n", over,
               over ? "   <-- boxes are being dropped: you can walk through walls there" : "");
    }
    // Where they are, so a capture can actually be pointed at one. A locked
    // door is one chunk in three and nothing else in the dump locates it.
    if (!locks.empty()) {
        printf("  locked doors (first %d of %d), as BACKROOMS_POS:\n",
               (int)std::min<size_t>(locks.size(), 5), (int)locks.size());
        for (size_t t = 0; t < locks.size() && t < 5; t++)
            printf("    door at x %.1f z %.1f (%s edge)   key at x %.1f z %.1f\n",
                   locks[t].dx, locks[t].dz, locks[t].west ? "west" : "north",
                   locks[t].kx, locks[t].kz);
    }
    for (int s = 0; s <= 4; s++)
        printf("  cells with %d solid    %6.2f%%\n", s, 100.0 * sides[s] / cells);
    printf("  cells with 2+ solid   %6.2f%%\n",
           100.0 * (sides[2] + sides[3] + sides[4]) / cells);

    // ---- sightlines. March until the world's own line-of-sight test fails.
    Rng rng(0x5EE1u ^ a.seed);
    std::vector<double> lens;
    lens.reserve(a.samples);
    int blockedAt20 = 0, tried20 = 0;
    const float MAXD = 240.0f;
    for (int i = 0; i < a.samples; i++) {
        int cx = rng.ri(-half + 2, half - 2), cz = rng.ri(-half + 2, half - 2);
        if (w.pillarAt(cx, cz)) continue;
        float ax = cx * CELL + 1.0f, az = cz * CELL + 1.0f;
        float ang = rng.f01() * TAU, dx = cosf(ang), dz = sinf(ang);
        float d = 0.5f;
        while (d < MAXD && w.lineOfSight(ax, az, ax + dx * d, az + dz * d)) d += 0.5f;
        lens.push_back(d);
        // and the same question the other way round: is a point 20 m off visible
        float bx = ax + dx * 20.0f, bz = az + dz * 20.0f;
        tried20++;
        if (!w.lineOfSight(ax, az, bx, bz)) blockedAt20++;
    }
    std::sort(lens.begin(), lens.end());
    auto pct = [&](double p) { return lens.empty() ? 0.0 : lens[(size_t)(p * (lens.size() - 1))]; };
    printf("\nsightlines (%zu samples)\n", lens.size());
    printf("  median                %6.1f m\n", pct(0.50));
    printf("  90th percentile       %6.1f m\n", pct(0.90));
    printf("  99th percentile       %6.1f m\n", pct(0.99));
    printf("  longest               %6.1f m\n", lens.empty() ? 0.0 : lens.back());
    printf("  hidden at 20 m        %6.1f%%   (%d of %d)\n",
           tried20 ? 100.0 * blockedAt20 / tried20 : 0.0, blockedAt20, tried20);

    // ---- reachability. A flood fill over the same canStep the pathfinder uses,
    // so a generator change that walls something off shows up here rather than
    // as a player stuck in a sealed room.
    std::vector<unsigned char> seen((size_t)N * N, 0);
    auto idx = [&](int x, int z) { return (size_t)(z + half) * N + (x + half); };
    std::vector<std::pair<int,int>> stack;
    // Not floor: a pillar, or a hole with no flight coming up it (the void of
    // an atrium, railed off). Counting a void as floor made every one of its
    // cells a sealed one-cell "pocket".
    auto notFloor = [&](int x, int z) {
        if (w.pillarAt(x, z)) return true;
        uint8_t vf = w.storeyH > 0.0f ? w.vflagAt(x, z) : 0;
        return (vf & VF_HOLE) && !(vf & VF_WALKHOLE);
    };
    long openCells = 0;
    for (int b = -half; b <= half; b++) for (int x = -half; x <= half; x++)
        if (!notFloor(x, b)) openCells++;
    // start from the first open cell at the centre, the way a run does
    for (int r = 0; r < half && stack.empty(); r++)
        for (int b = -r; b <= r && stack.empty(); b++)
            for (int x = -r; x <= r && stack.empty(); x++)
                if (!notFloor(x, b)) { stack.push_back({ x, b }); seen[idx(x, b)] = 1; }
    long reached = 0;
    while (!stack.empty()) {
        auto [x, z] = stack.back();
        stack.pop_back();
        reached++;
        const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], nz = z + dz[k];
            if (nx < -half || nx > half || nz < -half || nz > half) continue;
            if (seen[idx(nx, nz)] || notFloor(nx, nz)) continue;
            if (!w.canStep(x, z, nx, nz)) continue;
            seen[idx(nx, nz)] = 1;
            stack.push_back({ nx, nz });
        }
    }
    // What is left over, grouped. One big sealed region and a scatter of sealed
    // broom cupboards are both "92% reachable" and they are not the same bug:
    // the first is a generator that cut the world in half, the second is a few
    // rooms whose doors all landed on the same side.
    long pockets = 0, biggest = 0, inPockets = 0;
    int bigX = 0, bigZ = 0;
    for (int b = -half; b <= half; b++) for (int x = -half; x <= half; x++) {
        if (seen[idx(x, b)] || notFloor(x, b)) continue;
        long size = 0;
        bool touchesEdge = false;   // see below
        std::vector<std::pair<int,int>> q{ { x, b } };
        seen[idx(x, b)] = 1;
        while (!q.empty()) {
            auto [cx2, cz2] = q.back();
            q.pop_back();
            size++;
            if (cx2 <= -half || cx2 >= half || cz2 <= -half || cz2 >= half) touchesEdge = true;
            const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int nx = cx2 + dx[k], nz = cz2 + dz[k];
                if (nx < -half || nx > half || nz < -half || nz > half) continue;
                if (seen[idx(nx, nz)] || notFloor(nx, nz)) continue;
                if (!w.canStep(cx2, cz2, nx, nz)) continue;
                seen[idx(nx, nz)] = 1;
                q.push_back({ nx, nz });
            }
        }
        // A region touching the window border is almost certainly reachable via
        // cells outside the window, so counting it would be an artefact of where
        // the sample was cut rather than anything the generator did. The world
        // is infinite; the sample is not.
        if (touchesEdge) continue;
        pockets++;
        inPockets += size;
        if (size > biggest) { biggest = size; bigX = x; bigZ = b; }
    }
    printf("\nreachability (flood fill over canStep)\n");
    printf("  open cells            %ld\n", openCells);
    printf("  reached from centre   %ld  (%.2f%%)\n", reached, 100.0 * reached / (openCells ? openCells : 1));
    printf("  cut-off pockets       %ld  holding %ld cells\n", pockets, inPockets);
    printf("  largest pocket        %ld cells  (median %ld) at x %d z %d\n",
           biggest, pockets ? inPockets / pockets : 0, bigX, bigZ);

    // ---- the ways up and down from this storey, over the chunks the window
    // covers. "How far to the nearest stair" is the number a player feels.
    if (w.storeyH > 0.0f) {
        int cr = half / CCELLS, chunksN = 0, withWay = 0;
        int up[4] = {}, down[4] = {};
        for (int cz = -cr; cz <= cr; cz++) for (int cx = -cr; cx <= cr; cx++) {
            VertFeat fs[2];
            int n = w.featuresFor(cx, cz, w.storey, fs, 2);
            chunksN++;
            if (n) withWay++;
            for (int q = 0; q < n; q++) (fs[q].lo == w.storey ? up : down)[fs[q].kind]++;
        }
        printf("\nstoreys (%d chunks, %.0f%% with a way up or down)\n", chunksN, 100.0 * withWay / (chunksN ? chunksN : 1));
        printf("  up                    %d stairwells, %d flights, %d atria\n", up[VK_STAIRWELL], up[VK_STAIR], up[VK_ATRIUM]);
        printf("  down                  %d stairwells, %d flights, %d atria\n", down[VK_STAIRWELL], down[VK_STAIR], down[VK_ATRIUM]);
    }

    // ---- what is lying about. Densities in m2 per instance read better than
    // percentages here: "one doubloon per 1,753 m2" is the number that showed
    // the floor route was impossible.
    auto per = [&](long n) { return n ? area / n : 0.0; };
    printf("\ndensity (m2 per instance, 0 = none found)\n");
    printf("  pillar                %8.0f  (%ld)\n", per(pillars), pillars);
    printf("  prop                  %8.0f  (%ld)\n", per(props), props);
    printf("  hide spot             %8.0f  (%ld)\n", per(hides), hides);
    printf("  floor doubloon        %8.0f  (%ld)\n", per(coins), coins);
    printf("  soft floor            %8.0f  (%ld)\n", per(soft), soft);
    printf("  valve                 %8.0f  (%ld)\n", per(valves), valves);
    printf("  exit doorway          %8.0f  (%ld)\n", per(exitEdges), exitEdges);
    printf("  pool cell             %8.0f  (%ld)\n", per(pools), pools);
    printf("  raised floor          %8.0f  (%ld)\n", per(raised), raised);
    printf("  sunken floor          %8.0f  (%ld)\n", per(sunk), sunk);
    printf("  cells using elevation %6.2f%%\n", 100.0 * (raised + sunk) / cells);
    // Where the exits are, in world metres — the numbers BACKROOMS_POS takes —
    // so a capture can be pointed at one. On Level 0 these are the noclip walls.
    if (a.listStairs) {
        // Every vertical feature rising from or arriving at this storey, with a
        // BACKROOMS_POS that stands you at its foot looking up the flight (or,
        // for an atrium, at its near edge looking across it).
        static const char *KIND[] = { "none", "stairwell", "stair", "atrium" };
        printf("\nvertical features on storey %d (BACKROOMS_POS=x,z,yaw)\n", a.storey);
        int cr = half / CCELLS + 1;
        for (int cz = -cr; cz <= cr; cz++) for (int cx = -cr; cx <= cr; cx++) {
            VertFeat fs[2];
            int n = w.featuresFor(cx, cz, a.storey, fs, 2);
            for (int q = 0; q < n; q++) {
                const VertFeat &f = fs[q];
                bool rising = f.lo == a.storey;
                // stand in the approach (or at the arrival, for one arriving here)
                float u = f.kind == VK_STAIRWELL ? (rising ? 1.0f : 3.0f) : f.wu * CELL * 0.5f;
                // A stairwell: at its foot (rising) or its head (arriving), in
                // the shaft, facing up or down the first flight.
                float v = rising ? (f.kind == VK_STAIRWELL ? 0.8f : 0.2f)
                                 : (f.kind == VK_STAIRWELL ? 0.8f : f.lv * CELL - 0.6f);
                Vector3 at = w.featureWorld(f, cx, cz, u, 0, v);
                Vector3 ahead = w.featureWorld(f, cx, cz, u, 0, v + (rising || f.kind == VK_STAIRWELL ? 1.0f : -1.0f));
                float yaw = atan2f(ahead.z - at.z, ahead.x - at.x);
                printf("  %-9s %s  wu %d lv %d dir %d  %s%s  at %.1f,%.1f,%.2f\n", KIND[f.kind],
                       rising ? "up  " : "down", f.wu, f.lv, f.dir,
                       f.kind == VK_ATRIUM && f.stairU >= 0 ? "with flight " : "",
                       f.kind == VK_STAIR ? (f.wallSide ? "against a wall" : "free-standing") : "",
                       at.x, at.z, yaw);
            }
        }
    }
    if (a.listExits) {
        printf("\nexits (x z, world metres; * = cursed)\n");
        for (int k = -half; k <= half; k++) for (int i = -half; i <= half; i++) {
            bool n = w.wallNVal(i, k) == WALL_EXIT, ww = w.wallWVal(i, k) == WALL_EXIT;
            if (!n && !ww) continue;
            printf("  %7.1f %7.1f  %s%s\n", i * CELL + (n ? 1.0f : 0.0f), k * CELL + (n ? 0.0f : 1.0f),
                   n ? "north edge" : "west edge", w.cursedExit(i, k) ? "  *" : "");
        }
    }
    return 0;
}
