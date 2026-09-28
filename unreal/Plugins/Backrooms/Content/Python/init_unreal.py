"""Runs when the editor opens the project: imports the repository's models if
the project does not have them yet (backrooms_import.py). It waits for the
first editor tick, so the asset registry and the importers are up."""
import unreal

import backrooms_import

_tick = None


def _once(delta_seconds):
    unreal.unregister_slate_post_tick_callback(_tick)
    backrooms_import.import_missing()


_tick = unreal.register_slate_post_tick_callback(_once)
