"""Imports the repository's models and recorded sounds into the Unreal project.

The raylib build embeds assets/models/*.glb and assets/sounds/**/*.ogg; the
Unreal build imports the same files, so there is one copy of each. The editor
runs init_unreal.py when it opens the project, and that imports anything
missing. To import again after a file changes, run in the editor's Python
console (Output Log, Python):

    import backrooms_import; backrooms_import.import_revolver()
    import backrooms_import; backrooms_import.import_sounds()

The paths here are the defaults in Project Settings > Game > Backrooms.
"""
import os

import unreal

REVOLVER_DIR = "/Game/Backrooms/Revolver"
REVOLVER_MESH = REVOLVER_DIR + "/SK_Revolver"
# The GLB's clip names, lower case, and the names the settings look for.
REVOLVER_CLIPS = {
    "idle": "A_Revolver_Idle",
    "reload": "A_Revolver_Reload",
    "shoot": "A_Revolver_Shoot",
}


SOUND_DIR = "/Game/Backrooms/Sounds"
# The recordings the game loops (UNDERWATER_RECORDING and PARTY_RECORDING in
# src/port/sounds.h).
SOUND_LOOPS = {"underwater", "level_fun"}


def repo_file(*parts):
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", *parts))


def missing_revolver():
    names = [REVOLVER_MESH] + [REVOLVER_DIR + "/" + n for n in REVOLVER_CLIPS.values()]
    return [n for n in names if not unreal.EditorAssetLibrary.does_asset_exist(n)]


def import_revolver():
    source = repo_file("assets", "models", "revolver.glb")
    if not os.path.exists(source):
        unreal.log_error("Backrooms: no revolver at " + source)
        return False
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = REVOLVER_DIR
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    # The importer names assets after the file and the clips; give them the
    # names the settings expect.
    for path in unreal.EditorAssetLibrary.list_assets(REVOLVER_DIR, recursive=True, include_folder=False):
        package = path.split(".")[0]
        asset = unreal.EditorAssetLibrary.load_asset(package)
        target = None
        if isinstance(asset, unreal.SkeletalMesh):
            target = REVOLVER_MESH
        elif isinstance(asset, unreal.AnimSequence):
            name = asset.get_name().lower()
            for clip, wanted in REVOLVER_CLIPS.items():
                if clip in name:
                    target = REVOLVER_DIR + "/" + wanted
        if target and package != target:
            if unreal.EditorAssetLibrary.does_asset_exist(target):
                unreal.EditorAssetLibrary.delete_asset(target)
            unreal.EditorAssetLibrary.rename_asset(package, target)
    unreal.EditorAssetLibrary.save_directory(REVOLVER_DIR, only_if_is_dirty=False, recursive=True)

    left = missing_revolver()
    if left:
        unreal.log_error("Backrooms: the revolver import did not make " + ", ".join(left))
        return False
    unreal.log("Backrooms: imported the revolver to " + REVOLVER_DIR)
    return True


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
    left = missing_revolver()
    if left:
        unreal.log("Backrooms: importing the revolver; missing " + ", ".join(left))
        import_revolver()
    else:
        unreal.log("Backrooms: the revolver is already imported in " + REVOLVER_DIR)
    if missing_sounds():
        unreal.log("Backrooms: importing assets/sounds")
        import_sounds()
    else:
        unreal.log("Backrooms: the sounds are already imported in " + SOUND_DIR)
