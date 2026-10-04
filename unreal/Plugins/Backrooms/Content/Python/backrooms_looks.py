"""Builds the level looks from the raylib build's own surfaces.

`tools/unreal.sh surfaces` (which `make unreal-open` runs) writes every world
surface and looks.json to Saved/Surfaces with texdump. This imports them,
builds one material that reads a surface at its real size, an instance per
level surface, and a level look per level (UBackroomsLevelLook) with those, the
tubes' colour and output and the fog; and the actors' sprite sheets with the
two materials that draw them. init_unreal.py runs it when the files in
Saved/Surfaces differ from the ones the looks were built from. To build them
again by hand, in the editor's Python console:

    import backrooms_looks; backrooms_looks.build_looks()

A look's surface materials are set only where they are empty or are this
script's own, so a material assigned in the editor stays. Its light and fog
are LevelCfg's (src/levels.cpp) and are written every time; tune the overall
light in Project Settings > Game > Backrooms (FittingLumens).
"""
import hashlib
import json
import os

import unreal

SURFACE_DIR = "/Game/Backrooms/Surfaces"
SPRITE_DIR = "/Game/Backrooms/Sprites"
# The actors' sheets texdump writes, and UBackroomsSettings' default paths for
# them and the two materials.
SPRITES = ["clark", "smiler", "smiler_glow", "partygoer", "dog"]
SPRITE_MATERIAL = "M_BackroomsSprite"
SPRITE_GLOW_MATERIAL = "M_BackroomsSpriteGlow"
LOOK_DIR = "/Game/Backrooms/Looks"
MATERIAL_NAME = "M_BackroomsSurface"
# UBackroomsSettings' default LevelLooks.
LOOK_NAME = "DA_Level%d"
# Which greybox surfaces (EBackroomsSurface) take each of a level's surfaces.
PARTS = {
    "floor": ("Floor", ["FLOOR", "STAIR"]),
    "ceiling": ("Ceiling", ["CEILING"]),
    "walls": ("Walls", ["WALL", "STEP", "PILLAR"]),
}
# The detail map's red and green are slopes, height per metre along u and v:
# (byte - 128) / 127 (finishSurface, src/surfaces.cpp).
SLOPE_ZERO = 128.0 / 255.0
SLOPE_SCALE = 255.0 / 127.0
# The raylib shader draws no specular below a gloss of 0.10 (LevelCfg::gloss),
# and the Poolrooms' 0.55 is the glossiest level.
GLOSS_CUT = 0.10
GLOSS_FULL = 0.55
# The raylib build's ambient floor (LevelCfg::amb) as the surfaces' emissive
# albedo multiple. Its shader lifts that ambient through its tone curve's toe
# (AGENTS.md, "Lighting"); this is the starting point to tune from.
AMBIENT_GAIN = 1.0
# Recorded on the material: the digest of the files it was built from.
DIGEST_TAG = "BackroomsSurfaces"


# Looked up when called: init_unreal.py imports this module under -game too,
# where there is no editor.
def mel():
    return unreal.MaterialEditingLibrary


def eal():
    return unreal.EditorAssetLibrary


def surfaces_dir():
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    return os.path.join(saved, "Surfaces")


def read_looks():
    path = os.path.join(surfaces_dir(), "looks.json")
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
    return h.hexdigest()


def import_surfaces(names):
    """Imports name.png and name_detail.png for each name; returns
    {name: (albedo, detail)} or None."""
    folder = surfaces_dir()
    tasks = []
    for name in names:
        for suffix, asset in (("", "T_" + name), ("_detail", "T_" + name + "_Detail")):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(folder, name + suffix + ".png")
            task.destination_path = SURFACE_DIR
            task.destination_name = asset
            task.automated = True
            task.replace_existing = True
            task.save = False
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = {}
    for name in names:
        albedo = eal().load_asset(SURFACE_DIR + "/T_" + name)
        detail = eal().load_asset(SURFACE_DIR + "/T_" + name + "_Detail")
        if not isinstance(albedo, unreal.Texture2D) or not isinstance(detail, unreal.Texture2D):
            unreal.log_error("Backrooms: the surface import did not make T_" + name + " and its detail map")
            return None
        # The detail map holds numbers, not colour: linear, and uncompressed so
        # a slope keeps its byte.
        detail.set_editor_property("srgb", False)
        detail.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        textures[name] = (albedo, detail)
    eal().save_directory(SURFACE_DIR, only_if_is_dirty=False, recursive=True)
    return textures


def _link(a, a_out, b, b_in):
    if not mel().connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError("cannot connect %s.%s to %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def _scalar(mat, name, value, x, y):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def _texture(mat, name, texture, sampler, x, y):
    e = mel().create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", texture)
    e.set_editor_property("sampler_type", sampler)
    return e


def _slope(mat, detail, channel, relief, y):
    """The surface normal's tangent-space component along one texture axis:
    minus the slope there, times a Relief parameter."""
    centred = mel().create_material_expression(mat, unreal.MaterialExpressionSubtract, -500, y)
    centred.set_editor_property("const_b", SLOPE_ZERO)
    _link(detail, channel, centred, "A")
    scaled = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, y)
    scaled.set_editor_property("const_b", -SLOPE_SCALE)
    _link(centred, "", scaled, "A")
    out = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, y)
    _link(scaled, "", out, "A")
    _link(relief, "", out, "B")
    return out


