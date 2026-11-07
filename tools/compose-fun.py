#!/usr/bin/env python3
"""Render LEVEL FUN's music: an original toy-band loop, 16 bars at 120 bpm.

Writes assets/sounds/music/level_fun.ogg (needs numpy and ffmpeg with
libvorbis). The loop is exactly 32 s and every note's tail wraps round to the
start, so the recording loops with no seam.

    python3 tools/compose-fun.py [out.ogg]
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

SR = 44100
BPM = 120
BEAT = 60.0 / BPM
BARS = 16
LENGTH = int(round(BARS * 4 * BEAT * SR))
TARGET_LUFS = -17.7   # the loudness of the recording it replaced

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    return 12 * (int(name[-1]) + 1) + NOTE[name[0]] + name[1:-1].count("#")


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# One bar per line: note:beats, r for a rest. Each bar adds up to 4 beats.
MELODY = """
E5:.5 G5:.5 r:.5 G5:.5 A5:.5 G5:.5 E5:1
C5:.5 E5:.5 r:.5 E5:.5 D5:.5 C5:.5 A4:1
F5:.5 A5:.5 r:.5 A5:.5 G5:.5 F5:.5 C5:1
D5:.75 E5:.25 F5:.5 G5:.5 B4:1 r:1
E5:.5 G5:.5 r:.5 G5:.5 C6:.5 B5:.5 G5:1
A5:.5 G5:.5 E5:.5 C5:.5 D5:.5 E5:.5 C5:1
F5:.5 E5:.5 D5:.5 A4:.5 D5:.5 F5:.5 A5:1
G5:1.5 F5:.5 E5:.5 D5:.5 B4:1
A5:.5 r:.25 A5:.25 C6:.5 A5:.5 G5:1 F5:1
G5:.5 r:.25 G5:.25 B5:.5 G5:.5 F5:1 D5:1
E5:.5 G5:.5 B5:.5 G5:.5 E5:.5 G5:.5 B5:1
C6:.75 B5:.25 A5:.5 E5:.5 A5:2
D5:.5 F5:.5 A5:.5 D6:.5 C6:.5 A5:.5 F5:1
G5:.5 B5:.5 D6:.5 B5:.5 A5:.5 G5:.5 F5:.5 D5:.5
E5:.5 G5:.5 C6:.5 G5:.5 E5:.5 C5:.5 D5:.5 E5:.5
D5:1 r:1 G4:.5 B4:.5 D5:.5 F5:.5
"""

# Root and chord tones (MIDI, from C4) per bar.
CHORDS = [
    ("C", [60, 64, 67]), ("A", [57, 60, 64]), ("F", [57, 60, 65]), ("G", [55, 59, 62]),
    ("C", [60, 64, 67]), ("A", [57, 60, 64]), ("D", [57, 62, 65]), ("G", [55, 59, 62]),
    ("F", [57, 60, 65]), ("G", [55, 59, 62]), ("E", [55, 59, 64]), ("A", [57, 60, 64]),
    ("D", [57, 62, 65]), ("G", [55, 59, 62]), ("C", [60, 64, 67]), ("G", [53, 59, 62]),
]
BASS_ROOT = {"C": 36, "D": 38, "E": 40, "F": 41, "G": 43, "A": 45}

rng = np.random.default_rng(1994)
mix_l = np.zeros(LENGTH)
mix_r = np.zeros(LENGTH)


def place(sig, t0, gain, pan):
    """Add a note at t0 seconds, wrapping its tail round to the loop's start."""
    start = int(round(t0 * SR)) % LENGTH
    idx = (start + np.arange(len(sig))) % LENGTH
    a = pan * 0.5 * np.pi / 2 + np.pi / 4          # pan -1..1, equal power
    np.add.at(mix_l, idx, sig * gain * np.cos(a))
    np.add.at(mix_r, idx, sig * gain * np.sin(a))


def env(n, attack, decay):
    t = np.arange(n) / SR
    return np.minimum(t / attack, 1.0) * np.exp(-t / decay)


def toy_piano(f, dur):
    # Struck metal tines: a fundamental, a strong inharmonic partial and a
    # quick click. The partial ratios are a toy piano's, not a string's.
    n = int((dur + 1.2) * SR)
    t = np.arange(n) / SR
    s = (np.sin(2 * np.pi * f * t) * env(n, 0.002, 0.55)
         + 0.45 * np.sin(2 * np.pi * f * 3.93 * t) * env(n, 0.001, 0.12)
         + 0.20 * np.sin(2 * np.pi * f * 2.0 * t) * env(n, 0.002, 0.30)
         + 0.12 * np.sin(2 * np.pi * f * 9.1 * t) * env(n, 0.0005, 0.03))
    release = np.clip((dur + 0.4 - t) / 0.4, 0.0, 1.0)
    return s * release


