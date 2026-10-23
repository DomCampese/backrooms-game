#include "BackroomsChunkBuild.h"
#include "BackroomsCoords.h"
#include "BackroomsRender.h"
#include "ProceduralMeshComponent.h"
#include "core/world.h"
#include "port/chunk_mesh.h"

namespace
{
// raylib blends every draw. Unreal's masked pass cannot, so a face whose
// alpha the world shader leaves below 1 goes to a translucent section: glass
// and water by their vertex alpha (the shader's 0.30 to 0.62 band), and every
// face of the slots that are all decals (scrawl, contact shadows) or glass
// and water themselves.
bool BlendedSlot(int32 Slot)
{
	return Slot == MESH_SCRAWL || Slot == MESH_AO || Slot == MESH_WATER || Slot == MESH_GLASS;
}
bool BlendedAlpha(uint8 A) { return A >= 77 && A < 159; }   // 0.30 .. 0.62 of 255

// The slots MeshTracer traces the sim's rounds against.
bool SolidSlot(int32 Slot)
{
	return Slot != MESH_SCRAWL && Slot != MESH_WATER && Slot != MESH_AO;
}

struct FSection
{
	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colours;
	TArray<int32> Triangles;
	TMap<int32, int32> Remap;

	void Add(const MB& Part, int32 V)
	{
		if (const int32* Known = Remap.Find(V))
		{
			Triangles.Add(*Known);
			return;
		}
		const int32 Index = Vertices.Num();
		Remap.Add(V, Index);
		Vertices.Add(BackroomsCoords::ToUnreal({ Part.v[V * 3], Part.v[V * 3 + 1], Part.v[V * 3 + 2] }));
		Normals.Add(BackroomsCoords::ToUnrealDirection({ Part.n[V * 3], Part.n[V * 3 + 1], Part.n[V * 3 + 2] }));
		UVs.Add(FVector2D(Part.uv[V * 2], Part.uv[V * 2 + 1]));
		Colours.Add(FColor(Part.c[V * 4], Part.c[V * 4 + 1], Part.c[V * 4 + 2], Part.c[V * 4 + 3]));
		Triangles.Add(Index);
	}
};
}

void BackroomsChunkBuild::Build(World& W, int32 Cx, int32 Cz, const UBackroomsRender& Render, UProceduralMeshComponent& Mesh,
	bool bCollision)
{
	ChunkGeometry Geometry;
	bakeChunkGeometry(W, Cx, Cz, Geometry);
	int32 SectionIndex = 0;
	for (int32 Slot = 0; Slot < MESH_COUNT; Slot++)
	{
		const MB& Part = Geometry.parts[Slot];
		FSection Sections[2];   // masked, blended
		for (size_t T = 0; T + 2 < Part.idx.size(); T += 3)
		{
			const int32 A = Part.idx[T], B = Part.idx[T + 1], C = Part.idx[T + 2];
			const bool bBlend = BlendedSlot(Slot) || BlendedAlpha(Part.c[A * 4 + 3]);
			for (int32 V : { A, B, C })
			{
				Sections[bBlend ? 1 : 0].Add(Part, V);
			}
		}
		for (int32 Blend = 0; Blend < 2; Blend++)
		{
			const FSection& S = Sections[Blend];
			if (S.Triangles.IsEmpty())
			{
				continue;
			}
			Mesh.CreateMeshSection(SectionIndex, S.Vertices, S.Triangles, S.Normals, S.UVs, S.Colours,
				TArray<FProcMeshTangent>(), bCollision && SolidSlot(Slot));
			Mesh.SetMaterial(SectionIndex, Render.Chunk(Slot, Blend != 0));
			SectionIndex++;
		}
	}
	// The shader's uDrawRel: which storey this chunk is, against the player's.
	Mesh.SetCustomPrimitiveDataFloat(0, 1.0f);
	Mesh.SetCustomPrimitiveDataFloat(1, (float)W.qs);
}
