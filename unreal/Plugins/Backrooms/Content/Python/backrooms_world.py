"""Builds what the Unreal port draws the Web build's world with.

`tools/unreal.sh surfaces` (which `make unreal-open` runs) writes every
texture the Web build binds, and world.json, to Saved/Surfaces with texdump.
This imports the textures as raylib holds them (the bytes as written: no sRGB
decode, no compression, mipmapped and repeating or not as world.json says),
and builds:

- MPC_Backrooms, the world shader's per-frame uniforms, which
  UBackroomsRender sets every frame;
- M_BackroomsWorld (masked) and M_BackroomsWorldBlend (translucent): unlit,
  two-sided, their colour the Web build's world shader, ported to HLSL in
  Plugins/Backrooms/Shaders/BackroomsWorld.ush;
- M_BackroomsShape, M_BackroomsShapeBlend, M_BackroomsBillboard and
  M_BackroomsBillboardAdd: raylib's unlit shapes and billboards, their colour
  the vertex colour (times the texture);
- M_BackroomsPost: the Web build's post pass (BackroomsPost.ush) in place of
  Unreal's tonemapper;
- M_BackroomsTape: the camcorder's tape look (BackroomsTape.ush), which
  UBackroomsRender::ShowPost puts in the post material's place while the
  viewfinder is up or the tape plays back.

init_unreal.py runs it when the files in Saved/Surfaces differ from the ones
the materials were built from. To build again by hand, in the editor's Python
console:

    import backrooms_world; backrooms_world.build()
"""
import hashlib
import json
import os

import unreal

WORLD_DIR = "/Game/Backrooms/World"
TEXTURE_DIR = WORLD_DIR + "/Textures"
MPC_NAME = "MPC_Backrooms"
WORLD_INCLUDE = "/BackroomsShaders/BackroomsWorld.ush"
POST_INCLUDE = "/BackroomsShaders/BackroomsPost.ush"
TAPE_INCLUDE = "/BackroomsShaders/BackroomsTape.ush"
# Recorded on M_BackroomsWorld: the digest of the files it was built from.
DIGEST_TAG = "BackroomsWorld"

# MPC_Backrooms' vectors, which UBackroomsRender::Frame fills (each name's
# four components are listed there).
MPC_VECTORS = [
    "Frame", "ViewPos", "FlashDir", "FlarePos", "EntPos", "Ent", "Amb", "FogCol", "LightCol", "LampCol",
    "Air", "Tubes", "Surface", "Grid", "RoomMask", "Lamp", "Post", "Clear",
    "TapeA", "TapeB", "Tape", "TapeFrom",
]

# The world material's Custom node: the material's inputs into the HLSL's
# uniforms, Unreal's position and normal into core's frame (metres, y up),
# and the fragment.
WORLD_CODE = """
#if PIXELSHADER
FBRUniforms U;
U.Time = Frame.x; U.Blackout = Frame.y; U.Flash = Frame.z; U.FlareInt = Frame.w;
U.ViewPos = ViewPos.xyz; U.FlashDir = FlashDir.xyz; U.FlarePos = FlarePos.xyz; U.EntPos = EntPos.xyz;
U.EntDark = Ent.x; U.EntBlock = Ent.y; U.StoreyH = Ent.z; U.Storey = Ent.w;
U.Amb = Amb.xyz; U.FogCol = FogCol.xyz; U.LightCol = LightCol.xyz; U.LampCol = LampCol.xyz;
U.FogDen = Air.x; U.IR = Air.y; U.Macro = Air.z; U.Board = Air.w;
U.LS = Tubes.x; U.LY = Tubes.y; U.Dead = Tubes.z; U.LightMul = Tubes.w;
U.Gloss = GlossOverride >= 0.0 ? GlossOverride : Surface.x;
U.Vary = Surface.y; U.Faulty = Surface.z; U.Wet = Surface.w;
U.WetFrom = Grid.x; U.OccN = Grid.y; U.OccOrigin = Grid.zw;
U.RoomMask = RoomMask; U.Lamp = Lamp; U.ObjRefl = ObjRefl;
U.DrawRel = HasStorey * (ChunkStorey - Ent.w);
// What render.cpp sets round a viewmodel: no shadow grid, an ambient floor.
if (Viewmodel > 0.5)
{
    U.OccN = 0.0;
    U.Amb = max(U.Amb, float3(0.150, 0.142, 0.128));
}
float3 P = U.ViewPos + float3(CamRel.x, CamRel.z, CamRel.y) * 0.01;
float3 N = float3(Normal.x, Normal.z, Normal.y);
float4 C = BRWorldFragment(U, P, N, UV, float4(VertexRGB, VertexA), Tex0, Tex0Sampler, Tex1, Tex1Sampler, Occ);
return float4(C.rgb * Tint, C.a);
#else
return float4(0.0, 0.0, 0.0, 1.0);
#endif
"""

