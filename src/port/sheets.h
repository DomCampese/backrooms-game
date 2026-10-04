#pragma once
// How the actors' sprite sheets are laid out. The painters (textures.cpp) and
// the pose (sprites.h) both read this, so a sheet and the frame picked from it
// cannot disagree; disagree and a sliver of the next frame shows down one edge.

// Walk-cycle frames, laid out left to right in one sheet. A billboard picks
// two adjacent frames and cross-fades between them, so the count is also the
// resolution of the gait: six is enough that adjacent frames differ by a few
// pixels and the fade reads as motion blur rather than as a dissolve.
constexpr int ENT_FRAMES = 6;      // Clark, the Smiler and the partygoer
constexpr int DOG_FRAMES = 4;      // the pack
// A frame's size on its sheet, pixels.
constexpr int ENT_FRAME_W = 128, ENT_FRAME_H = 256;
constexpr int DOG_FRAME_W = 192, DOG_FRAME_H = 128;

// Rows of the entity sheet, ENT_FRAME_H each. A billboard always faces you, so
// neither a head turn nor a lean can come from the camera; both are in the
// sprite.
//
// Rows 0-2 are how far round his head is: at row 0 he is looking away and there
// is no eyeshine at all, at row 2 he is looking straight at you and the eye is
// lit. The eye appearing is the tell.
//
// Rows 3-4 are the same front-on head, sheared: he tips into the direction he is
// running. This is a shear in the sprite rather than a rotation at the draw call
// because raylib's DrawBillboardPro does not place a billboard the way
// DrawBillboardRec does (AGENTS.md).
enum EntRow { ENT_ROW_AWAY = 0, ENT_ROW_HALF, ENT_ROW_FACE, ENT_ROW_LEAN_L, ENT_ROW_LEAN_R, ENT_ROWS };
