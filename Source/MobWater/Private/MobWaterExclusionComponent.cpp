// Copyright (c) Jared Taylor

#include "MobWaterExclusionComponent.h"

#include "MobWaterModule.h"
#include "MobWaterSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "StaticMeshAttributes.h"
#endif

namespace
{
	/**
	 * How many texels a side a mesh outline is baked to.
	 *
	 * Small on purpose. What the mask carries is distance rather than coverage, so the edge it draws
	 * is as smooth as the distance is - and distance interpolates, where coverage does not. Sixty
	 * four square is sixteen kilobytes and holds a hull's outline more than well enough for something
	 * whose edge is softened over centimetres anyway.
	 */
	static constexpr int32 MobWaterSilhouetteSize = 64;

	/**
	 * Texels of guaranteed nothing around the outline.
	 *
	 * The distance transform measures out from whatever is not inside, so a shape touching the edge
	 * of the mask would be measured against the mask's border rather than against its own. One texel
	 * of margin costs a fraction of the footprint and makes the border unconditionally outside.
	 */
	static constexpr int32 MobWaterSilhouetteMargin = 1;
}

UMobWaterExclusionComponent::UMobWaterExclusionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UMobWaterExclusionComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITOR
	if (Shape == EMobWaterExclusionShape::Mesh && SilhouetteSource != ResolveMesh())
	{
		BuildSilhouette();
	}
#endif

	if (Shape == EMobWaterExclusionShape::Mesh && Silhouette.Num() == 0)
	{
		// Said once, where it can be acted on. Without an outline there is nothing to cut with, and
		// the shape falls back to the rectangle its extent describes rather than doing nothing at all.
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %s cuts with a mesh and has no outline baked. Set Exclusion Mesh, or put "
				 "this component on an actor that has a static mesh. It behaves as a Rect until then."),
			*GetPathName());
	}

	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->RegisterExclusion(this);
	}
}

void UMobWaterExclusionComponent::OnUnregister()
{
	if (UMobWaterSubsystem* Subsystem = UMobWaterSubsystem::Get(this))
	{
		Subsystem->UnregisterExclusion(this);
	}

	Super::OnUnregister();
}

#if WITH_EDITOR
void UMobWaterExclusionComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UMobWaterExclusionComponent, Shape)
		|| Name == GET_MEMBER_NAME_CHECKED(UMobWaterExclusionComponent, ExclusionMesh))
	{
		BuildSilhouette();
	}
}

void UMobWaterExclusionComponent::RebuildSilhouette()
{
	BuildSilhouette();
	MarkPackageDirty();
}

UStaticMesh* UMobWaterExclusionComponent::ResolveMesh() const
{
	if (Shape != EMobWaterExclusionShape::Mesh)
	{
		return nullptr;
	}

	if (ExclusionMesh)
	{
		return ExclusionMesh;
	}

	if (const AActor* Owner = GetOwner())
	{
		if (const UStaticMeshComponent* Primitive = Owner->FindComponentByClass<UStaticMeshComponent>())
		{
			return Primitive->GetStaticMesh();
		}
	}

	return nullptr;
}