def build_material(albedo, detail):
    """The one material every level surface is an instance of. The greybox's
    texture coordinates are metres (src/port/greybox.h), so TileU and TileV are
    the metres one repeat covers. The detail map's slopes become the normal
    (scaled by ReliefU and ReliefV),
    and its blue, the gloss mask, picks between RoughMatte and RoughGloss as
    far as Shine allows. Rebuilt in place, so the instances keep their parent."""
    path = LOOK_DIR + "/" + MATERIAL_NAME
    if eal().does_asset_exist(path):
        mat = eal().load_asset(path)
        mel().delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MATERIAL_NAME, LOOK_DIR, unreal.Material, unreal.MaterialFactoryNew())

    uv = mel().create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1300, 0)
    tile_u = _scalar(mat, "TileU", 2.0, -1300, 120)
    tile_v = _scalar(mat, "TileV", 2.0, -1300, 220)
    tile = mel().create_material_expression(mat, unreal.MaterialExpressionAppendVector, -1100, 160)
    _link(tile_u, "", tile, "A")
    _link(tile_v, "", tile, "B")
    repeats = mel().create_material_expression(mat, unreal.MaterialExpressionDivide, -950, 60)
    _link(uv, "", repeats, "A")
    _link(tile, "", repeats, "B")

    colour = _texture(mat, "Albedo", albedo, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -750, -200)
    _link(repeats, "", colour, "UVs")
    mel().connect_material_property(colour, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

    relief_map = _texture(mat, "Detail", detail, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, -750, 200)
    _link(repeats, "", relief_map, "UVs")
    # One per axis, so a sign Unreal reads the other way can be turned alone.
    across = _slope(mat, relief_map, "R", _scalar(mat, "ReliefU", 1.0, -500, 420), 150)
    down = _slope(mat, relief_map, "G", _scalar(mat, "ReliefV", 1.0, -500, 520), 300)
    flat = mel().create_material_expression(mat, unreal.MaterialExpressionAppendVector, -50, 200)
    _link(across, "", flat, "A")
    _link(down, "", flat, "B")
    up = mel().create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 420)
    up.set_editor_property("r", 1.0)
    # Unreal normalizes the material's normal itself.
    normal = mel().create_material_expression(mat, unreal.MaterialExpressionAppendVector, 100, 250)
    _link(flat, "", normal, "A")
    _link(up, "", normal, "B")
    mel().connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)

    shine = _scalar(mat, "Shine", 0.0, -500, 600)
    glossy = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 600)
    _link(relief_map, "B", glossy, "A")
    _link(shine, "", glossy, "B")
    rough = mel().create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -50, 600)
    _link(_scalar(mat, "RoughMatte", 0.9, -300, 720), "", rough, "A")
    _link(_scalar(mat, "RoughGloss", 0.2, -300, 820), "", rough, "B")
    _link(glossy, "", rough, "Alpha")
    mel().connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # The ambient floor: unlit corners keep a little of their own colour.
    ambient = mel().create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, -400)
    ambient.set_editor_property("parameter_name", "Ambient")
    ambient.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    floor = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, -300)
    _link(colour, "RGB", floor, "A")
    _link(ambient, "", floor, "B")
    mel().connect_material_property(floor, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel().recompile_material(mat)
    return mat


def import_sprites():
    """Imports each sheet in SPRITES; returns {name: texture} or None."""
    folder = surfaces_dir()
    tasks = []
    for name in SPRITES:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, name + ".png")
        task.destination_path = SPRITE_DIR
        task.destination_name = "T_" + name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    sheets = {}
    for name in SPRITES:
        sheet = eal().load_asset(SPRITE_DIR + "/T_" + name)
        if not isinstance(sheet, unreal.Texture2D):
            unreal.log_error("Backrooms: the sprite import did not make T_" + name)
            return None
        # Frames sit edge to edge on the sheet: a clamp keeps the last column
        # from sampling the first.
        sheet.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        sheet.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        sheets[name] = sheet
    eal().save_directory(SPRITE_DIR, only_if_is_dirty=False, recursive=True)
    return sheets


def build_sprite_material(name, sheet, glow):
    """A billboard's material (ABackroomsSpriteActor): translucent and
    two-sided, the texture parameter Sheet, opacity the sheet's alpha times the
    vertex alpha the cross-fade writes. The body is lit; the glow (the
    Smiler's eyes and grin) is unlit, as in the raylib build."""
    path = SPRITE_DIR + "/" + name
    if eal().does_asset_exist(path):
        mat = eal().load_asset(path)
        mel().delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, SPRITE_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    if glow:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    else:
        mat.set_editor_property("translucency_lighting_mode",
                                unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    texel = _texture(mat, "Sheet", sheet, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -600, 0)
    vertex = mel().create_material_expression(mat, unreal.MaterialExpressionVertexColor, -600, 300)
    opacity = mel().create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 200)
    _link(texel, "A", opacity, "A")
    _link(vertex, "A", opacity, "B")
    mel().connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    target = unreal.MaterialProperty.MP_EMISSIVE_COLOR if glow else unreal.MaterialProperty.MP_BASE_COLOR
    mel().connect_material_property(texel, "RGB", target)
    mel().recompile_material(mat)
    return mat


