// LEVEL FUN's presents: where each is, what is in it, and opening it. The
// scene lists them (port/scene.cpp) and port/draw.cpp draws them, so both
// builds show the same boxes.
#include "../core/fp_strict.h"
#include "sim.h"
#include "../core/hash.h"
#include <cmath>

bool Sim::presentAt(int a, int b, Present &out) {
    if (level != LEVEL_FUN || world.propAt(a, b) != PROP_PARTY_TABLE) return false;
    const uint32_t h = ih(a, b, pickupSalt() ^ 0x61F7u);
    if (h % PRESENT_ODDS != 0) return false;
    // On the floor beside the table, on one of its four sides: 0.74 m from
    // the cell's centre, between the table's collision box (0.6 m) and the
    // face of a wall on the cell's edge (0.89 m). Under the table an opened
    // box and a jack-in-the-box could not be seen.
    const int side = (int)((h >> 4) & 3);
    const float off = 0.74f, ox = side == 0 ? off : side == 1 ? -off : 0.0f, oz = side == 2 ? off : side == 3 ? -off : 0.0f;
    out.pos = { a * CELL + 1.0f + ox, world.floorY(a, b), b * CELL + 1.0f + oz };
    out.hw = 0.10f + 0.04f * (float)((h >> 6) & 7) / 7.0f;
    out.hh = 0.09f + 0.06f * (float)((h >> 9) & 7) / 7.0f;
    out.colour = (uint8_t)((h >> 12) % PARTY_COLOURS);
    // One in six a jack-in-the-box; the rest loot, doubloons most often.
    const uint32_t g = (h >> 16) % 12;
    out.gift = g < 4 ? Gift::Doubloons : g < 6 ? Gift::Flare : g < 8 ? Gift::Almond
             : g < 9 ? Gift::Battery : g < 10 ? Gift::Tape : Gift::Jack;
    out.amount = out.gift == Gift::Doubloons ? 2 + (int)((h >> 20) % 3) : 0;
    return true;
}

bool Sim::presentInReach(int &a, int &b) {
    if (level != LEVEL_FUN) return false;
    const int ci = cellOf(px), ck = cellOf(pz);
    float nearest = PRESENT_REACH * PRESENT_REACH;
    bool found = false;
    for (int dx = -1; dx <= 1; dx++)
        for (int dz = -1; dz <= 1; dz++) {
            Present p;
            const int i = ci + dx, k = ck + dz;
            if (!presentAt(i, k, p) || presentsOpened.count(cellKey2(i, k))) continue;
            const float ddx = px - (i * CELL + 1.0f), ddz = pz - (k * CELL + 1.0f), d2 = ddx * ddx + ddz * ddz;
            if (d2 >= nearest) continue;
            nearest = d2; a = i; b = k; found = true;
        }
    return found;
}

void Sim::openPresent(int a, int b) {
    Present p;
    if (!presentAt(a, b, p)) return;
    presentsOpened.insert(cellKey2(a, b));
    deckNoteT = 2.8f;
    if (p.gift != Gift::Jack) play(Sfx::GiftOpen).atPitch(0.95f + 0.1f * (float)(ih(a, b, 0x61F8u) & 255) / 255.0f);
    switch (p.gift) {
    case Gift::Doubloons:
        coins += p.amount;
        deckNote = p.amount == 2 ? "a present: two doubloons." : p.amount == 3 ? "a present: three doubloons." : "a present: four doubloons.";
        break;
    case Gift::Flare:
        if (flares < MAXFLARES) { flares++; deckNote = "a present: a road flare, with a bow on it."; }
        else { almond++; deckNote = "a present: a road flare. your hands are full, so you drink the water inside instead."; }
        break;
    case Gift::Almond:  almond++; deckNote = "a present: a carton of almond water."; break;
    case Gift::Battery: battery = clampf(battery + 0.6f, 0, 1); deckNote = "a present: batteries."; break;
    case Gift::Tape:    tapes++; deckNote = "a present: a cassette. the label says HAPPY BIRTHDAY."; break;
    case Gift::Jack: {
        play(Sfx::JackInBox);
        deckNote = "a jack-in-the-box. something heard it.";
        presentNoiseT = PRESENT_NOISE_TIME;
        presentNoiseX = p.pos.x;
        presentNoiseZ = p.pos.z;
        fear = fmaxf(fear, 0.8f);
        // A burst of confetti from its own hash, so grng is not moved by the burst.
        Rng r(hash64(((uint64_t)(uint32_t)a << 32) ^ (uint32_t)b ^ 0x7AC4ULL));
        const Vec3 at{ p.pos.x, p.pos.y + 2 * p.hh + 0.25f, p.pos.z };
        for (int n = 0; n < 24; n++) {
            const float ang = r.f01() * TAU, sp = 0.8f + r.f01() * 1.8f;
            confetti.push_back({ at, { portableCos(ang) * sp, 1.6f + r.f01() * 1.8f, portableSin(ang) * sp },
                                 1.4f + r.f01() * 0.8f, (uint8_t)(n % PARTY_COLOURS) });
        }
        callHunter();
        break;
    }
    }
}

void Sim::updatePresents(float dt) {
    if (presentEntry != levelEntries) {   // a new level or visit
        presentEntry = levelEntries;
        presentsOpened.clear();
        presentNoiseT = 0;
    }
    presentNoiseT = fmaxf(0, presentNoiseT - dt);
    int a, b;
    presentNear = !inMenu && deathT <= 0 && presentInReach(a, b);
}
