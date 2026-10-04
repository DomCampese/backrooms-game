#include "../core/fp_strict.h"
#include "trace.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace {

// Every InputFrame field, by name. Reading and writing both go through here,
// so a field added to InputFrame and here is recorded and replayed.
template <class F> void inputFields(InputFrame &in, F &&f) {
    f("playing", in.playing); f("touch", in.touch);
    f("forward", in.forward); f("back", in.back); f("left", in.left); f("right", in.right);
    f("forwardPressed", in.forwardPressed); f("moveScale", in.moveScale);
    f("sprint", in.sprint); f("crouch", in.crouch); f("squeeze", in.squeeze);
    f("jumpHeld", in.jumpHeld); f("jumpPressed", in.jumpPressed);
    f("lookX", in.look.x); f("lookY", in.look.y);
    f("wheel", in.wheel);
    f("pickRevolver", in.pickRevolver); f("pickFlare", in.pickFlare); f("pickDeck", in.pickDeck);
    f("fire", in.fire); f("aim", in.aim);
    f("reload", in.reload); f("throwFlare", in.throwFlare); f("flashlight", in.flashlight);
    f("use", in.use); f("drink", in.drink); f("chalk", in.chalk);
    f("begin", in.begin);
    f("devBlackout", in.dev.blackout); f("devSpawnAhead", in.dev.spawnAhead); f("devChase", in.dev.chase);
    f("devBanish", in.dev.banish); f("devRefill", in.dev.refill);
    f("devStoreyUp", in.dev.storeyUp); f("devStoreyDown", in.dev.storeyDown); f("devNextLevel", in.dev.nextLevel);
    f("screenFov", in.screenFov);
    f("forceSpawn", in.forceSpawn);
}

template <class F> void startFields(SimStart &s, F &&f) {
    f("seed", s.seed); f("exitTest", s.exitTest); f("manilaTest", s.manilaTest);
    f("noBlackout", s.noBlackout); f("fixedSeed", s.fixedSeed); f("keepRecords", s.keepRecords);
    f("placed", s.placed); f("x", s.x); f("z", s.z); f("yaw", s.yaw);
    f("pitched", s.pitched); f("pitch", s.pitch);
    f("level", s.level); f("storeyed", s.storeyed); f("storey", s.storey);
    f("flash", s.flash); f("menu", s.menu); f("fov", s.fov);
    f("bestEscapes", s.best.escapes); f("bestKills", s.best.kills); f("bestMetres", s.best.metres);
    f("bestWins", s.best.wins); f("bestTapes", s.best.tapes); f("bestDeepest", s.best.deepest);
    f("bestLongestRun", s.best.longestRun);
}

// One `name=value` token. Floats round-trip at 9 significant digits, doubles at 17.
struct Line {
    std::string s;
    void put(const char *fmt, ...) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        s += buf;
    }
    void operator()(const char *n, bool v) { put(" %s=%d", n, v ? 1 : 0); }
    void operator()(const char *n, int v) { put(" %s=%d", n, v); }
    void operator()(const char *n, unsigned v) { put(" %s=%u", n, v); }
    void operator()(const char *n, uint64_t v) { put(" %s=%llu", n, (unsigned long long)v); }
    void operator()(const char *n, float v) { put(" %s=%.9g", n, v); }
    void operator()(const char *n, double v) { put(" %s=%.17g", n, v); }
    void operator()(const char *n, Vec3 v) { put(" %s=%.9g,%.9g,%.9g", n, v.x, v.y, v.z); }
};

