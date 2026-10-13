#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "core/layout.h"
#include "BackroomsChunkActor.generated.h"

class UBackroomsLevelLook;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UProceduralMeshComponent;

// One chunk of one storey, spawned by UBackroomsWorldSubsystem at its storey's
// height. The mesh is in that storey's own frame: the greybox with the level's
// look (BackroomsLevelLook.h), plus its prop and fitting meshes, and a light at
// each live fitting, which lives as long as the chunk does. A Blueprint
// subclass can add to it; set it in Project Settings > Game > Backrooms.
UCLASS(Blueprintable, Transient)
class BACKROOMS_API ABackroomsChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsChunkActor();

	void Build(World& W, int32 Cx, int32 Cz, const UBackroomsLevelLook* Look);
	// Sets each fitting's light and panel for the moment: Blackout scales them
	// all (Sim::blackoutCur), and faulty tubes stutter on the clock Now.
	void ShowLights(float Blackout, float Now);
	const TArray<TObjectPtr<UPointLightComponent>>& GetLights() const { return Lights; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Backrooms")
	TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Instances;
	// Lights[i] is at Fittings[i], whose panel is instance i of Panels.
	TArray<LightFitting> Fittings;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Panels;
	// A full fitting's intensity, candelas, and what each fitting shows now.
	float Candelas = 0.0f;
	TArray<float> Shown;
};
