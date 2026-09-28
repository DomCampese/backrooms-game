#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BackroomsWorldSubsystem.generated.h"

struct World;
class ABackroomsChunkActor;
class APointLight;

// Owns core's World for one level and streams its chunks round the camera as
// greybox actors (M2 in docs/unreal-handoff.md). Each storey stands at its own
// height; core is pointed at the storey the camera is on.
UCLASS()
class BACKROOMS_API UBackroomsWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// A level as the raylib build names it. Returns where to put the camera:
	// beside (15, 15) at eye height, facing the game's opening yaw.
	FTransform StartLevel(int32 Level, uint32 Seed, uint32 Visit);

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void StreamAround(const FVector& Camera, float DeltaTime);
	void BuildChunk(const FIntVector& Key);
	void DropChunk(const FIntVector& Key);
	void DropAll();

	// Not a UObject: owned here, made by StartLevel, freed by Deinitialize.
	World* Maze = nullptr;
	// Keyed by (cx, cz, storey).
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>> Chunks;
	// Follows the camera, so a greybox with no fittings lit is still visible.
	UPROPERTY()
	TObjectPtr<APointLight> CameraLight;
	float SinceUnload = 0.0f;
};
