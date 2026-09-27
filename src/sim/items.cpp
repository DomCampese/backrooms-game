// Things in the world: loose pickups, drinking, the tape deck, the use key,
// chalk, and Level 1's supply crates.
#include "sim.h"
#include <cmath>
#include <cstdlib>

static const char *TAPE_LINES[] = {
    "day 12. the walls hum in b-flat. i've started humming back.",
    "if you find this, don't answer when it says your name.",
    "the vending machines take doubloons. i don't know why i know that.",
    "someone wrote NO CLIP on level 0. i wrote it. i don't remember writing it.",
    "three knocks means it's not him. two knocks means run.",
    "the almond water tastes like almonds. that's the whole warning.",
    "i counted eleven exits today. only one was real.",
    "he's slower than he looks. i am not fast enough anyway.",
};
static constexpr int TAPE_LINE_COUNT = sizeof(TAPE_LINES) / sizeof(TAPE_LINES[0]);

float Sim::bottleShelfY(int a, int b) {
    switch (world.propAt(a, b)) {
    case PROP_CABINET:     return 1.32f;
    case PROP_TABLE:       return 0.72f;
    case PROP_NIGHTSTAND:  return 0.60f;
    case PROP_PARTY_TABLE: return 0.74f;
    case PROP_DESK:        return 0.74f;
    case PROP_MANILA_TABLE: return 0.787f;  // the octagonal table's top (addManilaRoom)
    default:               return -1.0f;
    }
}

bool Sim::bottleAt(int a, int b) {
    if (world.pillarAt(a, b) || world.poolAt(a, b)) return false;
    if (world.propAt(a, b)) {
        // On furniture a carton is far likelier than on bare floor.
        if (bottleShelfY(a, b) < 0) return false;
        if (world.propAt(a, b) == PROP_MANILA_TABLE) return true;   // the cupboard under it
        return ih(a, b, pickupSalt() ^ 0xA1A2u) % 4 == 0;
    }
    return ih(a, b, pickupSalt() ^ 0xA1A1u) % 137 == 0;
}

// The payout lands on the middle swallow, not the keypress, so the animation
// is what you wait through.
void Sim::updateDrink(float dt) {
    if (drinkT <= 0) return;
    float el = DRINK_TIME - drinkT;             // s since the can went up
    if (!drinkLanded && el >= 0.62f) {
        drinkLanded = true;
        stamina = 1.0f;
        fear *= 0.35f;
        boostT = 8.0f;
        sanity = clampf(sanity + 0.34f + 0.04f * level, 0.0f, 1.0f);   // more back the deeper you are
        sanityWarnT = 0;
    }
    drinkT = fmaxf(0.0f, drinkT - dt);
}

Pickup Sim::pickupAt(int a, int b) {
    // A key is placed, not hashed, and its position means something: nothing masks it.
    if (world.keyAt(a, b)) return Pickup::Key;
    // Nothing lies on a flight or over a hole.
    if (world.storeyH > 0.0f && (world.vflagAt(a, b) & (VF_STAIR | VF_HOLE))) return Pickup::None;
    if (bottleAt(a, b))  return Pickup::AlmondWater;
    if (coinAt(a, b))    return Pickup::Doubloon;
    if (batteryAt(a, b)) return Pickup::Battery;
    if (tapeAt(a, b))    return Pickup::Tape;
    return Pickup::None;
}

// Level 0's first visit on storey 0 salts with the bare seed, so a fresh
// descent opens on the pickups it always did.
uint32_t Sim::pickupSalt() const {
    return (uint32_t)world.seed ^ ((uint32_t)level * 0x9E3779B9u)
                                ^ (world.visit * 0x85EBCA6Bu)
                                ^ ((uint32_t)world.storey * 0xC2B2AE35u);
}

bool Sim::coinAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, pickupSalt() ^ 0xC01Du) % 449 == 0;
}

bool Sim::batteryAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, pickupSalt() ^ 0xBA77u) % 379 == 0;
}

bool Sim::tapeAt(int a, int b) {
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    return ih(a, b, pickupSalt() ^ 0x7A9Eu) % 401 == 0;
}

