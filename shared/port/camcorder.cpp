#include "camcorder.h"
#include "../sim/sim.h"
#include "../core/hash.h"
#include "../core/vec.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// The playback on the death card: a burst of snow while the transport
// engages, up to PLAY_S of tape at speed, and its last SLOW_SPAN s at
// SLOW_RATE, which stops on the final frame. About 6 s in all, which the
// card's 12 (Sim::DEATH_CARD) has to leave room after for its words.
constexpr float STATIC_S = 0.45f;
constexpr float PLAY_S = 3.0f;
constexpr float SLOW_SPAN = 0.9f;
constexpr float SLOW_RATE = 0.32f;
constexpr int MAX_FRAMES = 64;   // 4.3 s at GRAB_HZ: PLAY_S + SLOW_SPAN and a margin
constexpr float ECHO_VOLUME = 0.7f;   // echo volume relative to the live sound

constexpr float PI_F = 3.14159265358979323846f;   // raylib's PI
constexpr float DEG2RAD_F = PI_F / 180.0f;
constexpr float RAD2DEG_F = 180.0f / PI_F;

constexpr Rgba BLACK_RGBA = { 0, 0, 0, 255 };

float smooth01(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// raylib's Fade: the colour at a clamped share of full alpha.
Rgba fade(Rgba c, float a) {
    a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
    return { c.r, c.g, c.b, (uint8_t)(255.0f * a) };
}

// ---- sounds, synthesized like the rest of the game's (sounds.cpp)

int16_t s16(float v) { return (int16_t)(clampf(v, -1.0f, 1.0f) * 32000.0f); }

// Fades a clip in and out over `edge` s so it starts and ends without a click.
float edges(int i, int n, float edge) {
    float t = (float)i / SAMPLE_RATE, end = (float)(n - i) / SAMPLE_RATE;
    return fminf(1.0f, fminf(t, end) / edge);
}

void addBeep(Pcm &p, float at, float dur, float hz, float vol) {
    int a = (int)(at * SAMPLE_RATE), n = (int)(dur * SAMPLE_RATE);
    if ((int)p.size() < a + n) p.resize(a + n, 0);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = sinf(TAU * hz * t) * vol * edges(i, n, 0.004f);
        p[a + i] = s16(p[a + i] / 32000.0f + v);
    }
}

// Zoom motor: a whine with amplitude flutter.
Pcm motor(float dur, float hz, float vol, uint64_t seed) {
    int n = (int)(dur * SAMPLE_RATE);
    Pcm p(n);
    Rng r(seed);
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float flutter = 1.0f + 0.04f * sinf(TAU * 23.0f * t);
        float ph = hz * flutter * t;
        lp += (r.f01() - 0.5f - lp) * 0.08f;
        float v = (sinf(TAU * ph) * 0.5f + sinf(TAU * ph * 2.0f) * 0.3f + sinf(TAU * ph * 3.0f) * 0.12f) * 0.6f + lp * 1.5f;
        p[i] = s16(v * vol * edges(i, n, 0.03f));
    }
    return p;
}

Pcm raiseClip() {
    Pcm p = motor(0.16f, 310.0f, 0.22f, 11);
    addBeep(p, 0.17f, 0.07f, 2650.0f, 0.28f);
    return p;
}

Pcm lowerClip() {
    Pcm p;
    addBeep(p, 0.0f, 0.045f, 2650.0f, 0.24f);
    addBeep(p, 0.085f, 0.045f, 2650.0f, 0.24f);
    return p;
}

// A slide switch: two clicks a few milliseconds apart.
Pcm switchClip() {
    int n = (int)(0.05f * SAMPLE_RATE);
    Pcm p(n);
    Rng r(7);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float a = expf(-t * 900.0f) + 0.6f * (t > 0.012f ? expf(-(t - 0.012f) * 1100.0f) : 0.0f);
        p[i] = s16((r.f01() - 0.5f) * a * 0.9f + sinf(TAU * 1900.0f * t) * a * 0.25f);
    }
    return p;
}

