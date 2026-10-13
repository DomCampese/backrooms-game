#include "BackroomsLighting.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace
{
constexpr int32 Channels = 3;
}

void BackroomsLighting::Apply(AActor& Actor, int32 Storey)
{
	const int32 C = ((Storey % Channels) + Channels) % Channels;
	const bool B0 = C == 0, B1 = C == 1, B2 = C == 2;
	TInlineComponentArray<UPrimitiveComponent*> Primitives(&Actor);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		const FLightingChannels& Now = Primitive->LightingChannels;
		if (Now.bChannel0 != B0 || Now.bChannel1 != B1 || Now.bChannel2 != B2)
		{
			Primitive->SetLightingChannels(B0, B1, B2);
		}
	}
	TInlineComponentArray<ULightComponent*> Lights(&Actor);
	for (ULightComponent* Light : Lights)
	{
		const FLightingChannels& Now = Light->LightingChannels;
		if (Now.bChannel0 != B0 || Now.bChannel1 != B1 || Now.bChannel2 != B2)
		{
			Light->SetLightingChannels(B0, B1, B2);
		}
	}
}
