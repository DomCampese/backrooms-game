#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BackroomsTypes.h"
#include "BackroomsWorldSubsystem.generated.h"

struct Sim;
struct InputFrame;
struct TraceWriter;
struct SolidTracer;
class ABackroomsChunkActor;
class UBackroomsLevelLook;
class APointLight;

// Owns the game: one Sim (the rules, and core's World inside it) for this
// Unreal world. Streams the world's chunks round the player as greybox actors
// (M2 in docs/unreal-handoff.md), each storey at its own height, and in a run
// steps the sim once a frame from the controller's input (M4).
//
// Two modes. A run is the game: the sim moves the player and the camera
// follows it. A free camera only streams a level round wherever the camera
// flies, for looking at the greybox.
UCLASS()
class BACKROOMS_API UBackroomsWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// A run, as the raylib build starts one with BACKROOMS_SEED and
	// BACKROOMS_LEVEL: Level 0 first, then Level (if not 0), no title screen.
	// With -BackroomsRecord=path on the command line, every call on the sim is
	// recorded there for tools/replay (src/sim/trace.h).
	void StartRun(uint32 Seed, int32 Level);
	UFUNCTION(BlueprintCallable, Category = "Backrooms", meta = (DisplayName = "Start Run"))
	void StartRunBP(int32 Seed, int32 Level) { StartRun((uint32)Seed, Level); }

	// What a HUD shows, as of the last frame.
	UFUNCTION(BlueprintPure, Category = "Backrooms")
	FBackroomsHud GetHud() const;
	// A level as the raylib build names it, for the free camera. Returns where
	// to put the camera: beside (15, 15) at eye height.
	FTransform StartFreeCamera(int32 Level, uint32 Seed, uint32 Visit);

	bool IsRunning() const { return bRun; }
	// One frame of a run: the title screen, a pause toggle, or a step, as
	// Game::tick does them. Dt is the frame time; the clock is read here.
	void TickRun(const InputFrame& In, bool bPauseToggled, float Dt);
	// The camera for the sim's current view, in Unreal space, and its vertical
	// field of view in degrees.
	FTransform ViewTransform(float& OutFovY) const;
	const Sim* GetSim() const { return Game; }

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void NewSim();
	void FreeSim();
	void FinishFrame();
	// Seconds since the subsystem started: the sim's clock, as GetTime() is the
	// raylib build's.
	double Now() const;
	FVector StoreyOrigin() const;
	void StreamAround(const FVector& Focus, int32 Storey, float DeltaTime);
	void BuildChunk(const FIntVector& Key);
	void DropChunk(const FIntVector& Key);
	void DropAll();
	void DrawDebugState() const;
	// The level's look from the project settings, loaded once per level.
	const UBackroomsLevelLook* CurrentLook();

	// Not UObjects: owned here, freed by Deinitialize.
	Sim* Game = nullptr;
	SolidTracer* Tracer = nullptr;
	SolidTracer* Recorder = nullptr;   // wraps Tracer while a trace is written
	TraceWriter* Trace = nullptr;
	bool bRun = false;
	double ClockZero = -1.0;

	// Keyed by (cx, cz, storey).
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>> Chunks;
	// Follows the camera, so a greybox with no fittings lit is still visible.
	UPROPERTY()
	TObjectPtr<APointLight> CameraLight;
	UPROPERTY()
	TObjectPtr<UBackroomsLevelLook> Look;
	int32 LookLevel = -1;
	float SinceUnload = 0.0f;
};