// Transport: a clunk, then a rising motor tone.
Pcm transportClip() {
    int n = (int)(0.55f * SAMPLE_RATE);
    Pcm p(n);
    Rng r(23);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float clunk = (sinf(TAU * 62.0f * t) * 0.9f + (r.f01() - 0.5f) * 0.6f) * expf(-t * 22.0f);
        float tick = t > 0.09f ? (r.f01() - 0.5f) * expf(-(t - 0.09f) * 300.0f) * 0.8f : 0.0f;
        float spin = t > 0.12f ? sinf(TAU * (180.0f * t + 220.0f * t * t)) * 0.12f * smooth01(0.12f, 0.3f, t) : 0.0f;
        p[i] = s16((clunk + tick + spin) * edges(i, n, 0.08f));
    }
    return p;
}

// Tape hiss with mains hum, made to loop: the last LAP s are crossfaded into
// the start, so the restart has no seam.
Pcm hissClip() {
    const float LAP = 0.25f;
    int n = (int)(2.0f * SAMPLE_RATE), lap = (int)(LAP * SAMPLE_RATE);
    std::vector<float> raw(n + lap);
    Rng r(5);
    float lp = 0, hp = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float w = r.f01() - 0.5f;
        lp += (w - lp) * 0.35f;      // gentle low-pass on the hiss
        hp = lp - hp * 0.2f;
        raw[i] = hp * 0.7f + sinf(TAU * 60.0f * t) * 0.03f + sinf(TAU * 120.0f * t) * 0.015f;
    }
    Pcm p(n);
    for (int i = 0; i < n; i++) {
        float v = raw[i];
        if (i < lap) v = raw[i] * ((float)i / lap) + raw[n + i] * (1.0f - (float)i / lap);
        p[i] = s16(v);
    }
    return p;
}

// How close the hunter or a dog is, 0..1, ignoring walls. The tape shows it as
// interference before the player can see it.
float threatOf(const Sim &sim) {
    float th = 0;
    if (sim.ent.st == EState::Stalk || sim.ent.st == EState::Chase || sim.ent.st == EState::Flee)
        th = smooth01(30.0f, 4.0f, sim.entDist) * (sim.ent.st == EState::Chase ? 1.0f : 0.7f);
    for (const Dog &d : sim.dogs) {
        if (d.st == DState::Gone) continue;
        float dist = hypotf(d.x - sim.px, d.z - sim.pz);
        th = fmaxf(th, smooth01(24.0f, 3.0f, dist) * (d.st == DState::Charge ? 1.0f : 0.6f));
    }
    // Low sanity also adds interference.
    if (sim.sanity < 0.25f) th = fmaxf(th, (0.25f - sim.sanity) * 1.6f);
    return clampf(th, 0.0f, 1.0f);
}

// The OSD's draws, sized to the window like the HUD and held to the width
// too: it sits in all four corners, so a phone held upright runs out of width
// before height.
struct Osd {
    std::vector<HudDraw> &out;
    int sw, sh;

    int px(float v) const {
        float k = fminf(sh / 850.0f, sw / 620.0f);
        int r = (int)(v * k + 0.5f);
        return r < 1 ? 1 : r;
    }
    void raw(const std::string &t, int x, int y, int size, Rgba c) {
        HudDraw d{ HudDraw::Text };
        d.x = x; d.y = y; d.h = size; d.col = c; d.text = t;
        out.push_back(d);
    }
    void bare(int x, int y, int w, int h, Rgba c) {
        HudDraw d{ HudDraw::Rect };
        d.x = x; d.y = y; d.w = w; d.h = h; d.col = c;
        out.push_back(d);
    }
    // Text and bars carry a dark offset behind them, as the HUD's do.
    void text(const std::string &t, int x, int y, int size, Rgba c) {
        int o = px(2);
        raw(t, x + o, y + o, size, fade(BLACK_RGBA, c.a / 255.0f * 0.75f));
        raw(t, x, y, size, c);
    }
    void rect(int x, int y, int w, int h, Rgba c) {
        int o = px(2);
        bare(x + o, y + o, w, h, fade(BLACK_RGBA, c.a / 255.0f * 0.75f));
        bare(x, y, w, h, c);
    }
    void circle(int x, int y, float r, Rgba c) {
        HudDraw d{ HudDraw::Circle };
        d.x = x; d.y = y; d.r = r; d.col = c;
        out.push_back(d);
    }
    void play(int x, int y, float w, int h, Rgba c) {
        HudDraw d{ HudDraw::Triangle };
        d.x = x; d.y = y; d.h = h; d.r = w; d.col = c;
        out.push_back(d);
    }
};

