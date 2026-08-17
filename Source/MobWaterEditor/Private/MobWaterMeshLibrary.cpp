// Copyright (c) Jared Taylor

#include "MobWaterMeshLibrary.h"

#include "MobWaterEditor.h"
#include "MobWaterSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"

namespace
{
	/**
	 * How finely a surface is subdivided.
	 *
	 * A Gerstner wave is displaced per vertex, so this is the resolution the waves actually have -
	 * too coarse and a swell reads as folded card. 64 each way is 8192 triangles for a whole body of
	 * water, which is less than one prop, and the mesh is shared by every pool in the level.
	 */
	constexpr int32 PlaneSegments = 64;

	constexpr int32 DiscRings = 32;
	constexpr int32 DiscSectors = 64;

	/**
	 * The ocean reaches to the horizon, so most of it is far away and a pixel high.
	 *
	 * More rings than a pool because it covers kilometres, and an exponent that puts nearly all of
	 * them inside the first tenth of the radius - which is the only part of an ocean anyone can see
	 * a wave on.
	 */
	constexpr int32 OceanRings = 96;
	constexpr int32 OceanSectors = 96;
	constexpr float OceanExponent = 3.f;

	/** Two kilometres across, built at that size rather than scaled up to it. */
	constexpr float OceanWorldSize = 200000.f;

	UStaticMesh* CreateMeshAsset(const FString& PackagePath, const FString& Name)
	{
		const FString PackageName = PackagePath / Name;

		// Loaded first when there is already one on disk, rather than created over the top of it.
		// CreatePackage leaves an existing file unread, and the save path fully loads it later - from
		// inside the save - so a mesh is loaded while the editor is loading packages, and finishing
		// its compilation there clears a flag that is not allowed to be cleared mid-load. The symptom
		// is an ensure in SetIsEditorLoadingPackage that names nothing to do with meshes.
		UPackage* Package = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}

		if (!Package)
		{
			Package = CreatePackage(*PackageName);
		}

		if (!Package)
		{
			return nullptr;
		}

		// Rebuilt in place, so a component already pointing at this mesh still points at it.
		if (UStaticMesh* Existing = FindObject<UStaticMesh>(Package, *Name))
		{
			FStaticMeshCompilingManager::Get().FinishCompilation({ Existing });

			Existing->GetStaticMaterials().Empty();
			Existing->SetNumSourceModels(0);
			Package->MarkPackageDirty();

			return Existing;
		}

		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
		if (Mesh)
		{
			FAssetRegistryModule::AssetCreated(Mesh);
			Package->MarkPackageDirty();
		}

		return Mesh;
	}

	/** Everything a static mesh needs per corner, set explicitly rather than computed from winding. */
	struct FCornerWriter
	{
		FMeshDescription& Mesh;
		FStaticMeshAttributes& Attributes;
		FPolygonGroupID Group;

		FVertexInstanceID Make(FVertexID Vertex, const FVector2f& UV) const
		{
			const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);

			Attributes.GetVertexInstanceUVs().Set(Instance, 0, UV);
			Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(0.f, 0.f, 1.f);
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(1.f, 0.f, 0.f);
			Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.f;
			Attributes.GetVertexInstanceColors()[Instance] = FVector4f(1.f, 1.f, 1.f, 1.f);

			return Instance;
		}

		void Triangle(FVertexID A, const FVector2f& UVA,
			FVertexID B, const FVector2f& UVB,
			FVertexID C, const FVector2f& UVC) const
		{
			TArray<FVertexInstanceID> Corners;
			Corners.Add(Make(A, UVA));
			Corners.Add(Make(B, UVB));
			Corners.Add(Make(C, UVC));

			Mesh.CreatePolygon(Group, Corners);
		}
	};

	void FinishMesh(UStaticMesh* Mesh)
	{
		Mesh->CommitMeshDescription(0);

		// Collision would be a lie: the surface the player touches is the displaced one, and the
		// collision hull would be the flat mesh this is built from. Immersion is answered by the wave
		// query instead, which knows where the surface actually is.
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Mesh->bAllowCPUAccess = false;

		Mesh->Build(false);
		Mesh->PostEditChange();

		// Handed back only once it has finished compiling. Build queues the work asynchronously, and
		// the first thing to serialise the mesh - saving it, or anything that loads its package -
		// blocks on that compilation from wherever it happens to be. Reached from inside package
		// loading it ensures, because finishing a compile clears the editor's loading flag and
		// clearing it mid-load is prohibited.
		FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });

		Mesh->MarkPackageDirty();
	}

}

