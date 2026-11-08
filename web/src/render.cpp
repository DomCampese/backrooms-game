#include "game.h"
#include "input.h"
#include "port/held.h"
#include "port/hud.h"
#include "port/scene.h"
#include "port/visible.h"
#include "textures.h"
#include "port/sprites.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include <ctime>

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

void Game::renderScene(double now) {
    syncLevelLook();
    if (captureTime >= 0) now = captureTime;
    // ---- render 3D scene into rt
    Camera3D cam = {};
    const SimView view = simView(sim);
    cam.position = toRl(view.eye);
    cam.target = Vector3Add(cam.position, toRl(view.forward));
    cam.up = toRl(view.up);
    cam.fovy = view.fovY;
    cam.projection = CAMERA_PERSPECTIVE;

    float timeF = (float)now;
    Vector3 viewPos = cam.position;
    const FrameLight light = frameLight(sim, timeF);
    SetShaderValue(worldShader, locTime, &light.time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locBlackout, &light.blackout, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locViewPos, &viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlash, &light.flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locFlashDir, &light.flashDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlarePos, &light.flarePos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locFlareInt, &light.flareInt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locEntPos, &light.entPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldShader, locEntDark, &light.entDark, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locEntBlock, &light.entBlock, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locRoomMask, light.roomMask, SHADER_UNIFORM_VEC4);
    SetShaderValue(worldShader, locLamp, light.lamp, SHADER_UNIFORM_VEC4);
    SetShaderValue(worldShader, locLampCol, &light.lampCol, SHADER_UNIFORM_VEC3);
    const Vec3 &lvAmb = LEVELS[sim.level].amb;
    const Vector3 amb = { lvAmb.x * light.ambScale, lvAmb.y * light.ambScale, lvAmb.z * light.ambScale };
    SetShaderValue(worldShader, locAmb, &amb, SHADER_UNIFORM_VEC3);
    Vector2 occOrigin = { (float)occOriginI, (float)occOriginK };
    float occN = occValid ? (float)OCC_N : 0.0f;
    SetShaderValue(worldShader, locOccOrigin, &occOrigin, SHADER_UNIFORM_VEC2);
    SetShaderValue(worldShader, locOccN, &occN, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(rt);
    // The Poolrooms clear to their fog colour, since its halls are longer than the
    // streamed area, and Level 94 to its sky.
    const Vec3 clear = clearColour(sim.level, sim.blackoutCur);
    ClearBackground({ (unsigned char)(clear.x * 255), (unsigned char)(clear.y * 255), (unsigned char)(clear.z * 255), 255 });
    BeginMode3D(cam);
    // This storey's chunks in view, then the other storeys' that an opening
    // shows, each nearest first (port/visible.h). After this storey's, so the
    // floors and ceilings around the player have already filled the depth
    // buffer and most of the other storeys fail the depth test before shading.
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
    // The sim's scene. Items on a pool floor are drawn now, before the water,
    // because the water writes depth and would hide them.
    sceneBuf.clear();
    sceneDraws(sim, light, timeF, sceneBuf);
    setDrawRel(0.0f);
    for (const SceneDraw &d : sceneBuf) if (d.sunken) drawScene(d, cam);
    // Shapes wait in raylib's batch until a flush; unflushed, they would be
    // drawn after the water and fail its depth.
    rlDrawRenderBatchActive();
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
    // A revolver not yet found, before its glow, which adds over it.
    if (Xform loose; looseRevolver(sim, timeF, loose)) {
        Matrix m{};
        m.m0 = loose.x.x; m.m1 = loose.x.y; m.m2 = loose.x.z;
        m.m4 = loose.y.x; m.m5 = loose.y.y; m.m6 = loose.y.z;
        m.m8 = loose.z.x; m.m9 = loose.z.y; m.m10 = loose.z.z;
        m.m12 = loose.t.x; m.m13 = loose.t.y; m.m14 = loose.t.z; m.m15 = 1;
        drawRevolver(m, 0, 0, Sim::MAXAMMO);
    }
    // The landing tubes, the sim's scene, the flares, the hunter and
    // the rest, as shared/port/draw.h decides them for both builds.
    for (const SceneDraw &d : sceneBuf) if (!d.sunken) drawScene(d, cam);
    const Held held = heldItem(sim);
    if (held != Held::None) {
        // Next to a wall the held item falls in the wall's shadow and goes black.
        // Turn the occlusion grid off for this draw (uOccN = 0 makes lightVis
        // return lit everywhere) and restore it straight after.
        float occOff = 0.0f;
        SetShaderValue(worldShader, locOccN, &occOff, SHADER_UNIFORM_FLOAT);
        // Also give it a minimum ambient, so it is visible in an unlit corridor.
        // This only matters where the room is dark.
        Vector3 vmAmb = { fmaxf(amb.x, 0.150f), fmaxf(amb.y, 0.142f), fmaxf(amb.z, 0.128f) };
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
        SetShaderValue(worldShader, locAmb, &amb, SHADER_UNIFORM_VEC3);
    }
    EndMode3D();
    EndTextureMode();
}

// Held geometry is drawn in the scene pass, so perspective, room lighting and
// post-processing apply to it as to the can and the deck.
void Game::drawHeldWeapon(const Camera3D &cam) {
    Vector3 right{sim.r2x, 0, sim.r2z};
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, toRl(sim.fwd)));
    const Matrix m = heldMatrix(heldWeapon(sim, simView(sim)));
    drawRevolver(m, sim.reloadT, sim.gunCd, sim.ammo);
    sceneBuf.clear();
    muzzleDraws(sim, fromRl(Vector3Transform(revolver.muzzlePosition, m)), fromRl(up), sceneBuf);
    for (const SceneDraw &d : sceneBuf) drawScene(d, cam);
}

