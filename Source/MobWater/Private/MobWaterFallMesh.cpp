// Copyright (c) Jared Taylor

#include "MobWaterFallMesh.h"

#include "MobWaterFallSplineComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

bool FMobWaterFallMesh::Build(const UMobWaterFallSplineComponent& Lip, UStaticMesh& OutMesh)
{
	if (Lip.GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	const float Length = Lip.GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FMeshDescription Description;

	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	Attributes.GetVertexInstanceUVs().SetNumChannels(1);

	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");

	const FTransform& Transform = Lip.GetComponentTransform();

	const int32 Along = FMath::Clamp(Lip.SegmentsAlong, 1, 256) + 1;
	const int32 Down = FMath::Clamp(Lip.SegmentsDown, 1, 128) + 1;

	const float Deepest = Lip.GetMaxDrop();

	// The tangent basis is the same for a whole column, the sheet being flat across its width by
	// construction, so it is worked out once per column rather than per vertex.
	const float BinormalSign = Lip.bMirror ? 1.f : -1.f;

	TArray<FVertexID> Vertices;
	TArray<FVector2f> UVs;
	TArray<FVector3f> Normals;
	TArray<FVector3f> Tangents;
	TArray<float> Shares;

	const int32 Count = Along * Down;
	Vertices.Reserve(Count);
	UVs.Reserve(Count);
	Normals.Reserve(Count);
	Tangents.Reserve(Count);
	Shares.Reserve(Count);

	for (int32 Step = 0; Step < Along; ++Step)
	{
		const float U = static_cast<float>(Step) / static_cast<float>(Along - 1);
		const float Distance = U * Length;

		const FVector Top = Lip.GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FVector Downstream = Lip.GetDownstreamAtDistance(Distance);

		const float Drop = FMath::Max(Lip.GetDropAtDistance(Distance), 0.f);
		const FVector Bottom = Top - FVector::UpVector * Drop + Downstream * Lip.Overhang;

		// The sheet faces the way the water is going, which is the side anyone looking at the fall is
		// standing on. Along the lip is U, so that is the tangent, and V is left to the binormal sign.
		const FVector Across = (Lip.GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World)
			* FVector(1.0, 1.0, 0.0)).GetSafeNormal();

		const FVector3f Normal = FVector3f(Transform.InverseTransformVectorNoScale(Downstream));
		const FVector3f Tangent = FVector3f(Transform.InverseTransformVectorNoScale(
			Across.IsNearlyZero() ? FVector::ForwardVector : Across));

		// How much of the deepest column this one is, so the material knows how far this water has
		// actually fallen. Without it every column would accelerate as though it had the longest
		// drop, and a fall over an uneven ledge would break up evenly across a line it never reached.
		const float Share = Drop / Deepest;

		for (int32 Row = 0; Row < Down; ++Row)
		{
			const float V = static_cast<float>(Row) / static_cast<float>(Down - 1);

			const FVertexID Vertex = Description.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] =
				FVector3f(Transform.InverseTransformPosition(FMath::Lerp(Top, Bottom, V)));

			Vertices.Add(Vertex);
			UVs.Add(FVector2f(U, V));
			Normals.Add(Normal);
			Tangents.Add(Tangent);
			Shares.Add(Share);
		}
	}

	auto Corner = [&Description, &Attributes, &UVs, &Normals, &Tangents, &Shares, BinormalSign]
		(int32 Index) -> FVertexInstanceID
	{
		const FVertexInstanceID Instance = Description.CreateVertexInstance(FVertexID(Index));

		Attributes.GetVertexInstanceUVs().Set(Instance, 0, UVs[Index]);
		Attributes.GetVertexInstanceNormals()[Instance] = Normals[Index];
		Attributes.GetVertexInstanceTangents()[Instance] = Tangents[Index];
		Attributes.GetVertexInstanceBinormalSigns()[Instance] = BinormalSign;

		// Red carries this column's share of the deepest drop. Encoded against the sRGB curve the
		// mesh build applies on the way to a byte, so the shader reads the fraction that was meant
		// rather than a brightened one, exactly as the shoreline distance is.
		const uint8 Quantised = static_cast<uint8>(
			FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Shares[Index], 0.f, 1.f) * 255.f), 0, 255));
		const float Encoded = FLinearColor(FColor(Quantised, 0, 0, 255)).R;

		Attributes.GetVertexInstanceColors()[Instance] = FVector4f(Encoded, 0.f, 0.f, 1.f);

		return Instance;
	};

	for (int32 Step = 0; Step < Along - 1; ++Step)
	{
		for (int32 Row = 0; Row < Down - 1; ++Row)
		{
			const int32 A = Step * Down + Row;
			const int32 B = A + 1;
			const int32 C = A + Down;
			const int32 D = C + 1;

			Description.CreatePolygon(Group, TArray<FVertexInstanceID>({ Corner(A), Corner(C), Corner(B) }));
			Description.CreatePolygon(Group, TArray<FVertexInstanceID>({ Corner(B), Corner(C), Corner(D) }));
		}
	}

	if (Description.Triangles().Num() == 0)
	{
		return false;
	}

	// A section takes its material index by matching the polygon group's slot name against the mesh's
	// own slots, and a mesh with none gets index -1 - the material set on the component then binds to
	// nothing and the sheet never draws at all.
	OutMesh.SetStaticMaterials({ FStaticMaterial(nullptr, TEXT("Water")) });

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = false;

	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&Description);

	OutMesh.BuildFromMeshDescriptions(Descriptions, Params);

	return true;
}