std::string fmt(const char *f, int a, int b, int c) {
    char buf[64];
    snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

} // namespace

Pcm camcorderClip(CamSound sound) {
    switch (sound) {
    case CamSound::Raise: return raiseClip();
    case CamSound::Lower: return lowerClip();
    case CamSound::Switch: return switchClip();
    case CamSound::Zoom: return motor(0.3f, 440.0f, 0.16f, 31);
    case CamSound::Transport: return transportClip();
    case CamSound::Hiss: return hissClip();
    default: return {};
    }
}

void Camcorder::fitReel(int screenW, int screenH) {
    float aspect = screenH > 0 ? (float)screenW / screenH : 1.0f;
    if (aspect >= 1.0f) { cellW = CELL_LONG; cellH = std::max(48, (int)(CELL_LONG / aspect + 0.5f)); }
    else                { cellH = CELL_LONG; cellW = std::max(48, (int)(CELL_LONG * aspect + 0.5f)); }
    cols = REEL_SIDE / cellW;
    slots = std::min(MAX_FRAMES, cols * (REEL_SIDE / cellH));
    frames.assign(slots, Frame{});
    startTape();
}

void Camcorder::control(InputFrame &in, const Sim &sim, float dt, bool toggle) {
    bool allowed = !sim.inMenu && !sim.paused && sim.deathT <= 0 && sim.drinkT <= 0 && sim.winT <= 0;
    bool lowerFor = in.pickRevolver || in.pickFlare || in.pickDeck || in.drink || in.throwFlare;
    if (allowed && in.playing && toggle) {
        up = !up;
        play(up ? CamSound::Raise : CamSound::Lower);
    } else if (up && (!allowed || lowerFor)) {
        up = false;
        if (allowed) play(CamSound::Lower);
    }
    float step = dt / RAISE_S;
    raise = up ? fminf(1.0f, raise + step) : fmaxf(0.0f, raise - step);

    float target = 1.0f;
    if (up) {
        if (in.fire) { night = !night; play(CamSound::Switch); }
        zoomSet = clampf(zoomSet * powf(1.25f, in.wheel), 1.0f, ZOOM_MAX);
        // A mouse holds the zoom like it holds the sights; a touch AIM tap
        // is an edge (the sim drops the latch every tick the aim is refused).
        if (in.touch && in.aim && !zoomedLast) zoomSet = zoomSet > 1.5f ? 1.0f : 3.0f;
        target = (!in.touch && in.aim) ? ZOOM_MAX : zoomSet;
        zoomedLast = in.aim;
        in.fire = in.aim = in.reload = false;
        in.wheel = 0;
        in.look.x /= zoom; in.look.y /= zoom;
    } else zoomedLast = false;
    float before = zoom;
    zoom += (target - zoom) * fminf(1.0f, dt * 5.0f);
    if (fabsf(target - zoom) > 0.03f && fabsf(zoom - before) > 1e-4f && viewing())
        play(CamSound::Zoom);

    if (up && night) irCharge = fmaxf(0.0f, irCharge - dt / IR_DRAIN_S);
    else irCharge = fminf(1.0f, irCharge + dt / IR_RECHARGE_S);
}

float Camcorder::irLamp() const {
    return night && irCharge > 0.0f ? clampf(raise * 2.0f - 1.0f, 0.0f, 1.0f) : 0.0f;
}

float Camcorder::fovY(float baseDeg) const {
    if (zoom <= 1.0001f) return baseDeg;
    return 2.0f * atanf(tanf(baseDeg * DEG2RAD_F * 0.5f) / zoom) * RAD2DEG_F;
}

