#include "game.h"
#include "raymath.h"
#include <cmath>

void Game::renderScene(double now) {
    // ---- render 3D scene into rt
    int pcx = fdiv(cellOf(px), CCELLS), pcz = fdiv(cellOf(pz), CCELLS);
    int pci = cellOf(px), pck = cellOf(pz);
    Camera3D cam = {};
    cam.position = { px, eyeY, pz };
    cam.target = Vector3Add(cam.position, fwd);
    // roll the up-vector a touch when strafing, so the camera leans into it
    float roll = leanCur * -0.035f;
    cam.up = { r2x * sinf(roll), cosf(roll), r2z * sinf(roll) };
    cam.fovy = fov;
    cam.projection = CAMERA_PERSPECTIVE;

    float timeF = (float)now;
    Vector3 viewPos = cam.position;
    SetShaderValue(worldShader, locTime, &timeF, SHADER_UNIFORM_FLOAT);
    float boSend = blackoutCur * (whisperT > 0 ? 0.86f : 1.0f);   // lights sag while it whispers
    SetShaderValue(worldShader, locBlackout, &boSend, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
    float flashSend = flashCur;
    if (flashOn && battery < 0.15f) {   // the battery's dying: a warning flicker before it goes dark
        float lowFlick = 0.55f + 0.45f * sinf(timeF * 19.0f) * sinf(timeF * 31.0f + 1.7f);
        flashSend *= clampf(battery / 0.15f, 0.15f, 1.0f) * (0.7f + 0.3f * lowFlick);
    }
    SetShaderValue(worldShader, locFlash, &flashSend, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locFlashDir, &fwd, SHADER_UNIFORM_VEC3);
    float flick = 0.91f + 0.09f * sinf(timeF * 31.0f) * sinf(timeF * 47.3f + 1.3f);
    float flareInt = flare.active
        ? clampf((FLAREBURN - flare.burn) * 6.0f, 0, 1) * clampf(flare.burn / 1.5f, 0, 1) * flick
        : 0.0f;
    Vector3 flarePos = { flare.x, flare.y + 0.06f, flare.z };
    if (!flare.active && muzzleT > 0) {   // muzzle flash borrows the flare point light
        flareInt = muzzleT / 0.09f * 1.3f;
        flarePos = { px + f2x * 0.6f, eyeY - 0.05f, pz + f2z * 0.6f };
    }
    SetShaderValue(worldShader, locFlarePos, &flarePos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlareInt, &flareInt, SHADER_UNIFORM_FLOAT);
    // the hunter's pool of dead light travels with it
    Vector3 entPos = { ent.x, ent.dispY + 1.0f, ent.z };
    float entDarkSend = (ent.st == EState::Hidden) ? 0.0f : entDarkCur;
    SetShaderValue(worldShader, locEntPos, &entPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locEntDark, &entDarkSend, SHADER_UNIFORM_FLOAT);
    // ...and it's solid: a beam that catches it throws its shadow down the hall
    float entBlock = (ent.st == EState::Hidden || ent.st == EState::Die) ? 0.0f : 1.0f;
    SetShaderValue(worldShader, locEntBlock, &entBlock, SHADER_UNIFORM_FLOAT);
    Vector2 occOrigin = { (float)occOriginI, (float)occOriginK };
    float occN = occValid ? (float)OCC_N : 0.0f;
    SetShaderValue(worldShader, locOccOrigin, &occOrigin, SHADER_UNIFORM_VEC2);
    SetShaderValue(worldShader, locOccN, &occN, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(rt);
    ClearBackground(BLACK);
    BeginMode3D(cam);
    Matrix ident = MatrixIdentity();
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
        int cx = pcx + dx, cz = pcz + dz;
        auto it = world.chunks.find(World::key(cx, cz));
        if (it == world.chunks.end() || !it->second.built) continue;
        float ccx = cx * CHUNK + CHUNK / 2 - px, ccz = cz * CHUNK + CHUNK / 2 - pz;
        if (ccx * f2x + ccz * f2z < -24.0f && (ccx * ccx + ccz * ccz) > 24 * 24) continue;
        for (int m = 0; m < 4; m++)
            if (it->second.meshes[m].vertexCount > 0)
                DrawMesh(it->second.meshes[m], mats[m], ident);
        if (it->second.meshes[5].vertexCount > 0)   // wall scrawl decals over the walls
            DrawMesh(it->second.meshes[5], mats[4], ident);
    }
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {   // blended passes last, over the opaque room
        auto it = world.chunks.find(World::key(pcx + dx, pcz + dz));
        if (it == world.chunks.end() || !it->second.built) continue;
        if (it->second.meshes[7].vertexCount > 0) DrawMesh(it->second.meshes[7], mats[5], ident);   // baked AO creases
        if (it->second.meshes[4].vertexCount > 0) DrawMesh(it->second.meshes[4], mats[0], ident);   // water
        if (it->second.meshes[6].vertexCount > 0) DrawMesh(it->second.meshes[6], mats[0], ident);   // window glass
    }
    // small props draw with raylib's unlit default shader, so estimate the room
    // light at each one (plus flare/muzzle glow) — no more balloons shining
    // through a blackout
    const LevelCfg &lc = LEVELS[level];
    float ambLumP = (lc.amb.x + lc.amb.y + lc.amb.z) / 3.0f;
    float edSend = (ent.st == EState::Hidden) ? 0.0f : entDarkCur;
    auto propLum = [&](float x, float y, float z) {
        float lum = lightAtCPU(x, y, z, blackoutCur, lc.ls, lc.wallH - 0.12f, lc.dead, lc.lightMul, ambLumP,
                               ent.x, ent.z, edSend);
        if (flareInt > 0.01f) {
            float fx = x - flarePos.x, fy = y - flarePos.y, fz = z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fy * fy + fz * fz)), 0.0f, 1.0f);
        }
        return 0.18f + 0.82f * lum;   // a hint of form survives the dark
    };
    auto lit = [](Color c, float f) {
        return Color{ cl8(c.r * f), cl8(c.g * f), cl8(c.b * f), c.a };
    };
    for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {   // world pickups nearby
        int a = pci + dx, b = pck + dz;
        if (taken.count(cellKey2(a, b))) continue;
        bool isB = bottleAt(a, b), isC = !isB && coinAt(a, b);
        bool isBat = !isB && !isC && batteryAt(a, b);
        bool isT = !isB && !isC && !isBat && tapeAt(a, b);
        if (!isB && !isC && !isBat && !isT) continue;
        float bxx = a * CELL + 1.0f, bzz = b * CELL + 1.0f;
        float gy = world.floorY(a, b);
        float pl = propLum(bxx, gy + 0.15f, bzz);
        if (isB) {   // almond water: the can itself, on the floor or on the furniture
            float shelf = bottleShelfY(a, b);
            float sy = gy + (shelf >= 0 ? shelf : 0.0f);
            // the mesh is built with its base on y=0, so this just puts the base
            // where it belongs. Spin each one by its cell so they aren't clones.
            float spin = (float)(ih(a, b, 0x0CA9u) & 1023) / 1023.0f * 6.2831853f;
            drawCan(MatrixMultiply(MatrixRotateY(spin), MatrixTranslate(bxx, sy, bzz)));
        } else if (isC) {
            float bob = sinf((float)now * 2.0f + a * 1.7f + b) * 0.03f;
            DrawCylinder({ bxx, gy + 0.06f + bob, bzz }, 0.085f, 0.085f, 0.024f, 12, lit({ 234, 188, 74, 255 }, pl));
        } else if (isBat) {   // a spare battery, standing on end
            DrawCube({ bxx, gy + 0.05f, bzz }, 0.06f, 0.10f, 0.06f, lit({ 70, 150, 90, 255 }, pl));
            DrawCylinder({ bxx, gy + 0.10f, bzz }, 0.018f, 0.018f, 0.02f, 8, lit({ 200, 180, 90, 255 }, pl));
        } else {   // a cassette tape, label up
            float bob = sinf((float)now * 1.6f + a * 2.1f + b) * 0.02f;
            DrawCube({ bxx, gy + 0.02f + bob, bzz }, 0.11f, 0.04f, 0.07f, lit({ 40, 38, 42, 255 }, pl));
            DrawCube({ bxx, gy + 0.041f + bob, bzz }, 0.075f, 0.001f, 0.05f, lit({ 210, 202, 182, 255 }, pl));
            DrawCylinder({ bxx - 0.028f, gy + 0.041f + bob, bzz }, 0.014f, 0.014f, 0.002f, 8, lit({ 30, 28, 30, 255 }, pl));
            DrawCylinder({ bxx + 0.028f, gy + 0.041f + bob, bzz }, 0.014f, 0.014f, 0.002f, 8, lit({ 30, 28, 30, 255 }, pl));
        }
    }
    for (auto &cw : coinsWorld) {
        float gy = world.floorY(cellOf(cw.x), cellOf(cw.z));
        float bob = sinf((float)now * 2.4f + cw.x) * 0.03f;
        DrawCylinder({ cw.x, gy + 0.06f + bob, cw.z }, 0.085f, 0.085f, 0.024f, 12,
                     lit({ 234, 188, 74, 255 }, propLum(cw.x, gy + 0.1f, cw.z)));
    }
    if (level == 4) {   // balloons nose against the ceiling, strings hanging down
        for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {
            int a = pci + dx, b = pck + dz;
            if (poppedBalloons.count(cellKey2(a, b))) continue;   // this one's been shot
            uint32_t h = ih(a, b, (uint32_t)world.seed ^ 0xBA11u);
            if (h % 17 != 0 || world.pillarAt(a, b)) continue;
            float bxx = a * CELL + 1.0f + (((h >> 4) & 7) / 7.0f - 0.5f) * 0.9f;
            float bzz = b * CELL + 1.0f + (((h >> 7) & 7) / 7.0f - 0.5f) * 0.9f;
            float bob = sinf((float)now * 0.8f + a * 1.3f + b * 2.1f) * 0.05f;
            float by = world.wallH - 0.21f + bob;
            float pl = propLum(bxx, by, bzz) * 0.85f;
            DrawSphere({ bxx, by, bzz }, 0.17f, lit(PARTY[(h >> 10) % 5], pl));
            DrawCylinderEx({ bxx, by - 0.15f, bzz }, { bxx + 0.04f, by - 0.95f, bzz + 0.02f },
                           0.005f, 0.005f, 4, lit({ 190, 185, 175, 150 }, pl));
        }
        for (int dx = -6; dx <= 6; dx++) for (int dz = -6; dz <= 6; dz++) {   // balloon bunches tied to party tables
            int a = pci + dx, b = pck + dz;
            if (poppedTableBunches.count(cellKey2(a, b))) continue;   // this bunch has been shot
            Vector3 bpos[4], tie; Color bcol[4];
            int nb = tableBalloonBunch(a, b, bpos, bcol, tie);        // same positions the aim uses
            for (int k = 0; k < nb; k++) {
                float sway = sinf((float)now * 0.9f + a * 1.7f + k * 2.3f) * 0.04f;
                float bxx = bpos[k].x + sway, by = bpos[k].y, bzz = bpos[k].z;
                float pl = propLum(bxx, by, bzz) * 0.9f;
                DrawSphere({ bxx, by, bzz }, 0.15f, lit(bcol[k], pl));
                DrawCylinderEx({ bxx, by - 0.13f, bzz }, { tie.x + 0.02f, tie.y + 0.02f, tie.z },
                               0.004f, 0.004f, 4, lit({ 200, 195, 185, 150 }, pl));
            }
        }
        for (auto &c : confetti) {   // bursts still tumbling to the carpet
            float pl = propLum(c.pos.x, c.pos.y, c.pos.z);
            float fade = clampf(c.life * 1.6f, 0, 1);
            Color cc = lit({ c.col.r, c.col.g, c.col.b, (unsigned char)(255 * fade) }, pl);
            DrawCube(c.pos, 0.05f, 0.05f, 0.05f, cc);
        }
    }
    for (auto &cm : chalk)
        if (fabsf(cm.x - px) < 30 && fabsf(cm.z - pz) < 30) {
            Color cc = lit({ 228, 228, 218, 200 }, propLum(cm.x, cm.y + 0.1f, cm.z));
            DrawCylinderEx({ cm.x - 0.18f, cm.y, cm.z - 0.18f }, { cm.x + 0.18f, cm.y, cm.z + 0.18f },
                           0.014f, 0.014f, 5, cc);
            DrawCylinderEx({ cm.x - 0.18f, cm.y, cm.z + 0.18f }, { cm.x + 0.18f, cm.y, cm.z - 0.18f },
                           0.014f, 0.014f, 5, cc);
        }
    if (wayOpen()) {   // enough doubloons: real exits burn green — the way out
        float pulse = 0.7f + 0.3f * sinf((float)now * 3.0f);
        for (int dx = -6; dx <= 6; dx++) for (int dz = -6; dz <= 6; dz++) {
            int i = pci + dx, k = pck + dz;
            float gx3 = -1, gz3 = -1;
            if (world.wallNVal(i, k) == 2) { gx3 = i * CELL + 1.0f; gz3 = k * CELL; }
            else if (world.wallWVal(i, k) == 2) { gx3 = i * CELL; gz3 = k * CELL + 1.0f; }
            else continue;
            if (world.cursedExit(i, k)) continue;   // cursed doors stay a trap, never green
            Vector3 gp = { gx3, 1.15f, gz3 };
            DrawSphere(gp, 0.34f, { 150, 255, 170, (unsigned char)(150 * pulse) });   // bright core
            DrawSphere(gp, 0.62f, { 90, 255, 120, (unsigned char)(95 * pulse) });
            DrawSphere(gp, 1.35f, { 60, 235, 110, (unsigned char)(40 * pulse) });     // wide halo
        }
    }
    if (level == 3) {   // valve wheels on their standpipes: red while open, green once shut
        for (int dx = -7; dx <= 7; dx++) for (int dz = -7; dz <= 7; dz++) {
            int a = pci + dx, b = pck + dz;
            if (!world.valveAt(a, b)) continue;
            float vx = a * CELL + 1.0f, vz = b * CELL + 1.0f;
            float wy = world.floorY(a, b) + 1.13f;
            bool shut = valvesTurned.count(cellKey2(a, b)) > 0;
            float pl = propLum(vx, wy, vz);
            Color rim = shut ? Color{ 96, 210, 128, 255 } : Color{ 196, 62, 48, 255 };
            // a handwheel lying flat on a vertical pipe: rim, punched centre, hub
            DrawCylinderEx({ vx, wy - 0.022f, vz }, { vx, wy + 0.022f, vz }, 0.21f, 0.21f, 12, lit(rim, pl));
            DrawCylinderEx({ vx, wy - 0.03f, vz }, { vx, wy + 0.03f, vz }, 0.125f, 0.125f, 12,
                           lit({ 84, 60, 44, 255 }, pl));
            DrawCylinderEx({ vx, wy - 0.05f, vz }, { vx, wy + 0.05f, vz }, 0.045f, 0.045f, 8,
                           lit({ 122, 96, 70, 255 }, pl));
        }
    }
    for (int i = 0; i < MAXDOGS; i++) {   // the pack, low and wide against the red
        const Dog &d = dogs[i];
        if (d.st == DState::Gone) continue;
        float ddx = d.x - px, ddz = d.z - pz;
        float dd = sqrtf(ddx * ddx + ddz * ddz);
        if (dd > 42) continue;
        const LevelCfg &c = LEVELS[level];
        float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
        float lum = lightAtCPU(d.x, d.dispY + 0.5f, d.z, blackoutCur,
                               c.ls, c.wallH - 0.12f, c.dead, c.lightMul, ambLum);
        if (flashCur > 0.05f) {
            float d2 = dd * dd + 1e-4f;
            float cone = powf(fmaxf((ddx * fwd.x + ddz * fwd.z) / (dd + 1e-4f), 0.0f), 26.0f);
            lum = clampf(lum + flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {
            float fx = d.x - flarePos.x, fz = d.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fx * fx + fz * fz)), 0.0f, 1.0f);
        }
        float fade = d.st == DState::Yelp ? clampf(1.0f - d.life / 2.6f, 0, 1) : 1.0f;
        unsigned char l8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(expf(-dd * c.fogDen) * 1.6f, 0, 1) * fade);
        // shoulder height ~0.75 m, and a long body — drawn wide, not tall
        DrawBillboardRec(cam, texDog, { 0, 0, 192, 128 },
                         { d.x, d.dispY + 0.46f, d.z }, { 1.45f, 0.97f }, { l8, l8, l8, al });
    }
    if (flare.active) {   // the flare itself: hot core, orange halo, stub of a body
        Vector3 fp = { flare.x, flare.y + 0.05f, flare.z };
        DrawCylinder({ flare.x, flare.y - 0.03f, flare.z }, 0.018f, 0.022f, 0.09f, 8, { 130, 30, 22, 255 });
        DrawSphere(fp, 0.035f + 0.012f * flick, { 255, 240, 208, 255 });
        DrawSphere(fp, 0.13f, { 255, 120, 40, (unsigned char)(90 * flareInt) });
        DrawSphere(fp, 0.30f, { 255, 70, 20, (unsigned char)(28 * flareInt) });
    }
    if (ent.st != EState::Hidden && entDist < 45) {
        const LevelCfg &c = LEVELS[level];
        float ambLum = (c.amb.x + c.amb.y + c.amb.z) / 3.0f;
        float eg = ent.dispY;
        float lum = lightAtCPU(ent.x, eg + 0.95f, ent.z, blackoutCur,
                               c.ls, c.wallH - 0.12f, c.dead, c.lightMul, ambLum,
                               ent.x, ent.z, entDarkCur);   // it stands in its own pool of dead light
        if (flashCur > 0.05f) {   // flashlight picks him out of the dark
            float vx2 = ent.x - px, vz2 = ent.z - pz;
            float d2 = vx2 * vx2 + vz2 * vz2 + 1e-4f, dl = sqrtf(d2);
            float cone = powf(fmaxf((vx2 * fwd.x + vz2 * fwd.z) / dl, 0.0f), 26.0f);
            lum = clampf(lum + flashCur * cone * 7.5f / (1.0f + 0.10f * d2), 0.0f, 1.0f);
        }
        if (flareInt > 0.01f) {   // flare glow (or muzzle flash) reaches him too
            float fvx = ent.x - flarePos.x, fvz = ent.z - flarePos.z;
            lum = clampf(lum + flareInt * 3.0f / (1.0f + 0.30f * (fvx * fvx + fvz * fvz)), 0.0f, 1.0f);
        }
        float sink = 0, dieA = 1;
        if (ent.st == EState::Die) {   // crumples into the carpet
            float t = clampf(ent.life / 1.2f, 0, 1);
            sink = 1.1f * t * t; dieA = 1.0f - t;
        }
        float fogf = expf(-entDist * c.fogDen);
        unsigned char lum8 = cl8(40 + 215 * lum);
        unsigned char al = cl8(255 * clampf(fogf * 1.6f, 0, 1) * dieA);
        // LEVEL FUN has its own resident; everywhere else it's Pirate Clark
        Texture2D &spr = (level == 4) ? texPartygoer : texEntity;
        DrawBillboardRec(cam, spr, { 0, 0, 128, 256 },
                         { ent.x, eg + 0.98f - sink, ent.z }, { 0.98f, 1.96f }, { lum8, lum8, lum8, al });
    }
    if (drinkT > 0) drawDrinkCan(cam);   // in the 3D pass: perspective does the foreshortening
    EndMode3D();
    EndTextureMode();
}

