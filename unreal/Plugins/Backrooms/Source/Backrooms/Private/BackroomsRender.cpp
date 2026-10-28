#include "BackroomsRender.h"
#include "BackroomsSettings.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/Paths.h"
#include "core/layout.h"
#include "core/world.h"
#include "port/camcorder.h"
#include "port/chunk_mesh.h"
#include "port/levels.h"

namespace
{
// Where backrooms_world.py builds everything.
const TCHAR* const WorldDir = TEXT("/Game/Backrooms/World");

// Game.h's material slots.
enum EMat : int32 { MatFloor, MatCeiling, MatWalls, MatProps, MatScrawl, MatFixtures, MatAO, MatCount };

// Which material each chunk mesh slot draws with (Game::renderScene): water
// and glass with the floor's.
EMat MatOfSlot(int32 Slot)
{
	switch (Slot)
	{
	case MESH_FLOOR: case MESH_WATER: case MESH_GLASS: return MatFloor;
	case MESH_CEILING: return MatCeiling;
	case MESH_WALLS: return MatWalls;
	case MESH_PROPS: return MatProps;
	case MESH_SCRAWL: return MatScrawl;
	case MESH_FIXTURES: return MatFixtures;
	default: return MatAO;
	}
}

// The sheets the billboards are cut from, by ShapeTex.
const TCHAR* const SheetNames[(int32)ShapeTex::Count] = {
	TEXT("particle"), TEXT("particle"), TEXT("clark"), TEXT("smiler"), TEXT("smiler_glow"), TEXT("partygoer"), TEXT("dog"),
};

// The colour raylib clears the scene to: the Poolrooms' haze, black elsewhere.
FLinearColor ClearColour(int32 Level)
{
	return Level == 2 ? FLinearColor(48 / 255.0f, 70 / 255.0f, 66 / 255.0f, 1.0f) : FLinearColor::Black;
}

FLinearColor V(const Vec3& A, float W = 0.0f) { return FLinearColor(A.x, A.y, A.z, W); }

template <class T>
T* LoadAt(const FString& Name)
{
	const FString Path = FString::Printf(TEXT("%s/%s.%s"), WorldDir, *Name, *FPaths::GetBaseFilename(Name));
	return LoadObject<T>(nullptr, *Path);
}
}

UTexture2D* UBackroomsRender::Texture(const FString& Name) const
{
	const TObjectPtr<UTexture2D>* Found = Textures.Find(Name);
	return Found ? Found->Get() : nullptr;
}

UMaterialInstanceDynamic* UBackroomsRender::MakeWorld(bool bBlend, UTexture2D* Tex0, UTexture2D* Tex1)
{
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(bBlend ? WorldBlend : WorldMasked, this);
	M->SetTextureParameterValue(TEXT("Tex0"), Tex0);
	M->SetTextureParameterValue(TEXT("Tex1"), Tex1);
	M->SetTextureParameterValue(TEXT("Occ"), Occupancy);
	return M;
}

