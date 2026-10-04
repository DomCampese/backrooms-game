#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "core/layout.h"
#include "BackroomsLightsActor.generated.h"

class UBackroomsLevelLook;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class URectLightComponent;

// A level's light, air and camera (M5): rect lights at the live fittings
// nearest the player, the Web build's fog, and a post-process volume with
// the look's fixed exposure and the post pass's colour split. Where the
// fittings are, and which are dead, part output or faulty, is core's
// (chunkLayout); the look gives the tubes' colour and output, the fog and the
// exposure. The subsystem spawns one and updates it every frame.
UCLASS(Transient)
class BACKROOMS_API ABackroomsLightsActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsLightsActor();

	// What the sim says about the moment, for the lights and the post.
	struct FMoment
	{
		float Blackout = 1.0f;   // Sim::blackoutCur: 1 lit, near 0 out
		float Now = 0.0f;        // the sim's clock, which faulty tubes stutter on
		float Fear = 0.0f;       // Sim::fear and Sim::migraine, for the colour split
		float Migraine = 0.0f;
	};

	// Lights the fittings round At (metres, in the frame of W's current storey),
	// which Origin places in the world.
	void Show(World& W, const Vec3& At, const UBackroomsLevelLook* Look, const FMoment& Moment,
		const FVector& Origin);
	// Chunk (x, y) of storey z was dropped or rebuilt: read its fittings again.
	void Forget(const FIntVector& Key) { Fittings.Remove(Key); }
	void ForgetAll() { Fittings.Reset(); }

private:
	const TArray<LightFitting>& FittingsOf(World& W, int32 Cx, int32 Cz);

	// The live fittings of each chunk read so far, keyed as the subsystem keys
	// chunks: (cx, cz, storey).
	TMap<FIntVector, TArray<LightFitting>> Fittings;
	UPROPERTY(Transient)
	TArray<TObjectPtr<URectLightComponent>> Pool;
	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> Post;
};
