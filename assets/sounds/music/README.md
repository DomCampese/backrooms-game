# Music sources

| File here | Title | Author | License |
| --- | --- | --- | --- |
| level_fun.ogg | LEVEL FUN toy-band loop | this project | the project's MIT License |

An original tune, rendered by `tools/compose-fun.py` (numpy and ffmpeg with
libvorbis): a toy piano melody over a plucked bass, off-beat ukulele strums, a
kick, finger snaps and a shaker, with a glockenspiel doubling the melody in the
second half. 16 bars at 120 bpm in C major, exactly 32 s, and every note's
tail wraps to the start so the loop has no seam. It is normalised to
-17.7 LUFS, the loudness of the recording it replaced.

LEVEL FUN plays it slightly flat with a slow pitch wander
(`LoopLevels::step`, shared/port/sounds.cpp). Edit the notes in the script and
run it again to change the tune.

It replaced "Sketchbook 2024-02-21_02" by Abstraction (CC0), played slowed to
0.84, which had replaced a synthesized "Happy Birthday" music box. The owner
asked for a better song.
