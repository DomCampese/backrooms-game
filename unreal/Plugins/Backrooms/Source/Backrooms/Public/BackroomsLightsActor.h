#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BackroomsLightsActor.generated.h"

class ABackroomsChunkActor;
class UBackroomsLevelLook;
class UExponentialHeightFogComponent;
class UPointLightComponent;
class UPostProcessComponent;

// A level's light, air and camera (M5): the fittings' lights, which the chunks
// own (ABackroomsChunkActor), set for the moment and the nearest given
// shadows; the Web build's fog; and a post-process volume with the look's
// fixed exposure and the post pass's colour split. The subsystem spawns one
// and updates it every frame.
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

	// Lights the streamed chunks (keyed (cx, cz, storey)) for the moment. The
	// fittings nearest At (Unreal space) on Storey cast shadows. Origin is where
	// the player's storey stands.
	void Show(const TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Chunks, int32 Storey, const FVector& At,
		const UBackroomsLevelLook* Look, const FMoment& Moment, const FVector& Origin);

private:
	void PickShadows(const TMap<FIntVector, TObjectPtr<ABackroomsChunkActor>>& Chunks, int32 Storey, const FVector& At);

	// The lights casting shadows, nearest first.
	TArray<TWeakObjectPtr<UPointLightComponent>> Shadowed;
	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> Post;
};