void Game::drawRevolver(const Matrix &m, float reloadT, float gunCd, int ammo) {
    SetShaderValue(worldShader, locGloss, &REVOLVER_GLOSS, SHADER_UNIFORM_FLOAT);
    revolver.pose(reloadT, gunCd, ammo);
    // The room reflection (uObjRefl in the world shader) is on only for the
    // revolver, the one object with metal in its detail map.
    float refl = 1.0f;
    SetShaderValue(worldShader, locObjRefl, &refl, SHADER_UNIFORM_FLOAT);
    revolver.draw(mats[MAT_PROPS], m);
    refl = 0.0f;
    SetShaderValue(worldShader, locObjRefl, &refl, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldShader, locGloss, &LEVELS[sim.level].gloss, SHADER_UNIFORM_FLOAT);
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
                                                       &texEntityGlow, &texPartygoer, &texUsher, &texAnimation };
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

// A list of 2D draws in window pixels (port/hud.h), with raylib.
static void drawHud(const std::vector<HudDraw> &draws) {
    for (const HudDraw &d : draws) {
        Color c = { d.col.r, d.col.g, d.col.b, d.col.a }, c2 = { d.col2.r, d.col2.g, d.col2.b, d.col2.a };
        switch (d.kind) {
        case HudDraw::Text: DrawText(d.text.c_str(), d.x, d.y, d.h, c); break;
        case HudDraw::Rect: DrawRectangle(d.x, d.y, d.w, d.h, c); break;
        case HudDraw::RectLines: DrawRectangleLines(d.x, d.y, d.w, d.h, c); break;
        case HudDraw::GradientV: DrawRectangleGradientV(d.x, d.y, d.w, d.h, c, c2); break;
        case HudDraw::GradientH: DrawRectangleGradientH(d.x, d.y, d.w, d.h, c, c2); break;
        case HudDraw::Circle: DrawCircle(d.x, d.y, d.r, c); break;
        case HudDraw::Triangle:
            DrawTriangle({ (float)d.x, (float)d.y }, { (float)d.x, (float)(d.y + d.h) },
                         { (float)d.x + d.r, (float)d.y + d.h * 0.5f }, c);
            break;
        }
    }
}

// The camcorder's date and time: the wall clock's, not the run's.
static const std::tm *wallClock() {
    time_t t = time(nullptr);
    return localtime(&t);
}

void Game::renderUI(double now) {
    if (captureTime >= 0) now = captureTime;
    // ---- post + UI
    float timeF = (float)now;
    BeginDrawing();
    ClearBackground(BLACK);
    SetShaderValue(postShader, locPTime, &timeF, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, locPFear, &sim.fear, SHADER_UNIFORM_FLOAT);
    float submerged = sim.level==2 && sim.world.poolAt(cellOf(sim.px),cellOf(sim.pz)) ? clampf((WATER_Y-sim.eyeY)*8,0,1) : 0;
    SetShaderValue(postShader,locPWater,&submerged,SHADER_UNIFORM_FLOAT);
    SetShaderValue(postShader, locPMigraine, &sim.migraine, SHADER_UNIFORM_FLOAT);
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    BeginShaderMode(postShader);
    DrawTextureRec(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height }, { 0, 0 }, WHITE);
    EndShaderMode();
    if (cleanShot && !sim.inMenu) { EndDrawing(); return; }

    // The tape plays under the death card's words, so it goes down first.
    if (sim.inMenu && sim.deathT > 0 && camcorder.replaying()) {
        camReel.present(camcorder.replayPicture(), timeF);
        drawHud(camcorder.replayOsd(sw, sh, wallClock(), MeasureText));
    }
    // The tape is running: a small REC mark, under the HUD and its fades.
    if (!sim.inMenu && !sim.paused && !debugHud) drawHud(camcorder.recMark(sw, sh, timeF));   // F3 writes there
    HudInput in;
    in.width = sw;
    in.height = sh;
    in.now = now;
    in.replaying = camcorder.replaying();
    in.cardAlpha = camcorder.cardAlpha();
    in.touch = inTouchActive();
    in.captureHint = !inCursorHidden() && !shotPath;
    in.fps = GetFPS();
    in.debug = debugHud;
    drawHud(hudDraws(sim, in, everFlashed, MeasureText));
    EndDrawing();
}
