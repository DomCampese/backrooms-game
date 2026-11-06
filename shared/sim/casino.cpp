// The casino's slot machines: a doubloon a pull, a prize drawn at the pull,
// and noise the hunter can follow.
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
