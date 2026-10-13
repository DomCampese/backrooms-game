#pragma once
// sin, cos, exp, atan2 and hypot from IEEE double arithmetic alone, so every
// platform returns the same float. A libm's sinf, expf or atan2f may round its
// last bit differently on another platform (glibc's sinf is not correctly
// rounded), and core and the sim feed their results into decisions: a trace
// recorded on Linux must replay on a Mac (docs/migration.md, "Floating point").
float portableSin(float x);
float portableCos(float x);
float portableExp(float x);
float portableAtan2(float y, float x);
float portableHypot(float x, float y);