bool UBackroomsRender::Load()
{
	if (bTried)
	{
		return bLoaded;
	}
	bTried = true;
	Collection = LoadAt<UMaterialParameterCollection>(TEXT("MPC_Backrooms"));
	WorldMasked = LoadAt<UMaterialInterface>(TEXT("M_BackroomsWorld"));
	WorldBlend = LoadAt<UMaterialInterface>(TEXT("M_BackroomsWorldBlend"));
	ShapeOpaque = LoadAt<UMaterialInterface>(TEXT("M_BackroomsShape"));
	ShapeBlend = LoadAt<UMaterialInterface>(TEXT("M_BackroomsShapeBlend"));
	PostMaterial = LoadAt<UMaterialInterface>(TEXT("M_BackroomsPost"));
	UMaterialInterface* TapeBase = LoadAt<UMaterialInterface>(TEXT("M_BackroomsTape"));
	UMaterialInterface* BillboardBase = LoadAt<UMaterialInterface>(TEXT("M_BackroomsBillboard"));
	UMaterialInterface* BillboardAddBase = LoadAt<UMaterialInterface>(TEXT("M_BackroomsBillboardAdd"));

	TArray<FString> Names = { TEXT("props"), TEXT("props_detail"), TEXT("fixtures"), TEXT("scrawl"), TEXT("ao"), TEXT("can"),
		TEXT("deck"), TEXT("neutral") };
	for (const TCHAR* Sheet : SheetNames)
	{
		Names.AddUnique(Sheet);
	}
	for (int32 S = 0; S < SURF_COUNT; S++)
	{
		Names.Add(UTF8_TO_TCHAR(SURF_NAMES[S]));
		Names.Add(FString(UTF8_TO_TCHAR(SURF_NAMES[S])) + TEXT("_detail"));
	}
	for (const FString& Name : Names)
	{
		if (UTexture2D* Found = LoadAt<UTexture2D>(TEXT("Textures/T_") + Name))
		{
			Textures.Add(Name, Found);
		}
	}
	FString Missing;
	if (!Collection || !WorldMasked || !WorldBlend || !ShapeOpaque || !ShapeBlend || !PostMaterial || !BillboardBase || !BillboardAddBase)
	{
		Missing = TEXT("its materials");
	}
	else if (Textures.Num() != Names.Num())
	{
		Missing = FString::Printf(TEXT("%d of its %d textures"), Names.Num() - Textures.Num(), Names.Num());
	}
	if (!Missing.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Backrooms: the world is not drawn: %s has not got %s yet. Run make unreal-open and let the editor build them (Output Log: \"built the world's materials\")"),
			WorldDir, *Missing);
		return false;
	}

	Occupancy = UTexture2D::CreateTransient(OCC_CELLS, OCC_CELLS * 2, PF_R8G8B8A8);
	Occupancy->SRGB = false;
	Occupancy->Filter = TF_Nearest;
	Occupancy->CompressionSettings = TC_VectorDisplacementmap;
	Occupancy->AddressX = Occupancy->AddressY = TA_Clamp;
	Occupancy->UpdateResource();
	OccBytes.SetNumZeroed(OCC_CELLS * OCC_CELLS * 4 * 2);

	UTexture2D* Neutral = Texture(TEXT("neutral"));
	// The camcorder's tape draws with it; without it the viewfinder shows the
	// post pass, and the world still draws. A project the editor opened before
	// the tape existed has every other material and not this one, and the game
	// (-game) cannot build it: insisting on it left the whole world black.
	if (TapeBase)
	{
		TapeMaterial = UMaterialInstanceDynamic::Create(TapeBase, this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Backrooms: %s has no M_BackroomsTape yet, so the camcorder shows the plain scene. Run make unreal-open and let the editor build it"),
			WorldDir);
	}
	ChunkMaterials.SetNum(MatCount * 2);
	for (int32 M = 0; M < MatCount; M++)
	{
		for (int32 Blend = 0; Blend < 2; Blend++)
		{
			// The surfaces are the level's (ApplyLevel); these keep their own.
			UTexture2D* Tex0 = M == MatProps ? Texture(TEXT("props")) : M == MatScrawl ? Texture(TEXT("scrawl"))
				: M == MatFixtures ? Texture(TEXT("fixtures")) : M == MatAO ? Texture(TEXT("ao")) : Neutral;
			UTexture2D* Tex1 = M == MatProps ? Texture(TEXT("props_detail")) : Neutral;
			ChunkMaterials[M * 2 + Blend] = MakeWorld(Blend != 0, Tex0, Tex1);
		}
	}
	const TCHAR* const ObjTex[3][2] = { { TEXT("props"), TEXT("props_detail") }, { TEXT("can"), TEXT("neutral") },
		{ TEXT("deck"), TEXT("neutral") } };
	Objects.SetNum(6);
	Lamps.SetNum(2);
	for (int32 Held = 0; Held < 2; Held++)
	{
		for (int32 O = 0; O < 3; O++)
		{
			UMaterialInstanceDynamic* M = MakeWorld(false, Texture(ObjTex[O][0]), Texture(ObjTex[O][1]));
			M->SetScalarParameterValue(TEXT("Viewmodel"), (float)Held);
			Objects[Held * 3 + O] = M;
		}
		UMaterialInstanceDynamic* L = MakeWorld(false, Texture(TEXT("deck")), Neutral);
		L->SetScalarParameterValue(TEXT("Viewmodel"), (float)Held);
		Lamps[Held] = L;
	}
	Flare = MakeWorld(false, Texture(TEXT("props")), Texture(TEXT("props_detail")));
	Flare->SetScalarParameterValue(TEXT("Viewmodel"), 1.0f);
	Flare->SetScalarParameterValue(TEXT("GlossOverride"), FLARE_GLOSS);

	// The revolver's maps, one pair per glTF material: its steel reflects the
	// room (uObjRefl) at the revolver's gloss, in hand.
	for (int32 I = 0;; I++)
	{
		UTexture2D* Albedo = LoadAt<UTexture2D>(FString::Printf(TEXT("Textures/T_revolver_%d"), I));
		UTexture2D* Detail = LoadAt<UTexture2D>(FString::Printf(TEXT("Textures/T_revolver_%d_detail"), I));
		if (!Albedo || !Detail)
		{
			break;
		}
		UMaterialInstanceDynamic* M = MakeWorld(false, Albedo, Detail);
		M->SetScalarParameterValue(TEXT("Viewmodel"), 1.0f);
		M->SetScalarParameterValue(TEXT("ObjRefl"), 1.0f);
		M->SetScalarParameterValue(TEXT("GlossOverride"), REVOLVER_GLOSS);
		RevolverMaterials.Add(M);
	}

	Billboards.SetNum((int32)ShapeTex::Count * 2);
	for (int32 T = 0; T < (int32)ShapeTex::Count; T++)
	{
		for (int32 Add = 0; Add < 2; Add++)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Add ? BillboardAddBase : BillboardBase, this);
			M->SetTextureParameterValue(TEXT("Sheet"), Texture(SheetNames[T]));
			Billboards[T * 2 + Add] = M;
		}
	}
	bLoaded = true;
	UE_LOG(LogTemp, Display, TEXT("Backrooms: the world draws with the Web build's shader (%d textures, %d revolver materials)"),
		Textures.Num(), RevolverMaterials.Num());
	return true;
}