void Game::renderUI(double now) {
    // ---- post + UI
    float timeF = (float)now;
    double elapsed = now - runStart;
    BeginDrawing();
    ClearBackground(BLACK);
    SetShaderValue(postShader, locPTime, &timeF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, locPFear, &fear, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(postShader);
    DrawTextureRec(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height }, { 0, 0 }, WHITE);
    EndShaderMode();

    int sw = GetScreenWidth(), sh = GetScreenHeight();

    if (inMenu) {   // title card over the drifting world
        // darken the top and bottom so the type reads over any hall
        DrawRectangleGradientV(0, 0, sw, sh / 2, Fade(BLACK, 0.62f), Fade(BLACK, 0.12f));
        DrawRectangleGradientV(0, sh / 2, sw, sh / 2, Fade(BLACK, 0.12f), Fade(BLACK, 0.72f));
        const char *t1 = "T H E   B A C K R O O M S";
        // faint flicker on the title, like a tired fluorescent
        float fl = 0.86f + 0.14f * sinf(timeF * 27.0f) * sinf(timeF * 41.3f + 0.7f);
        int ts = sh > 720 ? 66 : 48;
        int tw = MeasureText(t1, ts);
        DrawText(t1, sw / 2 - tw / 2 + 2, sh / 3 + 2, ts, Fade(BLACK, 0.55f));   // drop shadow
        DrawText(t1, sw / 2 - tw / 2, sh / 3, ts, Fade({ 228, 214, 158, 255 }, fl));
        const char *sub = "Level 0 — and everything under it";
        DrawText(sub, sw / 2 - MeasureText(sub, 20) / 2, sh / 3 + ts + 16, 20, { 150, 142, 108, 220 });
        // pulsing prompt
        float pl = 0.45f + 0.55f * (0.5f + 0.5f * sinf(timeF * 3.0f));
        const char *pr = "press any key to descend";
        DrawText(pr, sw / 2 - MeasureText(pr, 24) / 2, sh * 2 / 3, 24, Fade({ 210, 198, 150, 255 }, pl));
        if (bestEsc || bestKill || bestM || bestWins) {
            const char *tb = bestTapes > 0
                ? TextFormat("best:  %d got out   ·   %d clark%s put down   ·   %d m wandered   ·   %d tape%s found",
                             bestWins, bestKill, bestKill == 1 ? "" : "s", bestM, bestTapes, bestTapes == 1 ? "" : "s")
                : TextFormat("best:  %d got out   ·   %d clark%s put down   ·   %d m wandered",
                             bestWins, bestKill, bestKill == 1 ? "" : "s", bestM);
            DrawText(tb, sw / 2 - MeasureText(tb, 16) / 2, sh * 2 / 3 + 40, 16, { 140, 132, 100, 200 });
        }
        const char *tc = TextFormat("WASD move    SHIFT run    F flashlight    1/2 weapon    bank %d doubloons to leave",
                                    ESCAPE_COST);
        DrawText(tc, sw / 2 - MeasureText(tc, 15) / 2, sh - 42, 15, { 128, 122, 96, 170 });
        EndDrawing();
        return;
    }

    if (drinkT <= 0 && (weapon == 1 || (weapon == 0 && flares > 0))) {   // viewmodel, bottom-right
        // reload: the muzzle dips while the cylinder is out, then comes back up
        float dip = (weapon == 1 && reloadT > 0)
                  ? sinf(clampf(1.0f - reloadT / 1.8f, 0.0f, 1.0f) * 3.14159f) : 0.0f;
        float deg = (weapon == 1 ? 210.0f + recoil * 16.0f - dip * 26.0f : 236.0f);
        float rad = deg * DEG2RAD;
        float dirx = cosf(rad), diry = sinf(rad);   // along the barrel
        float nx = -diry, ny = dirx;                // toward the top of the gun
        float bobX = sinf(bobPhase * 3.14159f) * 3.0f * bobAmt;
        Vector2 piv = weapon == 1
            ? Vector2{ sw - 165.0f + bobX - dirx * recoil * 22.0f,
                       sh - 30.0f + fabsf(bobX) * 0.6f - diry * recoil * 22.0f + dip * 16.0f }
            : Vector2{ sw - 130.0f + bobX, sh - 12.0f + fabsf(bobX) * 0.6f };
        // gun-local frame: t along the barrel, s toward the sights
        const float k = 1.25f;   // overall viewmodel scale
        auto pt = [&](float t, float s) {
            return Vector2{ piv.x + (dirx * t + nx * s) * k, piv.y + (diry * t + ny * s) * k };
        };
        auto quad = [&](Vector2 a, Vector2 b, Vector2 c2, Vector2 d2, Color col) {
            DrawTriangle(a, b, c2, col); DrawTriangle(a, c2, d2, col);
        };
        auto box = [&](float t0, float t1, float s0, float s1, Color col) {
            quad(pt(t0, s1), pt(t0, s0), pt(t1, s0), pt(t1, s1), col);
        };
        if (weapon == 1) {   // revolver, kicks with recoil
            Color steel = { 40, 38, 44, 255 }, steel2 = { 57, 54, 62, 255 };
            Color dark = { 21, 20, 24, 255 }, wood = { 88, 58, 38, 255 };
            Color glint = { 86, 84, 96, 255 };
            // grip rakes back and down off the bottom of the screen
            Vector2 gv = { (-dirx * 0.42f - nx * 0.91f) * 70.0f * k, (-diry * 0.42f - ny * 0.91f) * 70.0f * k };
            Vector2 g0 = pt(-14, -8), g1 = pt(12, -8);
            quad(g0, g1, { g1.x + gv.x, g1.y + gv.y },
                 { g0.x + gv.x - dirx * 8 * k, g0.y + gv.y - diry * 8 * k }, wood);
            box(-16, 34, -10, 8, steel);                    // frame rear + recoil shield
            box(-6, 90, 8, 13, steel);                      // top strap over the cylinder
            box(30, 76, -14, 13, steel2);                   // cylinder bulge
            box(43, 46, -12, 11, dark);                     // cylinder flutes
            box(59, 62, -12, 11, dark);
            box(82, 168, -3, 11, steel);                    // barrel
            box(88, 138, -8, -3, steel2);                   // ejector rod shroud under it
            box(163, 168, -3, 11, dark);                    // muzzle band
            box(-26, -14, 9, 19, steel2);                   // hammer spur
            box(-10, -2, 13, 17, steel);                    // rear sight
            box(156, 163, 11, 17, steel);                   // front sight
            box(82, 163, 9, 11, glint);                     // ceiling light rides the barrel
            box(30, 76, 11, 13, glint);                     // and the cylinder
            DrawRing(pt(24, -15), 7.5f * k, 10.5f * k, 0, 360, 24, steel);   // trigger guard
            box(20, 24, -16, -9, dark);                     // trigger
            if (muzzleSmoke > 0.02f) {   // powder haze curling off the muzzle, drifting up
                Vector2 tip = pt(178, 4);
                float s = muzzleSmoke;
                for (int i = 0; i < 4; i++) {
                    float t = (float)now * 1.4f + i * 1.9f;
                    float rise = (1.0f - s) * 26.0f + i * 7.0f;
                    Vector2 pv = { tip.x + sinf(t) * (5 + i * 3) + dirx * i * 5,
                                   tip.y - rise + diry * i * 5 };
                    unsigned char al = (unsigned char)(clampf(s * 0.5f - i * 0.06f, 0, 1) * 90);
                    DrawCircleV(pv, (11 + i * 6) * (1.4f - s * 0.4f), { 150, 148, 150, al });
                }
            }
            if (muzzleT > 0) {
                float mt = muzzleT / 0.09f;
                Vector2 tip = pt(180, 4);
                DrawCircleV(tip, 46 * mt, { 255, 150, 60, (unsigned char)(90 * mt) });
                DrawCircleV(tip, 24 * mt, { 255, 225, 140, (unsigned char)(210 * mt) });
            }
        } else {   // road flare in hand, cap out, ready to strike and throw
            Color body = { 168, 42, 32, 255 }, edge = { 206, 74, 56, 255 };
            Color capc = { 56, 26, 22, 255 }, band = { 216, 204, 184, 255 };
            box(0, 96, -11, 11, body);                      // red tube
            box(0, 90, 7, 11, edge);                        // light along the top
            box(-4, 2, -9, 9, capc);                        // butt end
            box(78, 90, -12, 12, band);                     // striker band
            box(90, 100, -9, 9, capc);                      // cap
        }
        DrawCircle(sw / 2, sh / 2, 2.0f, { 230, 220, 190, 110 });   // aiming dot
    }

    if (elapsed < 9.0 && winT <= 0) {   // intro (suppressed while the escape screen is up)
        float a = 1.0f - clampf((float)elapsed / 3.0f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, a));
        float ta = clampf((float)elapsed / 1.5f, 0, 1) * (1.0f - clampf(((float)elapsed - 6.0f) / 3.0f, 0, 1));
        char t1[64];   // the level's name, letter-spaced
        int ti = 0;
        for (const char *p = LEVELS[level].name; *p && ti < 60; p++) {
            t1[ti++] = *p;
            t1[ti++] = ' ';
            if (*p == ' ') t1[ti++] = ' ';
        }
        t1[ti ? ti - 1 : 0] = 0;
        DrawText(t1, sw / 2 - MeasureText(t1, 52) / 2, sh / 3, 52, Fade({ 220, 205, 150, 255 }, ta));
        const char *t2 = "if you're reading this, you've already noclipped";
        DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 3 + 66, 18, Fade({ 160, 150, 110, 255 }, ta * 0.9f));
        const char *t3 = "WASD walk   SHIFT run   CTRL crouch   SPACE jump   F flashlight   1/2 weapon   3 drink   M chalk   E vend";
        DrawText(t3, sw / 2 - MeasureText(t3, 16) / 2, sh - 60, 16, Fade({ 140, 132, 100, 255 }, ta * 0.8f));
        if (bestEsc || bestKill || bestM || bestWins) {
            const char *tb = bestTapes > 0
                ? TextFormat("best: %d got out  ·  %d clark%s put down  ·  %d m wandered  ·  %d tape%s found",
                             bestWins, bestKill, bestKill == 1 ? "" : "s", bestM, bestTapes, bestTapes == 1 ? "" : "s")
                : TextFormat("best: %d got out  ·  %d clark%s put down  ·  %d m wandered",
                             bestWins, bestKill, bestKill == 1 ? "" : "s", bestM);
            DrawText(tb, sw / 2 - MeasureText(tb, 16) / 2, sh / 3 + 98, 16, Fade({ 150, 140, 105, 255 }, ta * 0.8f));
        }
        const char *tg = TextFormat("bank %d doubloons — fight Clark for them — then take a door out", ESCAPE_COST);
        DrawText(tg, sw / 2 - MeasureText(tg, 16) / 2, sh / 3 + 128, 16, Fade({ 120, 200, 140, 255 }, ta * 0.75f));
    }
    if (winT > 0) {   // you bought your way out and found a true door
        float a = clampf(winT > 7.2f ? (8.0f - winT) / 0.8f : winT / 7.2f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(Color{ 6, 12, 8, 255 }, a * 0.93f));
        const char *t = "YOU ESCAPED THE BACKROOMS";
        DrawText(t, sw / 2 - MeasureText(t, 54) / 2, sh / 3, 54, Fade({ 120, 235, 145, 255 }, a));
        const char *t2 = TextFormat("out the true door   ·   %02d:%02d   ·   %d m wandered   ·   %d clark%s put down",
                                    (int)winTime / 60, (int)winTime % 60, winM, winKills, winKills == 1 ? "" : "s");
        DrawText(t2, sw / 2 - MeasureText(t2, 20) / 2, sh / 3 + 74, 20, Fade({ 150, 200, 160, 255 }, a * 0.95f));
        const char *t3 = TextFormat("escape #%d   ·   best %d", winCount, bestWins);
        DrawText(t3, sw / 2 - MeasureText(t3, 18) / 2, sh / 3 + 106, 18, Fade({ 130, 175, 140, 255 }, a * 0.9f));
        const char *t4 = "...but the backrooms are patient. a new descent begins.";
        DrawText(t4, sw / 2 - MeasureText(t4, 16) / 2, sh - 78, 16, Fade({ 120, 150, 125, 255 }, a * 0.8f));
    }
    if (caughtT > 0) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, clampf(caughtT / 2.4f * 1.8f, 0, 1)));
        if (caughtT > 0.5f) {
            const char *t = (level == 4) ? "THE PARTYGOER FOUND YOU" : "PIRATE CLARK FOUND YOU";
            DrawText(t, sw / 2 - MeasureText(t, 60) / 2, sh / 2 - 30, 60, { 170, 20, 12, 255 });
            const char *t2 = TextFormat("you wake up somewhere else   ·   %d m wandered   ·   taken %d time%s",
                                        (int)distWalked, caughtCount, caughtCount == 1 ? "" : "s");
            DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 46, 18, { 120, 90, 80, 255 });
        }
    }
    if (fellT > 0) {   // the carpet gave way
        float a = clampf(fellT / 4.0f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, a * 0.5f * clampf((fellT - 3.4f) / 0.6f, 0, 1)));
        const char *t = "THE FLOOR GIVES WAY";
        DrawText(t, sw / 2 - MeasureText(t, 46) / 2, sh / 2 - 30, 46, Fade({ 200, 180, 120, 255 }, a));
        const char *t2 = "...there was another floor under this one";
        DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 30, 18, Fade({ 150, 138, 110, 255 }, a * 0.9f));
    }
    if (escapeT > 0) {
        float a = clampf(escapeT > 5.4f ? (6.0f - escapeT) / 0.6f : escapeT / 5.4f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(WHITE, a * (escapeT > 5.4f ? 0.9f : 0.12f)));
        const char *t = "YOU FOUND AN EXIT";
        DrawText(t, sw / 2 - MeasureText(t, 48) / 2, sh / 2 - 60, 48, Fade({ 235, 228, 200, 255 }, a));
        const char *t2 = TextFormat("...it leads to %s.  %d m wandered  ·  %d escape%s  ·  %s",
                                    LEVELS[level].name, (int)distWalked, escapeCount, escapeCount == 1 ? "" : "s",
                                    TextFormat("%02d:%02d", (int)elapsed / 60, (int)elapsed % 60));
        DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 4, 18, Fade({ 180, 170, 140, 255 }, a));
    }
    if (killT > 0) {
        float a = clampf(killT / 3.0f, 0, 1);
        const char *t = (level == 4) ? "THE PARTYGOER IS DOWN" : "PIRATE CLARK IS DOWN";
        DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 96, 44, Fade({ 205, 60, 40, 255 }, a));
        const char *t2 = TextFormat("...but nothing stays down, down here   ·   %d put down", killCount);
        DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 - 44, 18, Fade({ 150, 122, 100, 255 }, a * 0.9f));
    }
    if (closeCallT > 0) {   // it stood right beside you, and never knew
        float a = clampf(closeCallT > 2.3f ? (3.0f - closeCallT) / 0.7f : closeCallT / 1.4f, 0, 1);
        const char *t = "IT STOOD RIGHT THERE";
        DrawText(t, sw / 2 - MeasureText(t, 30) / 2, sh / 2 - 130, 30, Fade({ 200, 70, 60, 255 }, a));
        const char *t2 = "...and never saw you";
        DrawText(t2, sw / 2 - MeasureText(t2, 15) / 2, sh / 2 - 98, 15, Fade({ 160, 130, 120, 220 }, a * 0.9f));
    }
    if (tapeFoundT > 0) {   // a cassette recovered — someone else's fragment of the descent
        float a = clampf(tapeFoundT > 2.6f ? (3.2f - tapeFoundT) / 0.6f : tapeFoundT / 1.6f, 0, 1);
        const char *t = "TAPE RECOVERED";
        DrawText(t, sw / 2 - MeasureText(t, 22) / 2, sh - 150, 22, Fade({ 190, 178, 150, 255 }, a));
        DrawText(tapeLine, sw / 2 - MeasureText(tapeLine, 15) / 2, sh - 122, 15, Fade({ 150, 140, 118, 230 }, a * 0.9f));
    }
    if (!IsCursorHidden() && !shotPath) {
        const char *t = "click to capture mouse";
        DrawText(t, sw / 2 - MeasureText(t, 20) / 2, sh / 2 + 80, 20, { 200, 190, 150, 200 });
    }
    DrawText(TextFormat("%d", GetFPS()), sw - MeasureText(TextFormat("%d", GetFPS()), 16) - 14, 12, 16, { 190, 180, 140, 150 });
    // persistent flashlight reminder until first use; small state dot + charge bar after
    if (flashOn) everFlashed = true;
    if (!everFlashed && elapsed > 9.0)
        DrawText("F — flashlight", sw - MeasureText("F — flashlight", 16) - 16, sh - 28, 16, { 190, 180, 140, 160 });
    else if (flashOn)
        DrawText("[ flashlight ]", sw - MeasureText("[ flashlight ]", 14) - 16, sh - 26, 14, { 235, 225, 180, 120 });
    {   // your grip on the place: always up, because it is always going down
        int bw = 90, bx4 = sw - bw - 16, by4 = sh - 60;
        DrawRectangle(bx4 - 1, by4 - 1, bw + 2, 7, { 0, 0, 0, 120 });
        // steady cream, souring toward red as it empties; the last stretch pulses
        Color sc = sanity > 0.5f ? Color{ 168, 196, 176, 165 }
                 : sanity > 0.25f ? Color{ 214, 190, 120, 180 }
                                  : Color{ 214, 96, 84, 200 };
        if (sanity < 0.25f) sc.a = (unsigned char)(150 + 80 * (0.5f + 0.5f * sinf((float)now * 4.2f)));
        DrawRectangle(bx4, by4, (int)(bw * sanity), 5, sc);
        DrawText("grip", bx4 - MeasureText("grip", 12) - 6, by4 - 4, 12, { 150, 142, 122, 120 });
    }
    if (sanityWarnT > 0 && winT <= 0 && caughtT <= 0) {   // it just slipped a notch
        float a = clampf(sanityWarnT / 1.2f, 0, 1) * clampf((4.0f - sanityWarnT) / 0.4f, 0, 1);
        DrawText(sanityLine, sw / 2 - MeasureText(sanityLine, 19) / 2, sh / 2 + 96, 19,
                 Fade({ 206, 176, 176, 255 }, a * 0.9f));
    }
    if (flashOn || battery < 0.99f) {   // charge bar, once it's been used or spent at all
        int bw = 90, bx3 = sw - bw - 16, by3 = sh - 44;   // one row below the grip meter
        DrawRectangle(bx3 - 1, by3 - 1, bw + 2, 7, { 0, 0, 0, 120 });
        Color bc = battery < 0.15f ? Color{ 220, 90, 70, 190 } : Color{ 200, 190, 150, 150 };
        DrawRectangle(bx3, by3, (int)(bw * battery), 5, bc);
        if (flashOn && battery < 0.15f) {
            const char *t = "battery low";
            float pl = 0.5f + 0.5f * sinf((float)now * 5.0f);
            DrawText(t, sw - MeasureText(t, 13) - 16, sh - 78, 13, Fade({ 220, 120, 100, 220 }, pl));   // clear of the grip meter
        }
    }
    {   // inventory, bottom-left; the selected weapon is lit
        const char *w0 = TextFormat("1  flare  ×%d", flares);
        const char *w1 = reloadT > 0 ? "2  revolver  [reloading]"
                                     : TextFormat("2  revolver  %d/%d%s", ammo, MAXAMMO, ammo == 0 ? "  — R" : "");
        Color selc = { 235, 200, 130, 210 }, dimc = { 150, 138, 112, 110 };
        if (tapes > 0)
            DrawText(TextFormat("tapes  ×%d", tapes), 16, sh - 116, 16, { 172, 162, 190, 170 });
        if (coins > 0 || wayOpen())   // doubloons double as your ticket out (ESCAPE_COST to leave)
            DrawText(TextFormat("doubloons  ×%d / %d", coins, ESCAPE_COST), 16, sh - 94, 16,
                     wayOpen() ? Color{ 120, 230, 140, 210 } : Color{ 214, 178, 92, 170 });
        DrawText(TextFormat("3  almond water  ×%d", almond), 16, sh - 72, 16,
                 almond > 0 ? Color{ 150, 190, 235, 170 } : dimc);
        DrawText(w0, 16, sh - 50, 16, weapon == 0 ? selc : dimc);
        DrawText(w1, 16, sh - 28, 16, weapon == 1 ? selc : dimc);
    }
    if (wayOpen() && winT <= 0 && caughtT <= 0 && escapeT <= 0) {   // you can leave now — go find a door
        const char *t = "the doors know you now  —  find one that isn't cursed";
        float pl = 0.55f + 0.45f * sinf((float)now * 2.5f);
        DrawText(t, sw / 2 - MeasureText(t, 20) / 2, 70, 20, Fade({ 120, 235, 145, 255 }, pl));
    }
    if (stamina < 0.98f) {   // sprint bar, bottom centre
        int bw = 220, bx2 = sw / 2 - bw / 2, by2 = sh - 42;
        DrawRectangle(bx2 - 1, by2 - 1, bw + 2, 8, { 0, 0, 0, 120 });
        DrawRectangle(bx2, by2, (int)(bw * stamina), 6, { 200, 180, 120, 160 });
    }
    if (hidden)
        DrawText("[ hidden — hold still ]", sw / 2 - MeasureText("[ hidden — hold still ]", 14) / 2, sh - 62, 14,
                 { 140, 210, 165, 160 });
    else if (crouchCur > 0.5f)
        DrawText("[ crouched ]", sw / 2 - MeasureText("[ crouched ]", 14) / 2, sh - 62, 14, { 180, 170, 140, 120 });
    if (level == 3 && (valveT > 0 || (pipesShut && valveT > 0))) {   // just closed one
        float a = clampf(valveT / 1.6f, 0, 1);
        if (pipesShut) {
            const char *t = "THE PIPES GO QUIET";
            DrawText(t, sw / 2 - MeasureText(t, 34) / 2, sh / 2 - 110, 34, Fade({ 120, 230, 145, 255 }, a));
            const char *t2 = "...and the halls give up what they were holding";
            DrawText(t2, sw / 2 - MeasureText(t2, 16) / 2, sh / 2 - 74, 16, Fade({ 150, 200, 160, 255 }, a * 0.9f));
        } else {
            const char *t = TextFormat("VALVE SHUT   %d / %d", (int)valvesTurned.size(), VALVES_NEEDED);
            DrawText(t, sw / 2 - MeasureText(t, 26) / 2, sh / 2 - 100, 26, Fade({ 210, 180, 120, 255 }, a));
        }
    }
    if (level == 3 && !valvesTurned.empty() && !pipesShut)   // standing tally
        DrawText(TextFormat("valves  %d / %d", (int)valvesTurned.size(), VALVES_NEEDED),
                 16, sh - 138, 16, { 198, 150, 96, 175 });
    if (paused) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
        const char *t = "P A U S E D";
        DrawText(t, sw / 2 - MeasureText(t, 46) / 2, sh / 2 - 42, 46, { 225, 212, 160, 235 });
        const char *t2 = "P to resume";
        DrawText(t2, sw / 2 - MeasureText(t2, 18) / 2, sh / 2 + 16, 18, { 165, 155, 120, 200 });
    }
    if (debugHud) {
        DrawText(TextFormat("%d fps  pos(%.0f, %.0f)  chunks %d  entity %s  d=%.0fm  hidden=%d  batt=%.2f",
                            GetFPS(), px, pz, (int)world.chunks.size(),
                            ent.st == EState::Hidden ? "hidden" : ent.st == EState::Stalk ? "STALKING"
                                : ent.st == EState::Chase ? "CHASING"
                                : ent.st == EState::Flee ? "FLEEING" : "DYING",
                            entDist > 1e8 ? 0.0f : entDist, hidden ? 1 : 0, battery),
                 12, 12, 18, { 230, 220, 160, 220 });
        DrawText("dev: B blackout   E spawn   C chase   H hide   G flares   N next level",
                 12, 34, 16, { 200, 190, 140, 180 });
    }
    EndDrawing();
}

