#include "BackroomsWorldSubsystem.h"
#include "BackroomsChunkActor.h"
#include "BackroomsCoords.h"
#include "BackroomsHeldActor.h"
#include "BackroomsLevelLook.h"
#include "BackroomsSceneActor.h"
#include "BackroomsSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Stats/Stats.h"
#include "core/level_rules.h"
#include "port/scene.h"
#include "port/view.h"
#include "sim/start.h"
#include "sim/trace.h"

namespace
{
// How often core drops chunk data far from the player, and how far is far.
constexpr float UnloadEvery = 2.0f;
constexpr int32 UnloadRadius = 5;
// The yaw a descent opens with (Sim::beginDescent), for the free camera.
constexpr float OpeningYaw = 0.8f;
// The raylib build clamps a frame's time to this before stepping the sim.
constexpr float MaxDt = 0.05f;

// Bullets against level geometry: a line trace against the chunk actors, which
// carry complex collision of the greybox. The ray is in the sim's frame, the
// storey the player is on.
struct FUnrealTracer final : SolidTracer
{
	UWorld* Scene = nullptr;
	const Sim* Game = nullptr;

	bool nearestSolid(const Ray3& Ray, float& Nearest, Vec3& Normal) override
	{
		const FVector Origin(0.0, 0.0, BackroomsCoords::StoreyZ(Game->world.storey, Game->world.storeyH));
		const FVector Start = BackroomsCoords::ToUnreal(Ray.position) + Origin;
		const FVector End = Start + BackroomsCoords::ToUnrealDirection(Ray.direction) * (Nearest * BackroomsCoords::CmPerM);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BackroomsShot), true);
		if (!Scene->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return false;
		}
		Nearest *= Hit.Time;
		Normal = BackroomsCoords::FromUnrealDirection(Hit.ImpactNormal);
		return true;
	}
};
}

void UBackroomsWorldSubsystem::NewSim()
{
	FreeSim();
	Game = new Sim();
	FUnrealTracer* Shots = new FUnrealTracer();
	Shots->Scene = GetWorld();
	Shots->Game = Game;
	Tracer = Shots;
	Game->tracer = Tracer;
	if (ClockZero < 0.0)
	{
		ClockZero = FPlatformTime::Seconds();
	}
}

double UBackroomsWorldSubsystem::Now() const { return FPlatformTime::Seconds() - ClockZero; }

void UBackroomsWorldSubsystem::StartRun(uint32 Seed, int32 Level)
{
	NewSim();
	bRun = true;
	SimStart Start;
	Start.seed = Seed;
	Start.level = Level > 0 ? FMath::Min(Level, NLEVELS - 1) : -1;
	Start.menu = false;
	// The records file is the raylib build's; this one keeps none yet.
	Start.keepRecords = false;
	Start.fov = windowFovY(1440, 850, 0.0f);

	FString RecordPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("BackroomsRecord="), RecordPath))
	{
		Trace = new TraceWriter();
		if (Trace->open(TCHAR_TO_UTF8(*RecordPath)))
		{
			Recorder = new RecordingTracer(*Tracer, *Trace);
			Game->tracer = Recorder;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("BackroomsRecord: cannot write %s"), *RecordPath);
			delete Trace;
			Trace = nullptr;
		}
	}

	// The order Game::init uses: grng's draws depend on it.
	double T = Now();
	if (Trace) Trace->start(Start, T);
	simBegin(*Game, Start, T);
	for (int32 Lv : { 0, Start.level })
	{
		if (Lv < 0)
		{
			continue;
		}
		T = Now();
		if (Trace) Trace->level(Lv, T);
		Game->applyLevel(Lv, T);
	}
	if (Trace) Trace->place();
	simPlace(*Game, Start);
	FinishFrame();
}

FTransform UBackroomsWorldSubsystem::StartFreeCamera(int32 Level, uint32 Seed, uint32 Visit)
{
	NewSim();
	bRun = false;
	World& Maze = Game->world;
	const int32 Lv = FMath::Clamp(Level, 0, NLEVELS - 1);
	Maze.seed = Seed;
	Maze.level = Lv;
	Maze.visit = Visit;
	Maze.wallH = LEVEL_RULES[Lv].wallH;
	Maze.storeyH = LEVEL_RULES[Lv].storeyH;
	const Vec2 Spot = Maze.findOpenSpot(15, 15);
	const Vec3 Eye = { Spot.x, Maze.floorY(cellOf(Spot.x), cellOf(Spot.y)) + Sim::EYE_H, Spot.y };
	return FTransform(BackroomsCoords::ToUnrealRotator(OpeningYaw, 0.0f), BackroomsCoords::ToUnreal(Eye));
}

