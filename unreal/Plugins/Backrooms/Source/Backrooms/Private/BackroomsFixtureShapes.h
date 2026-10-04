#pragma once
// Where a chunkLayout fixture stands in Unreal, and the plain box drawn for it
// until a level look gives its kind a mesh. Chunk-local: the storey's frame,
// as the chunk actor's components are.
#include "CoreMinimal.h"
#include "BackroomsTypes.h"

struct Fixture;
class UMaterialInterface;
class UStaticMesh;

namespace BackroomsFixtureShapes
{
// The frame a look's mesh is placed in (FBackroomsFixtureLook).
FTransform Frame(const Fixture& F);

// The engine cube's transform for F's plain box, or false for a kind with no
// plain stand-in (scrawl, spalls, the Manila Room's furnishings).
bool Plain(const Fixture& F, FTransform& Out);

UStaticMesh* Mesh();
UMaterialInterface* Material(EBackroomsFixture Kind, UObject* Outer);
}
