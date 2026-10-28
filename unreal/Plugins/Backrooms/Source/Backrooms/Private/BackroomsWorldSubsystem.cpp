#include "BackroomsWorldSubsystem.h"
#include "BackroomsChunkActor.h"
#include "BackroomsCoords.h"
#include "BackroomsHeldActor.h"
#include "BackroomsReel.h"
#include "BackroomsRender.h"
#include "BackroomsSceneActor.h"
#include "BackroomsSettings.h"
#include "BackroomsSound.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Math/RotationMatrix.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"
#include "core/level_rules.h"
#include "port/camcorder.h"
#include "port/draw.h"
#include "port/stream.h"
#include "port/view.h"
#include "port/visible.h"
#include "sim/records.h"
#include "sim/start.h"
#include "sim/trace.h"
#include <algorithm>

namespace
{
// How often core drops chunk data far from the player, and how far is far.
constexpr float UnloadEvery = 2.0f;
constexpr int32 UnloadRadius = 5;
// The yaw a descent opens with (Sim::beginDescent), for the free camera.
constexpr float OpeningYaw = 0.8f;
// The Web build clamps a frame's time to this before stepping the sim.
constexpr float MaxDt = 0.05f;

// Bullets against level geometry: a line trace against the chunk actors, which
// carry complex collision of the chunk mesh. The ray is in the sim's frame, the
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
	Cam = new Camcorder();
	// Before the first tick, reel or no reel: this sizes the tape's list of
	// frames. ShowFrame fits it again to the viewport, with the reel.
	Cam->fitReel(1440, 850);
	ReelFor = FIntPoint::ZeroValue;
	PendingSlot = -1;
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

FString UBackroomsWorldSubsystem::RecordsPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("backrooms_best")));
}

void UBackroomsWorldSubsystem::SaveRecords()
{
	Game->recordsChanged = false;
	if (!saveRecords(TCHAR_TO_UTF8(*RecordsPath()), Game->best))
	{
		UE_LOG(LogTemp, Warning, TEXT("Backrooms: cannot write the records to %s"), *RecordsPath());
	}
}

void UBackroomsWorldSubsystem::StartRun(uint32 Seed, int32 Level, bool bTitleScreen)
{
	NewSim();
	bRun = true;
	if (!Sound)
	{
		Sound = NewObject<UBackroomsSound>(this);
	}
	Sound->Start(GetWorld());
	SimStart Start;
	Start.seed = Seed;
	Start.level = Level > 0 ? FMath::Min(Level, NLEVELS - 1) : -1;
	Start.menu = bTitleScreen;
	Start.keepRecords = true;
	Start.best = loadRecords(TCHAR_TO_UTF8(*RecordsPath()));
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

void UBackroomsWorldSubsystem::TickRun(const InputFrame& In, bool bPauseToggled, bool bCamToggle, float Dt)
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
		if (!Game->inMenu || Game->deathT <= 0) Cam->stopReplay();
		if (!Game->inMenu) Cam->startTape();
		Cam->stepReplay(Dt);
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
		FinishFrame(true);
		return;
	}
	// The camera takes its buttons out before the trace: what is recorded is
	// what the sim was given.
	InputFrame Frame = In;
	Cam->control(Frame, *Game, Dt, bCamToggle);
	if (Trace) Trace->step(Frame, Dt, T, Game->clockSeed);
	Game->step(Frame, Dt, T);
	if (Trace) Trace->digest(*Game);
	Cam->logSounds(Game->audio);
	if (Game->inMenu && Game->deathT > 0) Cam->beginReplay();
	if (!Game->inMenu)
	{
		const int32 Slot = Cam->record(*Game, Dt, (float)(T - Game->runStart), Reel && Reel->IsReady(),
			[](int Lo, int Hi) { return FMath::RandRange(Lo, Hi); });
		if (Slot >= 0)
		{
			PendingSlot = Slot;
		}
	}
	FinishFrame();
}

// What the Web build's finishStep, its paused tick and updateOccupancy do
// with the sim's outputs.
void UBackroomsWorldSubsystem::FinishFrame(bool bPaused)
{
	if (Sound)
	{
		Sound->Camera(Cam->noises);
		Sound->Play(Game->audio);
		if (bPaused)
		{
			Sound->HoldPaused(Game->ambience, Game->loopCue());
		}
		Sound->Feed();
	}
	Game->audio.clear();
	Cam->noises.clear();
	Game->dropAimLatch = false;
	if (Game->recordsChanged)
	{
		SaveRecords();
	}
}

FVector UBackroomsWorldSubsystem::StoreyOrigin() const
{
	return FVector(0.0, 0.0, BackroomsCoords::StoreyZ(Game->world.storey, Game->world.storeyH));
}