// Writes each field of a record from its token, by name. A field the record
// does not carry keeps its default; a token no field reads is an error.
struct Fields {
    std::map<std::string, std::string> tok;
    std::string bad;
    const char *get(const char *n) {
        auto it = tok.find(n);
        if (it == tok.end()) return nullptr;
        const char *v = it->second.c_str();
        used.push_back(n);
        return v;
    }
    std::vector<std::string> used;
    void operator()(const char *n, bool &v) { if (const char *t = get(n)) v = atoi(t) != 0; }
    void operator()(const char *n, int &v) { if (const char *t = get(n)) v = atoi(t); }
    void operator()(const char *n, unsigned &v) { if (const char *t = get(n)) v = (unsigned)strtoul(t, nullptr, 10); }
    void operator()(const char *n, float &v) { if (const char *t = get(n)) v = strtof(t, nullptr); }
    void operator()(const char *n, double &v) { if (const char *t = get(n)) v = strtod(t, nullptr); }
    void vec(const char *n, Vec3 &v) {
        if (const char *t = get(n)) {
            char *e;
            v.x = strtof(t, &e); v.y = strtof(e + 1, &e); v.z = strtof(e + 1, nullptr);
        }
    }
    // Tokens nothing read, so a trace from a newer build does not replay silently wrong.
    std::string unread() const {
        std::string out;
        for (const auto &kv : tok) {
            bool seen = false;
            for (const std::string &u : used) seen = seen || u == kv.first;
            if (!seen) out += " " + kv.first;
        }
        return out;
    }
};

// Splits `kind a=1 b=2` into the kind and its tokens.
std::string parse(const std::string &line, Fields &out) {
    size_t sp = line.find(' ');
    std::string kind = line.substr(0, sp);
    while (sp != std::string::npos) {
        size_t next = line.find(' ', sp + 1);
        std::string t = line.substr(sp + 1, next == std::string::npos ? std::string::npos : next - sp - 1);
        size_t eq = t.find('=');
        if (eq != std::string::npos) out.tok[t.substr(0, eq)] = t.substr(eq + 1);
        sp = next;
    }
    return kind;
}

// FNV-1a over the bytes of each value, so every platform computes the same hash.
struct Fnv {
    uint64_t h = 0xCBF29CE484222325ULL;
    void byte(uint8_t b) { h = (h ^ b) * 0x100000001B3ULL; }
    void u32(uint32_t v) { for (int s = 0; s < 32; s += 8) byte((uint8_t)(v >> s)); }
    void f32(float v) { uint32_t u; memcpy(&u, &v, 4); u32(u); }
};

uint64_t audioHash(const std::vector<AudioEvent> &events) {
    Fnv h;
    for (const AudioEvent &e : events) {
        h.byte(e.kind); h.byte((uint8_t)e.sfx); h.byte(e.variant); h.byte(e.set);
        h.f32(e.pitch); h.f32(e.volume); h.f32(e.pan); h.f32(e.dt);
        h.byte(e.loops.underwater); h.byte(e.loops.party);
        const AmbienceMix &m = e.mix;
        for (float v : { m.hum, m.growl, m.hiss, m.whisper, m.humLevel, m.droneLevel, m.panel, m.space }) h.f32(v);
    }
    return h.h;
}

}  // namespace

bool TraceWriter::open(const char *path) {
    close();
    f = fopen(path, "wb");
    if (f) fputs("backrooms-trace 1\n", f);
    return f != nullptr;
}

void TraceWriter::close() {
    if (f) fclose(f);
    f = nullptr;
}

void TraceWriter::start(const SimStart &s, double now) {
    Line l;
    l.put("start");
    SimStart copy = s;
    startFields(copy, [&](const char *n, auto &v) { l(n, v); });
    l("now", now);
    fprintf(f, "%s\n", l.s.c_str());
}

void TraceWriter::level(int lv, double now) { fprintf(f, "level lv=%d now=%.17g\n", lv, now); }
void TraceWriter::place() { fputs("place\n", f); }
void TraceWriter::fov(float v) { fprintf(f, "fov v=%.9g\n", v); }
void TraceWriter::pause(bool on, double now) { fprintf(f, "pause on=%d now=%.17g\n", on ? 1 : 0, now); }

namespace {
void frameRecord(FILE *f, const char *kind, const InputFrame &in, float dt, double now, uint32_t clockSeed) {
    Line l;
    l.put("%s dt=%.9g now=%.17g clockSeed=%u", kind, dt, now, clockSeed);
    InputFrame copy = in;
    inputFields(copy, [&](const char *n, auto &v) { l(n, v); });
    fprintf(f, "%s\n", l.s.c_str());
}
}  // namespace

void TraceWriter::menu(const InputFrame &in, float dt, double now, uint32_t clockSeed) {
    frameRecord(f, "menu", in, dt, now, clockSeed);
}
void TraceWriter::step(const InputFrame &in, float dt, double now, uint32_t clockSeed) {
    frameRecord(f, "step", in, dt, now, clockSeed);
}

