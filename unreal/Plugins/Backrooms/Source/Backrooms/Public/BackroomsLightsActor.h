#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "core/layout.h"
#include "BackroomsLightsActor.generated.h"

class UBackroomsLevelLook;
class UExponentialHeightFogComponent;
class URectLightComponent;

// A level's light and air (M5): rect lights at the live fittings nearest the
// player, and the raylib build's fog. Where the fittings are, and which are
// dead, part output or faulty, is core's (chunkLayout); the look gives the
// tubes' colour and output and the fog. The subsystem spawns one for a run and
// updates it after every step.
UCLASS(Transient)
class BACKROOMS_API ABackroomsLightsActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsLightsActor();

	// Lights the fittings round At (metres, in the frame of W's current storey),
	// which Origin places in the world. Blackout is the sim's blackoutCur
	// (1 lit, near 0 out) and Now its clock, which faulty tubes stutter on.
	void Show(World& W, const Vec3& At, const UBackroomsLevelLook* Look, float Blackout, float Now,
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
};
