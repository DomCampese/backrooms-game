#include "BackroomsWorldSubsystem.h"
#include "BackroomsChunkActor.h"
#include "BackroomsCoords.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Stats/Stats.h"
#include "core/level_rules.h"
#include "core/world.h"
#include "port/greybox.h"
#include "sim/sim.h"

namespace
{
// Chunks drawn round the camera: two rings on its storey, one on the storeys
// above and below. The raylib build streams the same square on your storey.
constexpr int32 ReachOwn = 2;
constexpr int32 ReachOther = 1;
// Chunk actors built per tick, so a level start does not stall a frame.
constexpr int32 BuildsPerTick = 2;
// How often core drops chunk data far from the camera, and how far is far.
constexpr float UnloadEvery = 2.0f;
constexpr int32 UnloadRadius = 5;
// The spawn yaw the raylib build opens a level with (Sim::beginDescent).
constexpr float OpeningYaw = 0.8f;
}

FTransform UBackroomsWorldSubsystem::StartLevel(int32 Level, uint32 Seed, uint32 Visit)
{
	DropAll();
	delete Maze;
	Maze = new World();
	const int32 Lv = FMath::Clamp(Level, 0, NLEVELS - 1);
	Maze->seed = Seed;
	Maze->level = Lv;
	Maze->visit = Visit;
	Maze->wallH = LEVEL_RULES[Lv].wallH;
	Maze->storeyH = LEVEL_RULES[Lv].storeyH;

	const Vec2 Spot = Maze->findOpenSpot(15, 15);
	const Vec3 Eye = { Spot.x, Maze->floorY(cellOf(Spot.x), cellOf(Spot.y)) + Sim::EYE_H, Spot.y };
	const FVector EyeUnreal = BackroomsCoords::ToUnreal(Eye);

	if (!CameraLight)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		CameraLight = GetWorld()->SpawnActor<APointLight>(EyeUnreal, FRotator::ZeroRotator, Params);
		if (CameraLight)
		{
			CameraLight->SetMobility(EComponentMobility::Movable);
			UPointLightComponent* Light = CameraLight->PointLightComponent;
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(60.0f);
			Light->SetAttenuationRadius(4000.0f);
			Light->SetCastShadows(false);
		}
	}
	return FTransform(BackroomsCoords::ToUnrealRotator(OpeningYaw, 0.0f), EyeUnreal);
}

void UBackroomsWorldSubsystem::Deinitialize()
{
	DropAll();
	delete Maze;
	Maze = nullptr;
	Super::Deinitialize();
}

bool UBackroomsWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UBackroomsWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBackroomsWorldSubsystem, STATGROUP_Tickables);
}

void UBackroomsWorldSubsystem::Tick(float DeltaTime)
{
	if (!Maze)
	{
		return;
	}
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return;
	}
	const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
	if (CameraLight)
	{
		CameraLight->SetActorLocation(Camera);
	}
	StreamAround(Camera, DeltaTime);
}

void UBackroomsWorldSubsystem::StreamAround(const FVector& Camera, float DeltaTime)
{
	const Vec3 At = BackroomsCoords::FromUnreal(Camera);
	const int32 Storey = Maze->storeyH > 0.0f ? FMath::FloorToInt32(At.y / Maze->storeyH) : 0;
	if (Storey != Maze->storey)
	{
		Maze->setStorey(Storey);
	}
	// What core has dropped or rebuilt goes first, before any actor is kept.
	for (const ChunkRef& Ref : Maze->staleChunks)
	{
		DropChunk(FIntVector(Ref.cx, Ref.cz, Ref.storey));
	}
	Maze->staleChunks.clear();

	// Wanted chunks, nearest first: this storey ring by ring, then the others.
	const int32 Pcx = fdiv(cellOf(At.x), CCELLS), Pcz = fdiv(cellOf(At.z), CCELLS);
	TArray<FIntVector> Wanted;
	const int32 Rel[3] = { 0, -1, 1 };
	for (int32 R : Rel)
	{
		if (R != 0 && Maze->storeyH <= 0.0f)
		{
			continue;
		}
		const int32 Reach = R == 0 ? ReachOwn : ReachOther;
		for (int32 Ring = 0; Ring <= Reach; Ring++)
		{
			for (int32 Dz = -Ring; Dz <= Ring; Dz++)
			{
				for (int32 Dx = -Ring; Dx <= Ring; Dx++)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dz)) == Ring)
					{
						Wanted.Add(FIntVector(Pcx + Dx, Pcz + Dz, Storey + R));
					}
				}
			}
		}
	}
	int32 Budget = BuildsPerTick;
	for (const FIntVector& Key : Wanted)
	{
		if (Budget > 0 && !Chunks.Contains(Key))
		{
			BuildChunk(Key);
			Budget--;
		}
	}
	TArray<FIntVector> Unwanted;
	for (const TPair<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Entry : Chunks)
	{
		if (!Wanted.Contains(Entry.Key))
		{
			Unwanted.Add(Entry.Key);
		}
	}
	for (const FIntVector& Key : Unwanted)
	{
		DropChunk(Key);
	}

	SinceUnload += DeltaTime;
	if (SinceUnload >= UnloadEvery)
	{
		SinceUnload = 0.0f;
		Maze->unloadFar(Pcx, Pcz, UnloadRadius);
	}
}

void UBackroomsWorldSubsystem::BuildChunk(const FIntVector& Key)
{
	StoreyScope Scope(*Maze, Key.Z);
	const GreyboxMesh Greybox = greyboxChunk(*Maze, Key.X, Key.Y);
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	const FVector Origin(0.0, 0.0, BackroomsCoords::StoreyZ(Key.Z, Maze->storeyH));
	ABackroomsChunkActor* Actor = GetWorld()->SpawnActor<ABackroomsChunkActor>(Origin, FRotator::ZeroRotator, Params);
	if (Actor)
	{
		Actor->Build(Greybox);
		Chunks.Add(Key, Actor);
	}
}

void UBackroomsWorldSubsystem::DropChunk(const FIntVector& Key)
{
	TObjectPtr<ABackroomsChunkActor> Actor;
	if (Chunks.RemoveAndCopyValue(Key, Actor) && Actor)
	{
		Actor->Destroy();
	}
}

void UBackroomsWorldSubsystem::DropAll()
{
	for (const TPair<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Entry : Chunks)
	{
		if (Entry.Value)
		{
			Entry.Value->Destroy();
		}
	}
	Chunks.Reset();
}