void UBackroomsRender::ApplyLevel(int32 NewLevel)
{
	if (!bLoaded || NewLevel == Level)
	{
		return;
	}
	Level = NewLevel;
	const LevelSurfaces& Set = LEVEL_SURFACES[Level];
	const SurfSlot Slots[3] = { Set.floor, Set.ceiling, Set.walls };
	for (int32 M = MatFloor; M <= MatWalls; M++)
	{
		const FString Name = UTF8_TO_TCHAR(SURF_NAMES[Slots[M]]);
		for (int32 Blend = 0; Blend < 2; Blend++)
		{
			ChunkMaterials[M * 2 + Blend]->SetTextureParameterValue(TEXT("Tex0"), Texture(Name));
			ChunkMaterials[M * 2 + Blend]->SetTextureParameterValue(TEXT("Tex1"), Texture(Name + TEXT("_detail")));
		}
	}
}

UMaterialInterface* UBackroomsRender::Chunk(int32 Slot, bool bBlend) const
{
	return bLoaded ? ChunkMaterials[MatOfSlot(Slot) * 2 + (bBlend ? 1 : 0)].Get() : nullptr;
}

UMaterialInterface* UBackroomsRender::Object(ObjMat Mat, bool bViewmodel) const
{
	return bLoaded ? Objects[(bViewmodel ? 3 : 0) + (int32)Mat].Get() : nullptr;
}

UMaterialInterface* UBackroomsRender::Lamp(bool bViewmodel, float Tint) const
{
	if (!bLoaded)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = Lamps[bViewmodel ? 1 : 0];
	M->SetScalarParameterValue(TEXT("Tint"), Tint);
	return M;
}

