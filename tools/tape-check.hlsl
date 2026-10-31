// tools/tape-check.sh's pixel shader: BackroomsTape.ush as the Custom node
// calls it, with TAPE_FS's uniforms and one texture for both cells. Under GL,
// SV_Position arrives as gl_FragCoord, which is what the .ush takes.
#include "../unreal/Plugins/Backrooms/Shaders/BackroomsTape.ush"

cbuffer Uniforms
{
	float uTime;
	float2 uRes;
	float4 uCellA;
	float4 uCellB;
	float uMix;
	float uTrack;
	float uStill;
	float uSnow;
};
Texture2D Picture;
SamplerState PictureSampler;

float4 main(float4 Pos : SV_Position) : SV_Target
{
	FBRTape P;
	P.Time = uTime;
	P.Res = uRes;
	P.CellA = uCellA;
	P.CellB = uCellB;
	P.Mix = uMix;
	P.Track = uTrack;
	P.Still = uStill;
	P.Snow = uSnow;
	FBRTapeLine L = BRTapeLine(P, Pos.xy);
	float3 Frames[7];
	[unroll]
	for (int I = 0; I < 7; I++)
	{
		float2 Q = BRTapeTap(L.Q, I);
		float3 A = Picture.Sample(PictureSampler, BRTapeCellUV(P.CellA, Q)).rgb;
		float3 B = Picture.Sample(PictureSampler, BRTapeCellUV(P.CellB, Q)).rgb;
		Frames[I] = lerp(A, B, P.Mix);
	}
	return float4(BRTapeFinish(P, Pos.xy, L, Frames), 1.0);
}