def build_sprites():
    sheets = import_sprites()
    if sheets is None:
        return False
    build_sprite_material(SPRITE_MATERIAL, sheets["clark"], False)
    build_sprite_material(SPRITE_GLOW_MATERIAL, sheets["smiler_glow"], True)
    eal().save_directory(SPRITE_DIR, only_if_is_dirty=False, recursive=True)
    unreal.log("Backrooms: built the actors' sprites in " + SPRITE_DIR)
    return True


def shine_of(gloss):
    return 0.0 if gloss < GLOSS_CUT else min(1.0, gloss / GLOSS_FULL)


def build_instance(mat, name, textures, tile_u, tile_v, gloss, ambient):
    path = LOOK_DIR + "/" + name
    if eal().does_asset_exist(path):
        mi = eal().load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, LOOK_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel().set_material_instance_parent(mi, mat)
    mel().set_material_instance_texture_parameter_value(mi, "Albedo", textures[0])
    mel().set_material_instance_texture_parameter_value(mi, "Detail", textures[1])
    mel().set_material_instance_scalar_parameter_value(mi, "TileU", tile_u)
    mel().set_material_instance_scalar_parameter_value(mi, "TileV", tile_v)
    mel().set_material_instance_scalar_parameter_value(mi, "Shine", shine_of(gloss))
    mel().set_material_instance_vector_parameter_value(
        mi, "Ambient", unreal.LinearColor(*[c * AMBIENT_GAIN for c in ambient], 1.0))
    mel().update_material_instance(mi)
    return mi


def _colour(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def build_look(index, level, looks, mat, textures):
    name = LOOK_NAME % index
    path = LOOK_DIR + "/" + name
    if eal().does_asset_exist(path):
        look = eal().load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.BackroomsLevelLook)
        look = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, LOOK_DIR, unreal.BackroomsLevelLook, factory)

    surfaces = look.get_editor_property("surfaces")
    for part, (label, kinds) in PARTS.items():
        walls = part == "walls"
        tile_u = looks["wallTileM"] if walls else looks["floorTileM"]
        tile_v = level["wallTileV"] if walls else looks["floorTileM"]
        mi = build_instance(mat, "MI_L%d_%s" % (index, label), textures[level[part]], tile_u, tile_v,
                            level["gloss"], level["ambient"])
        for kind in kinds:
            key = getattr(unreal.BackroomsSurface, kind)
            entry = surfaces[key] if key in surfaces else unreal.BackroomsSurfaceLook()
            current = entry.get_editor_property("material")
            if current is None or current.get_path_name().startswith(LOOK_DIR + "/MI_"):
                entry.set_editor_property("material", mi)
                surfaces[key] = entry
    look.set_editor_property("surfaces", surfaces)

    look.set_editor_property("light_colour", _colour(level["lightColour"]))
    look.set_editor_property("light_output", level["lightMul"])
    look.set_editor_property("fog", True)
    look.set_editor_property("fog_colour", _colour(level["fogColour"]))
    look.set_editor_property("fog_density", level["fogDensity"])
    return look


def build_looks():
    looks = read_looks()
    if not looks:
        return False
    names = sorted({level[part] for level in looks["levels"] for part in PARTS})
    textures = import_surfaces(names)
    if textures is None:
        return False
    mat = build_material(*textures[names[0]])
    for index, level in enumerate(looks["levels"]):
        build_look(index, level, looks, mat, textures)
    # The looks stand without the sprites: the hunter and the pack fall back
    # to plain shapes.
    build_sprites()
    eal().set_metadata_tag(mat, DIGEST_TAG, digest())
    eal().save_directory(LOOK_DIR, only_if_is_dirty=False, recursive=True)
    unreal.log("Backrooms: built %d level looks in %s from %d surfaces" % (len(looks["levels"]), LOOK_DIR, len(names)))
    return True


def stale():
    """Why the looks need building, or None."""
    if not os.path.exists(os.path.join(surfaces_dir(), "looks.json")):
        return None   # nothing to build from; read_looks says so when asked
    for index in range(len(read_looks()["levels"])):
        if not eal().does_asset_exist(LOOK_DIR + "/" + LOOK_NAME % index):
            return "a level look is missing"
    path = LOOK_DIR + "/" + MATERIAL_NAME
    if not eal().does_asset_exist(path):
        return "the material is missing"
    if not eal().does_asset_exist(SPRITE_DIR + "/" + SPRITE_GLOW_MATERIAL):
        return "the sprites are missing"
    if eal().get_metadata_tag(eal().load_asset(path), DIGEST_TAG) != digest():
        return "the surfaces in Saved/Surfaces have changed"
    return None