void TraceWriter::hit(const Ray3 &ray, float nearestIn, bool found, float nearestOut, Vec3 normal) {
    Line l;
    l.put("hit");
    l("from", ray.position); l("dir", ray.direction); l("in", nearestIn);
    l("found", found); l("out", nearestOut); l("normal", normal);
    fprintf(f, "%s\n", l.s.c_str());
}

// Flushed, so a game that is killed leaves every finished frame on disk.
void TraceWriter::digest(const Sim &sim) {
    fprintf(f, "digest%s\n", simDigest(sim).c_str());
    fflush(f);
}

bool RecordingTracer::nearestSolid(const Ray3 &ray, float &nearest, Vec3 &normal) {
    float in = nearest;
    bool found = inner.nearestSolid(ray, nearest, normal);
    out.hit(ray, in, found, nearest, normal);
    return found;
}

std::string simDigest(const Sim &sim) {
    Line l;
    l("px", sim.px); l("py", sim.py); l("pz", sim.pz); l("vy", sim.vy);
    l("velx", sim.velx); l("velz", sim.velz);
    l("yaw", sim.yaw); l("pitch", sim.pitch); l("eyeY", sim.eyeY); l("fov", sim.fov);
    l("health", sim.health); l("stamina", sim.stamina); l("sanity", sim.sanity); l("battery", sim.battery);
    l("level", sim.level); l("storey", sim.world.storey); l("visit", sim.world.visit); l("seed", sim.world.seed);
    l("grng", sim.grng.s);
    l("inMenu", sim.inMenu); l("paused", sim.paused);
    l("weapon", sim.weapon); l("ammo", sim.ammo); l("reloadT", sim.reloadT); l("flares", sim.flares);
    l("coins", sim.coins); l("almond", sim.almond); l("tapes", sim.tapes); l("keys", sim.keys);
    l("flashOn", sim.flashOn);
    l("entSt", (int)sim.ent.st); l("entX", sim.ent.x); l("entZ", sim.ent.z); l("entHp", sim.ent.hp);
    l("entNextSpawn", sim.ent.nextSpawn);
    for (int i = 0; i < Sim::MAXDOGS; i++) {
        char n[16];
        snprintf(n, sizeof n, "dog%dSt", i); l(n, (int)sim.dogs[i].st);
        snprintf(n, sizeof n, "dog%dX", i); l(n, sim.dogs[i].x);
        snprintf(n, sizeof n, "dog%dZ", i); l(n, sim.dogs[i].z);
    }
    l("nextBlackout", sim.nextBlackout); l("blackoutEnd", sim.blackoutEnd);
    l("bullets", (int)sim.bullets.size()); l("impacts", (int)sim.bulletImpacts.size());
    l("deaths", sim.deathCount); l("kills", sim.killCount);
    l("shifted", (int)sim.world.shifted.size()); l("unlocked", (int)sim.world.unlockedDoors.size());
    l("audio", (int)sim.audio.size()); l("audioHash", audioHash(sim.audio));
    return l.s;
}

namespace {

// Answers from the recording, in order, and notes the first query that is not
// the one recorded.
struct ReplayTracer : SolidTracer {
    std::deque<std::map<std::string, std::string>> answers;
    std::string mismatch;
    int used = 0;
    bool nearestSolid(const Ray3 &ray, float &nearest, Vec3 &normal) override {
        if (answers.empty()) {
            if (mismatch.empty()) mismatch = "the sim asked the tracer more often than the recording did";
            return false;
        }
        Fields a;
        a.tok = answers.front();
        answers.pop_front();
        used++;
        Vec3 from{}, dir{}, n{};
        float in = 0, out = 0;
        bool found = false;
        a.vec("from", from); a.vec("dir", dir); a("in", in); a("found", found); a("out", out); a.vec("normal", n);
        Line want, got;
        want("from", from); want("dir", dir); want("in", in);
        got("from", ray.position); got("dir", ray.direction); got("in", nearest);
        if (want.s != got.s && mismatch.empty()) mismatch = "tracer query differs:\n  want:" + want.s + "\n  got: " + got.s;
        if (found) { nearest = out; normal = n; }
        return found;
    }
};

// Names the tokens that differ between two digests.
std::string digestDiff(const std::string &want, const std::string &got) {
    Fields a, b;
    parse("d" + want, a);
    parse("d" + got, b);
    std::string out;
    for (const auto &kv : a.tok) {
        auto it = b.tok.find(kv.first);
        std::string g = it == b.tok.end() ? "(none)" : it->second;
        if (g != kv.second) out += "\n  " + kv.first + ": want " + kv.second + " got " + g;
    }
    for (const auto &kv : b.tok)
        if (!a.tok.count(kv.first)) out += "\n  " + kv.first + ": not recorded, got " + kv.second;
    return out;
}

void clearAfterFrame(Sim &sim) {
    sim.audio.clear();
    sim.dropAimLatch = false;
    sim.shadowsStale = false;
    sim.recordsChanged = false;
}

}  // namespace

