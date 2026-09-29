"""Runs when the editor opens the project: imports the repository's models if
the project does not have them yet (backrooms_import.py). It waits for the
first editor tick, so the asset registry and the importers are up.

Unreal also runs this file when the editor binary runs the game (-game, as
`make unreal-play` does). There is no editor then, and the first call into
EditorAssetLibrary crashed the game inside GetSubsystemInternal, so the
import only runs in the editor."""
import unreal

import backrooms_import


def _running_game():
    return "-game" in unreal.SystemLibrary.get_command_line().lower().split()


_tick = None


def _once(delta_seconds):
    unreal.unregister_slate_post_tick_callback(_tick)
    try:
        backrooms_import.import_missing()
    except Exception as error:
        # A Python error in a tick callback can vanish; say where it came from.
        unreal.log_error("Backrooms: the revolver import failed: " + repr(error))
        raise


if _running_game():
    unreal.log("Backrooms: running as a game, so the revolver import is skipped; open the editor to import")
else:
    _tick = unreal.register_slate_post_tick_callback(_once)