// Here, not in the header: the conversion to the base needs the class
// complete, and the header only declares it.
UMaterialInterface* UBackroomsRender::HeldFlare() const { return Flare.Get(); }

UMaterialInterface* UBackroomsRender::Revolver(int32 Index) const
{
	return RevolverMaterials.IsValidIndex(Index) ? RevolverMaterials[Index].Get() : nullptr;
}

UMaterialInterface* UBackroomsRender::Shape(bool bBlend) const { return bBlend ? ShapeBlend : ShapeOpaque; }

UMaterialInterface* UBackroomsRender::Billboard(ShapeTex Tex, bool bAdditive) const
{
	return bLoaded ? Billboards[(int32)Tex * 2 + (bAdditive ? 1 : 0)].Get() : nullptr;
}

void UBackroomsRender::UpdateOccupancy(World& W, float Px, float Pz, bool bStale)
{
	if (!bLoaded)
	{
		return;
	}
	if (bStale)
	{
		bOccValid = false;
	}
	const int32 WantI = cellOf(Px) - OCC_CELLS / 2, WantK = cellOf(Pz) - OCC_CELLS / 2;
	if (bOccValid && FMath::Abs(WantI - OccOriginI) < OCC_RECENTRE && FMath::Abs(WantK - OccOriginK) < OCC_RECENTRE)
	{
		return;
	}
	OccOriginI = WantI;
	OccOriginK = WantK;
	W.buildOccupancy(OccOriginI, OccOriginK, OCC_CELLS, OccBytes.GetData());
	bOccValid = true;
	OccStorey = W.storey;

	// The render thread copies it later: hand it a copy and a region of its own.
	uint8* Copy = new uint8[OccBytes.Num()];
	FMemory::Memcpy(Copy, OccBytes.GetData(), OccBytes.Num());
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, OCC_CELLS, OCC_CELLS * 2);
	Occupancy->UpdateTextureRegions(0, 1, Region, OCC_CELLS * 4, 4, Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions) {
			delete[] Data;
			delete Regions;
		});

	// The CPU half of the lighting reads the same grid.
	StoreyLightCPU Cpu;
	Cpu.storeyH = W.storeyH;
	Cpu.storey = W.storey;
	Cpu.occ = OccBytes.GetData();
	Cpu.occN = OCC_CELLS;
	Cpu.originI = OccOriginI;
	Cpu.originK = OccOriginK;
	setStoreyLightCPU(Cpu);
}

void UBackroomsRender::Frame(UWorld* Where, const FrameLight& L, const Vec3& ViewPos, float Fear, float Water, float Migraine)
{
	if (!bLoaded || Level < 0)
	{
		return;
	}
	UMaterialParameterCollectionInstance* P = Where->GetParameterCollectionInstance(Collection);
	if (!P)
	{
		return;
	}
	const LevelCfg& C = LEVELS[Level];
	P->SetVectorParameterValue(TEXT("Frame"), FLinearColor(L.time, L.blackout, L.flash, L.flareInt));
	P->SetVectorParameterValue(TEXT("ViewPos"), V(ViewPos));
	P->SetVectorParameterValue(TEXT("FlashDir"), V(L.flashDir));
	P->SetVectorParameterValue(TEXT("FlarePos"), V(L.flarePos));
	P->SetVectorParameterValue(TEXT("EntPos"), V(L.entPos));
	P->SetVectorParameterValue(TEXT("Ent"), FLinearColor(L.entDark, L.entBlock, C.storeyH, (float)OccStorey));
	P->SetVectorParameterValue(TEXT("Amb"), V(C.amb));
	P->SetVectorParameterValue(TEXT("FogCol"), V(C.fogCol));
	P->SetVectorParameterValue(TEXT("LightCol"), V(C.lightCol));
	P->SetVectorParameterValue(TEXT("LampCol"), V(L.lampCol));
	P->SetVectorParameterValue(TEXT("Air"), FLinearColor(C.fogDen, L.ir, SURF_MACRO[Level], CEIL_BOARD[Level]));
	P->SetVectorParameterValue(TEXT("Tubes"), FLinearColor(C.ls, C.wallH - LIGHT_DROP, C.dead, C.lightMul));
	P->SetVectorParameterValue(TEXT("Surface"), FLinearColor(C.gloss, C.vary, C.faulty, C.wet));
	P->SetVectorParameterValue(TEXT("Grid"),
		FLinearColor(C.wetFrom, bOccValid ? (float)OCC_CELLS : 0.0f, (float)OccOriginI, (float)OccOriginK));
	P->SetVectorParameterValue(TEXT("RoomMask"), FLinearColor(L.roomMask[0], L.roomMask[1], L.roomMask[2], L.roomMask[3]));
	P->SetVectorParameterValue(TEXT("Lamp"), FLinearColor(L.lamp[0], L.lamp[1], L.lamp[2], L.lamp[3]));
	P->SetVectorParameterValue(TEXT("Post"), FLinearColor(L.time, Fear, Water, Migraine));
	P->SetVectorParameterValue(TEXT("Clear"), ClearColour(Level));
	P->SetVectorParameterValue(TEXT("Quality"),
		FLinearColor((float)(int32)GetDefault<UBackroomsSettings>()->LightQuality, 0.0f, 0.0f, 0.0f));
}

