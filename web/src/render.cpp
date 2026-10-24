#include "game.h"
#include "input.h"
#include "port/held.h"
#include "port/hud.h"
#include "port/scene.h"
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
    int pcx = fdiv(cellOf(sim.px), CCELLS), pcz = fdiv(cellOf(sim.pz), CCELLS);
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
    struct VisibleChunk { const ChunkMeshes *data; float distance2; float yOff; };
    VisibleChunk visible[25 + 2 * STOREY_REACH * 25];
    int visibleCount = 0;
    Vector3 cameraRight = Vector3Normalize(Vector3CrossProduct(toRl(sim.fwd), cam.up));
    Vector3 cameraUp = Vector3Normalize(Vector3CrossProduct(cameraRight, toRl(sim.fwd)));
    float tanV = tanf(cam.fovy * DEG2RAD * 0.5f);
    float tanH = tanV * rt.texture.width / rt.texture.height;
    // Bounds include the deepest atrium, ceiling and slight wall overlap — and,
    // on a storeyed level, flights and shaft walls that climb to the next floor.
    float yLo = -2.5f, yHi = fmaxf(LEVELS[sim.level].wallH, sim.world.storeyH) + 1.2f;
    float halfY = (yHi - yLo) * 0.5f;
    float radius = sqrtf(CHUNK*CHUNK*0.5f + halfY*halfY) + 0.5f;
    // A sphere against the view frustum, conservatively.
    auto sphereVisible = [&](Vector3 c, float r) {
        Vector3 delta{c.x - sim.px, c.y - sim.eyeY, c.z - sim.pz};
        float depth = Vector3DotProduct(delta, toRl(sim.fwd));
        return !(depth < -r ||
                 fabsf(Vector3DotProduct(delta, cameraRight)) > depth*tanH + r*sqrtf(1+tanH*tanH) ||
                 fabsf(Vector3DotProduct(delta, cameraUp)) > depth*tanV + r*sqrtf(1+tanV*tanV));
    };
    auto consider = [&](int storey, int cx, int cz, float yOff) {
        const ChunkMeshes *baked = chunkMeshes.find(sim.world, storey, cx, cz);
        if (!baked) return;
        for (int q = 0; q < visibleCount; q++) if (visible[q].data == baked) return;
        Vector3 c{cx*CHUNK+CHUNK*0.5f, yLo + halfY + yOff, cz*CHUNK+CHUNK*0.5f};
        if (!sphereVisible(c, radius)) return;
        if (visibleCount < (int)(sizeof(visible) / sizeof(visible[0])))
            visible[visibleCount++] = {baked, Vector3LengthSqr(Vector3{c.x-sim.px, c.y-sim.eyeY, c.z-sim.pz}), yOff};
    };
    for (int dx=-2; dx<=2; ++dx) for (int dz=-2; dz<=2; ++dz) consider(sim.world.storey, pcx+dx, pcz+dz, 0.0f);
    std::sort(visible, visible+visibleCount, [](const VisibleChunk &a, const VisibleChunk &b) {
        return a.distance2 < b.distance2;
    });
    // The storeys above and below, through the openings near you: the chunks
    // round each opening, drawn a pitch up or down in their own frame. After
    // this storey's, so the floors and ceilings you are between have already
    // filled the depth buffer and most of the other floor is rejected before
    // it is lit.
    //
    // And only what an opening actually shows. Outside a feature's footprint
    // the slab between two storeys is closed — ceiling below, floor above,
    // fascias round the edge — so every line of sight into the other storey
    // crosses the plane of the upper floor inside that footprint. A chunk is
    // drawn only if its box, projected from the eye onto that plane, overlaps
    // the footprint: a portal test, one rectangle per opening. Drawing every
    // chunk round every link in the frustum put 22 chunks of other storeys
    // behind the 9 of this one and cost a tenth of the frame.
    struct Portal { float x0, z0, x1, z1, y; bool up; };
    Vector3 eye = cam.position;
    // The opening between storeys lo and lo + 1 (relative to yours) in a chunk.
    auto portalOf = [&](int cx, int cz, int lo, Portal &p) {
        VertFeat f;
        if (!sim.world.pairFeature(cx, cz, sim.world.storey + lo, f)) return false;
        float ax = (float)((cx * CCELLS + f.x0) * CELL), az = (float)((cz * CCELLS + f.z0) * CELL);
        float ex = (f.dir < 2 ? f.wu : f.lv) * CELL, ez = (f.dir < 2 ? f.lv : f.wu) * CELL;
        p = { ax - 0.1f, az - 0.1f, ax + ex + 0.1f, az + ez + 0.1f, (lo + 1) * sim.world.storeyH, lo >= 0 };
        // The prism between the two floors has to be in view at all.
        Vector3 c{ax + ex * 0.5f, (lo + 0.5f) * sim.world.storeyH, az + ez * 0.5f};
        return sphereVisible(c, sqrtf(ex*ex + ez*ez + sim.world.storeyH*sim.world.storeyH) * 0.5f + 0.5f);
    };
    // Can anything in this chunk's box (at storey offset yOff) be seen through p?
    auto through = [&](const Portal &p, int cx, int cz, float yOff) {
        // An eye at or past the plane — halfway down a flight, below the hole
        // it came through — is inside the opening: everything past it shows.
        if ((p.y > eye.y) != p.up || fabsf(p.y - eye.y) < 0.05f) return true;
        float y0 = yOff + yLo, y1 = yOff + yHi;
        if (p.up) y0 = fmaxf(y0, p.y); else y1 = fminf(y1, p.y);   // only the far side counts
        if (y0 > y1) return false;
        float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
        for (int i = 0; i < 8; i++) {
            float x = cx * CHUNK + ((i & 1) ? CHUNK : 0.0f), z = cz * CHUNK + ((i & 2) ? CHUNK : 0.0f);
            float y = (i & 4) ? y1 : y0;
            float t = (p.y - eye.y) / (y - eye.y);     // in (0, 1]: y is on the far side
            float X = eye.x + (x - eye.x) * t, Z = eye.z + (z - eye.z) * t;
            mnx = fminf(mnx, X); mxx = fmaxf(mxx, X); mnz = fminf(mnz, Z); mxz = fmaxf(mxz, Z);
        }
        return !(mxx < p.x0 || mnx > p.x1 || mxz < p.z0 || mnz > p.z1);
    };
    int ownCount = visibleCount;
    if (sim.world.storeyH > 0.0f)
        for (int rel = -1; rel <= 1; rel += 2)
            for (int dx=-1; dx<=1; ++dx) for (int dz=-1; dz<=1; ++dz) {
                Portal p1;
                if (!portalOf(pcx+dx, pcz+dz, rel > 0 ? 0 : -1, p1)) continue;
                for (int ex=-1; ex<=1; ++ex) for (int ez=-1; ez<=1; ++ez) {
                    int cx = pcx+dx+ex, cz = pcz+dz+ez;
                    if (!through(p1, cx, cz, rel * sim.world.storeyH)) continue;
                    consider(sim.world.storey + rel, cx, cz, rel * sim.world.storeyH);
                    // Stairs stack: the flight you are looking up often lands
                    // beside the next one, and through that one's opening is
                    // the storey after. Its own chunk is all you can see of it,
                    // and only through both openings.
                    Portal chain[STOREY_REACH];chain[0]=p1;
                    for(int depth=2;depth<=STOREY_REACH;++depth) {
                        if(!portalOf(cx,cz,rel>0 ? depth-1 : -depth,chain[depth-1])) break;
                        bool seen=true;
                        for(int j=0;j<depth;++j)
                            if(!through(chain[j],cx,cz,depth*rel*sim.world.storeyH)) { seen=false;break; }
                        if(!seen) break;
                        consider(sim.world.storey+depth*rel,cx,cz,depth*rel*sim.world.storeyH);
                    }
                }
            }
    std::sort(visible+ownCount, visible+visibleCount, [](const VisibleChunk &a, const VisibleChunk &b) {
        return a.distance2 < b.distance2;
    });
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
    if (filming) {
        camReel.present(camcorder.viewfinderPicture(), rt, timeF);
        drawHud(camcorder.viewfinderOsd(sw, sh, timeF, (float)elapsed, wallClock(), MeasureText));
    }
    else {
        BeginShaderMode(postShader);
        DrawTextureRec(rt.texture, { 0, 0, (float)rt.texture.width, -(float)rt.texture.height }, { 0, 0 }, WHITE);
        EndShaderMode();
    }
    if (!sim.inMenu && camcorder.raise > 0.0f && camcorder.raise < 1.0f)   // the eye meeting the finder
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 1.0f - fabsf(2.0f * camcorder.raise - 1.0f)));
    if (cleanShot && !sim.inMenu) { EndDrawing(); return; }

    // The tape plays under the death card's words, so it goes down first.
    if (sim.inMenu && sim.deathT > 0 && camcorder.replaying()) {
        camReel.present(camcorder.replayPicture(), rt, timeF);
        drawHud(camcorder.replayOsd(sw, sh, timeF, wallClock(), MeasureText));
    }
    HudInput in;
    in.width = sw;
    in.height = sh;
    in.now = now;
    in.filming = filming;
    in.replaying = camcorder.replaying();
    in.cardAlpha = camcorder.cardAlpha();
    in.touch = inTouchActive();
    in.captureHint = !inCursorHidden() && !shotPath;
    in.fps = GetFPS();
    in.debug = debugHud;
    drawHud(hudDraws(sim, in, everFlashed, MeasureText));
    EndDrawing();
}
