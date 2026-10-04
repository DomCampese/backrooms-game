#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BackroomsTypes.h"
#include "BackroomsLevelLook.generated.h"

class UMaterialInterface;
class UStaticMesh;

// How one surface of the greybox is drawn: a material, or else a flat colour.
USTRUCT(BlueprintType)
struct FBackroomsSurfaceLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UMaterialInterface> Material;

	// Used with the engine's plain material when Material is empty.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	FLinearColor Colour = FLinearColor(0.5f, 0.5f, 0.5f);
};

// A mesh standing where the generator put a prop. The prop's origin is the
// middle of its footprint on the floor, facing the prop's turn; Offset is in
// that frame, in Unreal units.
USTRUCT(BlueprintType)
struct FBackroomsPropLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	FTransform Offset;
};

// A mesh for a wall or ceiling fixture. Its frame's origin is the layout's
// point (src/core/layout.h, Fixture: on the wall face, the ceiling, or a run's
// first end), X out of the face, Z up (on a ceiling, Z along the world's X). A
// run (conduit, pipe, valve, streamer) has X along it instead, scaled to its
// length in metres: a mesh 100 units long along X spans the run. Offset is in
// that frame, in Unreal units. Empty keeps a plain box at the fixture's size,
// or nothing for a scrawl, a spall or the Manila Room.
USTRUCT(BlueprintType)
struct FBackroomsFixtureLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	FTransform Offset;
};

// A mesh for something the sim puts in the world (a pickup, the hunter, a
// flare, ...). Its origin is where the sim puts the thing: the base of it on
// the floor, or the centre for balloons, confetti and flares, facing the
// thing's heading. Offset is in that frame, in Unreal units. Empty keeps a
// plain shape at the thing's real size.
USTRUCT(BlueprintType)
struct FBackroomsItemLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Backrooms")
	FTransform Offset;
};

// How a level looks: materials for the greybox's surfaces, meshes for its
// props, fixtures and light fittings. Where the generator puts each thing is core's
// (chunkLayout); only its appearance is set here. Anything left empty is drawn
// as the greybox draws it. Assign one per level in Project Settings > Game >
// Backrooms.
UCLASS(BlueprintType)
class BACKROOMS_API UBackroomsLevelLook : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UBackroomsLevelLook();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfaces")
	TMap<EBackroomsSurface, FBackroomsSurfaceLook> Surfaces;

	// A prop with a mesh here is drawn with it instead of its collision box.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Props")
	TMap<EBackroomsProp, FBackroomsPropLook> Props;

	// Outlets, switches, signs, diffusers, conduit, pipes and the rest of what
	// chunkLayout fixes to walls and ceilings.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixtures")
	TMap<EBackroomsFixture, FBackroomsFixtureLook> Fixtures;

	// Placed at every light fitting in place of the greybox panel, centred on the
	// luminous plane. Dead fittings take DeadFittingMaterial when it is set.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	TObjectPtr<UStaticMesh> FittingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	TObjectPtr<UMaterialInterface> DeadFittingMaterial;

	// The tubes' colour, and their output as a multiple of FittingLumens in the
	// project settings: the raylib build's LevelCfg::lightCol and lightMul.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	FLinearColor LightColour = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights", meta = (ClampMin = 0))
	float LightOutput = 1.0f;

	// The raylib build's fog (LevelCfg::fogCol, fogDen): what the air fades to,
	// and its density per metre, as in exp(-density * metres).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air")
	bool bFog = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air")
	FLinearColor FogColour = FLinearColor::Black;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = 0))
	float FogDensity = 0.05f;

	// The camera's exposure, fixed: EV100, lower is brighter. Unreal's eye
	// adaptation would lift the dark levels to mid grey, and the raylib build's
	// levels are tuned to be as dark as they are.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	float ExposureEV100 = 6.0f;

	// Pickups, crates, the hunter, the pack and the rest the sim places each
	// frame (src/port/scene.h). The hunter is Pirate Clark on Level 0, a Smiler
	// on Levels 1 and 3 and the Partygoer on 4, so each level's look gives its own.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Items")
	TMap<EBackroomsItem, FBackroomsItemLook> Items;

	// The balloons' and confetti's colours, by the index the sim gives them.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Items")
	TArray<FLinearColor> PartyColours;

	// The greybox's flat colours, which a new look starts from.
	static FLinearColor DefaultColour(EBackroomsSurface Surface);
	// LEVEL FUN's palette in the raylib build (PARTY, src/util.cpp).
	static TArray<FLinearColor> DefaultPartyColours();
	// The prop kinds this look draws with meshes, as greyboxChunk's mask.
	uint32 MeshedProps() const;
};
