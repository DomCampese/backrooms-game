// Cash machines (ATMs), on every level. Where one is, is core's wallAtm, which
// the layout also builds from. The use key banks every doubloon carried;
// crouched, it draws the balance out, less a fee. The balance outlives a run:
// carried doubloons are lost when the player dies (beginDescent), banked ones
// are not.
#include "../core/fp_strict.h"
#include "sim.h"
#include <cmath>

bool Sim::atmInReach(float &ax, float &az) {
    const int pci = cellOf(px), pck = cellOf(pz);
    float nearest = ATM_REACH;
    bool found = false;
    for (int a = pci - 1; a <= pci + 2; a++) for (int b = pck - 1; b <= pck + 2; b++)
        for (int west = 0; west < 2; west++) {
            WallPhone w;
            if (!wallAtm(world, a, b, west != 0, w)) continue;
            // Within a metre of its floor, so not from the storey above or below.
            if (fabsf(py - (w.pos.y - ATM_Y)) > 1.0f) continue;
            float dx = px - w.pos.x, dz = pz - w.pos.z;
            if (dx * w.normal.x + dz * w.normal.z <= 0.0f) continue;   // behind the wall
            float d = sqrtf(dx * dx + dz * dz);
            if (d >= nearest) continue;
            nearest = d; ax = w.pos.x; az = w.pos.z; found = true;
        }
    return found;
}

void Sim::updateAtm(float dt) {
    atmNoiseT = fmaxf(0.0f, atmNoiseT - dt);
    float ax, az;
    atmNear = !inMenu && deathT <= 0 && atmInReach(ax, az);
}

void Sim::useAtm(float ax, float az, bool withdraw) {
    if (withdraw) {
        if (bank <= ATM_FEE) {
            play(Sfx::AtmBeep).atPitch(0.7f);
            deckNoteT = 2.6f;
            deckNote = bank > 0 ? "the balance does not cover the fee." : "the account is empty.";
            return;
        }
        const int out = bank - ATM_FEE;
        coins += out;
        bank = 0;
        play(Sfx::AtmCash);
        // The dispenser counting the doubloons out is a noise the hunter hears.
        atmNoiseT = ATM_NOISE_TIME; atmNoiseX = ax; atmNoiseZ = az;
        deckNoteT = 2.6f;
        deckNote = "the doubloons count out into the tray, less the fee.";
        return;
    }
    if (coins <= 0) {
        play(Sfx::AtmBeep).atPitch(0.7f);
        deckNoteT = 2.4f; deckNote = "nothing to deposit.";
        return;
    }
    bank += coins;
    deckNoteT = 2.6f;
    deckNote = "banked. they will keep if you die.";
    coins = 0;
    play(Sfx::AtmBeep);
}
