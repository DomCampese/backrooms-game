#pragma once
// GLSL sources for the two shaders: the world (procedural room lighting,
// flashlight cone, flare point light, fog) and the post pass (grain,
// vignette, chromatic aberration).
#include <string>

extern const char *WORLD_VS;
extern const char *POST_FS;
// The world fragment shader with core's numbers written in (PANEL_HALF).
// Load this, never the template it is built from.
std::string worldFragmentShader();
