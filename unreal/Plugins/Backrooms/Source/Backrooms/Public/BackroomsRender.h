#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "port/draw.h"
#include "BackroomsRender.generated.h"

struct World;
class APostProcessVolume;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTexture2D;

// What the Unreal port draws the Web build's world with, and the per-frame
// state the Web build hands its shaders. Every surface is unlit: its colour is
// the Web build's world shader (Shaders/BackroomsWorld.ush), or raylib's
// unlit shapes and billboards lit on the CPU (shared/port/draw.h), and the
// post pass (Shaders/BackroomsPost.ush) replaces Unreal's tonemapper. The
// editor builds the materials and imports the textures
// (Content/Python/backrooms_world.py); until it has, Load says so and the
// world is not drawn.
UCLASS(Transient)
class BACKROOMS_API UBackroomsRender : public UObject
{
	GENERATED_BODY()

public:
	// Loads the materials and textures and makes the instances. False, said
	// once in the log, while the editor has not built them.
	bool Load();
	bool IsLoaded() const { return bLoaded; }

	// The level's surfaces on the chunk materials, the level's numbers (LEVELS)
	// into the collection: Game::applyLevelLook.
	void ApplyLevel(int32 Level);

	// The material a chunk mesh slot (ChunkMesh) draws with, masked or blended.
	UMaterialInterface* Chunk(int32 Slot, bool bBlend) const;
	// A loose object's material (port/draw.h's ObjMat); in hand it is a
	// viewmodel, which the room does not shadow. The deck's record lamp has
	// its own, which breathes by its tint.
	UMaterialInterface* Object(ObjMat Mat, bool bViewmodel) const;
	UMaterialInterface* Lamp(bool bViewmodel, float Tint) const;
	// The flare in hand: the props atlas at the flare's gloss.
	UMaterialInterface* HeldFlare() const { return Flare.Get(); }
	// The revolver's glTF material Index, or null.
	UMaterialInterface* Revolver(int32 Index) const;
	// raylib's unlit shapes, opaque or blended, and billboards cut from Tex.
	UMaterialInterface* Shape(bool bBlend) const;
	UMaterialInterface* Billboard(ShapeTex Tex, bool bAdditive) const;

	// Rebuilds the shadow grid round (Px, Pz) when the player has moved far
	// enough from where it was built or bStale says the walls changed:
	// Game::updateOccupancy. Also gives lightAtCPU the same grid.
	void UpdateOccupancy(World& W, float Px, float Pz, bool bStale);
	// The grid must be rebuilt on the next update: a chunk of another storey
	// arrived, and the grid holds what is loaded.
	void InvalidateOccupancy() { bOccValid = false; }

	// The frame's uniforms into MPC_Backrooms. ViewPos is the eye in the
	// current storey's frame; Fear, Water and Migraine are the post pass's.
	void Frame(UWorld* Where, const FrameLight& Light, const Vec3& ViewPos, float Fear, float Water, float Migraine);
	// The post volume that puts the Web build's post pass in place of
	// Unreal's tonemapper, spawned into a game world once.
	void ShowPost(UWorld* Where);
	// The light of a still moment with no one in it: every tube at its
	// output, no torch, no flare, no hunter. For the editor's preview.
	static FrameLight Still();

private:
	UMaterialInstanceDynamic* MakeWorld(bool bBlend, UTexture2D* Tex0, UTexture2D* Tex1);
	UTexture2D* Texture(const FString& Name) const;

	bool bLoaded = false;
	bool bTried = false;
	int32 Level = -1;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WorldMasked;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WorldBlend;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeOpaque;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeBlend;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PostMaterial;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Textures;

	// Game.h's MAT_FLOOR .. MAT_AO, masked then blended: [Material * 2 + bBlend].
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ChunkMaterials;
	// ObjMat, then the same in hand: [bViewmodel * 3 + ObjMat].
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Objects;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Lamps;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Flare;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RevolverMaterials;
	// ShapeTex, translucent then additive: [Tex * 2 + bAdditive].
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Billboards;

	// The shadow grid: OCC_CELLS square, then as many rows of fitting masks.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Occupancy;
	TArray<uint8> OccBytes;
	int32 OccOriginI = 0, OccOriginK = 0, OccStorey = 0;
	bool bOccValid = false;

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> Post;
};
