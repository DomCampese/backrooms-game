#pragma once
// How the actors' sprite sheets are laid out. The painters (textures.cpp) and
// the pose (sprites.h) both read this, so a sheet and the frame picked from it
// cannot disagree; disagree and a sliver of the next frame shows down one edge.

// Walk-cycle frames, laid out left to right in one sheet. A billboard picks
// two adjacent frames and cross-fades between them, so the count is also the
// resolution of the gait: with six, adjacent frames differ by a few pixels and
// the fade looks like motion blur.
constexpr int ENT_FRAMES = 6;      // Clark, the Smiler and the partygoer
constexpr int DOG_FRAMES = 4;      // the pack
// A frame's size on its sheet, pixels.
constexpr int ENT_FRAME_W = 128, ENT_FRAME_H = 256;
constexpr int DOG_FRAME_W = 192, DOG_FRAME_H = 128;

// Rows of the entity sheet, ENT_FRAME_H each. A billboard always faces the
// camera, so a head turn or a lean has to be drawn into the sprite.
//
// Rows 0-2 are the head turn: row 0 faces away with no visible eye, row 2
// faces the camera with the eye lit. The eye appearing warns the player.
//
// Rows 3-4 are the front-facing head, sheared to lean into the direction of
// travel. This is a shear in the sprite rather than a rotation at the draw call
// because raylib's DrawBillboardPro does not place a billboard the way
// DrawBillboardRec does (AGENTS.md).
enum EntRow { ENT_ROW_AWAY = 0, ENT_ROW_HALF, ENT_ROW_FACE, ENT_ROW_LEAN_L, ENT_ROW_LEAN_R, ENT_ROWS };
