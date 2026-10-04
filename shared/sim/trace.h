#pragma once
// A trace is a recording of a platform driving the sim: the start, every call
// it made after that with its input and clock, what the SolidTracer answered,
// and a digest of the state after each frame. Replaying it through another
// build of the sim (tools/replay, the Unreal port) must reproduce every
// digest; the first that differs names the frame and the field.
//
// One record per line, `kind name=value ...`. Floats are printed with %.9g and
// doubles with %.17g, which round-trip, so a replay is exact.
#include "start.h"
#include <cstdio>
#include <string>

// Writes a trace, one record per call.
struct TraceWriter {
    FILE *f = nullptr;
    ~TraceWriter() { close(); }
    bool open(const char *path);
    void close();
    explicit operator bool() const { return f != nullptr; }

    void start(const SimStart &s, double now);   // before simBegin
    void level(int lv, double now);              // before Sim::applyLevel
    void place();                                // before simPlace
    void fov(float v);                           // the platform set sim.fov
    void pause(bool on, double now);             // before Sim::setPaused
    // A title-screen frame: menuDrift, then menuBegin with this input.
    void menu(const InputFrame &in, float dt, double now, uint32_t clockSeed);
    void step(const InputFrame &in, float dt, double now, uint32_t clockSeed);
    // One SolidTracer answer, as the tracer returned it.
    void hit(const Ray3 &ray, float nearestIn, bool found, float nearestOut, Vec3 normal);
    // The end of a frame that called the sim, before the platform clears its
    // audio and flags.
    void digest(const Sim &sim);
};

// Records every answer of the tracer it wraps.
struct RecordingTracer : SolidTracer {
    SolidTracer &inner;
    TraceWriter &out;
    RecordingTracer(SolidTracer &in, TraceWriter &w) : inner(in), out(w) {}
    bool nearestSolid(const Ray3 &ray, float &nearest, Vec3 &normal) override;
};

// The state a digest records, one `name=value` token per field.
std::string simDigest(const Sim &sim);

struct ReplayResult {
    int frames = 0;          // digests compared
    int hits = 0;            // tracer answers replayed
    bool ok = false;
    std::string report;      // what failed, or a one-line summary
};

// Replays the trace at `path` through a fresh Sim, answering the tracer from
// the recording, and compares each digest. Stops at the first difference.
ReplayResult replayTrace(const char *path);