UStaticMesh* UMobWaterMeshLibrary::BuildPlane(const FString& PackagePath, const FString& Name, int32 Segments)
{
	UStaticMesh* Mesh = CreateMeshAsset(PackagePath, Name);
	if (!Mesh)
	{
		return nullptr;
	}

	Mesh->GetStaticMaterials().Add(FStaticMaterial());

	// CreateMeshDescription answers null when there is no source model to hang it on, and it does so
	// quietly - the builder then returns nothing and the only symptom is a mesh that never appears.
	Mesh->AddSourceModel();

	FMeshDescription* Description = Mesh->CreateMeshDescription(0);
	if (!Description)
	{
		return nullptr;
	}

	FStaticMeshAttributes Attributes(*Description);
	Attributes.Register();
	Attributes.GetVertexInstanceUVs().SetNumChannels(1);

	const FPolygonGroupID Group = Description->CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");

	const int32 Count = FMath::Max(Segments, 1) + 1;

	TArray<FVertexID> Vertices;
	Vertices.Reserve(Count * Count);

	TArray<FVector2f> UVs;
	UVs.Reserve(Count * Count);

	for (int32 Y = 0; Y < Count; ++Y)
	{
		for (int32 X = 0; X < Count; ++X)
		{
			const float U = static_cast<float>(X) / static_cast<float>(Count - 1);
			const float V = static_cast<float>(Y) / static_cast<float>(Count - 1);

			const FVertexID Vertex = Description->CreateVertex();
			// Unit sized and centred, so the component's scale is the body's size in world units.
			Attributes.GetVertexPositions()[Vertex] = FVector3f(U - 0.5f, V - 0.5f, 0.f);

			Vertices.Add(Vertex);
			UVs.Add(FVector2f(U, V));
		}
	}

	const FCornerWriter Writer{ *Description, Attributes, Group };

	for (int32 Y = 0; Y < Count - 1; ++Y)
	{
		for (int32 X = 0; X < Count - 1; ++X)
		{
			const int32 A = Y * Count + X;
			const int32 B = A + 1;
			const int32 C = A + Count;
			const int32 D = C + 1;

			Writer.Triangle(Vertices[A], UVs[A], Vertices[C], UVs[C], Vertices[B], UVs[B]);
			Writer.Triangle(Vertices[B], UVs[B], Vertices[C], UVs[C], Vertices[D], UVs[D]);
		}
	}

	FinishMesh(Mesh);

	return Mesh;
}

UStaticMesh* UMobWaterMeshLibrary::BuildOceanRing(const FString& PackagePath, const FString& Name,
	int32 Rings, int32 Sectors, float Exponent, float WorldSize)
{
	return BuildDiscInternal(PackagePath, Name, Rings, Sectors, Exponent, WorldSize);
}

UStaticMesh* UMobWaterMeshLibrary::BuildDisc(const FString& PackagePath, const FString& Name, int32 Rings, int32 Sectors)
{
	// Evenly spaced and unit sized: a pool is looked at from above, its rim matters as much as its
	// middle, and the component scales it to whatever extent the artist asked for.
	return BuildDiscInternal(PackagePath, Name, Rings, Sectors, 1.f, 1.f);
}

