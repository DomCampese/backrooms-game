#include "BackroomsReel.h"
#include "BackroomsRender.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "port/camcorder.h"

namespace
{
// Bytes as raylib holds them: 8 bits a channel, no sRGB curve either way
// (the textures are imported the same way), bilinear as CamcorderReel::fit
// sets the reel.
UTextureRenderTarget2D* Target(UObject* Outer, int32 W, int32 H)
{
	UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Outer);
	T->ClearColor = FLinearColor::Black;
	T->Filter = TF_Bilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->InitCustomFormat(W, H, PF_B8G8R8A8, true);
	T->UpdateResourceImmediate(true);
	return T;
}
}

void UBackroomsReel::Fit(UWorld* Where, const Camcorder& Cam)
{
	Reel = Target(this, Cam.reelW(), Cam.reelH());
	Cell = Target(this, Cam.cellWidth(), Cam.cellHeight());
	if (!Capture)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Capture = Where->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (!Capture)
	{
		return;
	}
	USceneCaptureComponent2D* C = Capture->GetCaptureComponent2D();
	C->bCaptureEveryFrame = false;
	C->bCaptureOnMovement = false;
	// The scene before the post pass, which the tape look is drawn from: the
	// world materials tone-map as the Web build's shader does, so this is
	// what raylib's render target holds, clamped to 8 bits by the target.
	C->CaptureSource = SCS_SceneColorHDRNoAlpha;
	C->TextureTarget = Cell;
	UBackroomsRender::PinPost(C->PostProcessSettings);
	C->PostProcessBlendWeight = 1.0f;
	C->ShowFlags.SetTemporalAA(false);
	C->ShowFlags.SetAntiAliasing(false);
	C->ShowFlags.SetMotionBlur(false);
}

void UBackroomsReel::Grab(const Camcorder& Cam, int32 Slot, const FTransform& View, float FovY)
{
	if (!IsReady())
	{
		return;
	}
	USceneCaptureComponent2D* C = Capture->GetCaptureComponent2D();
	Capture->SetActorTransform(View);
	// The capture's angle is horizontal; the cell has the window's shape.
	const float HalfY = FMath::DegreesToRadians(FovY) * 0.5f;
	const float Aspect = (float)Cam.cellWidth() / (float)Cam.cellHeight();
	C->FOVAngle = FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfY) * Aspect));
	C->CaptureScene();

	// Then into its slot. Both targets are one format, so a copy moves the
	// bytes as they are; the reel's rows run down from its top, as cellY does.
	FTextureRenderTargetResource* From = Cell->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* To = Reel->GameThread_GetRenderTargetResource();
	const FIntVector At(Cam.cellX(Slot), Cam.cellY(Slot), 0);
	const FIntVector Size(Cam.cellWidth(), Cam.cellHeight(), 1);
	ENQUEUE_RENDER_COMMAND(BackroomsReelGrab)([From, To, At, Size](FRHICommandListImmediate& RHICmdList) {
		FRHITexture* Src = From ? From->GetRenderTargetTexture() : nullptr;
		FRHITexture* Dst = To ? To->GetRenderTargetTexture() : nullptr;
		if (!Src || !Dst)
		{
			return;
		}
		FRHICopyTextureInfo Info;
		Info.Size = Size;
		Info.DestPosition = At;
		RHICmdList.Transition({ FRHITransitionInfo(Src, ERHIAccess::Unknown, ERHIAccess::CopySrc),
			FRHITransitionInfo(Dst, ERHIAccess::Unknown, ERHIAccess::CopyDest) });
		RHICmdList.CopyTexture(Src, Dst, Info);
		RHICmdList.Transition({ FRHITransitionInfo(Src, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
			FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
	});
}

UTexture* UBackroomsReel::Texture() const { return Reel; }
