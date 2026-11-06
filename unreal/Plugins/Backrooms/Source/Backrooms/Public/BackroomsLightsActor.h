#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "core/layout.h"
#include "BackroomsLightsActor.generated.h"

struct FrameLight;
struct World;
class UExponentialHeightFogComponent;
class UPointLightComponent;
class URectLightComponent;
class USpotLightComponent;

// The Unreal look's lights and air: a rect light at each of the working
// ceiling panels nearest the player, the flashlight, the burning flare or
// muzzle flash, the chandelier or landing lamp, and the level's fog. Which
// fittings exist, and which are dead, part output or faulty, is core's
// (chunkLayout); what the moment does to them (blackouts, flicker, the
// hunter's pool of dead light, the torch) is the sim's (FrameLight), the
// same values the Web build's shader reads, so a panel's glow
// (BackroomsSurface.ush) and its light agree. The subsystem spawns one in
// the Unreal look and updates it every frame.
UCLASS(Transient)
class BACKROOMS_API ABackroomsLightsActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsLightsActor();

	// Lights the world round Eye (metres, the current storey's frame), which
	// Origin places in Unreal space, for level Level at the moment L.
	void Show(World& W, int32 Level, const Vec3& Eye, const FrameLight& L, const FVector& Origin);
	// Chunk (x, y) of storey z was dropped or rebuilt: read its fittings again.
	void Forget(const FIntVector& Key) { Fittings.Remove(Key); }
	void ForgetAll() { Fittings.Reset(); }

private:
	const TArray<LightFitting>& FittingsOf(World& W, int32 Cx, int32 Cz);
	void ShowFittings(World& W, int32 Level, const Vec3& Eye, const FrameLight& L, const FVector& Origin);
	void ShowFog(int32 Level, const FrameLight& L, const FVector& Origin);

	// The fittings that ever light, of each chunk read so far, keyed as the
	// subsystem keys chunks: (cx, cz, storey).
	TMap<FIntVector, TArray<LightFitting>> Fittings;
	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> Pool;
	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> Torch;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Flare;
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Lamp;
	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
};
