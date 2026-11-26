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
    if (smoke.ash > 0) dropAsh();
    smoke.burnT = smoke.dragT = smoke.coughT = smoke.flickT = smoke.buttT = 0;
    smoke.cough = smoke.lucky = false;
}

// A fresh cigarette is lit: from the lighter, or from the butt of the last.
// The pack's lucky one is its last, and lighting it brings the luck.
void Sim::lightCigarette() {
    cigs--;
    smoke.burnT = SMOKE_TIME;
    smoke.buttT = 0;
    smoke.lucky = luckyLeft && cigs == 0;
    if (smoke.lucky) {
        luckyLeft = false;
        charmed = true;
        deckNoteT = 2.6f; deckNote = "the lucky one, smoked last.";
    }
}

// The ash on the tip comes off: flicked, or too long to hold together.
void Sim::dropAsh() {
    smoke.ashFallen = smoke.ash;
    smoke.ashFallT = 0;
    smoke.ash = 0;
}

// Seconds of burning turned into ash on the tip, which drops off past ASH_MAX.
void Sim::growAsh(float burnS) {
    smoke.ash += burnS * CIG_PAPER_M * CIG_BURNS / SMOKE_TIME;
    const bool shaken = velx * velx + velz * velz > ASH_SHAKE_SPEED * ASH_SHAKE_SPEED;
    if (smoke.ash > (shaken ? ASH_MAX_SHAKEN : ASH_MAX)) dropAsh();
}

// With a cigarette in hand, the fire button lights one, then takes a drag.
// A drag inhales for DRAG_IN s, then breathes out or coughs.
void Sim::updateSmoking(const InputFrame &in, float dt) {
    smoke.coughT = fmaxf(0, smoke.coughT - dt);
    smoke.exhaleT += dt;
    smoke.ashFallT += dt;
    smoke.sparkT = fmaxf(0, smoke.sparkT - dt);
    if (smoke.flickT > 0) {
        float before = smoke.flickT;
        smoke.flickT = fmaxf(0, smoke.flickT - dt);
        if (before > FLICK_TIME * 0.5f && smoke.flickT <= FLICK_TIME * 0.5f && smoke.ash > 0) {
            dropAsh();
            play(Sfx::AshTap);
        }
    }
    if (weapon != WEAPON_CIGS) {
        if (smoke.burnT > 0 || smoke.buttT > 0) { putOutCigarette(); deckNoteT = 1.8f; deckNote = "you stub it out."; }
        return;
    }
    if (smoke.buttT > 0) {
        smoke.buttT -= dt;
        if (smoke.buttT <= 0) { putOutCigarette(); deckNoteT = 1.8f; deckNote = "the butt goes out."; }
    }
    if (smoke.burnT > 0) {
        smoke.burnT -= dt;
        growAsh(dt);
        if (smoke.burnT <= 0) {   // down to the filter: it smoulders there a moment
            if (smoke.ash > 0) dropAsh();
            smoke.burnT = smoke.flickT = 0;
            smoke.buttT = BUTT_TIME;
            deckNoteT = 1.8f;
            deckNote = cigs > 0 ? "down to the filter. light the next from it?" : "down to the filter.";
        }
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
    if (in.playing && in.reload && smoke.burnT > 0 && smoke.dragT <= 0 && smoke.flickT <= 0 && deathT <= 0)
        smoke.flickT = FLICK_TIME;
    if (!in.playing || !in.fire || deathT > 0 || drinkT > 0 || smoke.dragT > 0 || smoke.flickT > 0) return;
    if (smoke.burnT <= 0) {
        if (cigs <= 0) { deckNoteT = 1.8f; deckNote = "the pack is empty."; return; }
        if (smoke.buttT > 0) {   // lit from the butt of the last
            lightCigarette();
            play(Sfx::Drag).atVolume(0.6f);
            if (!smoke.lucky) { deckNoteT = 1.8f; deckNote = "you light the next from the last."; }
            return;
        }
        if (smoke.fails < LIGHTER_FAILS_MAX && smokeRng.next() % LIGHTER_FAIL_ODDS == 0) {
            smoke.fails++;
            smoke.sparkT = SPARK_TIME;
            play(Sfx::LighterSpark);
            return;
        }
        smoke.fails = 0;
        lightCigarette();
        play(Sfx::Lighter);
        return;
    }
    smoke.dragT = DRAG_TIME;
    float burnt = smoke.burnT - fmaxf(0.5f, smoke.burnT - DRAG_BURN);
    smoke.burnT -= burnt;
    growAsh(burnt);
    smoke.cough = smokeRng.next() % COUGH_ODDS == 0;
    sanity = clampf(sanity + DRAG_SANITY, 0.0f, 1.0f);
    fear *= 0.6f;
    play(Sfx::Drag);
}

// Blackjack at the card tables (CARDS_* in sim.h).
int Sim::handValue(const uint8_t *ranks, int n) {
    int total = 0, aces = 0;
    for (int i = 0; i < n; i++) {
        total += ranks[i] >= 10 ? 10 : ranks[i];
        if (ranks[i] == 1) aces++;
    }
    if (aces > 0 && total + 10 <= 21) total += 10;   // one ace counts 11
    return total;
}

bool Sim::cardTableInReach(int &a, int &b) {
    if (level != CASINO || inMenu || deathT > 0) return false;
    const int pci = cellOf(px), pck = cellOf(pz);
    float nearest = CARDS_REACH * CARDS_REACH;
    bool found = false;
    for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
        const int ca = pci + dx, cb = pck + dz;
        if (world.propAt(ca, cb) != PROP_CARD_TABLE) continue;
        const float ddx = px - (ca * CELL + 1.0f), ddz = pz - (cb * CELL + 1.0f), d2 = ddx * ddx + ddz * ddz;
        if (d2 >= nearest) continue;
        nearest = d2; a = ca; b = cb; found = true;
    }
    return found;
}