# The post material's Custom node: raylib's render target is clamped, 8-bit
# and cleared to the level's colour where nothing was drawn, and the scene
# colour is divided back out of Unreal's exposure, which the post volume pins
# at 1.
POST_CODE = """
FBRPost P;
P.Time = Post.x; P.Fear = Post.y; P.Water = Post.z; P.Migraine = Post.w;
P.InvSize = View.ViewSizeAndInvSize.zw;
float Throb;
float2 Uv = BRPostUV(P, ViewportUV, Throb);
float3 Taps[15];
[unroll]
for (int I = 0; I < 15; I++)
{
    float2 V = BRPostToViewport(P, BRPostTap(P, Uv, Throb, I));
    float Depth = SceneTextureLookup(ViewportUVToSceneTextureUV(V, 1), 1, false).r;
    float3 S = SceneTextureLookup(ViewportUVToSceneTextureUV(V, 14), 14, false).rgb / max(Exposure, 1e-6);
    Taps[I] = saturate(Depth > 1e7 ? Clear.rgb : S);
}
return float4(BRPostFinish(P, Uv, Throb, Taps), 1.0);
"""


# The tape material's Custom node: TAPE_FS's uniforms from the collection,
# gl_FragCoord from the pixel position (from the viewport UV it came out a
# rounding off, which moved the noise on 0.4% of pixels), and its texture:
# the scene as raylib's
# render target holds it (as POST_CODE reads it, point sampled), or the reel.
# The cells' v runs up the picture and Unreal's UV down it.
TAPE_CODE = """
FBRTape P;
P.Time = Post.x;
P.Res = View.ViewSizeAndInvSize.xy;
P.CellA = TapeA; P.CellB = TapeB;
P.Mix = Tape.x; P.Night = Tape.y; P.Track = Tape.z; P.Still = Tape.w;
P.Snow = TapeFrom.x;
float2 FragCoord = float2(PixelPosition.x, P.Res.y - PixelPosition.y);
FBRTapeLine L = BRTapeLine(P, FragCoord);
float3 Frames[7];
[unroll]
for (int I = 0; I < 7; I++)
{
    float2 Q = BRTapeTap(L.Q, I);
    float2 Uv[2] = { BRTapeCellUV(P.CellA, Q), BRTapeCellUV(P.CellB, Q) };
    float3 Got[2];
    [unroll]
    for (int K = 0; K < 2; K++)
    {
        float2 V = float2(Uv[K].x, 1.0 - Uv[K].y);
        if (TapeFrom.y > 0.5)
        {
            Got[K] = Texture2DSampleLevel(Reel, ReelSampler, V, 0.0).rgb;
        }
        else
        {
            float Depth = SceneTextureLookup(ViewportUVToSceneTextureUV(V, 1), 1, false).r;
            float3 S = SceneTextureLookup(ViewportUVToSceneTextureUV(V, 14), 14, false).rgb / max(Exposure, 1e-6);
            Got[K] = saturate(Depth > 1e7 ? Clear.rgb : S);
        }
    }
    Frames[I] = lerp(Got[0], Got[1], P.Mix);
}
return float4(BRTapeFinish(P, FragCoord, L, Frames), 1.0);
"""


def mel():
    return unreal.MaterialEditingLibrary


def eal():
    return unreal.EditorAssetLibrary


def tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def surfaces_dir():
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    return os.path.join(saved, "Surfaces")