// One can, lit by the room like every other surface. Alpha 255 on the vertices
// puts it down the shader's textured branch.
void Game::drawCan(Matrix xf) {
    DrawMesh(canMesh, mats[6], xf);
}

// The drink, in three dimensions. The 2D version faked foreshortening by
// squashing a sprite; here the can is real geometry held in front of the camera,
// so the projection does it properly — it grows as it closes on your face, the
// barrel shortens as it comes over, and the lid opens toward you on its own.
void Game::drawDrinkCan(const Camera3D &cam) {
    float el = DRINK_TIME - drinkT;
    auto ease = [](float t) { t = clampf(t, 0, 1); return t * t * (3.0f - 2.0f * t); };
    float up  = ease(el / 0.42f) * (1.0f - ease((el - 1.36f) / 0.39f));   // in, then away
    float tip = ease((el - 0.34f) / 0.34f) * (1.0f - ease((el - 1.30f) / 0.34f));
    float bob = 0;                                    // three swallows, timed to the sound
    for (int g = 0; g < 3; g++) {
        float gt = el - (0.40f + g * 0.36f);
        if (gt > 0 && gt < 0.26f) bob += sinf(gt / 0.26f * 3.14159f);
    }

    // camera basis, matching the convention the rest of the game uses
    Vector3 F = fwd;
    Vector3 Rt = { r2x, 0, r2z };
    Vector3 Up = Vector3Normalize(Vector3CrossProduct(Rt, F));

    // where the base of the can sits, relative to your eye. It comes in from
    // below and to the right, then draws in toward the middle as you tilt it.
    // It has to come AT you, so it tracks in toward the middle of the view and
    // closes most of the gap to your face while it does.
    float dist = 0.52f - tip * 0.20f;
    float side = 0.19f - tip * 0.115f;
    float vert = -0.52f + up * 0.38f + tip * 0.06f + bob * 0.006f;
    float bx4 = sinf(bobPhase * 3.14159f) * 0.012f * bobAmt;
    Vector3 pos = Vector3Add(cam.position,
                  Vector3Add(Vector3Scale(F, dist),
                  Vector3Add(Vector3Scale(Rt, side + bx4), Vector3Scale(Up, vert))));

    // Held items are drawn bigger than life or they read as toys at arm's
    // length; this is the usual viewmodel cheat, not a modelling error.
    const float SCALE = 1.30f;
    // Pitch is the main move: the can's axis swings over toward the camera so the
    // lid comes to your mouth, which is what drinking actually looks like from
    // behind your own eyes. Rolling it in the screen plane instead just reads as
    // tipping it out sideways. A little roll on top keeps it off dead-centre so
    // some of the barrel and the label stay in view.
    // 38 degrees is the ceiling worth using: enough that the lid is clearly
    // swinging toward your mouth, not so much that the barrel disappears behind
    // it and you are left looking at a metal disc.
    float pitch2 = (38.0f * DEG2RAD) * tip + (4.0f * DEG2RAD) * bob;
    float roll2  = (10.0f * DEG2RAD) * tip + (2.0f * DEG2RAD) * bob;
    Vector3 x0 = Rt, y0 = Up, z0 = Vector3Negate(F);  // label faces the camera
    // pitch about the can's own right axis: the lid comes toward you
    Vector3 y1 = Vector3Add(Vector3Scale(y0, cosf(pitch2)), Vector3Scale(z0, sinf(pitch2)));
    Vector3 z1 = Vector3Add(Vector3Scale(y0, -sinf(pitch2)), Vector3Scale(z0, cosf(pitch2)));
    // roll about that: the top leans away to the left, toward your mouth
    Vector3 tiltX = Vector3Add(Vector3Scale(x0, cosf(roll2)), Vector3Scale(y1, sinf(roll2)));
    Vector3 tiltY = Vector3Add(Vector3Scale(x0, -sinf(roll2)), Vector3Scale(y1, cosf(roll2)));
    Vector3 tiltZ = z1;

    // the mesh sits base-on-origin, so step back down its own axis to put the
    // middle of the can at pos and let it turn about its centre
    pos = Vector3Subtract(pos, Vector3Scale(tiltY, 0.061f * SCALE));   // 0.061 = half the can

    Matrix m = { 0 };
    m.m0 = tiltX.x * SCALE; m.m1 = tiltX.y * SCALE; m.m2 = tiltX.z * SCALE;
    m.m4 = tiltY.x * SCALE; m.m5 = tiltY.y * SCALE; m.m6 = tiltY.z * SCALE;
    m.m8 = tiltZ.x * SCALE; m.m9 = tiltZ.y * SCALE; m.m10 = tiltZ.z * SCALE;
    m.m12 = pos.x;          m.m13 = pos.y;          m.m14 = pos.z;
    m.m15 = 1.0f;
    drawCan(m);
    DrawMesh(handMesh, mats[6], m);   // the grip turns with it
}
