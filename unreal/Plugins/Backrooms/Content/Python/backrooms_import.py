"""Imports the repository's models into the Unreal project.

The raylib build embeds assets/models/*.glb; the Unreal build imports the same
files, so there is one copy of each model. The editor runs init_unreal.py when
it opens the project, and that imports anything missing. To import again after
the GLB changes, run in the editor's Python console (Output Log, Python):

    import backrooms_import; backrooms_import.import_revolver()

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


def repo_file(*parts):
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", *parts))


def missing():
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

    left = missing()
    if left:
        unreal.log_error("Backrooms: the revolver import did not make " + ", ".join(left))
        return False
    unreal.log("Backrooms: imported the revolver to " + REVOLVER_DIR)
    return True


def import_missing():
    if missing():
        import_revolver()
