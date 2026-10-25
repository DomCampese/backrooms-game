#include "game.h"
#include "input.h"
#include "port/held.h"
#include "port/scene.h"
#include "port/visible.h"
#include "textures.h"
#include "port/sprites.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>

static_assert(sizeof(PARTY) / sizeof(PARTY[0]) == PARTY_COLOURS, "the sim draws balloon colours from PARTY_COLOURS");

// A held model's transform: its axes along the frame's, scaled, at its origin.
static Matrix heldMatrix(const HeldFrame &f) {
    Matrix m{};
    m.m0=f.right.x*f.scale; m.m1=f.right.y*f.scale; m.m2=f.right.z*f.scale;
    m.m4=f.up.x*f.scale; m.m5=f.up.y*f.scale; m.m6=f.up.z*f.scale;
    m.m8=f.forward.x*f.scale; m.m9=f.forward.y*f.scale; m.m10=f.forward.z*f.scale;
    m.m12=f.pos.x; m.m13=f.pos.y; m.m14=f.pos.z; m.m15=1;
    return m;
}

// Every size and offset on the HUD was authored against the 1440x850 window the
// game opens in, and only the title card ever branched on resolution — so on a
// 4K display the whole thing shrank to an unreadable smear along the edges.
// hud() scales one of those numbers to the window in hand; every DrawText size
// and every offset measured from a screen edge goes through it. Fractions of the
// screen (sh/2, sw/3) are already resolution-independent and must not.
//
// The viewmodel scale (k = 1.25f in the draw* functions) is deliberately NOT
// this: that one is in gun-local metres and has nothing to do with pixels.
static int hud(float px) {
    int v = (int)(px * GetScreenHeight() / 850.0f + 0.5f);
    return v < 1 ? 1 : v;
}

// Nothing on the HUD sat on anything, so the bottom-left inventory block was
// already illegible over the Poolrooms' white tile at the window's own size.
// One dark offset behind every string — scaled with the text, and carrying the
// text's own alpha so a line fading in doesn't leave its shadow behind.
static void hudText(const char *t, int x, int y, int px, Color c) {
    int o = hud(1);
    DrawText(t, x + o, y + o, px, Fade(BLACK, c.a / 255.0f * 0.7f));
    DrawText(t, x, y, px, c);
}

// hud() scales type to the window's *height*, which is what the HUD corners
// need — but every centred line in here is a full-width row, and a phone held
// upright is short of width, not of height. At 412 px across, the letter-spaced
// title measured about 700 and ran off both edges, and the intro card's control
// list off the right. Shrink a line that does not fit rather than clipping it.
//
// 92% of the screen leaves a margin that reads as deliberate rather than as
// type touching the bezel. MeasureText is very nearly linear in size, so one
// proportional step lands within a pixel or two; the loop only rounds it down,
// because rounding up is the case that still overflows.
static int fitSize(const char *t, int px, int maxW) {
    if (px < 1) px = 1;
    if (maxW <= 0) return px;
    int w = MeasureText(t, px);
    if (w <= maxW) return px;
    px = px * maxW / (w > 0 ? w : 1);
    while (px > 1 && MeasureText(t, px) > maxW) px--;
    return px < 1 ? 1 : px;
}

static int hudFitW() { return (int)(GetScreenWidth() * 0.92f); }

// Centred on cx. Measuring at the scaled size rather than the authored one is
// the whole point: measure at 16 and draw at 38 and the line sits off-centre.
static void hudTextC(const char *t, int cx, int y, int px, Color c) {
    px = fitSize(t, px, hudFitW());
    hudText(t, cx - MeasureText(t, px) / 2, y, px, c);
}

// Right-aligned so its right edge lands on rx.
static void hudTextR(const char *t, int rx, int y, int px, Color c) {
    hudText(t, rx - MeasureText(t, px), y, px, c);
}