// tools/mapdump.cpp mirrors this list; change both.
bool Sim::hideSpotAt(int a, int b) {
    switch (world.propAt(a, b)) {
    case PROP_BOXES: case PROP_CABINET: case PROP_COUCH: case PROP_ARMOIRE:
    case PROP_NIGHTSTAND: case PROP_BED: case PROP_PARTY_TABLE: case PROP_DESK:
    case PROP_SHELVING:
        return true;
    default:
        return false;
    }
}

// The middle of the cell, except on the Manila Room's table, which is anchored
// on one cell and built on its far corner.
Vector2 Sim::pickupSpot(int a, int b) {
    if (world.propAt(a, b) == PROP_MANILA_TABLE) return { a * CELL + 1.78f, b * CELL + 1.84f };
    return { a * CELL + 1.0f, b * CELL + 1.0f };
}

// A playing tape restores sanity only as far as you can hear it (gone by 14 m,
// less through a wall), while the pack hears it from TAPE_NOISE: a deck set
// far enough out to draw them does nothing for you.
void Sim::updateTapeDeck(const InputFrame &in, float dt, double now) {
    deckNoteT = fmaxf(0, deckNoteT - dt);

    if (in.playing && in.fire && deathT <= 0 && drinkT <= 0 && weapon == WEAPON_DECK && deck.carried) {
        if (!deck.playing && tapes > 0) {
            tapes--;
            deck.playing = true;
            deck.t = TAPE_RUN;
            deckNoteT = 2.6f; deckNote = "the tape runs. someone is talking.";
            play(Sfx::Click).atPitch(1.05f);
            // The hunter comes to noise. Once, on the press: every tick would
            // pin his next spawn to the length of the tape.
            if (ent.st == EState::Hidden)
                ent.nextSpawn = fmin(ent.nextSpawn, now + 10 + grng.f01() * 9);
        } else if (deck.playing) {
            // Set down underarm, so it lands a few metres out.
            deck.carried = false; deck.flying = true;
            deck.x = px + fwd.x * 0.4f; deck.y = eyeY - 0.35f; deck.z = pz + fwd.z * 0.4f;
            deck.vx = fwd.x * 5.0f; deck.vz = fwd.z * 5.0f; deck.vy = fwd.y * 5.0f + 1.4f;
            deck.yaw = atan2f(fwd.x, fwd.z);
            deckNoteT = 2.6f; deckNote = "you leave it talking, and walk away.";
        } else if (tapes <= 0) {
            deckNoteT = 2.2f; deckNote = "no tape. the deck is empty.";
            play(Sfx::Click).atPitch(0.7f);
        }
    }

    if (!deck.carried && deck.flying) flyDeck(dt);

    if (deck.playing) {
        deck.t -= dt;
        deck.reel += dt * 2.3f;
        if (deck.reel > TAU) deck.reel -= TAU;
        if (deck.t <= 0) {
            deck.playing = false; deck.t = 0;
            deckNoteT = 2.6f; deckNote = "the tape runs out.";
            play(Sfx::Click).atPitch(0.85f);
        }
    }

    float sx = deck.carried ? px : deck.x, sz = deck.carried ? pz : deck.z;
    if (deck.playing) {
        float ddx = sx - px, ddz = sz - pz;
        float dist = sqrtf(ddx * ddx + ddz * ddz);
        AudioEvent &voice = audioEvent(AudioEvent::VOICE);
        voice.pan = dist > 0.25f ? clampf((ddx / dist) * r2x + (ddz / dist) * r2z, -1.0f, 1.0f) : 0.0f;
        voice.volume = clampf(1.0f / (1.0f + 0.05f * dist * dist), 0.0f, 0.9f);
        float aud = deck.carried ? 1.0f
                  : clampf(1.0f - dist / 14.0f, 0.0f, 1.0f) *
                    (world.lineOfSight(px, pz, deck.x, deck.z) ? 1.0f : 0.45f);
        sanity = clampf(sanity + 0.0105f * aud * dt, 0.0f, 1.0f);
    } else stopVoice();
}

