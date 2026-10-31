#include "hud.h"
#include "levels.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {

constexpr Rgba BLACK_RGBA = { 0, 0, 0, 255 };

// raylib's Fade: the colour at a clamped share of full alpha.
Rgba fade(Rgba c, float a) {
    a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
    return { c.r, c.g, c.b, (uint8_t)(255.0f * a) };
}

std::string fmt(const char *f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

struct Hud {
    const Sim &sim;
    const HudInput &in;
    const HudMeasure &measure;
    std::vector<HudDraw> out;
    int sw, sh;

    // Every HUD size was tuned for an 850 px high window; this scales it to the
    // current window height. Fractions of the screen (sh / 2, sw / 3) are
    // already resolution-independent and must not be scaled.
    int hud(float px) const {
        int v = (int)(px * sh / 850.0f + 0.5f);
        return v < 1 ? 1 : v;
    }

    void raw(const std::string &t, int x, int y, int px, Rgba c) {
        HudDraw d{ HudDraw::Text };
        d.x = x; d.y = y; d.h = px; d.col = c; d.text = t;
        out.push_back(d);
    }
    void box(HudDraw::Kind k, int x, int y, int w, int h, Rgba c, Rgba c2 = {}) {
        HudDraw d{ k };
        d.x = x; d.y = y; d.w = w; d.h = h; d.col = c; d.col2 = c2;
        out.push_back(d);
    }
    void rect(int x, int y, int w, int h, Rgba c) { box(HudDraw::Rect, x, y, w, h, c); }

    // A dark offset copy behind every string, scaled with the text and using its
    // alpha, so a fading line fades its shadow too. Without it the inventory is
    // unreadable over the Poolrooms' white tile.
    void text(const std::string &t, int x, int y, int px, Rgba c) {
        int o = hud(1);
        raw(t, x + o, y + o, px, fade(BLACK_RGBA, c.a / 255.0f * 0.7f));
        raw(t, x, y, px, c);
    }

    // Shrink a centred line that does not fit in 92% of the screen width rather
    // than clip it (a portrait phone is narrow). Text width is close to linear
    // in size, so one proportional step is within a pixel or two; the loop only
    // rounds down.
    int fitSize(const std::string &t, int px, int maxW) const {
        if (px < 1) px = 1;
        if (maxW <= 0) return px;
        int w = measure(t.c_str(), px);
        if (w <= maxW) return px;
        px = px * maxW / (w > 0 ? w : 1);
        while (px > 1 && measure(t.c_str(), px) > maxW) px--;
        return px < 1 ? 1 : px;
    }
    int fitW() const { return (int)(sw * 0.92f); }

    // Centred on cx, measured at the size it is drawn at.
    void textC(const std::string &t, int cx, int y, int px, Rgba c) {
        px = fitSize(t, px, fitW());
        text(t, cx - measure(t.c_str(), px) / 2, y, px, c);
    }
    // Its right edge on rx.
    void textR(const std::string &t, int rx, int y, int px, Rgba c) {
        text(t, rx - measure(t.c_str(), px), y, px, c);
    }

    void deathCard(float timeF);
    void titleCard(float timeF);
    void play(double now, double elapsed, bool &everFlashed);
};

void Hud::deathCard(float timeF) {
    float a = clampf(sim.deathT > Sim::DEATH_CARD - 0.6f ? (Sim::DEATH_CARD - sim.deathT) / 0.6f : sim.deathT / 1.0f, 0, 1);
    if (in.replaying) {
        // The tape plays first (the platform draws it), then the text fades in
        // over its last frame.
        a = fminf(clampf(sim.deathT, 0, 1), in.cardAlpha);
        rect(0, 0, sw, sh, fade({ 10, 3, 3, 255 }, 0.66f * a));
        if (sim.deathT < 1.0f) rect(0, 0, sw, sh, fade(BLACK_RGBA, 1.0f - sim.deathT));
    } else rect(0, 0, sw, sh, fade({ 10, 3, 3, 255 }, 0.88f));
    textC(sim.deathTitle, sw / 2, sh / 3, hud(54), fade({ 178, 34, 24, 255 }, a));
    textC(fmt("%s took you on %s", sim.deathBy, LEVELS[sim.deathLevel].name), sw / 2, sh / 3 + hud(74), hud(20),
          fade({ 176, 132, 122, 255 }, a * 0.95f));
    textC(fmt("%02d:%02d   ·   %d m wandered   ·   %d hunter%s put down", (int)sim.deathTime / 60,
              (int)sim.deathTime % 60, sim.deathM, sim.deathKills, sim.deathKills == 1 ? "" : "s"),
          sw / 2, sh / 3 + hud(106), hud(18), fade({ 150, 118, 110, 255 }, a * 0.9f));
    textC(fmt("run %d   ·   deepest ever  %s   ·   longest run  %02d:%02d", sim.deathCount,
              LEVELS[sim.best.deepest].name, sim.best.longestRun / 60, sim.best.longestRun % 60),
          sw / 2, sh / 3 + hud(140), hud(16), fade({ 130, 106, 100, 255 }, a * 0.8f));
    if (sim.deathT < Sim::DEATH_CARD - Sim::DEATH_CARD_HOLD)
        textC(in.touch ? "tap anywhere to descend again" : "press any key to descend again", sw / 2,
              sh * 2 / 3 + hud(30), hud(18),
              fade({ 168, 144, 112, 255 }, a * (0.45f + 0.55f * (0.5f + 0.5f * sinf(timeF * 3.0f)))));
}

void Hud::titleCard(float timeF) {
    // Darken the top and bottom so the text is readable over any background.
    box(HudDraw::GradientV, 0, 0, sw, sh / 2, fade(BLACK_RGBA, 0.62f), fade(BLACK_RGBA, 0.12f));
    box(HudDraw::GradientV, 0, sh / 2, sw, sh / 2, fade(BLACK_RGBA, 0.12f), fade(BLACK_RGBA, 0.72f));
    const std::string t1 = "T H E   B A C K R O O M S";
    // A slight flicker on the title.
    float fl = 0.86f + 0.14f * sinf(timeF * 27.0f) * sinf(timeF * 41.3f + 0.7f);
    int ts = fitSize(t1, hud(62), fitW());
    int tw = measure(t1.c_str(), ts);
    raw(t1, sw / 2 - tw / 2 + hud(2), sh / 3 + hud(2), ts, fade(BLACK_RGBA, 0.55f));   // drop shadow
    raw(t1, sw / 2 - tw / 2, sh / 3, ts, fade({ 228, 214, 158, 255 }, fl));
    textC("and everything under it", sw / 2, sh / 3 + ts + hud(16), hud(20), { 150, 142, 108, 220 });
    float pl = 0.45f + 0.55f * (0.5f + 0.5f * sinf(timeF * 3.0f));
    // On touch, the prompt names a tap, not a key.
    textC(in.touch ? "tap anywhere to descend" : "press any key to descend", sw / 2, sh * 2 / 3, hud(24),
          fade({ 210, 198, 150, 255 }, pl));
    const Records &b = sim.best;
    if (b.escapes || b.kills || b.metres || b.wins) {
        std::string tb = b.tapes > 0
            ? fmt("best:  %d got out   ·   %d hunter%s put down   ·   %d m wandered   ·   %d tape%s found", b.wins,
                  b.kills, b.kills == 1 ? "" : "s", b.metres, b.tapes, b.tapes == 1 ? "" : "s")
            : fmt("best:  %d got out   ·   %d hunter%s put down   ·   %d m wandered", b.wins, b.kills,
                  b.kills == 1 ? "" : "s", b.metres);
        textC(tb, sw / 2, sh * 2 / 3 + hud(40), hud(16), { 140, 132, 100, 200 });
    }
    if (b.deepest > 0 || b.longestRun > 0)
        textC(fmt("deepest  %s   ·   longest run  %02d:%02d", LEVELS[b.deepest].name, b.longestRun / 60,
                  b.longestRun % 60),
              sw / 2, sh * 2 / 3 + hud(62), hud(16), { 140, 132, 100, 200 });
    std::string tc = in.touch
        ? fmt("STICK move    AIM taps the sights up and down    LAMP torch    ITEM cycle    bank %d doubloons to leave",
              Sim::ESCAPE_COST)
        : fmt("WASD move    SHIFT run    C squeeze    F flashlight    1/2/4 item    bank %d doubloons to leave",
              Sim::ESCAPE_COST);
    textC(tc, sw / 2, sh - hud(42), hud(15), { 128, 122, 96, 170 });
}

void Hud::play(double now, double elapsed, bool &everFlashed) {
    if (sim.drinkT <= 0 && (sim.weapon == WEAPON_REVOLVER || (sim.weapon == WEAPON_FLARE && sim.flares > 0))) {
        // The crosshair dot fades as the sights come up.
        HudDraw d{ HudDraw::Circle };
        d.x = sw / 2; d.y = sh / 2; d.r = (float)hud(1.5f); d.col = fade({ 230, 220, 190, 110 }, 1 - sim.aimBlend);
        out.push_back(d);
    }

    if (elapsed < 9.0 && sim.winT <= 0) {   // the intro, held back while the escape screen is up
        float a = 1.0f - clampf((float)elapsed / 3.0f, 0, 1);
        rect(0, 0, sw, sh, fade(BLACK_RGBA, a));
        float ta = clampf((float)elapsed / 1.5f, 0, 1) * (1.0f - clampf(((float)elapsed - 6.0f) / 3.0f, 0, 1));
        char t1[64];   // the level's name, letter-spaced
        int ti = 0;
        for (const char *p = LEVELS[sim.level].name; *p && ti < 60; p++) {
            t1[ti++] = *p;
            // A UTF-8 character is several bytes (Level 0's "·" is two), and a
            // space pushed between them draws as two '?': space characters,
            // not bytes.
            if (((unsigned char)p[1] & 0xC0) == 0x80) continue;
            t1[ti++] = ' ';
            if (*p == ' ') t1[ti++] = ' ';
        }
        t1[ti ? ti - 1 : 0] = 0;
        textC(t1, sw / 2, sh / 3, hud(52), fade({ 220, 205, 150, 255 }, ta));
        textC("if you're reading this, you've already noclipped", sw / 2, sh / 3 + hud(66), hud(18),
              fade({ 160, 150, 110, 255 }, ta * 0.9f));
        const char *t3 = in.touch
            ? "STICK walk   push past its ring to run   DUCK crouch   SQUEEZE   JUMP   LAMP torch   ITEM cycle   DRINK   MARK chalk   USE vend/pick up"
            : "WASD walk   SHIFT or W W run   CTRL crouch   C squeeze   SPACE jump   F flashlight   1/2/4 item   3 drink   M chalk   E vend/pick up";
        textC(t3, sw / 2, sh - hud(60), hud(16), fade({ 140, 132, 100, 255 }, ta * 0.8f));
        const Records &b = sim.best;
        if (b.escapes || b.kills || b.metres || b.wins) {
            std::string tb = b.tapes > 0
                ? fmt("best: %d got out  ·  %d hunter%s put down  ·  %d m wandered  ·  %d tape%s found", b.wins,
                      b.kills, b.kills == 1 ? "" : "s", b.metres, b.tapes, b.tapes == 1 ? "" : "s")
                : fmt("best: %d got out  ·  %d hunter%s put down  ·  %d m wandered", b.wins, b.kills,
                      b.kills == 1 ? "" : "s", b.metres);
            textC(tb, sw / 2, sh / 3 + hud(98), hud(16), fade({ 150, 140, 105, 255 }, ta * 0.8f));
        }
        textC(fmt("bank %d doubloons · fight what hunts you for them · then take a door out", Sim::ESCAPE_COST),
              sw / 2, sh / 3 + hud(128), hud(16), fade({ 120, 200, 140, 255 }, ta * 0.75f));
    }
    if (sim.winT > 0) {   // escaped
        float a = clampf(sim.winT > 7.2f ? (8.0f - sim.winT) / 0.8f : sim.winT / 7.2f, 0, 1);
        rect(0, 0, sw, sh, fade({ 6, 12, 8, 255 }, a * 0.93f));
        textC("YOU ESCAPED THE BACKROOMS", sw / 2, sh / 3, hud(54), fade({ 120, 235, 145, 255 }, a));
        textC(fmt("out the true door   ·   %02d:%02d   ·   %d m wandered   ·   %d hunter%s put down",
                  (int)sim.winTime / 60, (int)sim.winTime % 60, sim.winM, sim.winKills, sim.winKills == 1 ? "" : "s"),
              sw / 2, sh / 3 + hud(74), hud(20), fade({ 150, 200, 160, 255 }, a * 0.95f));
        textC(fmt("escape #%d   ·   best %d", sim.winCount, sim.best.wins), sw / 2, sh / 3 + hud(106), hud(18),
              fade({ 130, 175, 140, 255 }, a * 0.9f));
        textC("...but the backrooms are patient. a new descent begins.", sw / 2, sh - hud(78), hud(16),
              fade({ 120, 150, 125, 255 }, a * 0.8f));
    }
    if (sim.fellT > 0) {   // fell through a rotten patch
        float a = clampf(sim.fellT / 4.0f, 0, 1);
        rect(0, 0, sw, sh, fade(BLACK_RGBA, a * 0.5f * clampf((sim.fellT - 3.4f) / 0.6f, 0, 1)));
        textC("THE FLOOR GIVES WAY", sw / 2, sh / 2 - hud(30), hud(46), fade({ 200, 180, 120, 255 }, a));
        textC("...there was another floor under this one", sw / 2, sh / 2 + hud(30), hud(18),
              fade({ 150, 138, 110, 255 }, a * 0.9f));
    }
    if (sim.escapeT > 0) {
        float a = clampf(sim.escapeT > 5.4f ? (6.0f - sim.escapeT) / 0.6f : sim.escapeT / 5.4f, 0, 1);
        rect(0, 0, sw, sh, fade(RGBA_WHITE, a * (sim.escapeT > 5.4f ? 0.9f : 0.12f)));
        textC("YOU FOUND AN EXIT", sw / 2, sh / 2 - hud(60), hud(48), fade({ 235, 228, 200, 255 }, a));
        textC(fmt("...it leads to %s.  %d m wandered  ·  %d escape%s  ·  %s", LEVELS[sim.level].name,
                  (int)sim.distWalked, sim.escapeCount, sim.escapeCount == 1 ? "" : "s",
                  fmt("%02d:%02d", (int)elapsed / 60, (int)elapsed % 60).c_str()),
              sw / 2, sh / 2 + hud(4), hud(18), fade({ 180, 170, 140, 255 }, a));
    }
    if (sim.killT > 0) {
        float a = clampf(sim.killT / 3.0f, 0, 1);
        const char *t = (sim.level == 4) ? "THE PARTYGOER IS DOWN" : sim.clarkLevel() ? "PIRATE CLARK IS DOWN" : "THE SMILER IS DOWN";
        textC(t, sw / 2, sh / 2 - hud(96), hud(44), fade({ 205, 60, 40, 255 }, a));
        textC(fmt("...but nothing stays down, down here   ·   %d put down", sim.killCount), sw / 2, sh / 2 - hud(44),
              hud(18), fade({ 150, 122, 100, 255 }, a * 0.9f));
    }
    if (sim.closeCallT > 0) {   // the hunter passed close by while the player was hidden
        float a = clampf(sim.closeCallT > 2.3f ? (3.0f - sim.closeCallT) / 0.7f : sim.closeCallT / 1.4f, 0, 1);
        textC("IT STOOD RIGHT THERE", sw / 2, sh / 2 - hud(130), hud(30), fade({ 200, 70, 60, 255 }, a));
        textC("...and never saw you", sw / 2, sh / 2 - hud(98), hud(15), fade({ 160, 130, 120, 220 }, a * 0.9f));
    }
    if (sim.deckNoteT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // a tape deck message
        float a = clampf(sim.deckNoteT > 2.0f ? (2.6f - sim.deckNoteT) / 0.6f : sim.deckNoteT / 1.2f, 0, 1);
        textC(sim.deckNote, sw / 2, sh - hud(150), hud(16), fade({ 196, 168, 214, 230 }, a * 0.9f));
    }
    if (sim.tapeFoundT > 0) {   // a tape was found
        float a = clampf(sim.tapeFoundT > 2.6f ? (3.2f - sim.tapeFoundT) / 0.6f : sim.tapeFoundT / 1.6f, 0, 1);
        textC("TAPE RECOVERED", sw / 2, sh - hud(150), hud(22), fade({ 190, 178, 150, 255 }, a));
        textC(sim.tapeLine, sw / 2, sh - hud(122), hud(15), fade({ 150, 140, 118, 230 }, a * 0.9f));
    }
    if (in.captureHint) textC("click to capture mouse", sw / 2, sh / 2 + hud(80), hud(20), { 200, 190, 150, 200 });
    textR(fmt("%d", in.fps), sw - hud(14), hud(12), hud(16), { 190, 180, 140, 150 });
    // The flashlight hint until it is first used, then its state.
    if (sim.flashOn) everFlashed = true;
    if (!everFlashed && elapsed > 9.0)
        // Not an em dash: raylib's default font stops at Latin-1, and anything
        // past it draws as a "?". U+00B7 is inside the range.
        textR("F · flashlight", sw - hud(16), sh - hud(28), hud(16), { 190, 180, 140, 160 });
    else if (sim.flashOn)
        textR("[ flashlight ]", sw - hud(16), sh - hud(26), hud(14), { 235, 225, 180, 120 });
    {   // the sanity meter, always shown
        const int w = hud(90), x = sw - w - hud(16), y = sh - hud(60), th = hud(5);
        rect(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        // Cream, shading to red as it empties; it pulses near the end.
        Rgba bar = sim.sanity > 0.5f  ? Rgba{ 168, 196, 176, 165 }
                 : sim.sanity > 0.25f ? Rgba{ 214, 190, 120, 180 }
                                      : Rgba{ 214, 96, 84, 200 };
        if (sim.sanity < 0.25f) bar.a = (unsigned char)(150 + 80 * (0.5f + 0.5f * sinf((float)now * 4.2f)));
        rect(x, y, (int)(w * sim.sanity), th, bar);
        textR("sanity", x - hud(6), y - hud(4), hud(12), { 150, 142, 122, 120 });
    }
    if (sim.manilaCardT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // entered the Manila Room
        float a = clampf(sim.manilaCardT > 4.2f ? (5.0f - sim.manilaCardT) / 0.8f : sim.manilaCardT / 1.6f, 0, 1);
        textC("THE MANILA ROOM", sw / 2, sh / 2 - hud(150), hud(34), fade({ 226, 206, 158, 255 }, a));
        textC("...the hum is quieter here.  someone left notes on the table  [E]", sw / 2, sh / 2 - hud(112), hud(16),
              fade({ 190, 172, 132, 255 }, a * 0.9f));
    }
    if (sim.noteT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // a Manila Room note
        float a = clampf(sim.noteT > 8.6f ? (9.0f - sim.noteT) / 0.4f : sim.noteT / 0.8f, 0, 1);
        int pw = std::min(sw - hud(40), hud(720)), ph = hud(170);
        int x0 = sw / 2 - pw / 2, y0 = sh / 2 - ph / 2 + hud(40);
        rect(x0, y0, pw, ph, fade({ 232, 222, 190, 255 }, 0.92f * a));
        box(HudDraw::RectLines, x0, y0, pw, ph, fade({ 150, 130, 96, 255 }, a));
        for (int ln = 0; ln < 4; ln++) {
            const char *t = MANILA_NOTES[sim.notePage][ln];
            if (!*t) continue;
            int fs = hud(18), tw = measure(t, fs);
            if (tw > pw - hud(30)) { fs = fs * (pw - hud(30)) / tw; tw = measure(t, fs); }
            raw(t, sw / 2 - tw / 2, y0 + hud(22) + ln * hud(34), fs,
                fade(ln == 3 ? Rgba{ 110, 96, 80, 255 } : Rgba{ 58, 50, 44, 255 }, a));
        }
    }
    if (sim.sanityWarnT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // a sanity warning
        float a = clampf(sim.sanityWarnT / 1.2f, 0, 1) * clampf((4.0f - sim.sanityWarnT) / 0.4f, 0, 1);
        textC(sim.sanityLine, sw / 2, sh / 2 + hud(96), hud(19), fade({ 206, 176, 176, 255 }, a * 0.9f));
    }
    if (sim.flashOn || sim.battery < 0.99f) {   // the battery meter, once the flashlight has been used
        const int w = hud(90), x = sw - w - hud(16), y = sh - hud(44), th = hud(5);   // a row below the sanity meter
        rect(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        Rgba bar = sim.battery < 0.15f ? Rgba{ 220, 90, 70, 190 } : Rgba{ 200, 190, 150, 150 };
        rect(x, y, (int)(w * sim.battery), th, bar);
        if (sim.flashOn && sim.battery < 0.15f) {
            float pulse = 0.5f + 0.5f * sinf((float)now * 5.0f);
            textR("battery low", sw - hud(16), sh - hud(78), hud(13), fade({ 220, 120, 100, 220 }, pulse));
        }
    }
    {   // the inventory, bottom left; the selected weapon is lit
        Rgba selc = { 235, 200, 130, 210 }, dimc = { 150, 138, 112, 110 };
        if (sim.tapes > 0) text(fmt("tapes  ×%d", sim.tapes), hud(16), sh - hud(138), hud(16), { 172, 162, 190, 170 });
        if (sim.keys > 0) text(fmt("keys  ×%d", sim.keys), hud(16), sh - hud(160), hud(16), { 214, 186, 110, 190 });
        // These four lines are placed in unscaled pixels, as the Web build
        // always placed them.
        if (sim.coins > 0 || sim.wayOpen())   // doubloons pay for the exit (ESCAPE_COST)
            raw(fmt("doubloons  ×%d / %d", sim.coins, Sim::ESCAPE_COST), 16, sh - 116, 16,
                sim.wayOpen() ? Rgba{ 120, 230, 140, 210 } : Rgba{ 214, 178, 92, 170 });
        raw(fmt("3  almond water  ×%d", sim.almond), 16, sh - 94, 16,
            sim.almond > 0 ? Rgba{ 150, 190, 235, 170 } : dimc);
        raw(sim.reloadT > 0 ? std::string("1  revolver  [reloading]")
                            : fmt("1  revolver  %d/%d%s  · %s", sim.ammo, Sim::MAXAMMO,
                                  sim.ammo == 0 ? (in.touch ? "  · LOAD" : "  · R") : "",
                                  in.touch ? "tap AIM" : "hold RMB aim"),
            16, sh - 50, 16, sim.weapon == WEAPON_REVOLVER ? selc : dimc);
        raw(fmt("2  flare  ×%d", sim.flares), 16, sh - 72, 16, sim.weapon == WEAPON_FLARE ? selc : dimc);
        raw(!sim.deck.carried
                ? (sim.deck.playing ? "4  tape player  [running · left behind]"
                   : in.touch       ? "4  tape player  [left behind · USE]"
                                    : "4  tape player  [left behind · E]")
            : sim.deck.playing ? fmt("4  tape player  [playing  %ds]", (int)sim.deck.t + 1)
            : sim.tapes > 0    ? "4  tape player  [tape ready]"
                               : "4  tape player  [no tape]",
            hud(16), sh - hud(28), hud(16),
            sim.weapon == WEAPON_DECK ? (sim.deck.playing ? Rgba{ 205, 150, 235, 220 } : selc) : dimc);
    }
    if (sim.wayOpen() && sim.winT <= 0 && sim.deathT <= 0 && sim.escapeT <= 0) {   // enough doubloons: find an exit
        const char *t = sim.level == 0 ? "the walls know you now  ·  find one that isn't cursed"
                                       : "the doors know you now  ·  find one that isn't cursed";
        float pl = 0.55f + 0.45f * sinf((float)now * 2.5f);
        textC(t, sw / 2, hud(70), hud(20), fade({ 120, 235, 145, 255 }, pl));
    }
    // No health bar. A hit flashes the screen edges red, and they stay red while
    // the player is wounded. When the red is gone, a hit is survivable again.
    if ((sim.hurtT > 0 || sim.health < 0.999f) && sim.deathT <= 0) {
        float a = fmaxf(sim.hurtT / Sim::HURT_GRACE * 0.55f, (1.0f - sim.health) * 0.45f);
        Rgba r0 = fade({ 120, 0, 0, 255 }, a), r1 = fade({ 120, 0, 0, 255 }, 0);
        int e = sh / 4;
        box(HudDraw::GradientV, 0, 0, sw, e, r0, r1);
        box(HudDraw::GradientV, 0, sh - e, sw, e, r1, r0);
        box(HudDraw::GradientH, 0, 0, e, sh, r0, r1);
        box(HudDraw::GradientH, sw - e, 0, e, sh, r1, r0);
    }
    if (sim.stamina < 0.98f) {   // the sprint bar, bottom centre
        const int w = hud(220), x = sw / 2 - w / 2, y = sh - hud(42), th = hud(6);
        rect(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        rect(x, y, (int)(w * sim.stamina), th, sim.sprintExhausted ? Rgba{ 190, 100, 66, 180 } : Rgba{ 200, 180, 120, 160 });
        if (sim.sprintExhausted) text("catch your breath", x + hud(48), y + hud(11), hud(12), { 195, 156, 119, 180 });
    }
    // The hiding hint, one per state. It used to say "hold still" even when
    // there was no cover.
    const char *coverLine = nullptr;
    Rgba coverCol = { 140, 210, 165, 160 };
    if (sim.hidden && sim.hideBreakT > 0) {   // hidden, but moving too fast
        coverLine = "[ hidden · you're giving it away ]";
        coverCol = { 228, 196, 128, 200 };
    } else if (sim.hidden) {
        coverLine = "[ hidden · hold still ]";
    } else if (sim.nearCover) {   // next to cover, but moving too fast to hide
        coverLine = "[ cover · stop to hide ]";
        coverCol = { 190, 180, 150, 140 };
    } else if (sim.squeezing) {
        coverLine = "[ squeezing ]";
    } else if (sim.crouchCur > 0.5f) {
        coverLine = "[ crouched ]";
        coverCol = { 180, 170, 140, 120 };
    }
    if (coverLine) textC(coverLine, sw / 2, sh - hud(62), hud(14), coverCol);
    if (sim.level == 3 && sim.valveT > 0) {   // a valve was just closed
        float a = clampf(sim.valveT / 1.6f, 0, 1);
        if (sim.pipesShut) {
            textC("THE PIPES GO QUIET", sw / 2, sh / 2 - hud(110), hud(34), fade({ 120, 230, 145, 255 }, a));
            textC("...and the halls give up what they were holding", sw / 2, sh / 2 - hud(74), hud(16),
                  fade({ 150, 200, 160, 255 }, a * 0.9f));
        } else {
            textC(fmt("VALVE SHUT   %d / %d", (int)sim.valvesTurned.size(), Sim::VALVES_NEEDED), sw / 2,
                  sh / 2 - hud(100), hud(26), fade({ 210, 180, 120, 255 }, a));
        }
    }
    if (sim.level == 3 && !sim.valvesTurned.empty() && !sim.pipesShut)   // valves closed so far
        text(fmt("valves  %d / %d", (int)sim.valvesTurned.size(), Sim::VALVES_NEEDED), hud(16), sh - hud(138), hud(16),
             { 198, 150, 96, 175 });
    if (sim.paused) {
        rect(0, 0, sw, sh, fade(BLACK_RGBA, 0.55f));
        textC("P A U S E D", sw / 2, sh / 2 - hud(42), hud(46), { 225, 212, 160, 235 });
        textC("P to resume", sw / 2, sh / 2 + hud(16), hud(18), { 165, 155, 120, 200 });
    }
    if (in.debug) {
        text(fmt("%d fps  pos(%.0f, %.0f)  chunks %d  entity %s  d=%.0fm  hidden=%d  batt=%.2f", in.fps, sim.px, sim.pz,
                 (int)sim.world.chunks.size(),
                 sim.ent.st == EState::Hidden  ? "hidden"
                 : sim.ent.st == EState::Stalk ? "STALKING"
                 : sim.ent.st == EState::Chase ? "CHASING"
                 : sim.ent.st == EState::Flee  ? "FLEEING"
                                               : "DYING",
                 sim.entDist > 1e8 ? 0.0f : sim.entDist, sim.hidden ? 1 : 0, sim.battery),
             hud(12), hud(12), hud(18), { 230, 220, 160, 220 });
        text("dev: B blackout   E spawn   C chase   H hide   G flares   N next level", hud(12), hud(34), hud(16),
             { 200, 190, 140, 180 });
    }
}

}  // namespace

std::vector<HudDraw> hudDraws(const Sim &sim, const HudInput &in, bool &everFlashed, const HudMeasure &measure) {
    Hud h{ sim, in, measure, {}, in.width, in.height };
    const float timeF = (float)in.now;
    const double elapsed = in.now - sim.runStart;
    if (sim.swimming && !sim.inMenu)
        h.textC(in.touch ? "hold JUMP  stay up   ·   let go  dive"
                         : "hold SPACE  stay up   ·   let go  dive   ·   SHIFT  swim faster",
                h.sw / 2, h.sh - h.hud(42), h.hud(16), { 174, 221, 214, 230 });
    if (sim.inMenu && sim.deathT > 0) h.deathCard(timeF);   // the death card, over the title screen
    else if (sim.inMenu) h.titleCard(timeF);              // over the drifting title camera
    else h.play(in.now, elapsed, everFlashed);
    return std::move(h.out);
}