def read_world():
    path = os.path.join(surfaces_dir(), "world.json")
    if not os.path.exists(path):
        unreal.log_warning("Backrooms: no " + path + "; run tools/unreal.sh surfaces, then open the editor again")
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def digest():
    h = hashlib.md5()
    folder = surfaces_dir()
    for name in sorted(os.listdir(folder)):
        h.update(name.encode())
        with open(os.path.join(folder, name), "rb") as f:
            h.update(f.read())
    # This file too: a change to how a material is built must rebuild them.
    # The .ush files are compiled by the engine and need no rebuild here.
    with open(__file__, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def import_textures(entries):
    """Imports each texture world.json lists; returns {name: Texture2D} or None."""
    folder = surfaces_dir()
    tasks = []
    for entry in entries:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, entry["name"] + ".png")
        task.destination_path = TEXTURE_DIR
        task.destination_name = "T_" + entry["name"]
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    tools().import_asset_tasks(tasks)

    textures = {}
    for entry in entries:
        texture = eal().load_asset(TEXTURE_DIR + "/T_" + entry["name"])
        if not isinstance(texture, unreal.Texture2D):
            unreal.log_error("Backrooms: the import did not make T_" + entry["name"])
            return None
        # raylib uploads the bytes and the shader reads them as they are.
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        texture.set_editor_property("never_stream", True)
        if entry["mips"]:
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
            texture.set_editor_property("filter", unreal.TextureFilter.TF_DEFAULT)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        else:
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        address = unreal.TextureAddress.TA_CLAMP if entry["clamp"] else unreal.TextureAddress.TA_WRAP
        texture.set_editor_property("address_x", address)
        texture.set_editor_property("address_y", address)
        textures[entry["name"]] = texture
    eal().save_directory(TEXTURE_DIR, only_if_is_dirty=False, recursive=True)
    return textures


def _link(a, a_out, b, b_in):
    if not mel().connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("cannot connect %s.%s to %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def _fresh_material(name):
    path = WORLD_DIR + "/" + name
    if eal().does_asset_exist(path):
        mat = eal().load_asset(path)
        mel().delete_all_material_expressions(mat)
    else:
        mat = tools().create_asset(name, WORLD_DIR, unreal.Material, unreal.MaterialFactoryNew())
    return mat


def _unlit(mat, blend):
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", blend)
    # raylib draws with face culling off.
    mat.set_editor_property("two_sided", True)
    if blend == unreal.BlendMode.BLEND_MASKED:
        mat.set_editor_property("opacity_mask_clip_value", 0.5)


def build_collection():
    path = WORLD_DIR + "/" + MPC_NAME
    if eal().does_asset_exist(path):
        mpc = eal().load_asset(path)
    else:
        mpc = tools().create_asset(MPC_NAME, WORLD_DIR, unreal.MaterialParameterCollection,
                                   unreal.MaterialParameterCollectionFactoryNew())
    have = [p.get_editor_property("parameter_name") for p in mpc.get_editor_property("vector_parameters")]
    if [str(n) for n in have] != MPC_VECTORS:
        params = []
        for name in MPC_VECTORS:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property("parameter_name", name)
            p.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
            params.append(p)
        mpc.set_editor_property("vector_parameters", params)
    eal().save_loaded_asset(mpc)
    return mpc


def _custom(mat, code, include, defines, inputs, x, y):
    custom = mel().create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("code", code)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "Backrooms")
    custom.set_editor_property("include_file_paths", [include])
    made = []
    for name, value in defines.items():
        d = unreal.CustomDefine()
        d.set_editor_property("define_name", name)
        d.set_editor_property("define_value", value)
        made.append(d)
    custom.set_editor_property("additional_defines", made)
    pins = []
    for name in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    return custom


def _collection(mat, mpc, name, x, y):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    e.set_editor_property("collection", mpc)
    e.set_editor_property("parameter_name", name)
    return e


def _scalar(mat, name, value, x, y, primitive_index=None):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    if primitive_index is not None:
        e.set_editor_property("use_custom_primitive_data", True)
        e.set_editor_property("primitive_data_index", primitive_index)
    return e


def _texture_object(mat, name, texture, x, y):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", texture)
    e.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return e


def _mask(mat, source, channels, x, y):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    for c in "rgba":
        e.set_editor_property(c, c in channels)
    _link(source, "", e, "")
    return e


def _hlsl_float(value):
    text = repr(float(value))
    return text if ("." in text or "e" in text) else text + ".0"


