#include "BackroomsTypes.h"
#include "core/world.h"
#include "port/greybox.h"

static_assert((int)EBackroomsSurface::Count == (int)GreySurface::Count, "EBackroomsSurface must list GreySurface");
static_assert((int)EBackroomsSurface::DeadLight == (int)GreySurface::DeadLight, "EBackroomsSurface must list GreySurface");
static_assert((int)EBackroomsProp::Vending == PROP_VENDING, "EBackroomsProp must list PropKind");
static_assert((int)EBackroomsProp::ManilaTable == PROP_MANILA_TABLE, "EBackroomsProp must list PropKind");
static_assert((int)EBackroomsProp::Count <= 32, "greyboxChunk's meshedProps is a 32-bit mask");