UStaticMesh* UMobWaterMeshLibrary::BuildDiscInternal(const FString& PackagePath, const FString& Name,
	int32 Rings, int32 Sectors, float Exponent, float WorldSize)
{
	UStaticMesh* Mesh = CreateMeshAsset(PackagePath, Name);
	if (!Mesh)
	{
		return nullptr;
	}

	Mesh->GetStaticMaterials().Add(FStaticMaterial());

	// CreateMeshDescription answers null when there is no source model to hang it on, and it does so
	// quietly - the builder then returns nothing and the only symptom is a mesh that never appears.
	Mesh->AddSourceModel();

	FMeshDescription* Description = Mesh->CreateMeshDescription(0);
	if (!Description)
	{
		return nullptr;
	}

	FStaticMeshAttributes Attributes(*Description);
	Attributes.Register();
	Attributes.GetVertexInstanceUVs().SetNumChannels(1);

	const FPolygonGroupID Group = Description->CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");

	const int32 RingCount = FMath::Max(Rings, 1);
	const int32 SectorCount = FMath::Max(Sectors, 3);

	// UV is the bounding square rather than a polar unwrap, because the shader measures distance to
	// the rim as length(UV - 0.5) * 2 - the same arithmetic the CPU query uses. A polar unwrap would
	// look tidier in a UV editor and would not answer that question.
	auto MakeVertex = [&](float Radius, float Angle, FVertexID& OutVertex, FVector2f& OutUV)
	{
		const float X = Radius * FMath::Cos(Angle);
		const float Y = Radius * FMath::Sin(Angle);

		OutVertex = Description->CreateVertex();

		// Built at its real size when one is given, rather than unit sized and scaled by the
		// component. An ocean scaled a hundred thousand times has its innermost vertices microns
		// apart in mesh space, which degenerates their tangents - the engine reports it as nearly
		// zero bi-normals and it shows as shading tearing exactly where the camera stands.
		Attributes.GetVertexPositions()[OutVertex] = FVector3f(X * 0.5f * WorldSize, Y * 0.5f * WorldSize, 0.f);

		OutUV = FVector2f(X * 0.5f + 0.5f, Y * 0.5f + 0.5f);
	};

	FVertexID Centre;
	FVector2f CentreUV;
	MakeVertex(0.f, 0.f, Centre, CentreUV);

	TArray<TArray<FVertexID>> RingVertices;
	TArray<TArray<FVector2f>> RingUVs;
	RingVertices.SetNum(RingCount);
	RingUVs.SetNum(RingCount);

	for (int32 Ring = 0; Ring < RingCount; ++Ring)
	{
		const float Even = static_cast<float>(Ring + 1) / static_cast<float>(RingCount);

		// Raised to a power so rings crowd towards the middle where the waves can be seen, but mixed
		// back with the even spacing rather than used raw. A cube of the first step is a millionth of
		// the radius, which puts dozens of rings on top of each other at the centre - the engine
		// reports it as nearly zero bi-normals, and it shows as shading tearing exactly where the
		// camera is standing.
		const float Curved = FMath::Pow(Even, FMath::Max(Exponent, 0.1f));
		const float Radius = FMath::Lerp(Even, Curved, 0.9f);

		for (int32 Sector = 0; Sector < SectorCount; ++Sector)
		{
			const float Angle = 2.f * UE_PI * static_cast<float>(Sector) / static_cast<float>(SectorCount);

			FVertexID Vertex;
			FVector2f UV;
			MakeVertex(Radius, Angle, Vertex, UV);

			RingVertices[Ring].Add(Vertex);
			RingUVs[Ring].Add(UV);
		}
	}

	const FCornerWriter Writer{ *Description, Attributes, Group };

	for (int32 Sector = 0; Sector < SectorCount; ++Sector)
	{
		const int32 Next = (Sector + 1) % SectorCount;

		Writer.Triangle(Centre, CentreUV,
			RingVertices[0][Sector], RingUVs[0][Sector],
			RingVertices[0][Next], RingUVs[0][Next]);
	}

	for (int32 Ring = 0; Ring < RingCount - 1; ++Ring)
	{
		for (int32 Sector = 0; Sector < SectorCount; ++Sector)
		{
			const int32 Next = (Sector + 1) % SectorCount;

			Writer.Triangle(RingVertices[Ring][Sector], RingUVs[Ring][Sector],
				RingVertices[Ring + 1][Sector], RingUVs[Ring + 1][Sector],
				RingVertices[Ring][Next], RingUVs[Ring][Next]);

			Writer.Triangle(RingVertices[Ring][Next], RingUVs[Ring][Next],
				RingVertices[Ring + 1][Sector], RingUVs[Ring + 1][Sector],
				RingVertices[Ring + 1][Next], RingUVs[Ring + 1][Next]);
		}
	}

	FinishMesh(Mesh);

	return Mesh;
}

void UMobWaterMeshLibrary::BuildSurfaceMeshes()
{
	const FString MeshRoot = TEXT("/MobWater/Meshes");

	if (BuildPlane(MeshRoot, TEXT("SM_MobWaterPlane"), PlaneSegments))
	{
		UE_LOG(LogMobWaterEditor, Display, TEXT("MobWater: built SM_MobWaterPlane (%d segments)."), PlaneSegments);
	}

	if (BuildDisc(MeshRoot, TEXT("SM_MobWaterDisc"), DiscRings, DiscSectors))
	{
		UE_LOG(LogMobWaterEditor, Display, TEXT("MobWater: built SM_MobWaterDisc (%d rings)."), DiscRings);
	}

	if (BuildOceanRing(MeshRoot, TEXT("SM_MobWaterOceanRing"), OceanRings, OceanSectors, OceanExponent, OceanWorldSize))
	{
		UE_LOG(LogMobWaterEditor, Display, TEXT("MobWater: built SM_MobWaterOceanRing (%d rings)."), OceanRings);
	}
}
