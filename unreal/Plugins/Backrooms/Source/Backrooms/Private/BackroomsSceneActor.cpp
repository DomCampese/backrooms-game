#include "BackroomsSceneActor.h"
#include "BackroomsCoords.h"
#include "BackroomsRender.h"
#include "ProceduralMeshComponent.h"
#include "port/objects.h"

namespace
{
// One section's triangles, in Unreal's world.
struct FBatch
{
	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colours;
	TArray<int32> Triangles;

	// Part's triangles, each vertex through Xf (port/draw.h) from the sim's
	// frame, which Origin places.
	void Add(const MB& Part, const Xform& Xf, const FVector& Origin)
	{
		const int32 Base = Vertices.Num();
		const size_t Count = Part.v.size() / 3;
		for (size_t V = 0; V < Count; V++)
		{
			const float X = Part.v[V * 3], Y = Part.v[V * 3 + 1], Z = Part.v[V * 3 + 2];
			const Vec3 P = { Xf.x.x * X + Xf.y.x * Y + Xf.z.x * Z + Xf.t.x, Xf.x.y * X + Xf.y.y * Y + Xf.z.y * Z + Xf.t.y,
				Xf.x.z * X + Xf.y.z * Y + Xf.z.z * Z + Xf.t.z };
			const float NX = Part.n[V * 3], NY = Part.n[V * 3 + 1], NZ = Part.n[V * 3 + 2];
			const Vec3 N = { Xf.x.x * NX + Xf.y.x * NY + Xf.z.x * NZ, Xf.x.y * NX + Xf.y.y * NY + Xf.z.y * NZ,
				Xf.x.z * NX + Xf.y.z * NY + Xf.z.z * NZ };
			Vertices.Add(BackroomsCoords::ToUnreal(P) + Origin);
			Normals.Add(BackroomsCoords::ToUnrealDirection(N).GetSafeNormal());
			UVs.Add(FVector2D(Part.uv[V * 2], Part.uv[V * 2 + 1]));
			Colours.Add(FColor(Part.c[V * 4], Part.c[V * 4 + 1], Part.c[V * 4 + 2], Part.c[V * 4 + 3]));
		}
		for (unsigned short I : Part.idx)
		{
			Triangles.Add(Base + I);
		}
	}
};

// The objects' meshes, built once.
const MB& ObjectMesh(ObjMesh M)
{
	static const MB Meshes[(int32)ObjMesh::Count] = { crateMesh(), crateLidMesh(), canMesh(), deckMesh(), reelMesh(),
		deckLampMesh(), flareMesh() };
	return Meshes[(int32)M];
}
}

ABackroomsSceneActor::ABackroomsSceneActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;
}

void ABackroomsSceneActor::Show(const std::vector<SceneDraw>& Draws, size_t FirstHeld, const SimView& View,
	const UBackroomsRender& Render, const FVector& Origin)
{
	// Sections in the order their materials first come up, which is the
	// order render.cpp draws in.
	TArray<UMaterialInterface*> Order;
	TMap<UMaterialInterface*, FBatch> Batches;
	auto BatchFor = [&](UMaterialInterface* Material) -> FBatch& {
		if (!Batches.Contains(Material))
		{
			Order.Add(Material);
		}
		return Batches.FindOrAdd(Material);
	};
	const Vec3 Right = cameraRight(View);
	const Xform Identity{};
	MB Triangles;
	for (size_t I = 0; I < Draws.size(); I++)
	{
		const SceneDraw& D = Draws[I];
		const bool bHeld = I >= FirstHeld;
		if (D.isMesh)
		{
			UMaterialInterface* Material = D.mesh.mesh == ObjMesh::DeckLamp ? Render.Lamp(bHeld, D.mesh.tint)
				: D.mesh.mesh == ObjMesh::Flare ? Render.HeldFlare()
				: Render.Object(objMat(D.mesh.mesh), bHeld);
			BatchFor(Material).Add(ObjectMesh(D.mesh.mesh), D.mesh.xf, Origin);
			continue;
		}
		const Shape& S = D.shape;
		int32 TexW = 1, TexH = 1;
		shapeTexSize(S.tex, TexW, TexH);
		Triangles = MB();
		shapeTriangles(S, Right, TexW, TexH, Triangles);
		UMaterialInterface* Material = S.kind == ShapeKind::Billboard ? Render.Billboard(S.tex, S.additive)
			: Render.Shape(S.colour.a < 255);
		BatchFor(Material).Add(Triangles, Identity, Origin);
	}

	// Most frames draw the same things in new places (the revolver, the
	// hunter's walk, a flare's flicker), so most sections keep their buffers
	// and only their vertices move. Building every section again each frame
	// cost the game thread a whole mesh upload per frame.
	for (int32 Section = 0; Section < Order.Num(); Section++)
	{
		const FBatch& B = Batches[Order[Section]];
		FBackroomsSceneSection* Was = Built.IsValidIndex(Section) ? &Built[Section] : nullptr;
		if (Was && Was->Material == Order[Section] && Was->Vertices == B.Vertices.Num() && Was->Triangles == B.Triangles)
		{
			Mesh->UpdateMeshSection(Section, B.Vertices, B.Normals, B.UVs, B.Colours, TArray<FProcMeshTangent>());
			continue;
		}
		Mesh->CreateMeshSection(Section, B.Vertices, B.Triangles, B.Normals, B.UVs, B.Colours, TArray<FProcMeshTangent>(), false);
		Mesh->SetMaterial(Section, Order[Section]);
		if (!Was)
		{
			Was = &Built.AddDefaulted_GetRef();
		}
		Was->Material = Order[Section];
		Was->Vertices = B.Vertices.Num();
		Was->Triangles = B.Triangles;
	}
	for (int32 Section = Order.Num(); Section < Built.Num(); Section++)
	{
		Mesh->ClearMeshSection(Section);
	}
	Built.SetNum(Order.Num());
}