void Camcorder::startTape() {
    head = count = 0;
    tapeClock = nextGrab = 0;
    sounds.clear();
    up = false; raise = 0; zoom = zoomSet = 1; track = 0; glitch = 0;
    irCharge = 1;
}

int Camcorder::record(const Sim &sim, float dt, float runSeconds, bool reelReady,
                      const std::function<int(int, int)> &random) {
    float threat = threatOf(sim);
    glitch = fmaxf(0.0f, glitch - dt);
    if (glitch <= 0 && random(0, 9999) < (int)(threat * dt * 1.6f * 10000.0f))
        glitch = 0.1f + random(0, 100) / 400.0f;
    float want = fmaxf(threat, glitch > 0 ? 0.9f : 0.0f);
    track += (want - track) * fminf(1.0f, dt * (want > track ? 6.0f : 1.5f));

    tapeClock += dt;
    while (!sounds.empty() && count == slots && sounds.front().tapeT < frameAt(0).tapeT) sounds.pop_front();
    if (!reelReady || tapeClock < nextGrab) return -1;
    nextGrab = tapeClock + 1.0f / GRAB_HZ;
    int slot = head;
    frames[head] = { tapeClock, runSeconds, night && viewing(), track };
    head = (head + 1) % slots;
    if (count < slots) count++;
    return slot;
}

void Camcorder::logSounds(const std::vector<AudioEvent> &events) {
    for (const AudioEvent &e : events)
        if (e.kind == AudioEvent::PLAY) sounds.push_back({ tapeClock, e });
}

const Camcorder::Frame &Camcorder::frameAt(int age) const {
    return frames[(head - count + age + slots) % slots];
}

// raylib's y is down, a framebuffer's rows are up.
CamCell Camcorder::cellUV(int age) const {
    int slot = (head - count + age + slots) % slots;
    float W = (float)reelW(), H = (float)reelH();
    float x = (float)cellX(slot), y = (float)cellY(slot);
    return { x / W, 1.0f - (y + cellH) / H, (x + cellW) / W, 1.0f - y / H };
}

bool Camcorder::beginReplay() {
    replayOn = false;
    if (count < 8) return false;
    tapeFrom = frameAt(0).tapeT;
    // The tick that ended the run played sounds (the sting) after the last
    // frame; they play over the still frame.
    tapeTo = fmaxf(frameAt(count - 1).tapeT, tapeClock);
    slowFrom = tapeTo - fminf(SLOW_SPAN, (tapeTo - tapeFrom) * 0.3f);
    tapeFrom = fmaxf(tapeFrom, slowFrom - PLAY_S);
    freezeAt = STATIC_S + (slowFrom - tapeFrom) + (tapeTo - slowFrom) / SLOW_RATE;
    replayT = 0;
    lastTape = tapeFrom;
    nextSound = 0;
    while (nextSound < sounds.size() && sounds[nextSound].tapeT <= tapeFrom) nextSound++;
    up = false; raise = 0; zoom = zoomSet = 1;
    play(CamSound::Transport);
    replayOn = true;
    return true;
}

float Camcorder::tapeAt(float t) const {
    float atSpeed = slowFrom - tapeFrom;
    if (t <= atSpeed) return tapeFrom + fmaxf(t, 0.0f);
    return fminf(tapeTo, slowFrom + (t - atSpeed) * SLOW_RATE);
}

void Camcorder::stepReplay(float dt) {
    if (!replayOn) return;
    replayT += dt;
    play(CamSound::Hiss);
    if (replayT < STATIC_S) return;
    float tape = tapeAt(replayT - STATIC_S);
    float speed = tape < slowFrom ? 1.0f : SLOW_RATE;
    // Replay every sound from this stretch of tape, at the playback rate, so
    // the slowed last second is pitched down too.
    while (nextSound < sounds.size() && sounds[nextSound].tapeT <= tape) {
        if (sounds[nextSound].tapeT > lastTape)
            noises.push_back({ CamSound::Echo, sounds[nextSound].e, speed, ECHO_VOLUME });
        nextSound++;
    }
    lastTape = tape;
}