def glock(f, dur):
    n = int(1.6 * SR)
    t = np.arange(n) / SR
    return (np.sin(2 * np.pi * f * t) * env(n, 0.001, 0.7)
            + 0.3 * np.sin(2 * np.pi * f * 2.76 * t) * env(n, 0.001, 0.15))


def pluck(f, dur):
    # Karplus-Strong: a ukulele-ish nylon pluck.
    period = int(round(SR / f))
    n = int((dur + 0.3) * SR)
    buf = rng.uniform(-1, 1, period)
    out = np.zeros(n)
    for i in range(n):
        v = buf[i % period]
        out[i] = v
        buf[i % period] = 0.5 * (v + buf[(i + 1) % period]) * 0.996
    t = np.arange(n) / SR
    return out * np.clip((dur + 0.15 - t) / 0.15, 0.0, 1.0)


def bass(f, dur):
    n = int((dur + 0.1) * SR)
    t = np.arange(n) / SR
    s = sum(np.sin(2 * np.pi * f * k * t) / k ** 1.6 for k in range(1, 6))
    return s * env(n, 0.004, 0.35) * np.clip((dur + 0.05 - t) / 0.05, 0.0, 1.0)


def kick():
    n = int(0.25 * SR)
    t = np.arange(n) / SR
    f = 50 + 90 * np.exp(-t / 0.03)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * env(n, 0.001, 0.09)


def snap():
    n = int(0.18 * SR)
    noise = rng.uniform(-1, 1, n)
    hp = np.diff(noise, prepend=0.0)               # bright: a finger snap, not a drum
    t = np.arange(n) / SR
    body = np.sin(2 * np.pi * 1800 * t) * env(n, 0.0005, 0.01)
    return (0.6 * hp + 0.3 * body) * env(n, 0.0008, 0.045)


def shaker():
    n = int(0.07 * SR)
    noise = np.diff(np.diff(rng.uniform(-1, 1, n), prepend=0.0), prepend=0.0)
    return noise * env(n, 0.01, 0.02)


def parse(text):
    notes, beat = [], 0.0
    for line in text.strip().splitlines():
        bar_start = beat
        for tok in line.split():
            name, d = tok.split(":")
            d = float(d)
            if name != "r":
                notes.append((beat, d, midi(name)))
            beat += d
        assert abs(beat - bar_start - 4) < 1e-9, line
    return notes


def render():
    melody = parse(MELODY)
    assert len(CHORDS) == BARS and melody[-1][0] < BARS * 4
    for beat, d, m in melody:
        place(toy_piano(hz(m), d * BEAT), beat * BEAT, 0.34, -0.15)
        if beat >= 8 * 4:                          # second half: glockenspiel an octave up
            place(glock(hz(m + 12), d * BEAT), beat * BEAT + 0.012, 0.10, 0.35)
    for bar, (root, tones) in enumerate(CHORDS):
        b0 = bar * 4
        r = BASS_ROOT[root]
        fifth = r + 7
        for beat, m in ((0, r), (1.5, r + 12), (2, fifth), (3.5, r)):
            place(bass(hz(m), 0.45 * BEAT), (b0 + beat) * BEAT, 0.42, 0.0)
        for beat in (1, 3, 3.5):                   # off-beat strums
            for k, m in enumerate(tones):
                place(pluck(hz(m), 0.4 * BEAT), (b0 + beat) * BEAT + k * 0.008, 0.16, 0.3)
        for beat in (0, 2):
            place(kick(), (b0 + beat) * BEAT, 0.55, 0.0)
        for beat in (1, 3):
            place(snap(), (b0 + beat) * BEAT, 0.28, -0.25)
        for e in range(8):
            place(shaker(), (b0 + e * 0.5) * BEAT, 0.07 if e % 2 else 0.04, 0.5)
    return np.stack([mix_l, mix_r], axis=1)


def write_ogg(stereo, out):
    stereo = stereo / np.max(np.abs(stereo)) * 0.7
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "fun.wav")
        with wave.open(wav, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes((stereo * 32767).astype("<i2").tobytes())
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav,
                        "-af", f"loudnorm=I={TARGET_LUFS}:TP=-1.5:LRA=11:linear=true",
                        "-ar", str(SR), "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", "6",
                        out], check=True)


if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "assets/sounds/music/level_fun.ogg")
    write_ogg(render(), out)
    print("wrote", out)
