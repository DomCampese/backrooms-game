#pragma once
#include "CoreMinimal.h"

class AActor;

namespace BackroomsLighting
{
// Storeys take Unreal's three lighting channels in turn, so a fitting lights
// its own storey and not the walls of the one above or below it through the
// slab. Storeys sharing a channel are three pitches apart, beyond any
// fitting's reach. A level without storeys is storey 0, channel 0: Unreal's
// default, which every other light and primitive is on.
//
// Puts every primitive and light Actor owns on Storey's channel.
void Apply(AActor& Actor, int32 Storey);
}
