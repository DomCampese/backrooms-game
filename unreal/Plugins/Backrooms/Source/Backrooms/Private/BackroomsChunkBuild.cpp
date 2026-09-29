#include "BackroomsChunkBuild.h"
#include "BackroomsCoords.h"
#include "BackroomsLevelLook.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "core/layout.h"
#include "port/greybox.h"

namespace
{
// The engine's plain material, which takes a colour parameter.
const TCHAR* BaseMaterial = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

UMaterialInterface* SurfaceMaterial(const UBackroomsLevelLook* Look, EBackroomsSurface Surface, UObject* Outer)
{
	const FBackroomsSurfaceLook* Entry = Look ? Look->Surfaces.Find(Surface) : nullptr;
	if (Entry && Entry->Material)
	{
		return Entry->Material;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BaseMaterial);
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Flat = UMaterialInstanceDynamic::Create(Base, Outer);
	Flat->SetVectorParameterValue(TEXT("Color"), Entry ? Entry->Colour : UBackroomsLevelLook::DefaultColour(Surface));
	return Flat;
}

UInstancedStaticMeshComponent* NewInstances(UProceduralMeshComponent& Parent, UStaticMesh* Mesh,
	TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances)
{
	UInstancedStaticMeshComponent* Component =
		NewObject<UInstancedStaticMeshComponent>(Parent.GetOwner(), NAME_None, RF_Transient);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetupAttachment(&Parent);
	Component->RegisterComponent();
	Instances.Add(Component);
	return Component;
}

void AddProps(const ChunkLayout& Layout, const UBackroomsLevelLook& Look, UProceduralMeshComponent& Parent,
	TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances)
{
	TMap<EBackroomsProp, UInstancedStaticMeshComponent*> ByKind;
	for (const PropPlacement& Prop : Layout.props)
	{
		const EBackroomsProp Kind = (EBackroomsProp)Prop.kind;
		const FBackroomsPropLook* Entry = Look.Props.Find(Kind);
		if (!Entry || !Entry->Mesh)
		{
			continue;
		}
		UInstancedStaticMeshComponent*& Component = ByKind.FindOrAdd(Kind);
		if (!Component)
		{
			Component = NewInstances(Parent, Entry->Mesh, Instances);
		}
		const FTransform Frame(BackroomsCoords::ToUnrealRotator(Prop.yaw, 0.0f),
			BackroomsCoords::ToUnreal({ Prop.x, Prop.floorY, Prop.z }));
		Component->AddInstance(Entry->Offset * Frame);
	}
}

void AddFittings(const ChunkLayout& Layout, const UBackroomsLevelLook& Look, UProceduralMeshComponent& Parent,
	TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances)
{
	UInstancedStaticMeshComponent* Lit = nullptr;
	UInstancedStaticMeshComponent* Dead = nullptr;
	for (const LightFitting& Fitting : Layout.fittings)
	{
		if (Fitting.gap != FittingGap::None)
		{
			continue;
		}
		UInstancedStaticMeshComponent*& Component = Fitting.dead ? Dead : Lit;
		if (!Component)
		{
			Component = NewInstances(Parent, Look.FittingMesh, Instances);
			if (Fitting.dead && Look.DeadFittingMaterial)
			{
				Component->SetMaterial(0, Look.DeadFittingMaterial);
			}
		}
		// A Level 1 batten runs along x: a quarter turn.
		const float Yaw = Fitting.turned ? UE_HALF_PI : 0.0f;
		Component->AddInstance(FTransform(BackroomsCoords::ToUnrealRotator(Yaw, 0.0f), BackroomsCoords::ToUnreal(Fitting.pos)));
	}
}
}

void BackroomsChunkBuild::Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look,
	UProceduralMeshComponent& Mesh, bool bCollision, TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances)
{
	const uint32 MeshedProps = Look ? Look->MeshedProps() : 0;
	const bool bMeshedFittings = Look && Look->FittingMesh;
	const GreyboxMesh Greybox = greyboxChunk(W, Cx, Cz, MeshedProps);
	int32 SectionIndex = 0;
	for (int32 S = 0; S < (int32)GreySurface::Count; S++)
	{
		const GreyboxMesh::Section& Section = Greybox.sections[S];
		const bool bFitting = S == (int32)GreySurface::Light || S == (int32)GreySurface::DeadLight;
		if (Section.index.empty() || (bFitting && bMeshedFittings))
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
		const bool bSolid = bCollision && S != (int32)GreySurface::Water;
		Mesh.CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, TArray<FVector2D>(),
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), bSolid);
		if (UMaterialInterface* Material = SurfaceMaterial(Look, (EBackroomsSurface)S, Mesh.GetOwner()))
		{
			Mesh.SetMaterial(SectionIndex, Material);
		}
		SectionIndex++;
	}
	if (Look && (MeshedProps || bMeshedFittings))
	{
		const ChunkLayout Layout = chunkLayout(W, Cx, Cz);
		if (MeshedProps)
		{
			AddProps(Layout, *Look, Mesh, Instances);
		}
		if (bMeshedFittings)
		{
			AddFittings(Layout, *Look, Mesh, Instances);
		}
	}
}
