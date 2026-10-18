#include "camcorder.h"
#include "game_audio.h"
#include "input.h"
#include "sfx.h"
#include "shaders.h"
#include "sim/sim.h"
#include "core/hash.h"
#include "core/vec.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <ctime>

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
constexpr float ECHO_VOLUME = 0.7f;   // a sound off the tape, against the room
constexpr float HISS_VOLUME = 0.22f;

float smooth01(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Sized to the window like the HUD (render.cpp hud()), and held to the width
// too: the OSD sits in all four corners, so a phone held upright runs out of
// width before height.
int osd(float px) {
    float k = fminf(GetScreenHeight() / 850.0f, GetScreenWidth() / 620.0f);
    int v = (int)(px * k + 0.5f);
    return v < 1 ? 1 : v;
}

void osdText(const char *t, int x, int y, int px, Color c) {
    int o = osd(2);
    DrawText(t, x + o, y + o, px, Fade(BLACK, c.a / 255.0f * 0.75f));
    DrawText(t, x, y, px, c);
}

void osdRect(int x, int y, int w, int h, Color c) {
    int o = osd(2);
    DrawRectangle(x + o, y + o, w, h, Fade(BLACK, c.a / 255.0f * 0.75f));
    DrawRectangle(x, y, w, h, c);
}

// ---- sounds, synthesized like the rest of the game's (port/sounds.cpp)

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

// The lens motor: a whine with the gear train's flutter on it.
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

// The deck taking the tape: a clunk as the heads load, then the capstan
// spinning up under it.
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

// Tape hiss with the deck's mains hum under it, built to loop: the last
// LAP s are crossfaded into the start, so the restart has no seam.
Pcm hissClip() {
    const float LAP = 0.25f;
    int n = (int)(2.0f * SAMPLE_RATE), lap = (int)(LAP * SAMPLE_RATE);
    std::vector<float> raw(n + lap);
    Rng r(5);
    float lp = 0, hp = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        float t = (float)i / SAMPLE_RATE;
        float w = r.f01() - 0.5f;
        lp += (w - lp) * 0.35f;      // hiss sits high, but not at the very top
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

// How close something that hunts is, 0..1, through walls or not: the tape
// picks it up as interference before you can see it.
float threatOf(const Sim &sim) {
    float th = 0;
    if (sim.ent.st == EState::Stalk || sim.ent.st == EState::Chase || sim.ent.st == EState::Flee)
        th = smooth01(30.0f, 4.0f, sim.entDist) * (sim.ent.st == EState::Chase ? 1.0f : 0.7f);
    for (const Dog &d : sim.dogs) {
        if (d.st == DState::Gone) continue;
        float dist = hypotf(d.x - sim.px, d.z - sim.pz);
        th = fmaxf(th, smooth01(24.0f, 3.0f, dist) * (d.st == DState::Charge ? 1.0f : 0.6f));
    }
    // The last of your sanity comes through as noise too.
    if (sim.sanity < 0.25f) th = fmaxf(th, (0.25f - sim.sanity) * 1.6f);
    return clampf(th, 0.0f, 1.0f);
}

} // namespace

void Camcorder::load() {
    shader = LoadShaderFromMemory(NULL, TAPE_FS);
    if (shader.id == rlGetShaderIdDefault())
        TraceLog(LOG_ERROR, "tape shader failed to compile - see the SHADER lines above");
    locTime = GetShaderLocation(shader, "uTime");
    locRes = GetShaderLocation(shader, "uRes");
    locCellA = GetShaderLocation(shader, "uCellA");
    locCellB = GetShaderLocation(shader, "uCellB");
    locMix = GetShaderLocation(shader, "uMix");
    locNight = GetShaderLocation(shader, "uNight");
    locTrack = GetShaderLocation(shader, "uTrack");
    locStill = GetShaderLocation(shader, "uStill");
    locSnow = GetShaderLocation(shader, "uSnow");
    sndRaise = loadPcmSound(raiseClip());
    sndLower = loadPcmSound(lowerClip());
    sndSwitch = loadPcmSound(switchClip());
    sndZoom = loadPcmSound(motor(0.3f, 440.0f, 0.16f, 31));
    sndTransport = loadPcmSound(transportClip());
    sndHiss = loadPcmSound(hissClip());
    SetSoundVolume(sndHiss, HISS_VOLUME);
}

void Camcorder::unload() {
    if (reel.id) UnloadRenderTexture(reel);
    reel = {};
}

void Camcorder::fitReel(int screenW, int screenH) {
    unload();
    float aspect = screenH > 0 ? (float)screenW / screenH : 1.0f;
    if (aspect >= 1.0f) { cellW = CELL_LONG; cellH = std::max(48, (int)(CELL_LONG / aspect + 0.5f)); }
    else                { cellH = CELL_LONG; cellW = std::max(48, (int)(CELL_LONG * aspect + 0.5f)); }
    cols = REEL_SIDE / cellW;
    slots = std::min(MAX_FRAMES, cols * (REEL_SIDE / cellH));
    int rows = (slots + cols - 1) / cols;
    reel = LoadRenderTexture(cols * cellW, rows * cellH);
    if (reel.id) SetTextureFilter(reel.texture, TEXTURE_FILTER_BILINEAR);
    frames.assign(slots, Frame{});
    startTape();
}

void Camcorder::control(InputFrame &in, const Sim &sim, float dt) {
    bool allowed = !sim.inMenu && !sim.paused && sim.deathT <= 0 && sim.drinkT <= 0 && sim.winT <= 0;
    bool lowerFor = in.pickRevolver || in.pickFlare || in.pickDeck || in.drink || in.throwFlare;
    if (allowed && in.playing && inKeyPressed(KEY_V)) {
        up = !up;
        PlaySound(up ? sndRaise : sndLower);
    } else if (up && (!allowed || lowerFor)) {
        up = false;
        if (allowed) PlaySound(sndLower);
    }
    webLightCam(up);
    float step = dt / RAISE_S;
    raise = up ? fminf(1.0f, raise + step) : fmaxf(0.0f, raise - step);

    float target = 1.0f;
    if (up) {
        if (in.fire) { night = !night; PlaySound(sndSwitch); }
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
    if (fabsf(target - zoom) > 0.03f && fabsf(zoom - before) > 1e-4f && viewing() && !IsSoundPlaying(sndZoom))
        PlaySound(sndZoom);

    if (up && night) irCharge = fmaxf(0.0f, irCharge - dt / IR_DRAIN_S);
    else irCharge = fminf(1.0f, irCharge + dt / IR_RECHARGE_S);
}

float Camcorder::irLamp() const {
    return night && irCharge > 0.0f ? clampf(raise * 2.0f - 1.0f, 0.0f, 1.0f) : 0.0f;
}

float Camcorder::fovY(float baseDeg) const {
    if (zoom <= 1.0001f) return baseDeg;
    return 2.0f * atanf(tanf(baseDeg * DEG2RAD * 0.5f) / zoom) * RAD2DEG;
}

void Camcorder::startTape() {
    head = count = 0;
    tapeClock = nextGrab = 0;
    sounds.clear();
    up = false; raise = 0; zoom = zoomSet = 1; track = 0; glitch = 0;
    irCharge = 1;
}

void Camcorder::record(const RenderTexture2D &rt, const Sim &sim, float dt, float runSeconds) {
    float threat = threatOf(sim);
    glitch = fmaxf(0.0f, glitch - dt);
    if (glitch <= 0 && GetRandomValue(0, 9999) < (int)(threat * dt * 1.6f * 10000.0f))
        glitch = 0.1f + GetRandomValue(0, 100) / 400.0f;
    float want = fmaxf(threat, glitch > 0 ? 0.9f : 0.0f);
    track += (want - track) * fminf(1.0f, dt * (want > track ? 6.0f : 1.5f));

    tapeClock += dt;
    while (!sounds.empty() && count == slots && sounds.front().tapeT < frameAt(0).tapeT) sounds.pop_front();
    if (!reel.id || tapeClock < nextGrab) return;
    nextGrab = tapeClock + 1.0f / GRAB_HZ;
    float x = (float)(head % cols * cellW), y = (float)(head / cols * cellH);
    BeginTextureMode(reel);
    // Opaque first: the scene target carries alpha (glass, water), and the
    // post pass draws it over black.
    DrawRectangle((int)x, (int)y, cellW, cellH, BLACK);
    DrawTexturePro(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height },
                   { x, y, (float)cellW, (float)cellH }, { 0, 0 }, 0, WHITE);
    EndTextureMode();
    frames[head] = { tapeClock, runSeconds, night && viewing(), track };
    head = (head + 1) % slots;
    if (count < slots) count++;
}

void Camcorder::logSounds(const std::vector<AudioEvent> &events) {
    for (const AudioEvent &e : events)
        if (e.kind == AudioEvent::PLAY) sounds.push_back({ tapeClock, e });
}

const Camcorder::Frame &Camcorder::frameAt(int age) const {
    return frames[(head - count + age + slots) % slots];
}

// The frame's rectangle on the reel as uv (min.xy, max.zw), with v running up
// the picture: raylib's y is down, a framebuffer's rows are up.
Vector4 Camcorder::cellUV(int age) const {
    int slot = (head - count + age + slots) % slots;
    float W = (float)reel.texture.width, H = (float)reel.texture.height;
    float x = (float)(slot % cols * cellW), y = (float)(slot / cols * cellH);
    return { x / W, 1.0f - (y + cellH) / H, (x + cellW) / W, 1.0f - y / H };
}

bool Camcorder::beginReplay() {
    replayOn = false;
    if (count < 8) return false;
    tapeFrom = frameAt(0).tapeT;
    // The tick that ended the run made sounds (the sting) after the last
    // frame; they land on the still.
    tapeTo = fmaxf(frameAt(count - 1).tapeT, tapeClock);
    slowFrom = tapeTo - fminf(SLOW_SPAN, (tapeTo - tapeFrom) * 0.3f);
    tapeFrom = fmaxf(tapeFrom, slowFrom - PLAY_S);
    freezeAt = STATIC_S + (slowFrom - tapeFrom) + (tapeTo - slowFrom) / SLOW_RATE;
    replayT = 0;
    lastTape = tapeFrom;
    nextSound = 0;
    while (nextSound < sounds.size() && sounds[nextSound].tapeT <= tapeFrom) nextSound++;
    up = false; raise = 0; zoom = zoomSet = 1;
    webLightCam(false);
    PlaySound(sndTransport);
    replayOn = true;
    return true;
}

float Camcorder::tapeAt(float t) const {
    float atSpeed = slowFrom - tapeFrom;
    if (t <= atSpeed) return tapeFrom + fmaxf(t, 0.0f);
    return fminf(tapeTo, slowFrom + (t - atSpeed) * SLOW_RATE);
}

void Camcorder::stepReplay(float dt, GameAudio &audio) {
    if (!replayOn) return;
    replayT += dt;
    if (!IsSoundPlaying(sndHiss)) PlaySound(sndHiss);
    if (replayT < STATIC_S) return;
    float tape = tapeAt(replayT - STATIC_S);
    float speed = tape < slowFrom ? 1.0f : SLOW_RATE;
    // Every sound the run made in this stretch, off the tape: slowed with the
    // picture, so the last second drags down in pitch with it.
    while (nextSound < sounds.size() && sounds[nextSound].tapeT <= tape) {
        if (sounds[nextSound].tapeT > lastTape) audio.echo(sounds[nextSound].e, speed, ECHO_VOLUME);
        nextSound++;
    }
    lastTape = tape;
}

void Camcorder::stopReplay() {
    if (!replayOn) return;
    replayOn = false;
    StopSound(sndHiss);
}

float Camcorder::cardAlpha() const {
    if (!replayOn) return 1.0f;
    return clampf((replayT - freezeAt) / 0.8f, 0.0f, 1.0f);
}

void Camcorder::present(Texture2D tex, Vector4 cellA, Vector4 cellB, float mix, float time,
                        float nightAmt, float trackAmt, float still, float snow) {
    Vector2 res = { (float)GetRenderWidth(), (float)GetRenderHeight() };
    SetShaderValue(shader, locTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locRes, &res, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, locCellA, &cellA, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locCellB, &cellB, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, locMix, &mix, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locNight, &nightAmt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locTrack, &trackAmt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locStill, &still, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locSnow, &snow, SHADER_UNIFORM_FLOAT);
    // The shader works out its own coordinates from the fragment's position,
    // so the quad only has to cover the screen with `tex` bound.
    BeginShaderMode(shader);
    DrawTexturePro(tex, { 0, 0, (float)tex.width, (float)tex.height },
                   { 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, { 0, 0 }, 0, WHITE);
    EndShaderMode();
}

void Camcorder::drawViewfinder(const RenderTexture2D &rt, float time, float runSeconds) {
    Vector4 whole = { 0, 0, 1, 1 };
    present(rt.texture, whole, whole, 0, time, night ? 1.0f : 0.0f, track, 0, 0);
    drawOsd(time, runSeconds, false, false);
}

void Camcorder::drawReplay(float time) {
    if (!replayOn) return;
    Vector4 first = cellUV(0);
    if (replayT < STATIC_S) {
        present(reel.texture, first, first, 0, time, 0, 1, 0, 1);
        drawOsd(time, frameAt(0).runT, true, false);
        return;
    }
    float tape = tapeAt(replayT - STATIC_S);
    int a = 0;
    while (a + 1 < count && frameAt(a + 1).tapeT <= tape) a++;
    int b = std::min(a + 1, count - 1);
    const Frame &fa = frameAt(a), &fb = frameAt(b);
    float mix = fb.tapeT > fa.tapeT ? clampf((tape - fa.tapeT) / (fb.tapeT - fa.tapeT), 0, 1) : 0;
    bool still = replayT >= freezeAt;
    float slow = tape >= slowFrom ? 0.2f : 0.0f;
    float trk = clampf(fa.track + (fb.track - fa.track) * mix + slow + (still ? 0.25f : 0.0f), 0, 1);
    float snow = clampf(1.0f - (replayT - STATIC_S) / 0.18f, 0, 1);
    present(reel.texture, cellUV(a), cellUV(b), mix, time, fa.night ? 1.0f : 0.0f, trk, still ? 1.0f : 0.0f, snow);
    drawOsd(time, fa.runT + (fb.runT - fa.runT) * mix, true, still);
}

void Camcorder::drawOsd(float time, float counterS, bool playback, bool still) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int mx = (int)(sw * 0.065f), my = (int)(sh * 0.075f);
    int len = osd(46), th = osd(3), fs = osd(30), small = osd(22);
    Color ink = { 236, 236, 228, 230 };
    // the viewfinder's corner marks
    for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) {
        int x = cx ? sw - mx : mx, y = cy ? sh - my : my;
        int hx = cx ? x - len : x, vy = cy ? y - len : y;
        osdRect(hx, cy ? y - th : y, len, th, ink);
        osdRect(cx ? x - th : x, vy, th, len, ink);
    }
    int tx = mx + osd(22), ty = my + osd(20);
    bool blinkOn = fmodf(time, 1.0f) < 0.62f;
    if (!playback) {
        if (blinkOn) {
            DrawCircle(tx + fs / 3 + osd(2), ty + fs / 3 + osd(2), fs / 3.0f, Fade(BLACK, 0.6f));
            DrawCircle(tx + fs / 3, ty + fs / 3, fs / 3.0f, { 228, 36, 30, 245 });
        }
        osdText("REC", tx + fs, ty, fs, ink);
    } else if (still) {
        int bw = fs / 4;
        osdRect(tx, ty, bw, fs - osd(4), ink);
        osdRect(tx + bw * 2, ty, bw, fs - osd(4), ink);
        osdText("STILL", tx + fs, ty, fs, ink);
    } else {
        osdText("PLAY", tx, ty, fs, ink);
        int px0 = tx + MeasureText("PLAY", fs) + osd(12);
        DrawTriangle({ (float)px0, (float)ty }, { (float)px0, (float)(ty + fs - osd(4)) },
                     { (float)(px0 + fs * 0.7f), (float)ty + (fs - osd(4)) * 0.5f }, ink);
    }
    if (!playback && night) {
        bool flat = irCharge <= 0.0f;
        if (!flat || blinkOn)
            osdText(flat ? "NIGHT SHOT  IR --" : "NIGHT SHOT", tx, ty + fs + osd(10), small,
                    flat ? Color{ 236, 120, 100, 230 } : ink);
    }

    int c = (int)fmaxf(counterS, 0.0f);
    const char *counter = TextFormat("%d:%02d:%02d", c / 3600, c / 60 % 60, c % 60);
    osdText(counter, sw - mx - osd(22) - MeasureText(counter, fs), ty, fs, ink);
    osdText("SP", sw - mx - osd(22) - MeasureText("SP", small), ty + fs + osd(10), small, ink);

    // Today, on a tape from the 1990s.
    static const char *MONTHS[12] = { "JAN.", "FEB.", "MAR.", "APR.", "MAY", "JUN.",
                                      "JUL.", "AUG.", "SEP.", "OCT.", "NOV.", "DEC." };
    time_t now = ::time(nullptr);
    const tm *lt = localtime(&now);
    int by = sh - my - osd(20) - fs;
    if (lt) {
        int h12 = lt->tm_hour % 12 == 0 ? 12 : lt->tm_hour % 12;
        osdText(TextFormat("%s %d:%02d:%02d", lt->tm_hour < 12 ? "AM" : "PM", h12, lt->tm_min, lt->tm_sec),
                tx, by - fs - osd(8), fs, ink);
        osdText(TextFormat("%s %d 1996", MONTHS[lt->tm_mon], lt->tm_mday), tx, by, fs, ink);
    }
    if (playback) return;

    {   // the battery: three bars of night-shot charge
        int bw = osd(52), bh = osd(24), bx = sw - mx - osd(22) - bw, bby = by + osd(4);
        bool low = irCharge < 0.15f;
        if (!low || blinkOn) {
            Color bc = low ? Color{ 236, 120, 100, 230 } : ink;
            osdRect(bx, bby, bw, th, bc);
            osdRect(bx, bby + bh - th, bw, th, bc);
            osdRect(bx, bby, th, bh, bc);
            osdRect(bx + bw - th, bby, th, bh, bc);
            osdRect(bx + bw, bby + bh / 4, th * 2, bh / 2, bc);
            int bars = (int)ceilf(irCharge * 3.0f - 0.001f);
            int gap = osd(3), seg = (bw - 2 * th - 4 * gap) / 3;
            for (int i = 0; i < bars; i++)
                DrawRectangle(bx + th + gap + i * (seg + gap), bby + th + gap, seg, bh - 2 * th - 2 * gap, bc);
        }
    }
    if (zoom > 1.03f || zoomSet > 1.03f) {   // W ---|--- T
        int zw = osd(220), zx = sw / 2 - zw / 2, zy = my + osd(26);
        osdText("W", zx - osd(26), zy - osd(8), small, ink);
        osdText("T", zx + zw + osd(10), zy - osd(8), small, ink);
        osdRect(zx, zy, zw, th, ink);
        int k = zx + (int)((zoom - 1.0f) / (ZOOM_MAX - 1.0f) * (zw - th));
        osdRect(k, zy - osd(9), th * 2, osd(18) + th, ink);
    }
}
