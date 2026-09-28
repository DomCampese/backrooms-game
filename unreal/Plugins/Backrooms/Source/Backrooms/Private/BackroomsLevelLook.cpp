#include "BackroomsLevelLook.h"
#include "Engine/StaticMesh.h"

FLinearColor UBackroomsLevelLook::DefaultColour(EBackroomsSurface Surface)
{
	// The same colours as tools/greybox-view.cpp, so a frame here can be held
	// against one from the raylib viewer.
	static const FColor Colours[(int32)EBackroomsSurface::Count] = {
		FColor(150, 140, 110),   // Floor
		FColor(200, 200, 190),   // Ceiling
		FColor(170, 165, 140),   // Wall
		FColor(120, 100, 80),    // Step
		FColor(140, 110, 90),    // Stair
		FColor(130, 130, 130),   // Pillar
		FColor(90, 110, 160),    // Prop
		FColor(110, 70, 40),     // Door
		FColor(60, 200, 90),     // Exit
		FColor(200, 40, 40),     // CursedExit
		FColor(150, 200, 230),   // Glass
		FColor(100, 100, 110),   // Rail
		FColor(60, 120, 200),    // Water
		FColor(255, 255, 240),   // Light
		FColor(60, 60, 60),      // DeadLight
	};
	const int32 I = FMath::Clamp((int32)Surface, 0, (int32)EBackroomsSurface::Count - 1);
	return FLinearColor(Colours[I]);
}

TArray<FLinearColor> UBackroomsLevelLook::DefaultPartyColours()
{
	return { FLinearColor(FColor(206, 64, 58)), FLinearColor(FColor(222, 172, 62)), FLinearColor(FColor(84, 142, 198)),
		FLinearColor(FColor(106, 178, 92)), FLinearColor(FColor(182, 96, 178)) };
}

UBackroomsLevelLook::UBackroomsLevelLook()
{
	PartyColours = DefaultPartyColours();
	for (int32 S = 0; S < (int32)EBackroomsSurface::Count; S++)
	{
		FBackroomsSurfaceLook Look;
		Look.Colour = DefaultColour((EBackroomsSurface)S);
		Surfaces.Add((EBackroomsSurface)S, Look);
	}
}

uint32 UBackroomsLevelLook::MeshedProps() const
{
	uint32 Mask = 0;
	for (const TPair<EBackroomsProp, FBackroomsPropLook>& Entry : Props)
	{
		if (Entry.Value.Mesh)
		{
			Mask |= 1u << (uint32)Entry.Key;
		}
	}
	return Mask;
}
