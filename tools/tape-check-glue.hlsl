// tools/tape-check.sh's second pixel shader: M_BackroomsTape's Custom node
// (TAPE_CODE in backrooms_world.py, pasted in where marked) over stand-ins for
// what the material template gives it. Its reel is Unreal's way up (rows down
// from the top, as the harness flips it), its UV top-left. A fragment's pixel
// position is Unreal's: from the top-left, at pixel centres.
#include "../unreal/Plugins/Backrooms/Shaders/BackroomsTape.ush"

struct FView
{
	float4 ViewSizeAndInvSize;
};
cbuffer Material
{
	FView View;
	float4 Post;
	float4 TapeA;
	float4 TapeB;
	float4 Tape;
	float4 TapeFrom;
};
Texture2D Reel;
SamplerState ReelSampler;
#define Texture2DSampleLevel(T, S, Uv, Level) T.SampleLevel(S, Uv, Level)

float4 TapeCode(float2 PixelPosition)
{
// TAPE_CODE
}

float4 main(float4 Pos : SV_Position) : SV_Target
{
	// Under GL, SV_Position is gl_FragCoord: from the bottom-left.
	return TapeCode(float2(Pos.x, View.ViewSizeAndInvSize.y - Pos.y));
}
