#pragma once
// The one place a position crosses between core and Unreal.
//
// Core is metres, y up, +z south, right-handed (as raylib draws it). Unreal is
// centimetres, z up, left-handed. Swapping y and z changes handedness without
// mirroring the world, so core (x, y, z) is Unreal (x, z, y) * 100. Core's yaw
// turns +x toward +z, which after the swap is Unreal's yaw turning +X toward +Y:
// the angle carries over unchanged.
#include "CoreMinimal.h"
#include "core/vec.h"

namespace BackroomsCoords
{
constexpr double CmPerM = 100.0;

inline FVector ToUnreal(const Vec3& V) { return FVector(V.x * CmPerM, V.z * CmPerM, V.y * CmPerM); }

inline Vec3 FromUnreal(const FVector& V)
{
	return { float(V.X / CmPerM), float(V.Z / CmPerM), float(V.Y / CmPerM) };
}

// A direction (a normal): the same swap, no scale.
inline FVector ToUnrealDirection(const Vec3& V) { return FVector(V.x, V.z, V.y); }
inline Vec3 FromUnrealDirection(const FVector& V) { return { float(V.X), float(V.Z), float(V.Y) }; }

// Core's yaw and pitch, radians, pitch up positive.
inline FRotator ToUnrealRotator(float Yaw, float Pitch)
{
	return FRotator(FMath::RadiansToDegrees(Pitch), FMath::RadiansToDegrees(Yaw), 0.0f);
}

// Where storey s stands: s pitches above storey 0. Core and the sim work in the
// frame of one storey at a time; Unreal places every storey at its own height.
inline double StoreyZ(int32 Storey, float StoreyH) { return Storey * StoreyH * CmPerM; }
}
