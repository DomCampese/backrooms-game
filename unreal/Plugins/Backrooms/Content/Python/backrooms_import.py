"""Imports the repository's recorded sounds into the Unreal project, and has
backrooms_world.py build the world's textures and materials.

The Web build embeds assets/sounds/**/*.ogg; the Unreal build imports the
same files, so there is one copy of each. The revolver is not imported: the
game reads assets/models/revolver.glb itself and poses it as the Web build
does (shared/port/revolver.h). The editor runs init_unreal.py when it opens the
project, and that imports anything missing. To import again after a file
changes, run in the editor's Python console (Output Log, Python):

    import backrooms_import; backrooms_import.import_sounds()

The paths here are the defaults in Project Settings > Game > Backrooms.
"""
import os

import unreal

import backrooms_world

SOUND_DIR = "/Game/Backrooms/Sounds"
# The recordings the game loops (UNDERWATER_RECORDING and PARTY_RECORDING in
# shared/port/sounds.h).
SOUND_LOOPS = {"underwater", "level_fun"}


def repo_file(*parts):
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", *parts))


def sound_files():
    """(source file, destination folder, asset name) for each recording, keeping
    assets/sounds' folders: sounds/water/swim_1.ogg is SOUND_DIR/water/swim_1."""
    root = repo_file("assets", "sounds")
    for folder, _, names in sorted(os.walk(root)):
        rel = os.path.relpath(folder, root).replace(os.sep, "/")
        dest = SOUND_DIR if rel == "." else SOUND_DIR + "/" + rel
        for name in sorted(names):
            if name.endswith(".ogg"):
                yield os.path.join(folder, name), dest, os.path.splitext(name)[0]


def missing_sounds():
    return [f for f in sound_files()
            if not unreal.EditorAssetLibrary.does_asset_exist(f[1] + "/" + f[2])]


def import_sounds():
    files = list(sound_files())
    tasks = []
    for source, dest, name in files:
        task = unreal.AssetImportTask()
        task.filename = source
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for _, dest, name in files:
        wave = unreal.EditorAssetLibrary.load_asset(dest + "/" + name)
        if isinstance(wave, unreal.SoundWave) and name in SOUND_LOOPS:
            wave.set_editor_property("looping", True)
    unreal.EditorAssetLibrary.save_directory(SOUND_DIR, only_if_is_dirty=False, recursive=True)

    left = missing_sounds()
    if left:
        unreal.log_error("Backrooms: the sound import did not make " + ", ".join(f[1] + "/" + f[2] for f in left)
                         + "; if this editor cannot import .ogg, convert those files to .wav and import them there")
        return False
    unreal.log("Backrooms: imported %d sounds to %s" % (len(files), SOUND_DIR))
    return True


def import_missing():
    if missing_sounds():
        unreal.log("Backrooms: importing assets/sounds")
        import_sounds()
    else:
        unreal.log("Backrooms: the sounds are already imported in " + SOUND_DIR)
    why = backrooms_world.stale()
    if why:
        unreal.log("Backrooms: building the world's materials; " + why)
        backrooms_world.build()
    elif backrooms_world.read_world():
        unreal.log("Backrooms: the world's materials are up to date in " + backrooms_world.WORLD_DIR)