// The same arc and bounce as a flare; water stops the tape.
void Sim::flyDeck(float dt) {
    deck.vy -= 18.0f * dt;
    deck.x += deck.vx * dt; deck.y += deck.vy * dt; deck.z += deck.vz * dt;
    float ox = deck.x, oz = deck.z;
    world.collideCircle(deck.x, deck.z, 0.07f, deck.y);
    if (fabsf(ox - deck.x) > 1e-5f) deck.vx *= -0.3f;
    if (fabsf(oz - deck.z) > 1e-5f) deck.vz *= -0.3f;
    float g = world.groundAt(deck.x, deck.z, deck.y);
    if (deck.y < g + 0.005f && deck.vy < 0) {
        deck.y = g + 0.005f;
        if (deck.vy < -2.2f) { deck.vy *= -0.22f; deck.vx *= 0.4f; deck.vz *= 0.4f; }
        else { deck.flying = false; deck.vx = deck.vy = deck.vz = 0; }
    }
    if (world.poolAt(cellOf(deck.x), cellOf(deck.z)) && deck.y < -0.10f && deck.playing) {
        deck.playing = false; deck.t = 0;
        play(Sfx::SplashIn, grng.ri(0, 2)).atPitch(1.25f).atVolume(0.5f);
        deckNoteT = 2.6f; deckNote = "the water takes it. the voice stops.";
    }
}

void Sim::updateInteraction(const InputFrame &in) {
    collectPickups();
    if (in.drink && almond > 0 && drinkT <= 0) {
        almond--;
        drinkT = DRINK_TIME;
        drinkLanded = false;
        play(Sfx::Gulp);
    }
    if (in.use) useWhatIsNear();
    if (in.chalk) markChalk();
}

void Sim::collectPickups() {
    int pci = cellOf(px), pck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        int a = pci + dx, b = pck + dz;
        uint64_t ky = cellKey(a, b);
        if (taken.count(ky)) continue;
        Pickup kind = pickupAt(a, b);
        if (kind == Pickup::None) continue;
        Vector2 spot = pickupSpot(a, b);
        float ddx = px - spot.x, ddz = pz - spot.y;
        // A carton on furniture is reached across it, since collision keeps
        // you off the piece; never through a wall (lineOfSight ignores props).
        bool onShelf = kind == Pickup::AlmondWater && bottleShelfY(a, b) >= 0;
        float gr = onShelf ? 1.35f : 0.8f;
        if (ddx * ddx + ddz * ddz >= gr * gr) continue;
        if (!world.lineOfSight(px, pz, spot.x, spot.y)) continue;
        if (fabsf(py - (world.floorY(a, b) + (onShelf ? bottleShelfY(a, b) : 0))) > 1.5f) continue;
        if (kind == Pickup::Battery && battery > 0.98f) continue;
        taken.insert(ky);
        switch (kind) {
        case Pickup::AlmondWater:
            almond++;
            play(Sfx::Click).atPitch(1.3f);
            break;
        case Pickup::Doubloon:
            coins++;
            play(Sfx::Click).atPitch(1.6f);
            break;
        case Pickup::Battery:
            battery = clampf(battery + 0.45f, 0, 1);
            play(Sfx::Click).atPitch(0.85f);
            break;
        case Pickup::Tape:
            tapes++;
            tapeFoundT = 3.2f;
            tapeLine = TAPE_LINES[grng.ri(0, TAPE_LINE_COUNT - 1)];
            play(Sfx::TapeChime);
            break;
        case Pickup::Key:
            keys++;
            deckNoteT = 2.4f; deckNote = "a key. something near here is locked.";
            play(Sfx::Click).atPitch(1.45f);
            break;
        case Pickup::None:
            break;
        }
    }
    for (size_t c2 = 0; c2 < coinsWorld.size();) {   // spilled doubloons
        float ddx = px - coinsWorld[c2].x, ddz = pz - coinsWorld[c2].z;
        // .y is the floor they lie on, so not ones a storey down.
        if (ddx * ddx + ddz * ddz < 0.7f * 0.7f && fabsf(coinsWorld[c2].y - py) < 1.2f) {
            coins++;
            play(Sfx::Click).atPitch(1.6f);
            coinsWorld.erase(coinsWorld.begin() + c2);
        } else ++c2;
    }
}

