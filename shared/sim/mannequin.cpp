#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>
#include <queue>
#include <set>

namespace {
constexpr float SPEED = 5.2f, REACH = 0.9f, FLARE_REACH = 5.0f;
constexpr float RELEASE_GRACE = 0.75f, ESCAPE_DISTANCE = 24, ESCAPE_TIME = 8;
constexpr double RESPAWN = 150;
}

bool Sim::spawnMannequin(double now) {
    if (mannequin.active || level > 1 || inMenu || inManila || !grounded || swimming) return false;
    struct Cell { int i, k, depth; };
    std::queue<Cell> queue;
    std::set<std::pair<int, int>> seen;
    int ci = cellOf(px), ck = cellOf(pz);
    queue.push({ci, ck, 0}); seen.insert({ci, ck});
    const int directions[][2] = {{1,0},{0,1},{-1,0},{0,-1}};
    while (!queue.empty()) {
        Cell c = queue.front(); queue.pop();
        Vec3 pos{(c.i + 0.5f) * CELL, world.floorY(c.i, c.k), (c.k + 0.5f) * CELL};
        float distance = portableHypot(pos.x - px, pos.z - pz);
        if (c.depth >= 5 && distance >= 10 && !world.manilaAt(c.i, c.k)) {
            mannequin = {};
            mannequin.active = true; mannequin.pos = pos; mannequin.storey = world.storey;
            mannequin.grace = 3; mannequin.yaw = portableAtan2(pz-pos.z,px-pos.x);
            deckNoteT = 2.6f; deckNote = "THE MANNEQUIN. keep it in the camcorder. flares hold it back.";
            return true;
        }
        if (c.depth >= 12) continue;
        for (const auto &d : directions) {
            int i = c.i + d[0], k = c.k + d[1];
            if (seen.count({i,k}) || !world.canStep(c.i, c.k, i, k)) continue;
            if ((world.storeyH > 0 && world.vflagAt(i,k)) || world.softAt(i,k) || world.poolAt(i,k)) continue;
            seen.insert({i,k}); queue.push({i,k,c.depth+1});
        }
    }
    mannequin.nextSpawn = now + 20;
    return false;
}

void Sim::updateMannequin(const InputFrame &in, float dt, double now) {
    auto &m = mannequin;
    if (!m.active) {
        if (now >= m.nextSpawn && ent.st == EState::Hidden) spawnMannequin(now);
        return;
    }
    if (m.storey != world.storey || inManila || level > 1) {
        m = {}; m.nextSpawn = now + RESPAWN; return;
    }
    Vec3 to = sub(add(m.pos, Vec3{0,1.5f,0}), Vec3{px,eyeY,pz});
    float length = sqrtf(to.x*to.x + to.y*to.y + to.z*to.z);
    float distance = portableHypot(to.x, to.z);
    bool los = world.lineOfSight(px,pz,m.pos.x,m.pos.z);
    bool pinned = in.playing && in.observing && distance < 32 && los &&
        (to.x*fwd.x + to.y*fwd.y + to.z*fwd.z) > length * in.observationCos;
    if (m.pinned && !pinned) { m.grace = RELEASE_GRACE; m.pose = (m.pose + 1) % 3; }
    m.pinned = pinned;
    m.warded = false;
    if (const FlareProj *flare = nearestLitFlare(m.pos.x,m.pos.z))
        m.warded = portableHypot(flare->x-m.pos.x, flare->z-m.pos.z) < FLARE_REACH &&
                   world.lineOfSight(flare->x,flare->z,m.pos.x,m.pos.z);
    m.grace = fmaxf(0,m.grace-dt); m.stagger = fmaxf(0,m.stagger-dt);
    m.lost = !los && distance > ESCAPE_DISTANCE ? m.lost + dt : 0;
    if (m.lost >= ESCAPE_TIME) {
        m = {}; m.nextSpawn = now + RESPAWN;
        deckNoteT = 2.6f; deckNote = "the footsteps have stopped."; return;
    }
    if (m.pinned || m.warded || m.grace > 0 || m.stagger > 0) return;
    int ci = cellOf(m.pos.x), ck = cellOf(m.pos.z), oi, ok;
    float tx = px, tz = pz;
    if (ci != cellOf(px) || ck != cellOf(pz)) {
        if (!world.pathStep(ci,ck,cellOf(px),cellOf(pz),oi,ok)) return;
        if (world.storeyH > 0 && world.vflagAt(oi,ok)) return;
        tx = (oi+0.5f)*CELL; tz = (ok+0.5f)*CELL;
    }
    float dx=tx-m.pos.x, dz=tz-m.pos.z, d=portableHypot(dx,dz);
    float move = fminf(SPEED*dt,d);
    Vec3 before = m.pos;
    if (d > 0.001f) m.yaw = portableAtan2(dz,dx);
    if (d > 0.001f) { m.pos.x += dx/d*move; m.pos.z += dz/d*move; }
    world.collideCircle(m.pos.x,m.pos.z,0.3f,m.pos.y);
    m.pos.y = world.groundAt(m.pos.x,m.pos.z,m.pos.y);
    float gait = m.gait;
    m.gait += portableHypot(m.pos.x-before.x,m.pos.z-before.z)/0.9f;
    if ((int)gait != (int)m.gait)
        play(Sfx::MannequinStep).atPitch(0.7f).atVolume(clampf(1/(1+distance*0.12f),0,0.8f))
            .atPan(clampf((to.x*r2x+to.z*r2z)/fmaxf(distance,0.01f),-1,1));
    if (distance < REACH && los && fabsf(py-m.pos.y)<1 && hurtT<=0) {
        if (hurtPlayer(now,ENTITY_HIT,"THE MANNEQUIN",m.pos.x,m.pos.z)) return;
        m.grace = 3;
    }
}
