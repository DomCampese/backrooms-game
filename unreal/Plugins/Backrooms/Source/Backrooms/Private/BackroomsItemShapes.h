#pragma once
// The plain stand-in for each thing the sim puts in the world, used until a
// level look gives it a mesh: an engine basic shape at the thing's real size,
// in a flat colour. The scene actor and the held actor both draw these.
#include "CoreMinimal.h"
#include "BackroomsTypes.h"

class UMaterialInterface;
class UStaticMesh;

namespace BackroomsItemShapes
{
// Scales a 1 m basic shape to the thing's size and lifts a base-anchored one
// by half its height. Applied before the thing's own frame, whose X is its
// heading, Y across and Z up.
FTransform Fit(EBackroomsItem Kind);

UStaticMesh* Mesh(EBackroomsItem Kind);

// The thing's flat colour, or Tint when it has one of its own (a party colour).
UMaterialInterface* Material(EBackroomsItem Kind, UObject* Outer, const FLinearColor* Tint = nullptr);
}