// E reaches a crate, the Manila notes, a standpipe, a vending machine, a
// locked door and a deck you set down. They never share a cell.
void Sim::useWhatIsNear() {
    int pci = cellOf(px), pck = cellOf(pz);
    if (level == 1) {
        for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
            int a = pci + dx, b = pck + dz;
            if (!crateAt(a, b) || cratesOpened.count(cellKey2(a, b))) continue;
            float ddx = px - (a * CELL + 1.0f), ddz = pz - (b * CELL + 1.0f);
            if (ddx * ddx + ddz * ddz < 1.35f * 1.35f) { openCrate(a, b); dx = dz = 2; }
        }
    }
    if (level == 0 && manilaNear) {
        float ddx = px - manilaX, ddz = pz - manilaZ;
        if (ddx * ddx + ddz * ddz < 1.9f * 1.9f) {
            notePage = notesRead ? (notePage + 1) % MANILA_NOTE_COUNT : 0;
            noteT = 9.0f;
            play(Sfx::Click).atPitch(1.9f);
            if (!notesRead) { notesRead = true; markWayOut(); }
        }
    }
    if (level == 3) {
        bool turned = false;
        for (int dx = -1; dx <= 1 && !turned; dx++) for (int dz = -1; dz <= 1 && !turned; dz++) {
            int a = pci + dx, b = pck + dz;
            if (!world.valveAt(a, b)) continue;
            uint64_t ky = cellKey2(a, b);
            if (valvesTurned.count(ky)) continue;
            float vx = a * CELL + 1.0f, vz = b * CELL + 1.0f;
            float ddx = px - vx, ddz = pz - vz;
            if (ddx * ddx + ddz * ddz > 1.7f * 1.7f) continue;
            valvesTurned.insert(ky);
            valveT = 3.4f;
            turned = true;
            play(Sfx::Valve);
            if ((int)valvesTurned.size() >= VALVES_NEEDED && !pipesShut) {
                pipesShut = true;
                play(Sfx::Win);
                // Once a descent: paid per visit, the cursed exit's loop back
                // here banked the way out without ever facing the hunter.
                if (!pipesPaid) {
                    pipesPaid = true;
                    for (int c2 = 0; c2 < 9; c2++) {
                        float aa = c2 * 0.698f + grng.f01();
                        float rr = 1.2f + grng.f01() * 1.1f;
                        coinsWorld.push_back({ px + cosf(aa) * rr, py, pz + sinf(aa) * rr });
                    }
                }
            }
        }
    }
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {   // vending: three doubloons a can
        int a = pci + dx, b = pck + dz;
        if (world.propAt(a, b) != PROP_VENDING) continue;
        float mx = a * CELL + 1.0f, mz = b * CELL + 1.0f;
        float ddx = px - mx, ddz = pz - mz;
        if (ddx * ddx + ddz * ddz < 1.6f * 1.6f && coins >= 3) {
            coins -= 3; almond++;
            play(Sfx::Click).atPitch(0.8f);
        }
    }
    bool opened = false;   // a locked door: a key turns it for good
    for (int dx = -1; dx <= 1 && !opened; dx++) for (int dz = -1; dz <= 1 && !opened; dz++) {
        int a = pci + dx, b = pck + dz;
        for (int west = 0; west < 2 && !opened; west++) {
            if ((west ? world.wallWVal(a, b) : world.wallNVal(a, b)) != WALL_LOCKED) continue;
            float ex = a * CELL + (west ? 0.0f : 1.0f), ez = b * CELL + (west ? 1.0f : 0.0f);   // the opening's middle
            float ddx = px - ex, ddz = pz - ez;
            if (ddx * ddx + ddz * ddz > 1.9f * 1.9f) continue;
            if (keys <= 0) {
                deckNoteT = 2.2f; deckNote = "locked. the key will be somewhere near.";
                opened = true;   // said once, not once per edge
                break;
            }
            keys--;
            world.unlockEdge(a, b, west != 0);
            // The light's grid is a snapshot rebuilt every six cells walked;
            // without this the open door goes on casting its shadow.
            shadowsStale = true;
            deckNoteT = 2.2f; deckNote = "the lock turns. the door swings in.";
            play(Sfx::Click).atPitch(0.7f);
            opened = true;
        }
    }
    if (!deck.carried) {   // pick the deck back up, running or not
        float ddx = px - deck.x, ddz = pz - deck.z;
        if (ddx * ddx + ddz * ddz < 1.5f * 1.5f) {
            deck.carried = true; deck.flying = false;
            deckNoteT = 2.2f; deckNote = deck.playing ? "you pick it up. it's still running."
                                                      : "you pick the deck back up.";
            play(Sfx::Click).atPitch(1.1f);
        }
    }
}

