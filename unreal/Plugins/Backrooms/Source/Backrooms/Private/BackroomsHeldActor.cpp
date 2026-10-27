#include "BackroomsHeldActor.h"
#include "BackroomsCoords.h"
#include "BackroomsRender.h"
#include "BackroomsSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "port/held.h"
#include "port/view.h"
#include "sim/sim_math.h"

namespace
{
Vec3 Apply(const Xform& X, float PX, float PY, float PZ)
{
	return { X.x.x * PX + X.y.x * PY + X.z.x * PZ + X.t.x, X.x.y * PX + X.y.y * PY + X.z.y * PZ + X.t.y,
		X.x.z * PX + X.y.z * PY + X.z.z * PZ + X.t.z };
}
}

ABackroomsHeldActor::ABackroomsHeldActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Gun = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Gun"));
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// A viewmodel casts no shadow on the wall beside you.
	Gun->SetCastShadow(false);
	Gun->SetVisibility(false);
	RootComponent = Gun;
}

bool ABackroomsHeldActor::Load()
{
	if (bTried)
	{
		return bLoaded;
	}
	bTried = true;
	// The repository's root is the project's parent (Backrooms.Build.cs).
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), GetDefault<UBackroomsSettings>()->RevolverModel));
	TArray<uint8> Bytes;
	std::string Error;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogTemp, Error, TEXT("Backrooms: revolver: cannot read %s"), *Path);
		return false;
	}
	if (!Model.load(Bytes.GetData(), (size_t)Bytes.Num(), Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Backrooms: revolver: %s: %s"), *Path, UTF8_TO_TCHAR(Error.c_str()));
		return false;
	}
	Positions.resize(Model.glb.prims.size());
	Normals.resize(Model.glb.prims.size());
	for (size_t P = 0; P < Model.glb.prims.size(); P++)
	{
		Model.skinned((int)P, Positions[P], Normals[P]);
	}
	UE_LOG(LogTemp, Display, TEXT("Backrooms: revolver: %s, %d primitives, %d bones, %d clips"), *Path,
		(int32)Model.glb.prims.size(), (int32)Model.glb.bones.size(), (int32)Model.glb.clips.size());
	bLoaded = true;
	return true;
}

void ABackroomsHeldActor::Show(const Sim& Game, bool bInHand, const UBackroomsRender& Render, const FVector& Origin,
	std::vector<SceneDraw>& Draws)
{
	if (!bInHand || heldItem(Game) != Held::Revolver || !Load())
	{
		Gun->SetVisibility(false);
		return;
	}
	if (Model.pose(Game.reloadT, Game.gunCd, Game.ammo))
	{
		for (size_t P = 0; P < Model.glb.prims.size(); P++)
		{
			Model.skinned((int)P, Positions[P], Normals[P]);
		}
	}
	// The GLB's frame into the world: heldWeapon's axes, a mirror like the
	// Web build's (port/held.h), then Unreal's.
	const Xform X = xformOf(heldWeapon(Game, simView(Game)));
	for (size_t P = 0; P < Model.glb.prims.size(); P++)
	{
		const GlbPrimitive& Prim = Model.glb.prims[P];
		const std::vector<float>& Pos = Positions[P];
		const std::vector<float>& Nrm = Normals[P];
		const int32 Count = (int32)(Pos.size() / 3);
		TArray<FVector> Vertices, VertexNormals;
		TArray<FVector2D> UVs;
		Vertices.Reserve(Count);
		VertexNormals.Reserve(Count);
		UVs.Reserve(Count);
		for (int32 V = 0; V < Count; V++)
		{
			Vertices.Add(BackroomsCoords::ToUnreal(Apply(X, Pos[V * 3], Pos[V * 3 + 1], Pos[V * 3 + 2])) + Origin);
			const Vec3 N = Apply({ X.x, X.y, X.z, { 0, 0, 0 } }, Nrm[V * 3], Nrm[V * 3 + 1], Nrm[V * 3 + 2]);
			VertexNormals.Add(BackroomsCoords::ToUnrealDirection(N).GetSafeNormal());
			UVs.Add(FVector2D(Prim.uv[V * 2], Prim.uv[V * 2 + 1]));
		}
		if (Gun->GetNumSections() <= (int32)P)
		{
			// raylib's glTF meshes have no vertex colours: white, opaque.
			TArray<FColor> White;
			White.Init(FColor::White, Count);
			TArray<int32> Triangles;
			for (uint32_t I : Prim.index)
			{
				Triangles.Add((int32)I);
			}
			Gun->CreateMeshSection((int32)P, Vertices, Triangles, VertexNormals, UVs, White, TArray<FProcMeshTangent>(), false);
			Gun->SetMaterial((int32)P, Render.Revolver(Prim.material));
		}
		else
		{
			Gun->UpdateMeshSection((int32)P, Vertices, VertexNormals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>());
		}
	}
	Gun->SetVisibility(true);

	// The muzzle flash and its smoke (Game::drawHeldWeapon).
	const Vec3 Right = { Game.r2x, 0, Game.r2z };
	muzzleDraws(Game, Apply(X, Model.muzzle.x, Model.muzzle.y, Model.muzzle.z), normalize(cross(Right, Game.fwd)), Draws);
}