FrameLight UBackroomsRender::Still()
{
	FrameLight L{};
	L.blackout = 1.0f;
	L.flashDir = { 1, 0, 0 };
	L.flick = 1.0f;
	const float NoMask[4] = { 1e6f, 1e6f, -1e6f, -1e6f };
	for (int32 I = 0; I < 4; I++)
	{
		L.roomMask[I] = NoMask[I];
	}
	return L;
}

void UBackroomsRender::PinPost(FPostProcessSettings& S)
{
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.0f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = 0;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.0f;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.0f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.0f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.0f;
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = 0.0f;
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.0f;
	S.bOverride_DynamicGlobalIlluminationMethod = true;
	S.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	S.bOverride_ReflectionMethod = true;
	S.ReflectionMethod = EReflectionMethod::None;
}

void UBackroomsRender::ShowPost(UWorld* Where, const CamPicture* Tape, UTexture* Reel)
{
	if (bLoaded && !Post)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Post = Where->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Post)
		{
			Post->bUnbound = true;
			Post->Priority = 1000.0f;
			// The post material replaces the tonemapper; the world materials
			// tone-map as the Web build's shader does. Exposure is pinned at 1,
			// and nothing of Unreal's own grading, bloom or lighting runs.
			Post->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, PostMaterial));
			PinPost(Post->Settings);
		}
	}
	if (!Post)
	{
		return;
	}
	// Both replace the tonemapper, so one is in the volume at a time.
	UMaterialInterface* Want = PostMaterial;
	if (Tape && TapeMaterial)
	{
		Want = TapeMaterial.Get();
		if (UMaterialParameterCollectionInstance* P = Where->GetParameterCollectionInstance(Collection))
		{
			P->SetVectorParameterValue(TEXT("TapeA"), FLinearColor(Tape->a.u0, Tape->a.v0, Tape->a.u1, Tape->a.v1));
			P->SetVectorParameterValue(TEXT("TapeB"), FLinearColor(Tape->b.u0, Tape->b.v0, Tape->b.u1, Tape->b.v1));
			P->SetVectorParameterValue(TEXT("Tape"), FLinearColor(Tape->mix, Tape->night, Tape->track, Tape->still));
			P->SetVectorParameterValue(TEXT("TapeFrom"), FLinearColor(Tape->snow, Tape->fromReel ? 1.0f : 0.0f, 0.0f, 0.0f));
		}
		if (Reel)
		{
			TapeMaterial->SetTextureParameterValue(TEXT("Reel"), Reel);
		}
	}
	FWeightedBlendable& Blendable = Post->Settings.WeightedBlendables.Array[0];
	if (Blendable.Object != Want)
	{
		Blendable.Object = Want;
	}
}