void Sim::markChalk() {
    float x = px + f2x * 0.5f, z = pz + f2z * 0.5f;
    if (!grounded || !world.lineOfSight(px, pz, x, z)) return;
    auto &marks = chalk[level];
    marks.push_back({{x, world.groundAt(x, z, py) + 0.016f, z}, yaw, true, world.storey});
    // Over the cap, drop your own oldest: the stranger's arrows are first in
    // the list and are the rarest thing on the floor.
    if ((int)marks.size() > MAXCHALK)
        for (size_t i = 0; i < marks.size(); i++)
            if (marks[i].mine) { marks.erase(marks.begin() + i); break; }
}

// A crate stands alone in a cell with nothing else in it, never in front of a
// way through and never at an arrival point.
bool Sim::crateAt(int a, int b) {
    if (level != 1) return false;
    if (ih(a, b, pickupSalt() ^ 0xC2A7Eu ^ (crateEpoch * 0x9E3779B9u)) % 37 != 0) return false;
    if (world.pillarAt(a, b) || world.propAt(a, b) || world.poolAt(a, b)) return false;
    if (pickupAt(a, b) != Pickup::None) return false;
    const uint8_t e[4] = { world.wallNVal(a, b), world.wallNVal(a, b + 1), world.wallWVal(a, b), world.wallWVal(a + 1, b) };
    for (uint8_t w : e) if (w == WALL_DOOR || w == WALL_EXIT || w == WALL_LOCKED) return false;
    return !(abs(a) <= 3 && abs(b) <= 3);
}

// Contents are a hash of the cell and the epoch: supplies half the time, the
// lore's junk otherwise.
void Sim::openCrate(int a, int b) {
    cratesOpened.insert(cellKey2(a, b));
    uint32_t h = ih(a, b, pickupSalt() ^ 0x10075u ^ (crateEpoch * 0x85EBCA6Bu));
    static const char *JUNK[] = {
        "assorted car parts.", "a box of crayons.", "used syringes. you put the lid back.",
        "partially burned paper. none of it legible.", "a live mouse. it is gone before you can blink.",
        "mice, not moving, with needle marks.", "shoelaces. a lot of shoelaces.", "loose change.",
        "a bundle of human hair.",
    };
    deckNoteT = 2.6f;
    play(Sfx::Click).atPitch(0.6f);
    switch (h % 10) {
    case 0: case 1: almond++;                                   deckNote = "a supply crate: a carton of almond water."; break;
    case 2:         battery = clampf(battery + 0.6f, 0, 1);     deckNote = "a supply crate: batteries. the torch will keep."; break;
    case 3:         if (flares < MAXFLARES) flares++; else almond++;
                    deckNote = flares < MAXFLARES ? "a supply crate: a road flare." : "a supply crate: a road flare, and water."; break;
    case 4:         tapes++;                                    deckNote = "a supply crate: a cassette, labelled in someone's hand."; break;
    default:        deckNote = JUNK[(h >> 8) % (sizeof(JUNK) / sizeof(JUNK[0]))]; break;
    }
}

void Sim::updateCrates(double now) {
    if (level != 1) return;
    // The epoch turns when the lights come back, so nobody sees a crate move.
    bool dark = now < blackoutEnd;
    if (crateWasDark && !dark) { crateEpoch++; cratesOpened.clear(); }
    crateWasDark = dark;
    // Crates are not in the chunk's collision: push the player off them here,
    // after world collision.
    const float HALF = 0.36f;   // the crate's collision half-width
    int ci = cellOf(px), ck = cellOf(pz);
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        int a = ci + dx, b = ck + dz;
        if (!crateAt(a, b)) continue;
        float cx = a * CELL + 1.0f, cz = b * CELL + 1.0f, r = HALF + PR;
        float ox = px - cx, oz = pz - cz;
        float qx = clampf(ox, -HALF, HALF), qz = clampf(oz, -HALF, HALF);   // nearest point on the box
        float ex = ox - qx, ez = oz - qz, e2 = ex * ex + ez * ez;
        if (py > 0.5f) continue;                                            // standing on it
        if (e2 < PR * PR && e2 > 1e-8f) {
            float e = sqrtf(e2), push = PR - e;
            px += ex / e * push; pz += ez / e * push;
        } else if (e2 <= 1e-8f) {                                           // inside: out the short way
            if (fabsf(ox) > fabsf(oz)) px = cx + (ox > 0 ? r : -r); else pz = cz + (oz > 0 ? r : -r);
        }
    }
}