def build_world_material(name, blend, mpc, defines, neutral):
    """The world shader on a mesh. Tex0 and Tex1 are the texture and detail
    map raylib binds (texture0, texture1), Occ the shadow grid (texture2);
    ObjRefl, GlossOverride (-1 for the level's), Viewmodel and Tint the
    draw's own uniforms (uObjRefl, uGloss, the viewmodel's grid and ambient,
    colDiffuse); HasStorey and ChunkStorey, from the chunk's custom primitive
    data, give uDrawRel."""
    mat = _fresh_material(name)
    _unlit(mat, blend)
    inputs = ["CamRel", "Normal", "UV", "VertexRGB", "VertexA", "Tex0", "Tex1", "Occ", "ObjRefl", "GlossOverride",
              "Viewmodel", "Tint", "HasStorey", "ChunkStorey"] + MPC_VECTORS[:16]
    custom = _custom(mat, WORLD_CODE, WORLD_INCLUDE, defines, inputs, -300, 0)

    position = mel().create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, -600)
    position.set_editor_property("world_position_shader_offset",
                                 unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    _link(position, "", custom, "CamRel")
    _link(mel().create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, -500), "", custom, "Normal")
    _link(mel().create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -400), "", custom, "UV")
    vertex = mel().create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, -300)
    _link(vertex, "", custom, "VertexRGB")
    _link(vertex, "A", custom, "VertexA")
    for i, tex in enumerate(("Tex0", "Tex1", "Occ")):
        _link(_texture_object(mat, tex, neutral, -900, -200 + 100 * i), "", custom, tex)
    scalars = [("ObjRefl", 0.0, None), ("GlossOverride", -1.0, None), ("Viewmodel", 0.0, None), ("Tint", 1.0, None),
               ("HasStorey", 0.0, 0), ("ChunkStorey", 0.0, 1)]
    for i, (pname, value, index) in enumerate(scalars):
        _link(_scalar(mat, pname, value, -900, 100 + 80 * i, index), "", custom, pname)
    for i, vname in enumerate(MPC_VECTORS[:16]):
        _link(_collection(mat, mpc, vname, -1200, -600 + 80 * i), "", custom, vname)

    rgb = _mask(mat, custom, "rgb", 0, -100)
    alpha = _mask(mat, custom, "a", 0, 100)
    mel().connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    target = unreal.MaterialProperty.MP_OPACITY_MASK if blend == unreal.BlendMode.BLEND_MASKED \
        else unreal.MaterialProperty.MP_OPACITY
    mel().connect_material_property(alpha, "", target)
    mel().recompile_material(mat)
    return mat


def build_shape_material(name, blend, textured, default_sheet=None):
    """raylib's default shader: the vertex colour, times a texture's (Sheet)
    for billboards. Additive is raylib's BLEND_ADDITIVE, colour times alpha
    added. Sheet is set per instance, but the parameter needs a texture of its
    own: without one the material does not compile, and the engine draws its
    default material instead, an opaque square where the muzzle smoke was."""
    mat = _fresh_material(name)
    _unlit(mat, blend)
    vertex = mel().create_material_expression(mat, unreal.MaterialExpressionVertexColor, -800, 0)
    colour, alpha = (vertex, ""), (vertex, "A")
    if textured:
        sheet = mel().create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, 300)
        sheet.set_editor_property("parameter_name", "Sheet")
        sheet.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        sheet.set_editor_property("texture", default_sheet)
        rgb = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 0)
        _link(vertex, "", rgb, "A")
        _link(sheet, "RGB", rgb, "B")
        a = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 200)
        _link(vertex, "A", a, "A")
        _link(sheet, "A", a, "B")
        colour, alpha = (rgb, ""), (a, "")
    if blend == unreal.BlendMode.BLEND_ADDITIVE:
        lit = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
        _link(colour[0], colour[1], lit, "A")
        _link(alpha[0], alpha[1], lit, "B")
        mel().connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        mel().connect_material_property(colour[0], colour[1], unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if blend == unreal.BlendMode.BLEND_TRANSLUCENT:
            mel().connect_material_property(alpha[0], alpha[1], unreal.MaterialProperty.MP_OPACITY)
    mel().recompile_material(mat)
    return mat


def build_post_material(mpc):
    mat = _fresh_material("M_BackroomsPost")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_REPLACING_TONEMAPPER)
    custom = _custom(mat, POST_CODE, POST_INCLUDE, {}, ["ViewportUV", "Exposure", "Post", "Clear", "Scene", "Depth"],
                     -300, 0)
    screen = mel().create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -900, -200)
    _link(screen, "ViewportUV", custom, "ViewportUV")
    _link(mel().create_material_expression(mat, unreal.MaterialExpressionEyeAdaptation, -900, -100), "", custom,
          "Exposure")
    _link(_collection(mat, mpc, "Post", -900, 0), "", custom, "Post")
    _link(_collection(mat, mpc, "Clear", -900, 100), "", custom, "Clear")
    # Read by SceneTextureLookup in the code; wired in so the material
    # declares the scene colour and depth it samples.
    for i, (pin, which) in enumerate((("Scene", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
                                      ("Depth", unreal.SceneTextureId.PPI_SCENE_DEPTH))):
        scene = mel().create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -900, 200 + 100 * i)
        scene.set_editor_property("scene_texture_id", which)
        _link(scene, "Color", custom, pin)
    rgb = _mask(mat, custom, "rgb", 0, 0)
    mel().connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel().recompile_material(mat)
    return mat