void Game::renderScene(double now) {
    syncLevelLook();
    if (captureTime >= 0) now = captureTime;
    // ---- render 3D scene into rt
    Camera3D cam = {};
    const SimView view = simView(sim);
    cam.position = toRl(view.eye);
    cam.target = Vector3Add(cam.position, toRl(view.forward));
    cam.up = toRl(view.up);
    cam.fovy = camcorder.fovY(view.fovY);
    cam.projection = CAMERA_PERSPECTIVE;

    float timeF = (float)now;
    Vector3 viewPos = cam.position;
    const FrameLight light = frameLight(sim, timeF, camcorder.irLamp());
    SetShaderValue(worldShader, locTime, &light.time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locBlackout, &light.blackout, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlash, &light.flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locFlashDir, &light.flashDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locIR, &light.ir, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locFlarePos, &light.flarePos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlareInt, &light.flareInt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locEntPos, &light.entPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locEntDark, &light.entDark, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locEntBlock, &light.entBlock, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locRoomMask, light.roomMask, SHADER_UNIFORM_VEC4);
    SetShaderValue(worldShader, locLamp, light.lamp, SHADER_UNIFORM_VEC4);
    SetShaderValue(worldShader, locLampCol, &light.lampCol, SHADER_UNIFORM_VEC3);
    Vector2 occOrigin = { (float)occOriginI, (float)occOriginK };
    float occN = occValid ? (float)OCC_N : 0.0f;
    SetShaderValue(worldShader, locOccOrigin, &occOrigin, SHADER_UNIFORM_VEC2);
    SetShaderValue(worldShader, locOccN, &occN, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(rt);
    // Long pool galleries fade into atmospheric colour beyond the streamed ring.
    ClearBackground(sim.level==2 ? Color{48,70,66,255} : BLACK);
    BeginMode3D(cam);
    // This storey's chunks in view, then the other storeys' that an opening
    // shows, each nearest first (port/visible.h). After this storey's, so the
    // floors and ceilings you are between have already filled the depth buffer
    // and most of the other floor is rejected before it is lit.
    const ChunkViewer viewer{ { sim.px, sim.eyeY, sim.pz }, sim.fwd, view.eye, view.up, cam.fovy,
                              rt.texture.width, rt.texture.height };
    const std::vector<VisibleChunk> inView = visibleChunks(sim.world, viewer, [&](int storey, int cx, int cz) {
        return chunkMeshes.find(sim.world, storey, cx, cz) != nullptr;
    });
    struct Drawn { const ChunkMeshes *data; float yOff; };
    std::vector<Drawn> visible;
    for (const VisibleChunk &c : inView) visible.push_back({ chunkMeshes.find(sim.world, c.storey, c.cx, c.cz), c.yOff });
    const int visibleCount = (int)visible.size();
    // The storey a chunk belongs to, for the shader's lighting (uDrawRel).
    auto setDrawRel = [&](float yOff) {
        float rel = sim.world.storeyH > 0.0f ? roundf(yOff / sim.world.storeyH) : 0.0f;
        SetShaderValue(worldShader, locDrawRel, &rel, SHADER_UNIFORM_FLOAT);
    };
    // Front to back lets depth rejection avoid expensive lighting on hidden rooms.
    for (int i=0; i<visibleCount; ++i) {
        const ChunkMeshes &chunk = *visible[i].data;
        Matrix xf = MatrixTranslate(0, visible[i].yOff, 0);
        setDrawRel(visible[i].yOff);
        for (int m=MESH_FLOOR; m<=MESH_PROPS; ++m)
            if (chunk.meshes[m].vertexCount > 0) DrawMesh(chunk.meshes[m], mats[m], xf);
        if (chunk.meshes[MESH_SCRAWL].vertexCount > 0)
            DrawMesh(chunk.meshes[MESH_SCRAWL], mats[MAT_SCRAWL], xf);
        if (chunk.meshes[MESH_FIXTURES].vertexCount > 0)
            DrawMesh(chunk.meshes[MESH_FIXTURES], mats[MAT_FIXTURES], xf);
    }
    // Share the frustum test with transparent geometry, and blend distant chunks
    // first. Within a chunk, the existing AO / water / glass order is retained.
    for (int i=visibleCount-1; i>=0; --i) {
        const ChunkMeshes &chunk = *visible[i].data;
        Matrix xf = MatrixTranslate(0, visible[i].yOff, 0);
        setDrawRel(visible[i].yOff);
        if (chunk.meshes[MESH_AO].vertexCount > 0)
            DrawMesh(chunk.meshes[MESH_AO], mats[MAT_AO], xf);
        if (chunk.meshes[MESH_WATER].vertexCount > 0)
            DrawMesh(chunk.meshes[MESH_WATER], mats[MAT_FLOOR], xf);
        if (chunk.meshes[MESH_GLASS].vertexCount > 0)
            DrawMesh(chunk.meshes[MESH_GLASS], mats[MAT_FLOOR], xf);
    }
    setDrawRel(0.0f);   // everything drawn after the chunks is on your storey
    // The landing tubes, the sim's scene, the pack, the flares, the hunter and
    // the rest, as shared/port/draw.h decides them for both builds.
    sceneBuf.clear();
    sceneDraws(sim, light, timeF, sceneBuf);
    for (const SceneDraw &d : sceneBuf) drawScene(d, cam);
    // Looking through the camcorder, whatever is in the other hand is out of frame.
    const Held held = camcorder.viewing() ? Held::None : heldItem(sim);
    if (held != Held::None) {
        // Held against a wall, the can falls inside that wall's shadow and goes
        // black in your hands. A viewmodel shouldn't be shadowed by the room it
        // is being held in, so switch the occlusion grid off for this one draw
        // — uOccN of 0 makes lightVis return "lit" everywhere — and put it back
        // straight after. Perspective still does the foreshortening.
        float occOff = 0.0f;
        SetShaderValue(worldShader, locOccN, &occOff, SHADER_UNIFORM_FLOAT);
        // ...and give it a floor of ambient, so a can held up in an unlit stretch
        // of corridor is still legible rather than a silhouette. Only lifts where
        // the room is genuinely dark; anywhere lit, the panels dominate this.
        const Vec3 &la = LEVELS[sim.level].amb;
        Vector3 vmAmb = { fmaxf(la.x, 0.150f), fmaxf(la.y, 0.142f), fmaxf(la.z, 0.128f) };
        SetShaderValue(worldShader, locAmb, &vmAmb, SHADER_UNIFORM_VEC3);
        // Where each is held is shared/port/held.cpp's, and what is drawn there
        // port/draw.cpp's, which the Unreal build reads too.
        if (held == Held::Revolver) drawHeldWeapon(cam);
        else {
            sceneBuf.clear();
            heldDraws(sim, view, timeF, sceneBuf);
            for (const SceneDraw &d : sceneBuf) drawScene(d, cam);
        }
        SetShaderValue(worldShader, locOccN, &occN, SHADER_UNIFORM_FLOAT);   // both as they were
        SetShaderValue(worldShader, locAmb, &la, SHADER_UNIFORM_VEC3);
    }
    EndMode3D();
    EndTextureMode();
}

// Held geometry belongs to the scene pass: perspective, room lighting and
// post-processing all affect it just as they affect the can and the deck.
void Game::drawHeldWeapon(const Camera3D &cam) {
    Vector3 right{sim.r2x, 0, sim.r2z};
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, toRl(sim.fwd)));
    const Matrix m = heldMatrix(heldWeapon(sim, simView(sim)));
    SetShaderValue(worldShader, locGloss, &REVOLVER_GLOSS, SHADER_UNIFORM_FLOAT);
    revolver.pose(sim.reloadT,sim.gunCd,sim.ammo);
    // Its steel reflects the room (uObjRefl in the world shader); nothing
    // else drawn has metal in its detail map, so nothing else pays for it.
    float refl = 1.0f;
    SetShaderValue(worldShader, locObjRefl, &refl, SHADER_UNIFORM_FLOAT);
    revolver.draw(mats[MAT_PROPS],m);
    refl = 0.0f;
    SetShaderValue(worldShader, locObjRefl, &refl, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locGloss, &LEVELS[sim.level].gloss, SHADER_UNIFORM_FLOAT);
    sceneBuf.clear();
    muzzleDraws(sim, fromRl(Vector3Transform(revolver.muzzlePosition, m)), fromRl(up), sceneBuf);
    for (const SceneDraw &d : sceneBuf) drawScene(d, cam);
}