void Sim::dealCards(int a, int b) {
    if (coins < CARDS_BET) {
        deckNoteT = 2.2f; deckNote = "one doubloon a hand.";
        return;
    }
    coins -= CARDS_BET;
    cards = CardHand{};
    cards.active = true;
    cards.a = a; cards.b = b;
    for (int i = 0; i < 2; i++) {   // drawn in dealing order: you, the dealer, you, the dealer
        cards.you[cards.nYou++] = (uint8_t)cardRng.ri(1, 13);
        cards.dealer[cards.nDealer++] = (uint8_t)cardRng.ri(1, 13);
    }
    play(Sfx::Click).atPitch(1.7f).atVolume(0.5f);
    if (handValue(cards.you, 2) == 21) standCards();   // a two-card 21 is settled at once
}

void Sim::hitCards() {
    if (cards.nYou >= CARDS_MAX) return;
    cards.you[cards.nYou++] = (uint8_t)cardRng.ri(1, 13);
    play(Sfx::Click).atPitch(1.7f).atVolume(0.5f);
    if (handValue(cards.you, cards.nYou) > 21) {
        cards.active = false; cards.revealed = true;
        cards.result = CardResult::Bust; cards.showT = CARDS_SHOW;
        play(Sfx::Click).atPitch(0.6f);
        deckNoteT = 2.2f; deckNote = "over twenty-one. the doubloon is gone.";
    }
}

void Sim::standCards() {
    cards.active = false;
    cards.revealed = true;
    cards.showT = CARDS_SHOW;
    const int you = handValue(cards.you, cards.nYou);
    const bool youNatural = cards.nYou == 2 && you == 21;
    const bool dealerNatural = handValue(cards.dealer, 2) == 21;
    if (!youNatural)
        while (handValue(cards.dealer, cards.nDealer) < 17 && cards.nDealer < CARDS_MAX)
            cards.dealer[cards.nDealer++] = (uint8_t)cardRng.ri(1, 13);
    const int dealer = handValue(cards.dealer, cards.nDealer);
    if (youNatural && !dealerNatural) cards.result = CardResult::Blackjack;
    else if (dealerNatural && !youNatural) cards.result = CardResult::Lose;
    else if (dealer > 21 || you > dealer) cards.result = CardResult::Win;
    else if (you == dealer) cards.result = CardResult::Push;
    else cards.result = CardResult::Lose;
    switch (cards.result) {
    case CardResult::Blackjack:
        coins += 3; play(Sfx::SlotWin).atPitch(1.2f);
        deckNoteT = 2.6f; deckNote = "twenty-one. three doubloons.";
        break;
    case CardResult::Win:
        coins += 2; play(Sfx::SlotWin).atPitch(1.1f).atVolume(0.7f);
        deckNoteT = 2.2f; deckNote = "you win. two doubloons.";
        break;
    case CardResult::Push:
        coins += 1; play(Sfx::Click).atPitch(1.2f);
        deckNoteT = 2.2f; deckNote = "a tie. the doubloon comes back.";
        break;
    default:
        play(Sfx::Click).atPitch(0.6f);
        deckNoteT = 2.2f; deckNote = "the dealer wins.";
        break;
    }
}

void Sim::updateCards(const InputFrame &in, float dt) {
    if (cardsEntry != levelEntries) {   // a new level or visit: no hand, a fresh shoe
        cardsEntry = levelEntries;
        cards = CardHand{};
        cardRng = Rng(hash64((uint64_t)world.seed ^ ((uint64_t)world.visit << 32) ^ 0xCA2D5ULL));
    }
    cards.showT = fmaxf(0.0f, cards.showT - dt);
    int a, b;
    const bool reach = cardTableInReach(a, b);
    cardsNear = reach && !cards.active;
    if (!cards.active) return;
    const float dx = px - (cards.a * CELL + 1.0f), dz = pz - (cards.b * CELL + 1.0f);
    if (deathT > 0 || level != CASINO || dx * dx + dz * dz > CARDS_LEAVE * CARDS_LEAVE || in.jumpPressed) standCards();
}
