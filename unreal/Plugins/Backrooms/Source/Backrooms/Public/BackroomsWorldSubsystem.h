#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BackroomsTypes.h"
#include "BackroomsWorldSubsystem.generated.h"

struct Sim;
struct Camcorder;
struct Vec3;
struct InputFrame;
struct TraceWriter;
struct SolidTracer;
class ABackroomsChunkActor;
class ABackroomsSceneActor;
class ABackroomsHeldActor;
class UBackroomsReel;
class UBackroomsRender;
class UBackroomsSound;

// Owns the game: one Sim (the rules, and core's World inside it) for this
// Unreal world. Streams the world's chunks round the player, each storey at
// its own height, and draws them and what the sim puts among them as the Web
// build draws them (UBackroomsRender); in a run steps the sim once a frame
// from the controller's input.
//
// Two modes. A run is the game: the sim moves the player and the camera
// follows it. A free camera only streams a level round wherever the camera
// flies, for looking round a level.
UCLASS()
class BACKROOMS_API UBackroomsWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// A run, as the Web build starts one: Level 0 first, then Level (if not
	// 0), and with bTitleScreen the title screen over it, which a key or a
	// click begins as a fresh descent. The player's records are read from and
	// written to RecordsPath(). With -BackroomsRecord=path on the command
	// line, every call on the sim is recorded there for tools/replay
	// (shared/sim/trace.h).
	void StartRun(uint32 Seed, int32 Level, bool bTitleScreen = false);
	UFUNCTION(BlueprintCallable, Category = "Backrooms", meta = (DisplayName = "Start Run"))
	void StartRunBP(int32 Seed, int32 Level, bool bTitleScreen) { StartRun((uint32)Seed, Level, bTitleScreen); }
	// The records file: the Web build's format (shared/sim/records.h), in the
	// project's Saved folder.
	static FString RecordsPath();

	// What a HUD shows, as of the last frame.
	UFUNCTION(BlueprintPure, Category = "Backrooms")
	FBackroomsHudState GetHud() const;
	// A level as the Web build names it, for the free camera. Returns where
	// to put the camera: beside (15, 15) at eye height.
	FTransform StartFreeCamera(int32 Level, uint32 Seed, uint32 Visit);

	bool IsRunning() const { return bRun; }
	// One frame of a run: the title screen, a pause toggle, or a step, as
	// Game::tick does them. bCamToggle raises or lowers the camcorder. Dt is
	// the frame time; the clock is read here.
	void TickRun(const InputFrame& In, bool bPauseToggled, bool bCamToggle, float Dt);
	// The camera for the sim's current view, in Unreal space, and its vertical
	// field of view in degrees.
	FTransform ViewTransform(float& OutFovY) const;
	const Sim* GetSim() const { return Game; }
	const Camcorder* GetCamcorder() const { return Cam; }
	// The clock the sim is stepped with, for what is drawn from it (the HUD).
	double SimClock() const { return Now(); }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void NewSim();
	void FreeSim();
	// Plays the frame's sound and clears the flags the platform owns. A paused
	// frame also holds the ambience and the loops.
	void FinishFrame(bool bPaused = false);
	// Writes the sim's records when they improved (Game::saveRecords).
	void SaveRecords();
	// Seconds since the subsystem started: the sim's clock, as GetTime() is the
	// Web build's.
	double Now() const;
	FVector StoreyOrigin() const;
	// Builds and drops chunk actors round Focus on the current storey, in
	// port/stream.h's order.
	void StreamAround(const FVector& Focus, float DeltaTime);
	void BuildChunk(const FIntVector& Key);
	void DropChunk(const FIntVector& Key);
	void DropAll();
	// The frame's light and what the sim puts in the world: the world
	// shader's uniforms for an eye at Eye (metres, the current storey's
	// frame), then in a run the scene and the revolver.
	void ShowFrame(const Vec3& Eye);
	// Hides the chunk actors the sim's view cannot see: those outside the
	// frustum, and other storeys' outside the openings in view
	// (port/visible.h), as the Web build leaves them undrawn.
	void HideUnseenChunks();
	// Fits the camcorder and its reel to the viewport when its size changes.
	void FitReel();

	// Not UObjects: owned here, freed by Deinitialize.
	Sim* Game = nullptr;
	// The camcorder's rules and OSD (shared/port/camcorder.h); its frames are
	// on Reel.
	Camcorder* Cam = nullptr;
	SolidTracer* Tracer = nullptr;
	SolidTracer* Recorder = nullptr;   // wraps Tracer while a trace is written
	TraceWriter* Trace = nullptr;
	bool bRun = false;
	double ClockZero = -1.0;

	// Keyed by (cx, cz, storey).
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>> Chunks;
	UPROPERTY()
	TObjectPtr<UBackroomsRender> Render;
	UPROPERTY()
	TObjectPtr<ABackroomsSceneActor> Scene;
	UPROPERTY()
	TObjectPtr<ABackroomsHeldActor> Hand;
	// A run's sound; none in a free camera.
	UPROPERTY()
	TObjectPtr<UBackroomsSound> Sound;
	// The camcorder's reel, fitted to the viewport (ReelFor) as the Web build
	// fits it to the window; and the slot Camcorder::record asked for this
	// frame, grabbed once ShowFrame has placed the frame's scene.
	UPROPERTY()
	TObjectPtr<UBackroomsReel> Reel;
	FIntPoint ReelFor = FIntPoint::ZeroValue;
	int32 PendingSlot = -1;
	float SinceUnload = 0.0f;
};