// One of port/draw.h's draws, with raylib: a mesh through the world shader,
// or a shape or billboard unlit, as render.cpp has always drawn them.
void Game::drawScene(const SceneDraw &d, const Camera3D &cam) {
    if (d.isMesh) {
        const MeshDraw &md = d.mesh;
        const Mesh *meshes[(int)ObjMesh::Count] = { &crateMesh, &crateLidMesh, &canMesh, &deckMesh, &reelMesh,
                                                    &deckLampMesh, &flareMesh };
        const int matOf[3] = { MAT_PROPS, MAT_CAN, MAT_DECK };
        Material &mat = mats[matOf[(int)objMat(md.mesh)]];
        const float gloss = objGloss(md.mesh);
        if (gloss >= 0) SetShaderValue(worldShader, locGloss, &gloss, SHADER_UNIFORM_FLOAT);
        // colDiffuse multiplies the world shader's output: the deck's lamp breathes by it
        const unsigned char t8 = cl8(255 * md.tint);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = { t8, t8, t8, 255 };
        Matrix xf{};
        xf.m0 = md.xf.x.x; xf.m1 = md.xf.x.y; xf.m2 = md.xf.x.z;
        xf.m4 = md.xf.y.x; xf.m5 = md.xf.y.y; xf.m6 = md.xf.y.z;
        xf.m8 = md.xf.z.x; xf.m9 = md.xf.z.y; xf.m10 = md.xf.z.z;
        xf.m12 = md.xf.t.x; xf.m13 = md.xf.t.y; xf.m14 = md.xf.t.z; xf.m15 = 1;
        DrawMesh(*meshes[(int)md.mesh], mat, xf);
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        if (gloss >= 0) SetShaderValue(worldShader, locGloss, &LEVELS[sim.level].gloss, SHADER_UNIFORM_FLOAT);
        return;
    }
    const Shape &s = d.shape;
    const Color c = { s.colour.r, s.colour.g, s.colour.b, s.colour.a };
    switch (s.kind) {
    case ShapeKind::Cube: DrawCube(toRl(s.a), s.size.x, s.size.y, s.size.z, c); break;
    case ShapeKind::Sphere: DrawSphere(toRl(s.a), s.r0, c); break;
    case ShapeKind::Cylinder: DrawCylinder(toRl(s.a), s.r0, s.r1, s.h, s.sides, c); break;
    case ShapeKind::CylinderEx: DrawCylinderEx(toRl(s.a), toRl(s.b), s.r0, s.r1, s.sides, c); break;
    case ShapeKind::Billboard: {
        const Texture2D *tex[(int)ShapeTex::Count] = { &texParticle, &texParticle, &texClark, &texEntity,
                                                       &texEntityGlow, &texPartygoer, &texDog };
        if (s.additive) BeginBlendMode(BLEND_ADDITIVE);
        if (!s.depthWrite) { rlDrawRenderBatchActive(); rlDisableDepthMask(); }
        DrawBillboardRec(cam, *tex[(int)s.tex], { s.src[0], s.src[1], s.src[2], s.src[3] }, toRl(s.a),
                         { s.size.x, s.size.y }, c);
        if (!s.depthWrite) { rlDrawRenderBatchActive(); rlEnableDepthMask(); }
        if (s.additive) EndBlendMode();
        break;
    }
    }
}