void Camcorder::stopReplay() {
    if (!replayOn) return;
    replayOn = false;
    play(CamSound::HissStop);
}

float Camcorder::cardAlpha() const {
    if (!replayOn) return 1.0f;
    return clampf((replayT - freezeAt) / 0.8f, 0.0f, 1.0f);
}

// The two frames either side of tape time `tape`, and how far between them.
Camcorder::Playhead Camcorder::playhead(float tape) const {
    int a = 0;
    while (a + 1 < count && frameAt(a + 1).tapeT <= tape) a++;
    int b = std::min(a + 1, count - 1);
    const Frame &fa = frameAt(a), &fb = frameAt(b);
    float mix = fb.tapeT > fa.tapeT ? clampf((tape - fa.tapeT) / (fb.tapeT - fa.tapeT), 0, 1) : 0;
    return { a, b, mix };
}

CamPicture Camcorder::viewfinderPicture() const {
    CamCell whole = { 0, 0, 1, 1 };
    return { false, whole, whole, 0, night ? 1.0f : 0.0f, track, 0, 0 };
}

CamPicture Camcorder::replayPicture() const {
    CamCell first = cellUV(0);
    if (replayT < STATIC_S) return { true, first, first, 0, 0, 1, 0, 1 };
    float tape = tapeAt(replayT - STATIC_S);
    Playhead p = playhead(tape);
    const Frame &fa = frameAt(p.a), &fb = frameAt(p.b);
    float mix = p.mix;
    bool still = replayT >= freezeAt;
    float slow = tape >= slowFrom ? 0.2f : 0.0f;
    float trk = clampf(fa.track + (fb.track - fa.track) * mix + slow + (still ? 0.25f : 0.0f), 0, 1);
    float snow = clampf(1.0f - (replayT - STATIC_S) / 0.18f, 0, 1);
    return { true, cellUV(p.a), cellUV(p.b), mix, fa.night ? 1.0f : 0.0f, trk, still ? 1.0f : 0.0f, snow };
}

std::vector<HudDraw> Camcorder::viewfinderOsd(int w, int h, float time, float runSeconds,
                                              const std::tm *clock, const HudMeasure &measure) const {
    std::vector<HudDraw> out;
    osd(out, w, h, time, runSeconds, false, false, clock, measure);
    return out;
}

std::vector<HudDraw> Camcorder::replayOsd(int w, int h, float time, const std::tm *clock,
                                          const HudMeasure &measure) const {
    std::vector<HudDraw> out;
    if (replayT < STATIC_S) {
        osd(out, w, h, time, frameAt(0).runT, true, false, clock, measure);
        return out;
    }
    Playhead p = playhead(tapeAt(replayT - STATIC_S));
    const Frame &fa = frameAt(p.a), &fb = frameAt(p.b);
    osd(out, w, h, time, fa.runT + (fb.runT - fa.runT) * p.mix, true, replayT >= freezeAt, clock, measure);
    return out;
}

