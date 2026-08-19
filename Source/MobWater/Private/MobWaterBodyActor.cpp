// Copyright (c) Jared Taylor

#include "MobWaterBodyActor.h"

#include "MobWaterComponent.h"
#include "MobWaterModule.h"
#include "MobWaterSplineComponent.h"
#include "MobWaterSplineMesh.h"
#include "MobWaterTypes.h"
#include "Components/BillboardComponent.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#endif

#define LOCTEXT_NAMESPACE "MobWaterBody"

AMobWaterBody::AMobWaterBody()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<UMobWaterSplineComponent>(TEXT("Spline"));
	RootComponent = Spline;

	Water = CreateDefaultSubobject<UMobWaterComponent>(TEXT("Water"));
	Water->SetupAttachment(Spline);
	Water->Shape = EMobWaterShape::Spline;

#if WITH_EDITORONLY_DATA
	// A lake is a spline of thin curves and a surface with no thickness. Neither is easy to hit, and
	// clicking the surface selects the mesh rather than the actor that owns the shape.
	Sprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		Sprite->SetupAttachment(Spline);
		Sprite->bIsScreenSizeScaled = true;
		Sprite->SetHiddenInGame(true);
		Sprite->SetUsingAbsoluteScale(true);
		MobWaterSprite::Apply(Sprite);
	}
#endif
}

void AMobWaterBody::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// The mesh is transient, so it is gone every time the level is loaded and has to be made again.
	// Saving it instead would put a megabyte of triangles in the map for something that is a hundred
	// bytes of spline.
	RebuildSurface();
}

#if WITH_EDITOR
void AMobWaterBody::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RebuildSurface();
}

void AMobWaterBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Only when the drag ends. A spline point being dragged fires this every frame, and remeshing a
	// sixty-four square grid per frame turns the handle to treacle.
	if (bFinished)
	{
		RebuildSurface();
	}
}

void AMobWaterBody::CheckForErrors()
{
	Super::CheckForErrors();

	// A river only. A lake's surface is flattened to the component's plane as it is built, so its
	// spline can wander in Z without the water it draws or the water it answers with moving at all.
	if (!Spline || Spline->IsClosedLoop() || Spline->GetNumberOfSplinePoints() < 2)
	{
		return;
	}

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double PlaneZ = Spline->GetComponentLocation().Z;

	const int32 Steps = FMath::Clamp(Spline->SegmentsAlong, 2, 512);

	double Drift = 0.0;
	double Rise = 0.0;

	FVector Previous = Spline->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);

	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Distance = Length * static_cast<float>(Step) / static_cast<float>(Steps);
		const FVector At = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		Drift = FMath::Max(Drift, FMath::Abs(At.Z - PlaneZ));

		const double Run = FVector2D::Distance(FVector2D(Previous.X, Previous.Y), FVector2D(At.X, At.Y));
		if (Run > KINDA_SMALL_NUMBER)
		{
			Rise = FMath::Max(Rise, FMath::Abs(At.Z - Previous.Z) / Run);
		}

		Previous = At;
	}

	// The steep one first, and only one of the two, because a course that stands up has drifted as
	// well and saying so twice about the same segment is noise.
	if (Rise > MobWaterCourse::Slope)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Angle"), FMath::RoundToInt(FMath::RadiansToDegrees(FMath::Atan(Rise))));

		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MobWaterBody_Steep",
				"Water course runs at {Angle} degrees somewhere along it. The banks are held level as "
				"the surface is built, so a course this steep folds the water rather than sloping it. "
				"Break the course either side of the drop and put a waterfall between them."), Arguments)));
		return;
	}

	if (Drift > MobWaterCourse::Drift)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Drift"), FMath::RoundToInt(Drift));

		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MobWaterBody_Drift",
				"Water course leaves the actor's plane by {Drift}cm. The surface is drawn along the "
				"spline, but every query answers with the actor's own height, so anything floating, "
				"submerged or joined to this water is that far out where the course has fallen away."),
				Arguments)));
	}
}

#endif

void AMobWaterBody::RebuildSurface()
{
	if (!Spline || !Water)
	{
		return;
	}

	if (!GeneratedMesh)
	{
		GeneratedMesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	}

	if (!FMobWaterSplineMesh::Build(*Spline, *GeneratedMesh))
	{
		// Left alone rather than emptied. A spline mid-edit briefly has one point, and blinking the
		// water out every time someone adds one is worse than a frame of stale geometry.
		return;
	}

	Water->SetShoreSpline(Spline);
	Water->SetStaticMesh(GeneratedMesh);

	// The generated mesh is already in world-scale component space, so the component must not also
	// scale it the way a unit-sized pool mesh is scaled.
	Water->SetRelativeScale3D(FVector::OneVector);

	Water->ApplySurface();
	Water->MarkRenderStateDirty();
}

#undef LOCTEXT_NAMESPACE