void Game::renderUI(double now) {
    if (captureTime >= 0) now = captureTime;
    // ---- post + UI
    float timeF = (float)now;
    double elapsed = now - sim.runStart;
    BeginDrawing();
    ClearBackground(BLACK);
    SetShaderValue(postShader, locPTime, &timeF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, locPFear, &sim.fear, SHADER_UNIFORM_FLOAT);
    float submerged = sim.level==2 && sim.world.poolAt(cellOf(sim.px),cellOf(sim.pz)) ? clampf((WATER_Y-sim.eyeY)*8,0,1) : 0;
    SetShaderValue(postShader,locPWater,&submerged,SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, locPMigraine, &sim.migraine, SHADER_UNIFORM_FLOAT);
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    // Through the camcorder the tape's look replaces the eye's, and the
    // inventory and meters give way to its OSD.
    const bool filming = camcorder.viewing() && !sim.inMenu;
    if (filming) camcorder.drawViewfinder(rt, timeF, (float)elapsed);
    else {
        BeginShaderMode(postShader);
        DrawTextureRec(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height }, { 0, 0 }, WHITE);
        EndShaderMode();
    }
    if (!sim.inMenu && camcorder.raise > 0.0f && camcorder.raise < 1.0f)   // the eye meeting the finder
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 1.0f - fabsf(2.0f * camcorder.raise - 1.0f)));
    if (cleanShot && !sim.inMenu) { EndDrawing(); return; }

    if (sim.swimming && !sim.inMenu)
        hudTextC(inTouchActive() ? "hold JUMP  stay up   ·   let go  dive" : "hold SPACE  stay up   ·   let go  dive   ·   SHIFT  swim faster",
                 sw/2,sh-hud(42),hud(16),Color{174,221,214,230});

    if (sim.inMenu && sim.deathT > 0) {   // the run just ended; the card holds the title screen
        float a = clampf(sim.deathT > Sim::DEATH_CARD - 0.6f ? (Sim::DEATH_CARD - sim.deathT) / 0.6f : sim.deathT / 1.0f, 0, 1);
        if (camcorder.replaying()) {
            // The tape plays first; the words come up over its last frame.
            camcorder.drawReplay(timeF);
            a = fminf(clampf(sim.deathT, 0, 1), camcorder.cardAlpha());
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 10, 3, 3, 255 }, 0.66f * a));
            if (sim.deathT < 1.0f) DrawRectangle(0, 0, sw, sh, Fade(BLACK, 1.0f - sim.deathT));
        } else DrawRectangle(0, 0, sw, sh, Fade(Color{ 10, 3, 3, 255 }, 0.88f));
        hudTextC(sim.deathTitle, sw / 2, sh / 3, hud(54), Fade({ 178, 34, 24, 255 }, a));
        hudTextC(TextFormat("%s took you on %s", sim.deathBy, LEVELS[sim.deathLevel].name),
                 sw / 2, sh / 3 + hud(74), hud(20), Fade({ 176, 132, 122, 255 }, a * 0.95f));
        hudTextC(TextFormat("%02d:%02d   ·   %d m wandered   ·   %d hunter%s put down",
                            (int)sim.deathTime / 60, (int)sim.deathTime % 60, sim.deathM,
                            sim.deathKills, sim.deathKills == 1 ? "" : "s"),
                 sw / 2, sh / 3 + hud(106), hud(18), Fade({ 150, 118, 110, 255 }, a * 0.9f));
        hudTextC(TextFormat("run %d   ·   deepest ever  %s   ·   longest run  %02d:%02d",
                            sim.deathCount, LEVELS[sim.best.deepest].name, sim.best.longestRun / 60, sim.best.longestRun % 60),
                 sw / 2, sh / 3 + hud(140), hud(16), Fade({ 130, 106, 100, 255 }, a * 0.8f));
        if (sim.deathT < Sim::DEATH_CARD - Sim::DEATH_CARD_HOLD)
            hudTextC(inTouchActive() ? "tap anywhere to descend again" : "press any key to descend again",
                     sw / 2, sh * 2 / 3 + hud(30), hud(18),
                     Fade({ 168, 144, 112, 255 }, a * (0.45f + 0.55f * (0.5f + 0.5f * sinf(timeF * 3.0f)))));
        EndDrawing();
        return;
    }
    if (sim.inMenu) {   // title card over the drifting world
        // darken the top and bottom so the type reads over any hall
        DrawRectangleGradientV(0, 0, sw, sh / 2, Fade(BLACK, 0.62f), Fade(BLACK, 0.12f));
        DrawRectangleGradientV(0, sh / 2, sw, sh / 2, Fade(BLACK, 0.12f), Fade(BLACK, 0.72f));
        const char *t1 = "T H E   B A C K R O O M S";
        // faint flicker on the title, like a tired fluorescent
        float fl = 0.86f + 0.14f * sinf(timeF * 27.0f) * sinf(timeF * 41.3f + 0.7f);
        int ts = fitSize(t1, hud(62), hudFitW());
        int tw = MeasureText(t1, ts);
        DrawText(t1, sw / 2 - tw / 2 + hud(2), sh / 3 + hud(2), ts, Fade(BLACK, 0.55f));   // drop shadow
        DrawText(t1, sw / 2 - tw / 2, sh / 3, ts, Fade({ 228, 214, 158, 255 }, fl));
        const char *sub = "and everything under it";
        hudTextC(sub, sw / 2, sh / 3 + ts + hud(16), hud(20), { 150, 142, 108, 220 });
        // pulsing prompt
        float pl = 0.45f + 0.55f * (0.5f + 0.5f * sinf(timeF * 3.0f));
        // A phone has no key to press, and telling it to press one reads as the
        // game being broken rather than as a line written for a keyboard.
        const char *pr = inTouchActive() ? "tap anywhere to descend" : "press any key to descend";
        hudTextC(pr, sw / 2, sh * 2 / 3, hud(24), Fade({ 210, 198, 150, 255 }, pl));
        if (sim.best.escapes || sim.best.kills || sim.best.metres || sim.best.wins) {
            const char *tb = sim.best.tapes > 0
                ? TextFormat("best:  %d got out   ·   %d hunter%s put down   ·   %d m wandered   ·   %d tape%s found",
                             sim.best.wins, sim.best.kills, sim.best.kills == 1 ? "" : "s", sim.best.metres, sim.best.tapes, sim.best.tapes == 1 ? "" : "s")
                : TextFormat("best:  %d got out   ·   %d hunter%s put down   ·   %d m wandered",
                             sim.best.wins, sim.best.kills, sim.best.kills == 1 ? "" : "s", sim.best.metres);
            hudTextC(tb, sw / 2, sh * 2 / 3 + hud(40), hud(16), { 140, 132, 100, 200 });
        }
        if (sim.best.deepest > 0 || sim.best.longestRun > 0)
            hudTextC(TextFormat("deepest  %s   ·   longest run  %02d:%02d", LEVELS[sim.best.deepest].name,
                                sim.best.longestRun / 60, sim.best.longestRun % 60),
                     sw / 2, sh * 2 / 3 + hud(62), hud(16), { 140, 132, 100, 200 });
        const char *tc = inTouchActive()
            ? TextFormat("STICK move    AIM taps the sights up and down    LAMP torch    ITEM cycle    CAM camcorder    bank %d doubloons to leave",
                         Sim::ESCAPE_COST)
            : TextFormat("WASD move    SHIFT run    C squeeze    F flashlight    1/2/4 item    V camcorder    bank %d doubloons to leave",
                         Sim::ESCAPE_COST);
        hudTextC(tc, sw / 2, sh - hud(42), hud(15), { 128, 122, 96, 170 });
        EndDrawing();
        return;
    }

    if (!filming && sim.drinkT <= 0 && (sim.weapon == WEAPON_REVOLVER || (sim.weapon == WEAPON_FLARE && sim.flares > 0)))
        // Scaled like every other HUD element (BUG-10), and faded out as the
        // sights come up (the iron-sight work): with the revolver aimed the
        // blade is the sight, and a dot floating over it reads as a smudge.
        DrawCircle(sw / 2, sh / 2, hud(1.5f), Fade({230,220,190,110}, 1-sim.aimBlend));

    if (elapsed < 9.0 && sim.winT <= 0) {   // intro (suppressed while the escape screen is up)
        float a = 1.0f - clampf((float)elapsed / 3.0f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, a));
        float ta = clampf((float)elapsed / 1.5f, 0, 1) * (1.0f - clampf(((float)elapsed - 6.0f) / 3.0f, 0, 1));
        char t1[64];   // the level's name, letter-spaced
        int ti = 0;
        for (const char *p = LEVELS[sim.level].name; *p && ti < 60; p++) {
            t1[ti++] = *p;
            // a UTF-8 character is several bytes (Level 0's "·" is two), and
            // a space pushed between them draws as two '?' — space characters,
            // not bytes
            if (((unsigned char)p[1] & 0xC0) == 0x80) continue;
            t1[ti++] = ' ';
            if (*p == ' ') t1[ti++] = ' ';
        }
        t1[ti ? ti - 1 : 0] = 0;
        hudTextC(t1, sw / 2, sh / 3, hud(52), Fade({ 220, 205, 150, 255 }, ta));
        const char *t2 = "if you're reading this, you've already noclipped";
        hudTextC(t2, sw / 2, sh / 3 + hud(66), hud(18), Fade({ 160, 150, 110, 255 }, ta * 0.9f));
        const char *t3 = inTouchActive()
            ? "STICK walk   push past its ring to run   DUCK crouch   SQUEEZE   JUMP   LAMP torch   ITEM cycle   DRINK   MARK chalk   USE vend/pick up   CAM camcorder"
            : "WASD walk   SHIFT or W W run   CTRL crouch   C squeeze   SPACE jump   F flashlight   1/2/4 item   3 drink   M chalk   E vend/pick up   V camcorder";
        hudTextC(t3, sw / 2, sh - hud(60), hud(16), Fade({ 140, 132, 100, 255 }, ta * 0.8f));
        if (sim.best.escapes || sim.best.kills || sim.best.metres || sim.best.wins) {
            const char *tb = sim.best.tapes > 0
                ? TextFormat("best: %d got out  ·  %d hunter%s put down  ·  %d m wandered  ·  %d tape%s found",
                             sim.best.wins, sim.best.kills, sim.best.kills == 1 ? "" : "s", sim.best.metres, sim.best.tapes, sim.best.tapes == 1 ? "" : "s")
                : TextFormat("best: %d got out  ·  %d hunter%s put down  ·  %d m wandered",
                             sim.best.wins, sim.best.kills, sim.best.kills == 1 ? "" : "s", sim.best.metres);
            hudTextC(tb, sw / 2, sh / 3 + hud(98), hud(16), Fade({ 150, 140, 105, 255 }, ta * 0.8f));
        }
        const char *tg = TextFormat("bank %d doubloons · fight what hunts you for them · then take a door out", Sim::ESCAPE_COST);
        hudTextC(tg, sw / 2, sh / 3 + hud(128), hud(16), Fade({ 120, 200, 140, 255 }, ta * 0.75f));
    }
    if (sim.winT > 0) {   // you bought your way out and found a true door
        float a = clampf(sim.winT > 7.2f ? (8.0f - sim.winT) / 0.8f : sim.winT / 7.2f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(Color{ 6, 12, 8, 255 }, a * 0.93f));
        const char *t = "YOU ESCAPED THE BACKROOMS";
        hudTextC(t, sw / 2, sh / 3, hud(54), Fade({ 120, 235, 145, 255 }, a));
        const char *t2 = TextFormat("out the true door   ·   %02d:%02d   ·   %d m wandered   ·   %d hunter%s put down",
                                    (int)sim.winTime / 60, (int)sim.winTime % 60, sim.winM, sim.winKills, sim.winKills == 1 ? "" : "s");
        hudTextC(t2, sw / 2, sh / 3 + hud(74), hud(20), Fade({ 150, 200, 160, 255 }, a * 0.95f));
        const char *t3 = TextFormat("escape #%d   ·   best %d", sim.winCount, sim.best.wins);
        hudTextC(t3, sw / 2, sh / 3 + hud(106), hud(18), Fade({ 130, 175, 140, 255 }, a * 0.9f));
        const char *t4 = "...but the backrooms are patient. a new descent begins.";
        hudTextC(t4, sw / 2, sh - hud(78), hud(16), Fade({ 120, 150, 125, 255 }, a * 0.8f));
    }
    if (sim.fellT > 0) {   // the carpet gave way
        float a = clampf(sim.fellT / 4.0f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, a * 0.5f * clampf((sim.fellT - 3.4f) / 0.6f, 0, 1)));
        const char *t = "THE FLOOR GIVES WAY";
        hudTextC(t, sw / 2, sh / 2 - hud(30), hud(46), Fade({ 200, 180, 120, 255 }, a));
        const char *t2 = "...there was another floor under this one";
        hudTextC(t2, sw / 2, sh / 2 + hud(30), hud(18), Fade({ 150, 138, 110, 255 }, a * 0.9f));
    }
    if (sim.escapeT > 0) {
        float a = clampf(sim.escapeT > 5.4f ? (6.0f - sim.escapeT) / 0.6f : sim.escapeT / 5.4f, 0, 1);
        DrawRectangle(0, 0, sw, sh, Fade(WHITE, a * (sim.escapeT > 5.4f ? 0.9f : 0.12f)));
        const char *t = "YOU FOUND AN EXIT";
        hudTextC(t, sw / 2, sh / 2 - hud(60), hud(48), Fade({ 235, 228, 200, 255 }, a));
        const char *t2 = TextFormat("...it leads to %s.  %d m wandered  ·  %d escape%s  ·  %s",
                                    LEVELS[sim.level].name, (int)sim.distWalked, sim.escapeCount, sim.escapeCount == 1 ? "" : "s",
                                    TextFormat("%02d:%02d", (int)elapsed / 60, (int)elapsed % 60));
        hudTextC(t2, sw / 2, sh / 2 + hud(4), hud(18), Fade({ 180, 170, 140, 255 }, a));
    }
    if (sim.killT > 0) {
        float a = clampf(sim.killT / 3.0f, 0, 1);
        const char *t = (sim.level == 4) ? "THE PARTYGOER IS DOWN" : sim.clarkLevel() ? "PIRATE CLARK IS DOWN" : "THE SMILER IS DOWN";
        hudTextC(t, sw / 2, sh / 2 - hud(96), hud(44), Fade({ 205, 60, 40, 255 }, a));
        const char *t2 = TextFormat("...but nothing stays down, down here   ·   %d put down", sim.killCount);
        hudTextC(t2, sw / 2, sh / 2 - hud(44), hud(18), Fade({ 150, 122, 100, 255 }, a * 0.9f));
    }
    if (sim.closeCallT > 0) {   // it stood right beside you, and never knew
        float a = clampf(sim.closeCallT > 2.3f ? (3.0f - sim.closeCallT) / 0.7f : sim.closeCallT / 1.4f, 0, 1);
        const char *t = "IT STOOD RIGHT THERE";
        hudTextC(t, sw / 2, sh / 2 - hud(130), hud(30), Fade({ 200, 70, 60, 255 }, a));
        const char *t2 = "...and never saw you";
        hudTextC(t2, sw / 2, sh / 2 - hud(98), hud(15), Fade({ 160, 130, 120, 220 }, a * 0.9f));
    }
    if (sim.deckNoteT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // the deck just did something
        float a = clampf(sim.deckNoteT > 2.0f ? (2.6f - sim.deckNoteT) / 0.6f : sim.deckNoteT / 1.2f, 0, 1);
        hudTextC(sim.deckNote, sw / 2, sh - hud(150), hud(16), Fade({ 196, 168, 214, 230 }, a * 0.9f));
    }
    if (sim.tapeFoundT > 0) {   // a cassette recovered — someone else's fragment of the descent
        float a = clampf(sim.tapeFoundT > 2.6f ? (3.2f - sim.tapeFoundT) / 0.6f : sim.tapeFoundT / 1.6f, 0, 1);
        const char *t = "TAPE RECOVERED";
        hudTextC(t, sw / 2, sh - hud(150), hud(22), Fade({ 190, 178, 150, 255 }, a));
        hudTextC(sim.tapeLine, sw / 2, sh - hud(122), hud(15), Fade({ 150, 140, 118, 230 }, a * 0.9f));
    }
    if (!inCursorHidden() && !shotPath) {
        const char *t = "click to capture mouse";
        hudTextC(t, sw / 2, sh / 2 + hud(80), hud(20), { 200, 190, 150, 200 });
    }
    if (!filming) hudTextR(TextFormat("%d", GetFPS()), sw - hud(14), hud(12), hud(16), { 190, 180, 140, 150 });
    // persistent flashlight reminder until first use; small state dot + charge bar after
    if (sim.flashOn) everFlashed = true;
    if (!everFlashed && elapsed > 9.0 && !filming)
        // NOT an em dash: raylib's default font stops at Latin-1, and anything
        // past it draws as a literal "?" on the HUD. U+00B7 is inside the range.
        hudTextR("F · flashlight", sw - hud(16), sh - hud(28), hud(16), { 190, 180, 140, 160 });
    else if (sim.flashOn && !filming)
        hudTextR("[ flashlight ]", sw - hud(16), sh - hud(26), hud(14), { 235, 225, 180, 120 });
    if (!filming) {   // sanity: always up, because it is always going down
        const int w = hud(90), x = sw - w - hud(16), y = sh - hud(60), th = hud(5);
        DrawRectangle(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        // steady cream, souring toward red as it empties; the last stretch pulses
        Color bar = sim.sanity > 0.5f  ? Color{ 168, 196, 176, 165 }
                  : sim.sanity > 0.25f ? Color{ 214, 190, 120, 180 }
                                   : Color{ 214, 96, 84, 200 };
        if (sim.sanity < 0.25f) bar.a = (unsigned char)(150 + 80 * (0.5f + 0.5f * sinf((float)now * 4.2f)));
        DrawRectangle(x, y, (int)(w * sim.sanity), th, bar);
        hudTextR("sanity", x - hud(6), y - hud(4), hud(12), { 150, 142, 122, 120 });
    }
    if (sim.manilaCardT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // you found it
        float a = clampf(sim.manilaCardT > 4.2f ? (5.0f - sim.manilaCardT) / 0.8f : sim.manilaCardT / 1.6f, 0, 1);
        hudTextC("THE MANILA ROOM", sw / 2, sh / 2 - hud(150), hud(34), Fade({ 226, 206, 158, 255 }, a));
        hudTextC("...the hum is quieter here.  someone left notes on the table  [E]",
                 sw / 2, sh / 2 - hud(112), hud(16), Fade({ 190, 172, 132, 255 }, a * 0.9f));
    }
    if (sim.noteT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // a note from the table, held up to read
        float a = clampf(sim.noteT > 8.6f ? (9.0f - sim.noteT) / 0.4f : sim.noteT / 0.8f, 0, 1);
        int pw = std::min(sw - hud(40), hud(720)), ph = hud(170);
        int x0 = sw / 2 - pw / 2, y0 = sh / 2 - ph / 2 + hud(40);
        DrawRectangle(x0, y0, pw, ph, Fade({ 232, 222, 190, 255 }, 0.92f * a));
        DrawRectangleLines(x0, y0, pw, ph, Fade({ 150, 130, 96, 255 }, a));
        for (int ln = 0; ln < 4; ln++) {
            const char *t = MANILA_NOTES[sim.notePage][ln];
            if (!*t) continue;
            int fs = hud(18), tw = MeasureText(t, fs);
            if (tw > pw - hud(30)) { fs = fs * (pw - hud(30)) / tw; tw = MeasureText(t, fs); }
            DrawText(t, sw / 2 - tw / 2, y0 + hud(22) + ln * hud(34), fs,
                     Fade(ln == 3 ? Color{ 110, 96, 80, 255 } : Color{ 58, 50, 44, 255 }, a));
        }
    }
    if (sim.sanityWarnT > 0 && sim.winT <= 0 && sim.deathT <= 0) {   // it just slipped a notch
        float a = clampf(sim.sanityWarnT / 1.2f, 0, 1) * clampf((4.0f - sim.sanityWarnT) / 0.4f, 0, 1);
        hudTextC(sim.sanityLine, sw / 2, sh / 2 + hud(96), hud(19), Fade({ 206, 176, 176, 255 }, a * 0.9f));
    }
    if (!filming && (sim.flashOn || sim.battery < 0.99f)) {   // charge bar, once it's been used or spent at all
        const int w = hud(90), x = sw - w - hud(16), y = sh - hud(44), th = hud(5);   // one row below the sanity meter
        DrawRectangle(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        Color bar = sim.battery < 0.15f ? Color{ 220, 90, 70, 190 } : Color{ 200, 190, 150, 150 };
        DrawRectangle(x, y, (int)(w * sim.battery), th, bar);
        if (sim.flashOn && sim.battery < 0.15f) {
            const char *t = "battery low";
            float pulse = 0.5f + 0.5f * sinf((float)now * 5.0f);
            hudTextR(t, sw - hud(16), sh - hud(78), hud(13), Fade({ 220, 120, 100, 220 }, pulse));   // clear of the sanity meter
        }
    }
    if (!filming) {   // inventory, bottom-left; the selected weapon is lit
        // Draw each line straight off its TextFormat rather than holding the
        // pointers and drawing at the end: TextFormat hands back a slot in a
        // small rotating buffer, and this block now formats more lines than
        // there are slots, so a held pointer comes back as a later line's text.
        Color selc = { 235, 200, 130, 210 }, dimc = { 150, 138, 112, 110 };
        if (sim.tapes > 0)
            hudText(TextFormat("tapes  ×%d", sim.tapes), hud(16), sh - hud(138), hud(16), { 172, 162, 190, 170 });
        if (sim.keys > 0)
            hudText(TextFormat("keys  ×%d", sim.keys), hud(16), sh - hud(160), hud(16), { 214, 186, 110, 190 });
        if (sim.coins > 0 || sim.wayOpen())   // doubloons double as your ticket out (ESCAPE_COST to leave)
            DrawText(TextFormat("doubloons  ×%d / %d", sim.coins, Sim::ESCAPE_COST), 16, sh - 116, 16,
                     sim.wayOpen() ? Color{ 120, 230, 140, 210 } : Color{ 214, 178, 92, 170 });
        DrawText(TextFormat("3  almond water  ×%d", sim.almond), 16, sh - 94, 16,
                 sim.almond > 0 ? Color{ 150, 190, 235, 170 } : dimc);
        // One TextFormat call per line, drawn straight off it: the return is a
        // slot in a small rotating buffer, so a second call before this one is
        // drawn would hand the revolver the flare's string. Hence the ternaries
        // inside the format arguments rather than a second TextFormat.
        DrawText(sim.reloadT > 0 ? "1  revolver  [reloading]"
                    : TextFormat("1  revolver  %d/%d%s  · %s", sim.ammo, Sim::MAXAMMO,
                                 sim.ammo == 0 ? (inTouchActive() ? "  · LOAD" : "  · R") : "",
                                 inTouchActive() ? "tap AIM" : "hold RMB aim"),
                    16, sh - 50, 16, sim.weapon == WEAPON_REVOLVER ? selc : dimc);
        DrawText(TextFormat("2  flare  ×%d", sim.flares), 16, sh - 72, 16, sim.weapon == WEAPON_FLARE ? selc : dimc);
        DrawText(!sim.deck.carried
                     ? (sim.deck.playing ? "4  tape player  [running · left behind]"
                                     : inTouchActive() ? "4  tape player  [left behind · USE]"
                                                       : "4  tape player  [left behind · E]")
                 : sim.deck.playing ? TextFormat("4  tape player  [playing  %ds]", (int)sim.deck.t + 1)
                 : sim.tapes > 0    ? "4  tape player  [tape ready]"
                                : "4  tape player  [no tape]",
                hud(16), sh - hud(28), hud(16),
                sim.weapon == WEAPON_DECK ? (sim.deck.playing ? Color{ 205, 150, 235, 220 } : selc) : dimc);
    }
    if (sim.wayOpen() && sim.winT <= 0 && sim.deathT <= 0 && sim.escapeT <= 0) {   // you can leave now — go find a door
        const char *t = sim.level == 0 ? "the walls know you now  ·  find one that isn't cursed"
                                   : "the doors know you now  ·  find one that isn't cursed";
        float pl = 0.55f + 0.45f * sinf((float)now * 2.5f);
        hudTextC(t, sw / 2, hud(70), hud(20), Fade({ 120, 235, 145, 255 }, pl));
    }
    // No health bar: there are only two states worth showing, whole and one
    // hit from dead. A hit flashes the edges red, and they stay tinged for as
    // long as you are wounded, fading as you heal. When the red is gone, you
    // can take a hit again.
    if ((sim.hurtT > 0 || sim.health < 0.999f) && sim.deathT <= 0) {
        float a = fmaxf(sim.hurtT / Sim::HURT_GRACE * 0.55f, (1.0f - sim.health) * 0.45f);
        Color r0 = Fade({120, 0, 0, 255}, a), r1 = Fade({120, 0, 0, 255}, 0);
        int e = sh / 4;
        DrawRectangleGradientV(0, 0, sw, e, r0, r1);
        DrawRectangleGradientV(0, sh - e, sw, e, r1, r0);
        DrawRectangleGradientH(0, 0, e, sh, r0, r1);
        DrawRectangleGradientH(sw - e, 0, e, sh, r1, r0);
    }
    if (!filming && sim.stamina < 0.98f) {   // sprint bar, bottom centre
        const int w = hud(220), x = sw / 2 - w / 2, y = sh - hud(42), th = hud(6);
        DrawRectangle(x - hud(1), y - hud(1), w + hud(2), th + hud(2), { 0, 0, 0, 120 });
        DrawRectangle(x, y, (int)(w * sim.stamina), th,
                      sim.sprintExhausted ? Color{190,100,66,180} : Color{200,180,120,160});
        if (sim.sprintExhausted) hudText("catch your breath", x + hud(48), y + hud(11), hud(12), {195,156,119,180});
    }
    // Say which of the three states you are actually in. This line used to read
    // "hold still" whether or not stillness did anything, which was the one
    // instruction the game gave you and it was not true.
    const char *coverLine = nullptr;
    Color coverCol = { 140, 210, 165, 160 };
    if (sim.hidden && sim.hideBreakT > 0) {   // moving, and about to lose it
        coverLine = "[ hidden · you're giving it away ]";
        coverCol = { 228, 196, 128, 200 };
    } else if (sim.hidden) {
        coverLine = "[ hidden · hold still ]";
    } else if (sim.nearCover) {           // cover is right there, you are just too quick for it
        coverLine = "[ cover · stop to hide ]";
        coverCol = { 190, 180, 150, 140 };
    } else if (sim.squeezing) {
        coverLine = "[ squeezing ]";
    } else if (sim.crouchCur > 0.5f) {
        coverLine = "[ crouched ]";
        coverCol = { 180, 170, 140, 120 };
    }
    if (coverLine && !filming)
        hudTextC(coverLine, sw / 2, sh - hud(62), hud(14), coverCol);
    if (sim.level == 3 && sim.valveT > 0) {   // just closed one
        float a = clampf(sim.valveT / 1.6f, 0, 1);
        if (sim.pipesShut) {
            const char *t = "THE PIPES GO QUIET";
            hudTextC(t, sw / 2, sh / 2 - hud(110), hud(34), Fade({ 120, 230, 145, 255 }, a));
            const char *t2 = "...and the halls give up what they were holding";
            hudTextC(t2, sw / 2, sh / 2 - hud(74), hud(16), Fade({ 150, 200, 160, 255 }, a * 0.9f));
        } else {
            const char *t = TextFormat("VALVE SHUT   %d / %d", (int)sim.valvesTurned.size(), Sim::VALVES_NEEDED);
            hudTextC(t, sw / 2, sh / 2 - hud(100), hud(26), Fade({ 210, 180, 120, 255 }, a));
        }
    }
    if (sim.level == 3 && !sim.valvesTurned.empty() && !sim.pipesShut)   // standing tally
        hudText(TextFormat("valves  %d / %d", (int)sim.valvesTurned.size(), Sim::VALVES_NEEDED),
                hud(16), sh - hud(138), hud(16), { 198, 150, 96, 175 });
    if (sim.paused) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
        const char *t = "P A U S E D";
        hudTextC(t, sw / 2, sh / 2 - hud(42), hud(46), { 225, 212, 160, 235 });
        const char *t2 = "P to resume";
        hudTextC(t2, sw / 2, sh / 2 + hud(16), hud(18), { 165, 155, 120, 200 });
    }
    if (debugHud) {
        hudText(TextFormat("%d fps  pos(%.0f, %.0f)  chunks %d  entity %s  d=%.0fm  hidden=%d  batt=%.2f",
                            GetFPS(), sim.px, sim.pz, (int)sim.world.chunks.size(),
                            sim.ent.st == EState::Hidden ? "hidden" : sim.ent.st == EState::Stalk ? "STALKING"
                                : sim.ent.st == EState::Chase ? "CHASING"
                                : sim.ent.st == EState::Flee ? "FLEEING" : "DYING",
                            sim.entDist > 1e8 ? 0.0f : sim.entDist, sim.hidden ? 1 : 0, sim.battery),
                hud(12), hud(12), hud(18), { 230, 220, 160, 220 });
        hudText("dev: B blackout   E spawn   C chase   H hide   G flares   N next level",
                hud(12), hud(34), hud(16), { 200, 190, 140, 180 });
    }
    EndDrawing();
}