void Camcorder::osd(std::vector<HudDraw> &out, int sw, int sh, float time, float counterS, bool playback,
                    bool still, const std::tm *clock, const HudMeasure &measure) const {
    Osd o{ out, sw, sh };
    int mx = (int)(sw * 0.065f), my = (int)(sh * 0.075f);
    int len = o.px(46), th = o.px(3), fs = o.px(30), small = o.px(22);
    Rgba ink = { 236, 236, 228, 230 };
    // the viewfinder's corner marks
    for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) {
        int x = cx ? sw - mx : mx, y = cy ? sh - my : my;
        int hx = cx ? x - len : x, vy = cy ? y - len : y;
        o.rect(hx, cy ? y - th : y, len, th, ink);
        o.rect(cx ? x - th : x, vy, th, len, ink);
    }
    int tx = mx + o.px(22), ty = my + o.px(20);
    bool blinkOn = fmodf(time, 1.0f) < 0.62f;
    if (!playback) {
        if (blinkOn) {
            o.circle(tx + fs / 3 + o.px(2), ty + fs / 3 + o.px(2), fs / 3.0f, fade(BLACK_RGBA, 0.6f));
            o.circle(tx + fs / 3, ty + fs / 3, fs / 3.0f, { 228, 36, 30, 245 });
        }
        o.text("REC", tx + fs, ty, fs, ink);
    } else if (still) {
        int bw = fs / 4;
        o.rect(tx, ty, bw, fs - o.px(4), ink);
        o.rect(tx + bw * 2, ty, bw, fs - o.px(4), ink);
        o.text("STILL", tx + fs, ty, fs, ink);
    } else {
        o.text("PLAY", tx, ty, fs, ink);
        int px0 = tx + measure("PLAY", fs) + o.px(12);
        o.play(px0, ty, fs * 0.7f, fs - o.px(4), ink);
    }
    if (!playback && night) {
        bool flat = irCharge <= 0.0f;
        if (!flat || blinkOn)
            o.text(flat ? "NIGHT SHOT  IR --" : "NIGHT SHOT", tx, ty + fs + o.px(10), small,
                   flat ? Rgba{ 236, 120, 100, 230 } : ink);
    }

    int c = (int)fmaxf(counterS, 0.0f);
    std::string counter = fmt("%d:%02d:%02d", c / 3600, c / 60 % 60, c % 60);
    o.text(counter, sw - mx - o.px(22) - measure(counter.c_str(), fs), ty, fs, ink);
    o.text("SP", sw - mx - o.px(22) - measure("SP", small), ty + fs + o.px(10), small, ink);

    // Today, on a tape from the 1990s.
    static const char *MONTHS[12] = { "JAN.", "FEB.", "MAR.", "APR.", "MAY", "JUN.",
                                      "JUL.", "AUG.", "SEP.", "OCT.", "NOV.", "DEC." };
    int by = sh - my - o.px(20) - fs;
    if (clock) {
        int h12 = clock->tm_hour % 12 == 0 ? 12 : clock->tm_hour % 12;
        char line[64];
        snprintf(line, sizeof line, "%s %d:%02d:%02d", clock->tm_hour < 12 ? "AM" : "PM", h12,
                 clock->tm_min, clock->tm_sec);
        o.text(line, tx, by - fs - o.px(8), fs, ink);
        snprintf(line, sizeof line, "%s %d 1996", MONTHS[clock->tm_mon], clock->tm_mday);
        o.text(line, tx, by, fs, ink);
    }
    if (playback) return;

    {   // the battery: three bars of night-shot charge
        int bw = o.px(52), bh = o.px(24), bx = sw - mx - o.px(22) - bw, bby = by + o.px(4);
        bool low = irCharge < 0.15f;
        if (!low || blinkOn) {
            Rgba bc = low ? Rgba{ 236, 120, 100, 230 } : ink;
            o.rect(bx, bby, bw, th, bc);
            o.rect(bx, bby + bh - th, bw, th, bc);
            o.rect(bx, bby, th, bh, bc);
            o.rect(bx + bw - th, bby, th, bh, bc);
            o.rect(bx + bw, bby + bh / 4, th * 2, bh / 2, bc);
            int bars = (int)ceilf(irCharge * 3.0f - 0.001f);
            int gap = o.px(3), seg = (bw - 2 * th - 4 * gap) / 3;
            for (int i = 0; i < bars; i++)
                o.bare(bx + th + gap + i * (seg + gap), bby + th + gap, seg, bh - 2 * th - 2 * gap, bc);
        }
    }
    if (zoom > 1.03f || zoomSet > 1.03f) {   // W ---|--- T
        int zw = o.px(220), zx = sw / 2 - zw / 2, zy = my + o.px(26);
        o.text("W", zx - o.px(26), zy - o.px(8), small, ink);
        o.text("T", zx + zw + o.px(10), zy - o.px(8), small, ink);
        o.rect(zx, zy, zw, th, ink);
        int k = zx + (int)((zoom - 1.0f) / (ZOOM_MAX - 1.0f) * (zw - th));
        o.rect(k, zy - o.px(9), th * 2, o.px(18) + th, ink);
    }
}
