#include "BackroomsHeldActor.h"
#include "BackroomsCoords.h"
#include "BackroomsSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Math/RotationMatrix.h"
#include "port/held.h"
#include "port/view.h"

namespace
{
// A point in the GLB's frame (metres) as the imported mesh holds it: the glTF
// importer maps (x, y, z) to (z, x, y) and metres to centimetres.
FVector FromGlb(const Vec3& V) { return FVector(V.z, V.x, V.y) * BackroomsCoords::CmPerM; }
}

ABackroomsHeldActor::ABackroomsHeldActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Animator = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Animator"));
	Animator->SetupAttachment(RootComponent);
	Animator->SetHiddenInGame(true);
	Animator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Hidden, it would otherwise stop evaluating its pose.
	Animator->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Gun = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(RootComponent);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// A viewmodel's shadow lands on the wall beside you as a large dark gun.
	Gun->SetCastShadow(false);
	Gun->SetVisibility(false);

	// The raylib build's flash borrows the flare's point light for 0.09 s.
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(RootComponent);
	Flash->SetIntensityUnits(ELightUnits::Candelas);
	Flash->SetLightColor(FLinearColor(FColor(255, 175, 70)));
	Flash->SetAttenuationRadius(800.0f);
	Flash->SetCastShadows(false);
	Flash->SetVisibility(false);
}

bool ABackroomsHeldActor::Load()
{
	if (bLoaded || bMissing)
	{
		return bLoaded;
	}
	const UBackroomsSettings* Settings = GetDefault<UBackroomsSettings>();
	USkeletalMesh* Mesh = Settings->RevolverMesh.LoadSynchronous();
	Clips = { Settings->RevolverIdle.LoadSynchronous(), Settings->RevolverReload.LoadSynchronous(),
		Settings->RevolverShoot.LoadSynchronous() };
	if (!Mesh || !Clips[0] || !Clips[1] || !Clips[2])
	{
		UE_LOG(LogTemp, Warning, TEXT("Backrooms: the revolver's assets are missing; open the project in the editor to import them"));
		bMissing = true;
		return false;
	}
	Animator->SetSkinnedAssetAndUpdate(Mesh, true);
	Animator->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Gun->SetSkinnedAssetAndUpdate(Mesh, true);

	// The drum and the six live rounds turn together; the spent cases do not.
	for (int32 Bone = 0; Bone < Gun->GetNumBones(); ++Bone)
	{
		const FName Name = Gun->GetBoneName(Bone);
		const FString Text = Name.ToString();
		if (Text == TEXT("DEF_Cylinder") || (Text.StartsWith(TEXT("DEF_Bullet")) && !Text.StartsWith(TEXT("DEF_BulletFired"))))
		{
			DrumBones.Add(Name);   // bone index order: a parent before its children
		}
		if (Text == TEXT("DEF_RevolverHandle"))
		{
			Handle = Name;
		}
	}
	if (Handle.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("Backrooms: the revolver mesh has no DEF_RevolverHandle joint"));
		bMissing = true;
		return false;
	}
	bLoaded = true;
	return true;
}

void ABackroomsHeldActor::Show(const Sim& Game, const FVector& Origin)
{
	const bool bRevolver = heldItem(Game) == Held::Revolver;
	if (!bRevolver || !Load())
	{
		Gun->SetVisibility(false);
		Flash->SetVisibility(false);
		return;
	}
	Gun->SetVisibility(true);

	// The model's +x, +y, +z go to the frame's right, up and forward; imported,
	// those are the mesh's +y, +z and +x.
	const HeldFrame Frame = heldWeapon(Game, simView(Game));
	const FMatrix Axes = FRotationMatrix::MakeFromXZ(BackroomsCoords::ToUnrealDirection(Frame.forward),
		BackroomsCoords::ToUnrealDirection(Frame.up));
	const FTransform Placed(Axes.ToQuat(), BackroomsCoords::ToUnreal(Frame.pos) + Origin, FVector(Frame.scale));
	const FTransform Fix(GetDefault<UBackroomsSettings>()->RevolverMeshRotation);
	Animator->SetWorldTransform(Fix * Placed);
	Gun->SetWorldTransform(Fix * Placed);

	// Pose the hidden animator at the clip's time and copy it.
	const RevolverPose Pose = revolverPose(Game.reloadT, Game.gunCd, Game.ammo);
	UAnimSequence* Clip = Clips[(int32)Pose.clip];
	UAnimSingleNodeInstance* Node = Animator->GetSingleNodeInstance();
	if (!Node || Node->GetAnimationAsset() != Clip)
	{
		Animator->PlayAnimation(Clip, false);
		Node = Animator->GetSingleNodeInstance();
	}
	if (Node)
	{
		Node->SetPlaying(false);
		Node->SetPosition(Pose.progress * Clip->GetPlayLength(), false);
	}
	Animator->TickAnimation(0.0f, false);
	Animator->RefreshBoneTransforms();
	Gun->CopyPoseFromSkeletalComponent(Animator);

	// Turn the drum and its rounds about the handle's +z (the mesh's +x).
	// Every target is read before any is set, so a round parented to the drum
	// is not turned twice.
	const FTransform HandleAt = Gun->GetBoneTransformByName(Handle, EBoneSpaces::ComponentSpace);
	const FVector Pivot = HandleAt.TransformPosition(FromGlb(REVOLVER_DRUM_PIVOT));
	const FVector Axis = HandleAt.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
	const FQuat Spin(Axis, Pose.drumTurns * UE_PI / 3.0f);
	TArray<FTransform> Turned;
	for (const FName& Bone : DrumBones)
	{
		FTransform At = Gun->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace);
		At.SetLocation(Pivot + Spin.RotateVector(At.GetLocation() - Pivot));
		At.SetRotation(Spin * At.GetRotation());
		Turned.Add(At);
	}
	for (int32 i = 0; i < DrumBones.Num(); ++i)
	{
		Gun->SetBoneTransformByName(DrumBones[i], Turned[i], EBoneSpaces::ComponentSpace);
	}

	if (Game.muzzleT > 0.0f)
	{
		const float Life = Game.muzzleT / Sim::MUZZLE_FLASH;
		Flash->SetWorldLocation(Gun->GetComponentTransform().TransformPosition(HandleAt.TransformPosition(FromGlb(REVOLVER_MUZZLE))));
		Flash->SetIntensity(80.0f * Life);
		Flash->SetVisibility(true);
	}
	else
	{
		Flash->SetVisibility(false);
	}
}
