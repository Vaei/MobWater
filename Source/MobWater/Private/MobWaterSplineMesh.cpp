// Copyright (c) Jared Taylor

#include "MobWaterSplineMesh.h"

#include "MobWaterModule.h"
#include "MobWaterSplineComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

namespace
{
	/** Everything a corner needs, set explicitly rather than inferred from winding. */
	struct FCornerWriter
	{
		FMeshDescription& Mesh;
		FStaticMeshAttributes& Attributes;
		FPolygonGroupID Group;

		FVertexInstanceID Make(FVertexID Vertex, const FVector2f& UV, float ShoreDistance) const
		{
			const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);

			Attributes.GetVertexInstanceUVs().Set(Instance, 0, UV);
			Attributes.GetVertexInstanceNormals()[Instance] = FVector3f(0.f, 0.f, 1.f);
			Attributes.GetVertexInstanceTangents()[Instance] = FVector3f(1.f, 0.f, 0.f);
			Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.f;

			// Red carries how far inside the water this corner is, scaled into the range a colour
			// can hold. The shader multiplies it back out; nothing else reads the other channels.
			const float Stored = FMath::Clamp(ShoreDistance / MOB_WATER_SHORE_REFERENCE, 0.f, 1.f);

			// Encoded against the sRGB curve the mesh build applies on the way to a byte, so the
			// shader reads the fraction that was meant rather than a brightened one. Without it the
			// shore the vertex shader sees is not the shore the CPU query answers from.
			const uint8 Quantised = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Stored * 255.f), 0, 255));
			const float Encoded = FLinearColor(FColor(Quantised, 0, 0, 255)).R;

			Attributes.GetVertexInstanceColors()[Instance] = FVector4f(Encoded, 0.f, 0.f, 1.f);

			return Instance;
		}

		void Triangle(
			FVertexID A, const FVector2f& UVA, float ShoreA,
			FVertexID B, const FVector2f& UVB, float ShoreB,
			FVertexID C, const FVector2f& UVC, float ShoreC) const
		{
			TArray<FVertexInstanceID> Corners;
			Corners.Add(Make(A, UVA, ShoreA));
			Corners.Add(Make(B, UVB, ShoreB));
			Corners.Add(Make(C, UVC, ShoreC));

			Mesh.CreatePolygon(Group, Corners);
		}
	};

	bool BuildLake(const UMobWaterSplineComponent& Spline, FMeshDescription& Description,
		FStaticMeshAttributes& Attributes, const FPolygonGroupID Group)
	{
		TArray<FVector2D> Shore;
		Spline.GetShorelinePoints(Shore);

		if (Shore.Num() < 3)
		{
			return false;
		}

		FBox2D Bounds(ForceInit);
		for (const FVector2D& Point : Shore)
		{
			Bounds += Point;
		}

		// Expanded, so the grid has somewhere to fade out. A grid that stops exactly at the shoreline
		// has its outermost vertices sitting on zero and the fade has no room to happen in.
		Bounds = Bounds.ExpandBy(MOB_WATER_SHORE_REFERENCE * 0.25);

		const FTransform& Transform = Spline.GetComponentTransform();
		const int32 Count = FMath::Clamp(Spline.GridResolution, 4, 256) + 1;

		TArray<FVertexID> Vertices;
		TArray<FVector2f> UVs;
		TArray<float> Shores;

		Vertices.Reserve(Count * Count);
		UVs.Reserve(Count * Count);
		Shores.Reserve(Count * Count);

		for (int32 Y = 0; Y < Count; ++Y)
		{
			for (int32 X = 0; X < Count; ++X)
			{
				const float U = static_cast<float>(X) / static_cast<float>(Count - 1);
				const float V = static_cast<float>(Y) / static_cast<float>(Count - 1);

				const FVector2D World(
					FMath::Lerp(Bounds.Min.X, Bounds.Max.X, U),
					FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, V));

				const float Inside = Spline.GetDistanceInside(FVector(World.X, World.Y, 0.0));

				const FVertexID Vertex = Description.CreateVertex();

				// Stored in the component's own space, because the mesh hangs off the component and
				// would otherwise be offset by wherever the actor happens to stand.
				const FVector Local = Transform.InverseTransformPosition(FVector(World.X, World.Y, Transform.GetLocation().Z));
				Attributes.GetVertexPositions()[Vertex] = FVector3f(Local);

				Vertices.Add(Vertex);
				UVs.Add(FVector2f(U, V));
				Shores.Add(Inside);
			}
		}

		const FCornerWriter Writer{ Description, Attributes, Group };

		for (int32 Y = 0; Y < Count - 1; ++Y)
		{
			for (int32 X = 0; X < Count - 1; ++X)
			{
				const int32 A = Y * Count + X;
				const int32 B = A + 1;
				const int32 C = A + Count;
				const int32 D = C + 1;

				// Quads entirely outside the water are dropped rather than drawn transparent. They
				// would shade to nothing anyway, and a lake in the corner of a large bounding box is
				// mostly this.
				const float Most = FMath::Max(FMath::Max(Shores[A], Shores[B]), FMath::Max(Shores[C], Shores[D]));
				if (Most <= 0.f)
				{
					continue;
				}

				Writer.Triangle(
					Vertices[A], UVs[A], Shores[A],
					Vertices[C], UVs[C], Shores[C],
					Vertices[B], UVs[B], Shores[B]);

				Writer.Triangle(
					Vertices[B], UVs[B], Shores[B],
					Vertices[C], UVs[C], Shores[C],
					Vertices[D], UVs[D], Shores[D]);
			}
		}

		return true;
	}

	bool BuildRiver(const UMobWaterSplineComponent& Spline, FMeshDescription& Description,
		FStaticMeshAttributes& Attributes, const FPolygonGroupID Group)
	{
		const float Length = Spline.GetSplineLength();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FTransform& Transform = Spline.GetComponentTransform();

		const int32 Along = FMath::Clamp(Spline.SegmentsAlong, 2, 512) + 1;
		const int32 Across = FMath::Clamp(Spline.SegmentsAcross, 2, 64) + 1;

		TArray<FVertexID> Vertices;
		TArray<FVector2f> UVs;
		TArray<float> Shores;

		Vertices.Reserve(Along * Across);
		UVs.Reserve(Along * Across);
		Shores.Reserve(Along * Across);

		for (int32 Step = 0; Step < Along; ++Step)
		{
			const float T = static_cast<float>(Step) / static_cast<float>(Along - 1);
			const float Distance = T * Length;

			const FVector Centre = Spline.GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
			const FVector Tangent = Spline.GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

			// Flattened before the side vector is taken. A river that dips downhill would otherwise
			// have banks that tilt with it, and a water surface that is not level is not water.
			const FVector Flat = FVector(Tangent.X, Tangent.Y, 0.0).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Flat, FVector::UpVector).GetSafeNormal();

			const float HalfWidth = Spline.GetWidthAtDistance(Distance) * 0.5f;

			for (int32 Lane = 0; Lane < Across; ++Lane)
			{
				const float S = static_cast<float>(Lane) / static_cast<float>(Across - 1);
				const float Offset = (S - 0.5f) * 2.f * HalfWidth;

				const FVector World = Centre + Side * Offset;

				const FVertexID Vertex = Description.CreateVertex();
				Attributes.GetVertexPositions()[Vertex] = FVector3f(Transform.InverseTransformPosition(World));

				Vertices.Add(Vertex);

				// V runs across the river and U along it, so a flowing texture scrolls in U and the
				// banks are the two edges of V - which is what every river material expects.
				UVs.Add(FVector2f(T * Length / FMath::Max(HalfWidth * 2.f, 1.f), S));

				// Distance to the nearer bank, which is what the wave attenuation wants.
				Shores.Add(HalfWidth - FMath::Abs(Offset));
			}
		}

		const FCornerWriter Writer{ Description, Attributes, Group };

		for (int32 Step = 0; Step < Along - 1; ++Step)
		{
			for (int32 Lane = 0; Lane < Across - 1; ++Lane)
			{
				const int32 A = Step * Across + Lane;
				const int32 B = A + 1;
				const int32 C = A + Across;
				const int32 D = C + 1;

				Writer.Triangle(
					Vertices[A], UVs[A], Shores[A],
					Vertices[C], UVs[C], Shores[C],
					Vertices[B], UVs[B], Shores[B]);

				Writer.Triangle(
					Vertices[B], UVs[B], Shores[B],
					Vertices[C], UVs[C], Shores[C],
					Vertices[D], UVs[D], Shores[D]);
			}
		}

		return true;
	}
}

bool FMobWaterSplineMesh::Build(const UMobWaterSplineComponent& Spline, UStaticMesh& OutMesh)
{
	if (Spline.GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	FMeshDescription Description;

	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	Attributes.GetVertexInstanceUVs().SetNumChannels(1);

	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");

	const bool bBuilt = Spline.IsClosedLoop()
		? BuildLake(Spline, Description, Attributes, Group)
		: BuildRiver(Spline, Description, Attributes, Group);

	if (!bBuilt || Description.Triangles().Num() == 0)
	{
		return false;
	}

	// A section takes its material index by matching the polygon group's slot name against the mesh's
	// own slots, and a mesh with none gets index -1 - the material set on the component then binds to
	// nothing and the surface never draws at all.
	OutMesh.SetStaticMaterials({ FStaticMaterial(nullptr, TEXT("Water")) });

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;

	// Vertex colours have to survive the build, and they are the whole point of this mesh - they
	// carry the distance to the shore that the surface fades and lies its waves down against.
	Params.bAllowCpuAccess = false;

	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&Description);

	OutMesh.BuildFromMeshDescriptions(Descriptions, Params);

	return true;
}