void UMobWaterExclusionComponent::BuildSilhouette()
{
	Silhouette.Reset();
	SilhouetteSize = 0;
	SilhouetteExtent = FVector2D::ZeroVector;
	SilhouetteSource = nullptr;
	SilhouetteTexture = nullptr;

	UStaticMesh* Mesh = ResolveMesh();
	if (!Mesh)
	{
		return;
	}

	const FMeshDescription* Description = Mesh->GetMeshDescription(0);
	if (!Description || Description->Triangles().Num() == 0)
	{
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %s has no mesh description to take an outline from."), *Mesh->GetPathName());
		return;
	}

	const FStaticMeshConstAttributes Attributes(*Description);
	const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

	FBox2D Footprint(ForceInit);
	for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
	{
		const FVector3f& P = Positions[Vertex];
		Footprint += FVector2D(P.X, P.Y);
	}

	const FVector2D Half = Footprint.GetExtent();
	if (Half.X <= UE_KINDA_SMALL_NUMBER || Half.Y <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogMobWater, Warning,
			TEXT("MobWater: %s is edge on in plan view, so it has no outline to cut with."),
			*Mesh->GetPathName());
		return;
	}

	const int32 Size = MobWaterSilhouetteSize;

	// The mask covers slightly more than the mesh does, so the outer texels are outside it whatever
	// shape it is. The published extent is the padded one, because that is what the mask's 0 to 1
	// actually spans.
	const double Pad = static_cast<double>(Size) / static_cast<double>(Size - 2 * MobWaterSilhouetteMargin);

	SilhouetteExtent = Half * Pad;
	SilhouetteSize = Size;

	const FVector2D Centre = Footprint.GetCenter();

	// Inside or not, at the centre of each texel. A distance transform over this is what turns one
	// bit a texel into an edge that softens over a distance.
	TArray<bool> Inside;
	Inside.SetNumZeroed(Size * Size);

	const FVector2D Origin = Centre - SilhouetteExtent;
	const FVector2D TexelSize = SilhouetteExtent * 2.0 / static_cast<double>(Size);

	for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
	{
		TArrayView<const FVertexID> Corners = Description->GetTriangleVertices(Triangle);
		if (Corners.Num() != 3)
		{
			continue;
		}

		const FVector2D A(Positions[Corners[0]].X, Positions[Corners[0]].Y);
		const FVector2D B(Positions[Corners[1]].X, Positions[Corners[1]].Y);
		const FVector2D C(Positions[Corners[2]].X, Positions[Corners[2]].Y);

		const double Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
		if (FMath::Abs(Area) <= UE_DOUBLE_SMALL_NUMBER)
		{
			// A triangle seen exactly edge on covers no texels and would divide by zero being asked
			// which side of itself a point is.
			continue;
		}

		const FVector2D Low = FVector2D(FMath::Min3(A.X, B.X, C.X), FMath::Min3(A.Y, B.Y, C.Y));
		const FVector2D High = FVector2D(FMath::Max3(A.X, B.X, C.X), FMath::Max3(A.Y, B.Y, C.Y));

		const int32 MinX = FMath::Clamp(FMath::FloorToInt((Low.X - Origin.X) / TexelSize.X), 0, Size - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt((High.X - Origin.X) / TexelSize.X), 0, Size - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt((Low.Y - Origin.Y) / TexelSize.Y), 0, Size - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt((High.Y - Origin.Y) / TexelSize.Y), 0, Size - 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				if (Inside[Y * Size + X])
				{
					continue;
				}

				const FVector2D P(
					Origin.X + (X + 0.5) * TexelSize.X,
					Origin.Y + (Y + 0.5) * TexelSize.Y);

				const double W0 = (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
				const double W1 = (C.X - B.X) * (P.Y - B.Y) - (C.Y - B.Y) * (P.X - B.X);
				const double W2 = (A.X - C.X) * (P.Y - C.Y) - (A.Y - C.Y) * (P.X - C.X);

				const bool bCovered = Area > 0.0
					? (W0 >= 0.0 && W1 >= 0.0 && W2 >= 0.0)
					: (W0 <= 0.0 && W1 <= 0.0 && W2 <= 0.0);

				if (bCovered)
				{
					Inside[Y * Size + X] = true;
				}
			}
		}
	}

	// Chamfer, two passes, three by three. A brute force distance would be four thousand texels
	// against four thousand texels; this is the same answer to within a few percent for two sweeps.
	const float Diagonal = FMath::Sqrt(2.f);
	const float Far = static_cast<float>(Size * 2);

	TArray<float> Distance;
	Distance.SetNumUninitialized(Size * Size);
	for (int32 Index = 0; Index < Distance.Num(); ++Index)
	{
		Distance[Index] = Inside[Index] ? Far : 0.f;
	}

	auto Relax = [&Distance, Size](int32 X, int32 Y, int32 DX, int32 DY, float Cost)
	{
		const int32 SX = X + DX;
		const int32 SY = Y + DY;
		if (SX < 0 || SX >= Size || SY < 0 || SY >= Size)
		{
			return;
		}

		const float Candidate = Distance[SY * Size + SX] + Cost;
		if (Candidate < Distance[Y * Size + X])
		{
			Distance[Y * Size + X] = Candidate;
		}
	};

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			Relax(X, Y, -1, 0, 1.f);
			Relax(X, Y, 0, -1, 1.f);
			Relax(X, Y, -1, -1, Diagonal);
			Relax(X, Y, 1, -1, Diagonal);
		}
	}

	for (int32 Y = Size - 1; Y >= 0; --Y)
	{
		for (int32 X = Size - 1; X >= 0; --X)
		{
			Relax(X, Y, 1, 0, 1.f);
			Relax(X, Y, 0, 1, 1.f);
			Relax(X, Y, 1, 1, Diagonal);
			Relax(X, Y, -1, 1, Diagonal);
		}
	}

	// Normalised by half the mask, which is the deepest anything inside it can be, so the stored byte
	// means the same thing whatever size the mesh was.
	const float Span = static_cast<float>(Size) * 0.5f;

	Silhouette.SetNumUninitialized(Size * Size);
	for (int32 Index = 0; Index < Distance.Num(); ++Index)
	{
		const float Normalised = FMath::Clamp(Distance[Index] / Span, 0.f, 1.f);
		Silhouette[Index] = static_cast<uint8>(FMath::RoundToInt(Normalised * 255.f));
	}

	SilhouetteSource = Mesh;

	UE_LOG(LogMobWater, Verbose, TEXT("MobWater: baked %s outline at %dx%d over %.0f by %.0f cm."),
		*Mesh->GetName(), Size, Size, SilhouetteExtent.X * 2.0, SilhouetteExtent.Y * 2.0);
}
#endif