def build_tape_material(mpc, neutral):
    """The tape look, which takes M_BackroomsPost's place in the post volume.
    Reel is the camcorder's reel, set at run time (UBackroomsRender::ShowPost);
    the scene and depth are read as POST_CODE reads them."""
    mat = _fresh_material("M_BackroomsTape")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_REPLACING_TONEMAPPER)
    custom = _custom(mat, TAPE_CODE, TAPE_INCLUDE, {},
                     ["PixelPosition", "Exposure", "Post", "Clear", "TapeA", "TapeB", "Tape", "TapeFrom", "Reel", "Scene",
                      "Depth"], -300, 0)
    screen = mel().create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -900, -300)
    _link(screen, "PixelPosition", custom, "PixelPosition")
    _link(mel().create_material_expression(mat, unreal.MaterialExpressionEyeAdaptation, -900, -200), "", custom,
          "Exposure")
    for i, name in enumerate(("Post", "Clear", "TapeA", "TapeB", "Tape", "TapeFrom")):
        _link(_collection(mat, mpc, name, -900, -100 + 80 * i), "", custom, name)
    _link(_texture_object(mat, "Reel", neutral, -900, 400), "", custom, "Reel")
    for i, (pin, which) in enumerate((("Scene", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
                                      ("Depth", unreal.SceneTextureId.PPI_SCENE_DEPTH))):
        scene = mel().create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -900, 500 + 100 * i)
        scene.set_editor_property("scene_texture_id", which)
        _link(scene, "Color", custom, pin)
    rgb = _mask(mat, custom, "rgb", 0, 0)
    mel().connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel().recompile_material(mat)
    return mat


def build():
    world = read_world()
    if not world:
        return False
    textures = import_textures(world["textures"])
    if textures is None:
        return False
    mpc = build_collection()
    neutral = textures["neutral"]
    # Core's numbers as HLSL floats, and the occupancy grid's bits as ints.
    defines = {k: _hlsl_float(v) for k, v in world["defines"].items()}
    defines.update({k: str(int(v)) for k, v in world.get("bits", {}).items()})
    mat = build_world_material("M_BackroomsWorld", unreal.BlendMode.BLEND_MASKED, mpc, defines, neutral)
    build_world_material("M_BackroomsWorldBlend", unreal.BlendMode.BLEND_TRANSLUCENT, mpc, defines, neutral)
    build_shape_material("M_BackroomsShape", unreal.BlendMode.BLEND_OPAQUE, False)
    build_shape_material("M_BackroomsShapeBlend", unreal.BlendMode.BLEND_TRANSLUCENT, False)
    for name, blend in (("M_BackroomsBillboard", unreal.BlendMode.BLEND_TRANSLUCENT),
                        ("M_BackroomsBillboardAdd", unreal.BlendMode.BLEND_ADDITIVE)):
        # Sheet is set per instance at run time (UBackroomsRender::Billboard).
        build_shape_material(name, blend, True, textures["particle"])
    build_post_material(mpc)
    build_tape_material(mpc, neutral)
    eal().set_metadata_tag(mat, DIGEST_TAG, digest())
    eal().save_directory(WORLD_DIR, only_if_is_dirty=False, recursive=True)
    unreal.log("Backrooms: built the world's materials and %d textures in %s" % (len(textures), WORLD_DIR))
    return True


def stale():
    """Why the world's materials need building, or None."""
    if not os.path.exists(os.path.join(surfaces_dir(), "world.json")):
        return None   # nothing to build from; read_world says so when asked
    for name in ("M_BackroomsWorld", "M_BackroomsWorldBlend", "M_BackroomsShape", "M_BackroomsShapeBlend",
                 "M_BackroomsBillboard", "M_BackroomsBillboardAdd", "M_BackroomsPost", "M_BackroomsTape",
                 MPC_NAME):
        if not eal().does_asset_exist(WORLD_DIR + "/" + name):
            return name + " is missing"
    if eal().get_metadata_tag(eal().load_asset(WORLD_DIR + "/M_BackroomsWorld"), DIGEST_TAG) != digest():
        return "the files in Saved/Surfaces or the material code have changed"
    return None
