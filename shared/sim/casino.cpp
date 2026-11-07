// The casino: its slot machines (a doubloon a pull, a prize drawn at the pull,
// and noise the hunter can follow) and its gold pack of cigarettes.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

bool Sim::slotInReach(int &a, int &b) {
    if (level != CASINO || inMenu || deathT > 0) return false;
    int pci = cellOf(px), pck = cellOf(pz);
    float nearest = SLOT_REACH * SLOT_REACH;
    bool found = false;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        int ca = pci + dx, cb = pck + dz;
        if (world.propAt(ca, cb) != PROP_SLOTS) continue;
        float ddx = px - (ca * CELL + 1.0f), ddz = pz - (cb * CELL + 1.0f), d2 = ddx * ddx + ddz * ddz;
        if (d2 >= nearest) continue;
        nearest = d2; a = ca; b = cb; found = true;
    }
    return found;
}

void Sim::pullSlot(int a, int b) {
    if (slotSpin.t > 0) return;   // the reels are still turning
    if (coins < SLOT_COST) {
        deckNoteT = 2.2f; deckNote = "one doubloon a pull.";
        return;
    }
    coins -= SLOT_COST;
    float r = slotRng.f01();
    slotSpin.prize = r < SLOT_JACKPOT_ODDS ? SlotPrize::Jackpot
               : r < SLOT_JACKPOT_ODDS + SLOT_SMALL_ODDS ? SlotPrize::Small
               : r < SLOT_JACKPOT_ODDS + SLOT_SMALL_ODDS + SLOT_CAN_ODDS ? SlotPrize::Can : SlotPrize::None;
    slotSpin.t = SLOT_SPIN;
    slotSpin.x = a * CELL + 1.0f; slotSpin.z = b * CELL + 1.0f;
    play(Sfx::SlotSpin);
}

void Sim::updateSlots(float dt) {
    slotSpin.bellsT = fmaxf(0, slotSpin.bellsT - dt);
    slotNear = false;
    int a, b;
    if (slotInReach(a, b)) slotNear = slotSpin.t <= 0;
    if (slotSpin.t <= 0) return;
    slotSpin.t -= dt;
    if (slotSpin.t > 0) return;
    slotSpin.t = 0;
    switch (slotSpin.prize) {
    case SlotPrize::Jackpot:
        coins += SLOT_JACKPOT;
        sanity = clampf(sanity + SLOT_JACKPOT_SANITY, 0.0f, 1.0f);
        slotSpin.bellsT = SLOT_BELLS;
        play(Sfx::Jackpot);
        deckNoteT = 3.2f; deckNote = "JACKPOT. eight doubloons. the bells won't stop.";
        callHunter();
        break;
    case SlotPrize::Small:
        coins += SLOT_SMALL;
        play(Sfx::SlotWin);
        deckNoteT = 2.2f; deckNote = "two doubloons rattle into the tray.";
        break;
    case SlotPrize::Can:
        almond++;
        play(Sfx::SlotWin).atPitch(0.8f);
        deckNoteT = 2.2f; deckNote = "a can of almond water drops out.";
        break;
    case SlotPrize::None:
        deckNoteT = 1.6f; deckNote = "nothing.";
        break;
    }
}

// Placed like the revolver, but only in the casino, and still placed once the
// pack is found so the other pickups do not move.
bool Sim::cigsAt(int a, int b) {
    if (level != CASINO) return false;
    if (ih(a, b, pickupSalt() ^ 0xC16Au) % CIGS_ODDS != 0) return false;
    return !world.pillarAt(a, b) && !world.propAt(a, b) && !world.poolAt(a, b);
}

void Sim::putOutCigarette() {
    smoke.burnT = smoke.dragT = smoke.coughT = 0;
    smoke.cough = false;
}

// With a cigarette in hand, the fire button lights one, then takes a drag.
// A drag inhales for DRAG_IN s, then breathes out or coughs.
void Sim::updateSmoking(const InputFrame &in, float dt) {
    smoke.coughT = fmaxf(0, smoke.coughT - dt);
    smoke.exhaleT += dt;
    if (weapon != WEAPON_CIGS) {
        if (smoke.burnT > 0) { putOutCigarette(); deckNoteT = 1.8f; deckNote = "you stub it out."; }
        return;
    }
    if (smoke.burnT > 0) {
        smoke.burnT -= dt;
        if (smoke.burnT <= 0) { putOutCigarette(); deckNoteT = 1.8f; deckNote = "it burns down to the filter."; }
    }
    if (smoke.dragT > 0) {
        float before = smoke.dragT;
        smoke.dragT = fmaxf(0, smoke.dragT - dt);
        const float out = DRAG_TIME - DRAG_IN;   // dragT when the breath turns
        if (before > out && smoke.dragT <= out) {
            smoke.exhaleT = 0;
            if (smoke.cough) {
                smoke.coughT = COUGH_TIME;
                play(Sfx::Cough, smokeRng.ri(0, 2));
            } else {
                play(Sfx::Exhale);
            }
        }
    }
    if (!in.playing || !in.fire || deathT > 0 || drinkT > 0 || smoke.dragT > 0) return;
    if (smoke.burnT <= 0) {
        if (cigs <= 0) { deckNoteT = 1.8f; deckNote = "the pack is empty."; return; }
        cigs--;
        smoke.burnT = SMOKE_TIME;
        play(Sfx::Lighter);
        return;
    }
    smoke.dragT = DRAG_TIME;
    smoke.burnT = fmaxf(0.5f, smoke.burnT - DRAG_BURN);
    smoke.cough = smokeRng.next() % COUGH_ODDS == 0;
    sanity = clampf(sanity + DRAG_SANITY, 0.0f, 1.0f);
    fear *= 0.6f;
    play(Sfx::Drag);
}