FVector2D UMobWaterExclusionComponent::GetWorldExtent() const
{
	if (Shape != EMobWaterExclusionShape::Mesh || Silhouette.Num() == 0)
	{
		return FVector2D(FMath::Max(Extent.X, 1.0), FMath::Max(Extent.Y, 1.0));
	}

	const FVector Scale = GetComponentTransform().GetScale3D();

	return FVector2D(
		FMath::Max(SilhouetteExtent.X * FMath::Abs(Scale.X), 1.0),
		FMath::Max(SilhouetteExtent.Y * FMath::Abs(Scale.Y), 1.0));
}

float UMobWaterExclusionComponent::GetSilhouetteSpan() const
{
	// The mask holds distance normalised by half its own width, so what one unit of it is worth in
	// centimetres is the mean of the two half extents. On a square footprint that is exact; on a long
	// thin one it splits the difference, which is the same approximation the chamfer already made.
	const FVector2D World = GetWorldExtent();
	return static_cast<float>((World.X + World.Y) * 0.5);
}

UTexture2D* UMobWaterExclusionComponent::GetSilhouetteTexture() const
{
	if (Silhouette.Num() == 0 || SilhouetteSize <= 0)
	{
		return nullptr;
	}

	if (SilhouetteTexture)
	{
		return SilhouetteTexture;
	}

	// Four channels rather than one, and uncompressed. A single channel format would be half the
	// memory and a different sampler type from every other texture the field materials read, which
	// the material only checks against the parameter's default - so it would compile clean and bind
	// something the shader reads a different way. Sixteen kilobytes is not worth finding that out.
	UTexture2D* Texture = UTexture2D::CreateTransient(SilhouetteSize, SilhouetteSize, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->SRGB = false;
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;

	FTexturePlatformData* Data = Texture->GetPlatformData();
	if (!Data || Data->Mips.Num() == 0)
	{
		return nullptr;
	}

	uint8* Pixels = static_cast<uint8*>(Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Index = 0; Index < Silhouette.Num(); ++Index)
	{
		const uint8 Value = Silhouette[Index];

		Pixels[Index * 4 + 0] = Value;
		Pixels[Index * 4 + 1] = Value;
		Pixels[Index * 4 + 2] = Value;
		Pixels[Index * 4 + 3] = 255;
	}
	Data->Mips[0].BulkData.Unlock();

	Texture->UpdateResource();

	SilhouetteTexture = Texture;
	return SilhouetteTexture;
}

float UMobWaterExclusionComponent::SampleSilhouette(const FVector2D& UV) const
{
	const int32 Size = SilhouetteSize;

	// Half a texel in, and clamped, which is what a clamped bilinear sampler does at the border.
	const double X = FMath::Clamp(UV.X * Size - 0.5, 0.0, static_cast<double>(Size - 1));
	const double Y = FMath::Clamp(UV.Y * Size - 0.5, 0.0, static_cast<double>(Size - 1));

	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const int32 X1 = FMath::Min(X0 + 1, Size - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Size - 1);

	const float FX = static_cast<float>(X - X0);
	const float FY = static_cast<float>(Y - Y0);

	const float A = Silhouette[Y0 * Size + X0] / 255.f;
	const float B = Silhouette[Y0 * Size + X1] / 255.f;
	const float C = Silhouette[Y1 * Size + X0] / 255.f;
	const float D = Silhouette[Y1 * Size + X1] / 255.f;

	return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
}

void UMobWaterExclusionComponent::PackForShader(FLinearColor& OutA, FLinearColor& OutB) const
{
	const FVector Location = GetComponentLocation();
	const float Yaw = FMath::DegreesToRadians(GetComponentRotation().Yaw);

	const bool bRadial = Shape == EMobWaterExclusionShape::Disc || Shape == EMobWaterExclusionShape::Sphere;

	const FVector2D World = GetWorldExtent();

	OutA = FLinearColor(
		static_cast<float>(Location.X),
		static_cast<float>(Location.Y),
		static_cast<float>(World.X),
		static_cast<float>(World.Y));

	// Cosine and sine rather than the angle, so the shader compares without calling sincos on every
	// pixel for a value that is the same across the whole volume.
	OutB = FLinearColor(
		FMath::Cos(Yaw),
		FMath::Sin(Yaw),
		bRadial ? 0.f : 1.f,
		Strength * (IsActive() ? 1.f : 0.f));
}

void UMobWaterExclusionComponent::PackMeshForShader(FLinearColor& OutA, FLinearColor& OutB) const
{
	PackForShader(OutA, OutB);

	// The third slot carries what a mask unit is worth against the softness instead of which shape
	// this is, because a mesh is only ever one of them.
	OutB.B = GetSilhouetteSpan() / FMath::Max(EdgeSoftness, 1.f);
}

float UMobWaterExclusionComponent::GetExclusionAt(const FVector& Location) const
{
	if (!IsActive() || Strength <= 0.f)
	{
		return 0.f;
	}

	const FVector Centre = GetComponentLocation();
	const float Yaw = FMath::DegreesToRadians(GetComponentRotation().Yaw);

	const FVector2D Offset(Location.X - Centre.X, Location.Y - Centre.Y);

	// Into the volume's own frame, which is the same rotation the shader applies.
	const float CosYaw = FMath::Cos(Yaw);
	const float SinYaw = FMath::Sin(Yaw);
	const FVector2D Local(
		Offset.X * CosYaw + Offset.Y * SinYaw,
		-Offset.X * SinYaw + Offset.Y * CosYaw);

	const FVector2D World = GetWorldExtent();
	const float HalfX = static_cast<float>(World.X);
	const float HalfY = static_cast<float>(World.Y);

	float Inside;
	if (IsMesh())
	{
		const FVector2D UV(
			Local.X / (HalfX * 2.f) + 0.5,
			Local.Y / (HalfY * 2.f) + 0.5);

		if (UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0)
		{
			return 0.f;
		}

		Inside = SampleSilhouette(UV) * GetSilhouetteSpan();
	}
	else if (Shape == EMobWaterExclusionShape::Disc || Shape == EMobWaterExclusionShape::Sphere)
	{
		Inside = HalfX - static_cast<float>(Local.Size());
	}
	else
	{
		Inside = FMath::Min(
			HalfX - FMath::Abs(static_cast<float>(Local.X)),
			HalfY - FMath::Abs(static_cast<float>(Local.Y)));
	}

	if (Inside <= 0.f)
	{
		return 0.f;
	}

	const float Softness = FMath::Max(EdgeSoftness, 1.f);
	return FMath::Clamp(Inside / Softness, 0.f, 1.f) * Strength;
}
