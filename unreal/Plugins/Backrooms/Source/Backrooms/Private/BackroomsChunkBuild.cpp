#include "BackroomsChunkBuild.h"
#include "BackroomsCoords.h"
#include "BackroomsFixtureShapes.h"
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

// The tangent along +u. Unreal derives the bitangent as Normal x Tangent and
// flips it on request; a normal map's green follows +v (down the texture), so it
// is flipped wherever that cross product points the other way. The handedness
// swap in BackroomsCoords flips cross products, so this is decided in Unreal's
// space.
FProcMeshTangent Tangent(const FVector& Normal, const Vec3& UAxis, const Vec3& VAxis)
{
	const FVector U = BackroomsCoords::ToUnrealDirection(UAxis);
	const FVector V = BackroomsCoords::ToUnrealDirection(VAxis);
	return FProcMeshTangent(U, FVector::DotProduct(FVector::CrossProduct(Normal, U), V) < 0.0);
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

// Every fixture: the look's mesh for its kind in the fixture's frame, or a
// plain box. Fixtures have no collision, as in the raylib build.
void AddFixtures(const ChunkLayout& Layout, const UBackroomsLevelLook* Look, UProceduralMeshComponent& Parent,
	TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Instances)
{
	TMap<EBackroomsFixture, UInstancedStaticMeshComponent*> ByKind;
	for (const Fixture& Item : Layout.fixtures)
	{
		const EBackroomsFixture Kind = (EBackroomsFixture)Item.kind;
		const FBackroomsFixtureLook* Own = Look ? Look->Fixtures.Find(Kind) : nullptr;
		const bool bMeshed = Own && Own->Mesh;
		FTransform Placed;
		if (bMeshed)
		{
			Placed = Own->Offset * BackroomsFixtureShapes::Frame(Item);
		}
		else if (!BackroomsFixtureShapes::Plain(Item, Placed))
		{
			continue;
		}
		UInstancedStaticMeshComponent*& Component = ByKind.FindOrAdd(Kind);
		if (!Component)
		{
			Component = NewInstances(Parent, bMeshed ? Own->Mesh.Get() : BackroomsFixtureShapes::Mesh(), Instances);
			UMaterialInterface* Material = Own && Own->Material ? Own->Material.Get()
				: bMeshed ? nullptr : BackroomsFixtureShapes::Material(Kind, Parent.GetOwner());
			if (Material)
			{
				Component->SetMaterial(0, Material);
			}
		}
		Component->AddInstance(Placed);
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
		const int32 Count = (int32)Section.pos.size();
		TArray<FVector> Vertices, Normals;
		TArray<FVector2D> UVs;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Triangles;
		Vertices.Reserve(Count);
		Normals.Reserve(Count);
		UVs.Reserve(Count);
		Tangents.Reserve(Count);
		Triangles.Reserve((int32)Section.index.size());
		for (int32 V = 0; V < Count; V++)
		{
			Vertices.Add(BackroomsCoords::ToUnreal(Section.pos[V]));
			Normals.Add(BackroomsCoords::ToUnrealDirection(Section.normal[V]));
			UVs.Add(FVector2D(Section.uv[V].x, Section.uv[V].y));
			Tangents.Add(Tangent(Normals.Last(), Section.uAxis[V], Section.vAxis[V]));
		}
		for (uint32_t I : Section.index)
		{
			Triangles.Add((int32)I);
		}
		// Rounds pass through water, as in the raylib build's MeshTracer.
		const bool bSolid = bCollision && S != (int32)GreySurface::Water;
		Mesh.CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs,
			TArray<FLinearColor>(), Tangents, bSolid);
		if (UMaterialInterface* Material = SurfaceMaterial(Look, (EBackroomsSurface)S, Mesh.GetOwner()))
		{
			Mesh.SetMaterial(SectionIndex, Material);
		}
		SectionIndex++;
	}
	const ChunkLayout Layout = chunkLayout(W, Cx, Cz);
	AddFixtures(Layout, Look, Mesh, Instances);
	if (MeshedProps)
	{
		AddProps(Layout, *Look, Mesh, Instances);
	}
	if (bMeshedFittings)
	{
		AddFittings(Layout, *Look, Mesh, Instances);
	}
}