void UBackroomsWorldSubsystem::TickRun(const InputFrame& In, bool bPauseToggled, float Dt)
{
	if (!Game || !bRun)
	{
		return;
	}
	Dt = FMath::Min(Dt, MaxDt);
	const double T = Now();
	Game->clockSeed = (uint32_t)FDateTime::UtcNow().ToUnixTimestamp();

	if (Game->inMenu)
	{
		if (Trace) Trace->menu(In, Dt, T, Game->clockSeed);
		Game->menuDrift(Dt, T);
		Game->menuBegin(In, T);
		if (Trace) Trace->digest(*Game);
		FinishFrame();
		return;
	}
	if (bPauseToggled)
	{
		const bool bPause = !Game->paused;
		if (Trace) Trace->pause(bPause, T);
		Game->setPaused(bPause, T);
		if (Trace && bPause) Trace->digest(*Game);
	}
	if (Game->paused)
	{
		FinishFrame();
		return;
	}
	if (Trace) Trace->step(In, Dt, T, Game->clockSeed);
	Game->step(In, Dt, T);
	if (Trace) Trace->digest(*Game);
	FinishFrame();
}

// What the raylib build's finishStep and updateOccupancy do with the sim's
// outputs. Sound is M6: the events are dropped for now.
void UBackroomsWorldSubsystem::FinishFrame()
{
	Game->audio.clear();
	Game->dropAimLatch = false;
	Game->shadowsStale = false;
	Game->recordsChanged = false;
}

FVector UBackroomsWorldSubsystem::StoreyOrigin() const
{
	return FVector(0.0, 0.0, BackroomsCoords::StoreyZ(Game->world.storey, Game->world.storeyH));
}

FTransform UBackroomsWorldSubsystem::ViewTransform(float& OutFovY) const
{
	const SimView View = simView(*Game);
	OutFovY = View.fovY;
	const FMatrix Axes = FRotationMatrix::MakeFromXZ(BackroomsCoords::ToUnrealDirection(View.forward),
		BackroomsCoords::ToUnrealDirection(View.up));
	return FTransform(Axes.Rotator(), BackroomsCoords::ToUnreal(View.eye) + StoreyOrigin());
}

void UBackroomsWorldSubsystem::FreeSim()
{
	DropAll();
	if (Scene)
	{
		Scene->Destroy();
		Scene = nullptr;
	}
	if (Hand)
	{
		Hand->Destroy();
		Hand = nullptr;
	}
	delete Recorder;
	Recorder = nullptr;
	delete Trace;   // closes the file
	Trace = nullptr;
	delete Game;
	Game = nullptr;
	delete Tracer;
	Tracer = nullptr;
	bRun = false;
}

void UBackroomsWorldSubsystem::Deinitialize()
{
	FreeSim();
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
	if (!Game)
	{
		return;
	}
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return;
	}
	const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
	if (!CameraLight && GetDefault<UBackroomsSettings>()->bCameraLight)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		CameraLight = GetWorld()->SpawnActor<APointLight>(Camera, FRotator::ZeroRotator, Params);
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
	if (CameraLight)
	{
		CameraLight->SetActorLocation(Camera);
	}
	if (bRun)
	{
		// The sim owns which storey is current (it rebases as you climb).
		StreamAround(BackroomsCoords::ToUnreal({ Game->px, 0.0f, Game->pz }), Game->world.storey, DeltaTime);
		ShowScene();
	}
	else
	{
		const World& Maze = Game->world;
		const float Metres = float(Camera.Z / BackroomsCoords::CmPerM);
		const int32 Storey = Maze.storeyH > 0.0f ? FMath::FloorToInt32(Metres / Maze.storeyH) : 0;
		if (Storey != Maze.storey)
		{
			Game->world.setStorey(Storey);
		}
		StreamAround(Camera, Storey, DeltaTime);
	}
}

