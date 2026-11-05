#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "port/draw.h"
#include "BackroomsRender.generated.h"

struct CamPicture;
struct FPostProcessSettings;
struct World;
class APostProcessVolume;
class AStaticMeshActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTexture;
class UTexture2D;

// The materials the Unreal port draws the world with, and the per-frame
// state its shaders read. Two looks (UBackroomsSettings::Look):
//
// - Unreal: the surfaces are lit materials (Shaders/BackroomsSurface.ush)
//   that Unreal's lights light (ABackroomsLightsActor), with Unreal's
//   tonemapper, exposure, bloom and Lumen.
// - WebShader: every surface is unlit, its colour the Web build's world
//   shader (Shaders/BackroomsWorld.ush), and the Web build's post pass
//   (Shaders/BackroomsPost.ush) replaces Unreal's tonemapper.
//
// In both, raylib's shapes and billboards are unlit and lit on the CPU
// (shared/port/draw.h), and on the death card the camcorder's tape look
// (Shaders/BackroomsTape.ush) replaces the tonemapper. The editor builds the
// materials and imports the textures (Content/Python/backrooms_world.py);
// until it has, Load says so and the world is not drawn.
UCLASS(Transient)
class BACKROOMS_API UBackroomsRender : public UObject
{
	GENERATED_BODY()

public:
	// One unit of the Web shader's light in the Unreal look, in nits: the lit
	// materials' glow (their Nits parameter) and the fog's colour. The Unreal
	// look's exposure (UBackroomsSettings) shows this as a bright surface.
	static constexpr float WebUnitNits = 160.0f;

	// Loads the materials and textures and makes the instances. False, said
	// once in the log, while the editor has not built them.
	bool Load();
	bool IsLoaded() const { return bLoaded; }
	// The Unreal look: lit materials and Unreal's lights. False for the Web
	// shader look, or when the lit materials have not been built.
	bool IsLit() const { return bLit; }

	// The level's surfaces on the chunk materials, the level's numbers (LEVELS)
	// into the collection: Game::applyLevelLook.
	void ApplyLevel(int32 Level);

	// The material a chunk mesh slot (ChunkMesh) draws with, masked or blended.
	UMaterialInterface* Chunk(int32 Slot, bool bBlend) const;
	// A loose object's material (port/draw.h's ObjMat); in hand it is a
	// viewmodel, which the room does not shadow. The deck's record lamp has
	// its own, which pulses through its tint.
	UMaterialInterface* Object(ObjMat Mat, bool bViewmodel) const;
	UMaterialInterface* Lamp(bool bViewmodel, float Tint) const;
	// The flare in hand: the props atlas at the flare's gloss.
	UMaterialInterface* HeldFlare() const;
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
	// Unreal's tonemapper, spawned into a game world once. With a Tape the
	// tape look takes the post pass's place, as render.cpp's renderUI draws
	// one or the other: frames off Reel on the death card.
	void ShowPost(UWorld* Where, const CamPicture* Tape = nullptr, UTexture* Reel = nullptr);
	// What every view of the world pins in the Web shader look: exposure at
	// 1, and none of Unreal's own grading, bloom or lighting. The post volume
	// and the reel's capture.
	static void PinPost(FPostProcessSettings& S);
	// The Unreal look's camera: Lumen, bounded auto exposure, bloom, ambient
	// occlusion and a little grain. The post volume and the reel's capture.
	static void LitPost(FPostProcessSettings& S);
	// A box of the level's clear colour round Eye (Unreal space), behind
	// everything: raylib clears its render target to that colour, which the
	// camcorder's reel records where nothing is drawn (the Poolrooms' haze;
	// black elsewhere). Without M_BackroomsClear there is none, and those
	// pixels are black.
	void ShowBackdrop(UWorld* Where, const FVector& Eye);
	// The light of a still moment with no one in it: every tube at its
	// output, no torch, no flare, no hunter. For the editor's preview.
	static FrameLight Still();

private:
	UMaterialInstanceDynamic* MakeWorld(bool bBlend, UTexture2D* Tex0, UTexture2D* Tex1);
	UTexture2D* Texture(const FString& Name) const;

	// The Unreal look's per-frame camera: the fear and migraine colour split,
	// the vignette, and the water's tint.
	void FrameLitPost();

	bool bLoaded = false;
	bool bLit = false;
	float PostFear = 0.0f, PostWater = 0.0f, PostMigraine = 0.0f, PostTime = 0.0f;
	bool bTried = false;
	int32 Level = -1;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WorldMasked;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WorldBlend;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LitMasked;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LitBlend;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeOpaque;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShapeBlend;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PostMaterial;
	// M_BackroomsTape, with the reel it plays back from.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TapeMaterial;
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
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ClearMaterial;
	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> Backdrop;
};
