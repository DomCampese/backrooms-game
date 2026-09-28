#include "BackroomsChunkActor.h"
#include "BackroomsCoords.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "port/greybox.h"

namespace
{
// The engine's plain material, which takes a colour parameter.
const TCHAR* BaseMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

// Flat colours per surface, the same as tools/greybox-view.cpp, so a frame
// here can be held against one from the raylib viewer.
const FColor SurfaceColour[(int)GreySurface::Count] = {
	FColor(150, 140, 110),   // Floor
	FColor(200, 200, 190),   // Ceiling
	FColor(170, 165, 140),   // Wall
	FColor(120, 100, 80),    // Step
	FColor(140, 110, 90),    // Stair
	FColor(130, 130, 130),   // Pillar
	FColor(90, 110, 160),    // Prop
	FColor(110, 70, 40),     // Door
	FColor(60, 200, 90),     // Exit
	FColor(200, 40, 40),     // CursedExit
	FColor(150, 200, 230),   // Glass
	FColor(100, 100, 110),   // Rail
	FColor(60, 120, 200),    // Water
	FColor(255, 255, 240),   // Light
	FColor(60, 60, 60),      // DeadLight
};
}

ABackroomsChunkActor::ABackroomsChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	// Queried by the sim's bullets (FUnrealTracer), against the triangles
	// themselves. Nothing simulates physics against it: core does collision.
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->bUseComplexAsSimpleCollision = true;
	RootComponent = Mesh;
}

void ABackroomsChunkActor::Build(const GreyboxMesh& Greybox)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterial);
	int32 SectionIndex = 0;
	for (int32 S = 0; S < (int32)GreySurface::Count; S++)
	{
		const GreyboxMesh::Section& Section = Greybox.sections[S];
		if (Section.index.empty())
		{
			continue;
		}
		TArray<FVector> Vertices, Normals;
		TArray<int32> Triangles;
		Vertices.Reserve((int32)Section.pos.size());
		Normals.Reserve((int32)Section.normal.size());
		Triangles.Reserve((int32)Section.index.size());
		for (size_t V = 0; V < Section.pos.size(); V++)
		{
			Vertices.Add(BackroomsCoords::ToUnreal(Section.pos[V]));
			Normals.Add(BackroomsCoords::ToUnrealDirection(Section.normal[V]));
		}
		for (uint32_t I : Section.index)
		{
			Triangles.Add((int32)I);
		}
		// Rounds pass through water, as in the raylib build's MeshTracer.
		const bool bSolid = S != (int32)GreySurface::Water;
		Mesh->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, TArray<FVector2D>(),
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), bSolid);
		if (Base)
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(SurfaceColour[S]));
			Mesh->SetMaterial(SectionIndex, Material);
		}
		SectionIndex++;
	}
}