ReplayResult replayTrace(const char *path) {
    ReplayResult r;
    FILE *f = fopen(path, "rb");
    if (!f) { r.report = std::string("cannot open ") + path; return r; }
    std::vector<std::string> lines;
    {
        std::string cur;
        for (int c; (c = fgetc(f)) != EOF;) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += (char)c;
        }
        if (!cur.empty()) lines.push_back(cur);
        fclose(f);
    }
    if (lines.empty() || lines[0] != "backrooms-trace 1") { r.report = "not a version 1 trace"; return r; }

    std::unique_ptr<Sim> simp(new Sim());
    Sim &sim = *simp;
    ReplayTracer tracer;
    sim.tracer = &tracer;
    SimStart start;
    char where[64];
    for (size_t n = 1; n < lines.size(); n++) {
        snprintf(where, sizeof where, "line %zu, frame %d: ", n + 1, r.frames);
        Fields tok;
        std::string kind = parse(lines[n], tok);
        if (kind == "hit") continue;   // gathered by the frame that asked
        auto fail = [&](const std::string &why) { r.report = where + why; return r; };
        auto unread = [&]() -> bool { return !tok.unread().empty(); };
        if (kind == "start") {
            double now = 0;
            startFields(start, tok);
            tok("now", now);
            if (unread()) return fail("start: unknown fields" + tok.unread());
            simBegin(sim, start, now);
        } else if (kind == "level") {
            int lv = 0; double now = 0;
            tok("lv", lv); tok("now", now);
            sim.applyLevel(lv, now);
        } else if (kind == "place") {
            simPlace(sim, start);
        } else if (kind == "fov") {
            tok("v", sim.fov);
        } else if (kind == "pause") {
            bool on = false; double now = 0;
            tok("on", on); tok("now", now);
            sim.setPaused(on, now);
        } else if (kind == "menu" || kind == "step") {
            InputFrame in;
            float dt = 0; double now = 0; unsigned clockSeed = 0;
            tok("dt", dt); tok("now", now); tok("clockSeed", clockSeed);
            inputFields(in, tok);
            if (unread()) return fail(kind + ": unknown fields" + tok.unread());
            // This frame's tracer answers follow it, up to its digest.
            for (size_t m = n + 1; m < lines.size() && lines[m].compare(0, 6, "digest") != 0; m++) {
                Fields h;
                if (parse(lines[m], h) == "hit") tracer.answers.push_back(h.tok);
            }
            sim.clockSeed = clockSeed;
            if (kind == "menu") {
                sim.menuDrift(dt, now);
                sim.menuBegin(in, now);
            } else {
                sim.step(in, dt, now);
            }
            r.hits += tracer.used;
            tracer.used = 0;
            if (!tracer.mismatch.empty()) return fail(tracer.mismatch);
            if (!tracer.answers.empty()) return fail("the sim asked the tracer less often than the recording did");
        } else if (kind == "digest") {
            std::string want = lines[n].substr(6), got = simDigest(sim);
            if (want != got) return fail("digest differs:" + digestDiff(want, got));
            r.frames++;
            clearAfterFrame(sim);
        } else {
            return fail("unknown record " + kind);
        }
    }
    r.ok = true;
    snprintf(where, sizeof where, "%d frames, %d tracer answers: all digests match", r.frames, r.hits);
    r.report = where;
    return r;
}
