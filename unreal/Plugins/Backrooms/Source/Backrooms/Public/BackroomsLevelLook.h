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

// How a level looks: materials for the greybox's surfaces, meshes for its
// props and light fittings. Where the generator puts each thing is core's
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

	// Placed at every light fitting in place of the greybox panel, centred on the
	// luminous plane. Dead fittings take DeadFittingMaterial when it is set.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	TObjectPtr<UStaticMesh> FittingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	TObjectPtr<UMaterialInterface> DeadFittingMaterial;

	// The greybox's flat colours, which a new look starts from.
	static FLinearColor DefaultColour(EBackroomsSurface Surface);
	// The prop kinds this look draws with meshes, as greyboxChunk's mask.
	uint32 MeshedProps() const;
};