FTransform UBackroomsWorldSubsystem::ViewTransform(float& OutFovY) const
{
	const SimView View = simView(*Game);
	OutFovY = Cam ? Cam->fovY(View.fovY) : View.fovY;
	const FMatrix Axes = FRotationMatrix::MakeFromXZ(BackroomsCoords::ToUnrealDirection(View.forward),
		BackroomsCoords::ToUnrealDirection(View.up));
	return FTransform(Axes.Rotator(), BackroomsCoords::ToUnreal(View.eye) + StoreyOrigin());
}

void UBackroomsWorldSubsystem::FreeSim()
{
	// What Game::shutdown banks: a run that ends with the game keeps its tallies.
	if (Game && bRun)
	{
		Game->bankRecords();
		if (Game->recordsChanged)
		{
			SaveRecords();
		}
	}
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
	if (Sound)
	{
		Sound->Stop();
	}
	delete Recorder;
	Recorder = nullptr;
	delete Trace;   // closes the file
	Trace = nullptr;
	delete Game;
	Game = nullptr;
	delete Cam;
	Cam = nullptr;
	delete Tracer;
	Tracer = nullptr;
	bRun = false;
}

void UBackroomsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Opening the .uproject does not recompile changed source, so an old plugin
	// binary runs without saying so. This line dates the one that is running.
	UE_LOG(LogTemp, Display, TEXT("Backrooms: plugin compiled %s %s"), ANSI_TO_TCHAR(__DATE__), ANSI_TO_TCHAR(__TIME__));
	// Project Settings > Game > Backrooms > Performance. Set by code, so a
	// value typed into the console still wins.
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	auto Put = [](const TCHAR* Name, int32 Value) {
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByCode);
		}
	};
	Put(TEXT("t.MaxFPS"), Settings->MaxFPS);
	Put(TEXT("r.ScreenPercentage"), Settings->ScreenPercentage);
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
	if (!Render)
	{
		Render = NewObject<UBackroomsRender>(this);
	}
	// Without the editor's materials nothing is drawn; Load has said why.
	if (!Render->Load())
	{
		return;
	}
	Render->ApplyLevel(Game->world.level);
	Render->ShowBackdrop(GetWorld(), Camera);
	if (bRun)
	{
		// The sim owns which storey is current (it rebases as you climb).
		StreamAround(BackroomsCoords::ToUnreal({ Game->px, 0.0f, Game->pz }), DeltaTime);
		ShowFrame(simView(*Game).eye);
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
		StreamAround(Camera, DeltaTime);
		ShowFrame(BackroomsCoords::FromUnreal(Camera - StoreyOrigin()));
	}
}

// Focus is in Unreal space; only its x and y (core's x and z) are read.
void UBackroomsWorldSubsystem::StreamAround(const FVector& Focus, float DeltaTime)
{
	World& Maze = Game->world;
	// What core has dropped or rebuilt goes first, before any actor is kept.
	for (const ChunkRef& Ref : Maze.staleChunks)
	{
		DropChunk(FIntVector(Ref.cx, Ref.cz, Ref.storey));
	}
	Maze.staleChunks.clear();

	// Wanted chunks, nearest first, in the Web build's order (port/stream.h):
	// this storey ring by ring, then the other storeys only where an opening
	// shows them. Every wanted chunk is listed, so the rest can be dropped; a
	// frame builds the first few that are missing.
	const Vec3 At = BackroomsCoords::FromUnreal(Focus);
	const int32 Pcx = fdiv(cellOf(At.x), CCELLS), Pcz = fdiv(cellOf(At.z), CCELLS);
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	TArray<FIntVector> Wanted;
	int32 Budget = Settings->BuildsPerFrame;
	streamChunks(Maze, Pcx, Pcz, Settings->ReachOwnStorey, [&](const StreamChunk& C) {
		const FIntVector Key(C.cx, C.cz, C.storey);
		Wanted.Add(Key);
		if (Budget > 0 && !Chunks.Contains(Key))
		{
			BuildChunk(Key);
			Budget--;
			if (C.inShadowGrid)
			{
				Game->shadowsStale = true;
			}
		}
		return true;
	});
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

void UBackroomsWorldSubsystem::HideUnseenChunks()
{
	const Sim& S = *Game;
	FVector2D Size(0.0, 0.0);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	TSet<FIntVector> Seen;
	if (Size.X >= 1.0 && Size.Y >= 1.0)
	{
		const SimView View = simView(S);
		const ChunkViewer Viewer{ { S.px, S.eyeY, S.pz }, S.fwd, View.eye, View.up, View.fovY, (int)Size.X, (int)Size.Y };
		for (const VisibleChunk& C : visibleChunks(Game->world, Viewer, [this](int Storey, int Cx, int Cz) {
				return Chunks.Contains(FIntVector(Cx, Cz, Storey));
			}))
		{
			Seen.Add(FIntVector(C.cx, C.cz, C.storey));
		}
	}
	for (const TPair<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Entry : Chunks)
	{
		// Without a viewport to frame (a headless test), show everything.
		const bool bShow = Seen.Num() == 0 || Seen.Contains(Entry.Key);
		if (Entry.Value && Entry.Value->IsHidden() == bShow)
		{
			Entry.Value->SetActorHiddenInGame(!bShow);
		}
	}
}

void UBackroomsWorldSubsystem::BuildChunk(const FIntVector& Key)
{
	World& Maze = Game->world;
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
		// The shadow grid holds the storeys above and below as they are loaded:
		// a chunk of one arriving means rebuilding it (Game::streamChunks).
		if (Key.Z != Maze.storey)
		{
			Render->InvalidateOccupancy();
		}
		StoreyScope Scope(Maze, Key.Z);
		Actor->Build(Maze, Key.X, Key.Y, *Render);
		Chunks.Add(Key, Actor);
	}
}