// Focus is in Unreal space; only its x and y (core's x and z) are read.
void UBackroomsWorldSubsystem::StreamAround(const FVector& Focus, int32 Storey, float DeltaTime)
{
	World& Maze = Game->world;
	// What core has dropped or rebuilt goes first, before any actor is kept.
	for (const ChunkRef& Ref : Maze.staleChunks)
	{
		DropChunk(FIntVector(Ref.cx, Ref.cz, Ref.storey));
	}
	Maze.staleChunks.clear();

	// Wanted chunks, nearest first: this storey ring by ring, then the others.
	const Vec3 At = BackroomsCoords::FromUnreal(Focus);
	const int32 Pcx = fdiv(cellOf(At.x), CCELLS), Pcz = fdiv(cellOf(At.z), CCELLS);
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	TArray<FIntVector> Wanted;
	const int32 Rel[3] = { 0, -1, 1 };
	for (int32 R : Rel)
	{
		if (R != 0 && Maze.storeyH <= 0.0f)
		{
			continue;
		}
		const int32 Reach = R == 0 ? Settings->ReachOwnStorey : Settings->ReachOtherStoreys;
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
	int32 Budget = Settings->BuildsPerFrame;
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
		Maze.unloadFar(Pcx, Pcz, UnloadRadius);
	}
}

void UBackroomsWorldSubsystem::BuildChunk(const FIntVector& Key)
{
	World& Maze = Game->world;
	const UBackroomsLevelLook* LevelLook = CurrentLook();
	UClass* ChunkClass = GetDefault<UBackroomsSettings>()->ChunkClass.LoadSynchronous();
	if (!ChunkClass)
	{
		ChunkClass = ABackroomsChunkActor::StaticClass();
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	const FVector Origin(0.0, 0.0, BackroomsCoords::StoreyZ(Key.Z, Maze.storeyH));
	ABackroomsChunkActor* Actor =
		GetWorld()->SpawnActor<ABackroomsChunkActor>(ChunkClass, Origin, FRotator::ZeroRotator, Params);
	if (Actor)
	{
		StoreyScope Scope(Maze, Key.Z);
		Actor->Build(Maze, Key.X, Key.Y, LevelLook);
		Chunks.Add(Key, Actor);
	}
}

const UBackroomsLevelLook* UBackroomsWorldSubsystem::CurrentLook()
{
	if (LookLevel != Game->world.level)
	{
		LookLevel = Game->world.level;
		Look = GetDefault<UBackroomsSettings>()->LookFor(LookLevel);
	}
	return Look;
}

FBackroomsHud UBackroomsWorldSubsystem::GetHud() const
{
	FBackroomsHud Hud;
	if (!Game)
	{
		return Hud;
	}
	const Sim& S = *Game;
	Hud.bRunning = bRun;
	Hud.bTitleScreen = S.inMenu;
	Hud.bPaused = S.paused;
	Hud.Level = S.level;
	Hud.LevelName = FText::FromString(UTF8_TO_TCHAR(LEVEL_RULES[S.level].name));
	Hud.Storey = S.world.storey;
	Hud.Health = S.health;
	Hud.Sanity = S.sanity;
	Hud.Stamina = S.stamina;
	Hud.Battery = S.battery;
	Hud.bFlashlightOn = S.flashOn;
	Hud.Weapon = S.weapon;
	Hud.Ammo = S.ammo;
	Hud.Flares = S.flares;
	Hud.Coins = S.coins;
	Hud.AlmondWater = S.almond;
	Hud.Tapes = S.tapes;
	Hud.Keys = S.keys;
	if (S.deckNoteT > 0.0f)
	{
		Hud.Note = FText::FromString(UTF8_TO_TCHAR(S.deckNote));
	}
	if (S.sanityWarnT > 0.0f)
	{
		Hud.SanityWarning = FText::FromString(UTF8_TO_TCHAR(S.sanityLine));
	}
	Hud.bDeathCard = S.inMenu && S.deathT > 0.0f;
	Hud.DeathTitle = FText::FromString(UTF8_TO_TCHAR(S.deathTitle));
	Hud.DeathBy = FText::FromString(UTF8_TO_TCHAR(S.deathBy));
	return Hud;
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

// The raylib build draws pickups 7 cells out and balloons 9; 24 m covers both.
void UBackroomsWorldSubsystem::ShowScene()
{
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	if (!Scene)
	{
		Scene = GetWorld()->SpawnActor<ABackroomsSceneActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (!Hand)
	{
		Hand = GetWorld()->SpawnActor<ABackroomsHeldActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (Scene)
	{
		Scene->Show(simScene(*Game, 24.0f), CurrentLook(), StoreyOrigin());
	}
	if (Hand)
	{
		Hand->Show(*Game, StoreyOrigin());
	}
}
