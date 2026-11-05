#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BackroomsReel.generated.h"

struct Camcorder;
class ASceneCapture2D;
class UTexture;
class UTextureRenderTarget2D;

// The camcorder's reel: the Web build's CamcorderReel. One render target of
// frame cells laid out as Camcorder::cellX/cellY say, which the tape material
// plays back on the death card. A frame is the scene as the eye sees it
// before the tape look: in the Web shader look before the post pass
// (raylib's render target), in the Unreal look after the tonemapper.
// Captured at the cell's size and copied into its slot.
UCLASS(Transient)
class BACKROOMS_API UBackroomsReel : public UObject
{
	GENERATED_BODY()

public:
	// A reel of Cam's size (after Camcorder::fitReel), and the capture that
	// fills it, in Where. bLit: the Unreal look, whose frames are captured
	// after Unreal's tonemapper.
	void Fit(UWorld* Where, const Camcorder& Cam, bool bLit);
	bool IsReady() const { return Reel != nullptr && Capture != nullptr; }
	// The scene from View (Unreal space) at a vertical field of view of FovY
	// degrees, into Slot. Draw the frame's scene first: the capture renders
	// the world as it stands when this is called.
	void Grab(const Camcorder& Cam, int32 Slot, const FTransform& View, float FovY);
	UTexture* Texture() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Reel;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Cell;
	UPROPERTY(Transient)
	TObjectPtr<ASceneCapture2D> Capture;
};