FBackroomsHudState UBackroomsWorldSubsystem::GetHud() const
{
	FBackroomsHudState Hud;
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

void UBackroomsWorldSubsystem::ShowFrame(const Vec3& Eye)
{
	Sim& S = *Game;
	World& Maze = S.world;
	Render->UpdateOccupancy(Maze, bRun ? S.px : Eye.x, bRun ? S.pz : Eye.z, S.shadowsStale);
	S.shadowsStale = false;
	const float T = (float)Now();
	if (!bRun)
	{
		// A free camera has no blackouts, torch or nerves.
		FrameLight Still = UBackroomsRender::Still();
		Still.time = T;
		Render->Frame(GetWorld(), Still, Eye, 0.0f, 0.0f, 0.0f);
		Render->ShowPost(GetWorld());
		return;
	}
	const FrameLight Light = frameLight(S, T, Cam->irLamp());
	const float Water = S.level == 2 && Maze.poolAt(cellOf(S.px), cellOf(S.pz))
		? FMath::Clamp((WATER_Y - S.eyeY) * 8.0f, 0.0f, 1.0f) : 0.0f;
	Render->Frame(GetWorld(), Light, Eye, S.fear, Water, S.migraine);
	FitReel();
	// Through the camcorder the tape's look replaces the eye's, and on the
	// death card the tape plays (render.cpp's renderUI).
	const bool bFilming = Cam->viewing() && !S.inMenu;
	const bool bReplaying = S.inMenu && S.deathT > 0 && Cam->replaying();
	CamPicture Picture{};
	if (bFilming)
	{
		Picture = Cam->viewfinderPicture();
	}
	else if (bReplaying)
	{
		Picture = Cam->replayPicture();
	}
	Render->ShowPost(GetWorld(), bFilming || bReplaying ? &Picture : nullptr, Reel ? Reel->Texture() : nullptr);
	HideUnseenChunks();

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
	if (!Scene || !Hand)
	{
		return;
	}
	std::vector<SceneDraw> Draws;
	sceneDraws(S, Light, T, Draws);
	const size_t FirstHeld = Draws.size();
	const SimView View = simView(S);
	// Looking through the camcorder, whatever is in the other hand is out of frame.
	const bool bInHand = !Cam->viewing();
	if (bInHand) heldDraws(S, View, T, Draws);
	Hand->Show(S, bInHand, *Render, StoreyOrigin(), Draws);
	Scene->Show(Draws, FirstHeld, View, *Render, StoreyOrigin());

	// The frame onto tape, as the Web build grabs its render target after
	// drawing the scene and before the post pass.
	if (PendingSlot >= 0 && Reel)
	{
		float FovY = 0.0f;
		const FTransform Camera = ViewTransform(FovY);
		Reel->Grab(*Cam, PendingSlot, Camera, FovY);
	}
	PendingSlot = -1;
}

void UBackroomsWorldSubsystem::FitReel()
{
	FVector2D Size(0.0, 0.0);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	const FIntPoint Want((int32)Size.X, (int32)Size.Y);
	if (Want.X < 1 || Want.Y < 1 || Want == ReelFor)
	{
		return;
	}
	// Frame cells take the window's shape, so a new size starts a fresh tape
	// (Game::tick on a resize).
	ReelFor = Want;
	PendingSlot = -1;
	Cam->fitReel(Want.X, Want.Y);
	if (!Reel)
	{
		Reel = NewObject<UBackroomsReel>(this);
	}
	Reel->Fit(GetWorld(), *Cam);
}
