// Copyright (c) Jared Taylor

#include "MobWaterBuoyancyComponent.h"

#include "MobWaterInfo.h"
#include "MobWaterModule.h"
#include "MobWaterStatics.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * A damping force, held to what can stop this much momentum in one frame.
	 *
	 * Wanted is the force damping asks for and Speed is what it is opposing, both signed the same
	 * way. Stoppable is the momentum per unit of speed divided by the frame, so Speed times it is
	 * exactly the force that arrives at zero rather than past it.
	 */
	FORCEINLINE float Brake(float Wanted, float Speed, float Stoppable)
	{
		const float Limit = FMath::Abs(Speed) * Stoppable;
		return FMath::Clamp(Wanted, -Limit, Limit);
	}
}

#if !UE_BUILD_SHIPPING
static int32 GMobWaterBuoyancyDebug = 0;
static FAutoConsoleVariableRef CVarMobWaterBuoyancyDebug(
	TEXT("mob.Water.Buoyancy"),
	GMobWaterBuoyancyDebug,
	TEXT("Draws every pontoon and the surface height it found. 1 draws, 2 also logs."),
	ECVF_Cheat);
#endif

UMobWaterBuoyancyComponent::UMobWaterBuoyancyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Before physics, not after. A force added after the solver has run is a force the solver sees
	// on the next frame, which is a whole frame of the body falling through the water it was meant
	// to be held up by - and it presents as buoyancy being too weak rather than as being too late.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMobWaterBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoPontoons && Pontoons.Num() == 0)
	{
		BuildPontoonsFromBounds();
	}
}

void UMobWaterBuoyancyComponent::BuildPontoonsFromBounds()
{
	const UPrimitiveComponent* Primitive = FloatedBody ? FloatedBody.Get()
		: (GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr);

	// A metre either way, so a component on something with no bounds still floats something rather
	// than collapsing every pontoon onto one point and spinning.
	FVector Extent(100.0, 100.0, 100.0);

	if (Primitive)
	{
		Extent = Primitive->CalcBounds(FTransform::Identity).BoxExtent;
	}

	Pontoons.Reset(4);

	// The four corners, at the body's own height rather than under it, and that is the whole of what
	// keeps it upright. Buoyancy acting below the centre of mass is a pendulum stood on its point:
	// tilt it and the net lift, now off to one side, pushes it further over. Four pontoons in a
	// rectangle do produce a righting couple - the low one goes deeper and lifts harder - but on
	// anything as tall as it is wide that couple is the smaller of the two and the body rolls.
	//
	// Level with the centre of mass, the lift has no arm of its own and only the couple is left.
	const double X = FMath::Max(Extent.X * 0.7, 10.0);
	const double Y = FMath::Max(Extent.Y * 0.7, 10.0);

	// Half the body's height, so it settles half submerged - which is where a crate floats and is
	// also a soft enough spring that a wave passing under lifts it rather than kicking it.
	const float Radius = static_cast<float>(FMath::Max(Extent.Z * 0.5, 10.0));

	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		FMobWaterPontoon Pontoon;
		Pontoon.Offset = FVector(Corner & 1 ? X : -X, Corner & 2 ? Y : -Y, 0.0);
		Pontoon.Radius = Radius;
		Pontoons.Add(Pontoon);
	}
}

UPrimitiveComponent* UMobWaterBuoyancyComponent::ResolveBody()
{
	if (Body && Body->IsSimulatingPhysics())
	{
		return Body;
	}

	Body = FloatedBody;

	if (!Body)
	{
		if (const AActor* Owner = GetOwner())
		{
			Body = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		}
	}

	if (Body && Body->IsSimulatingPhysics())
	{
		return Body;
	}

	if (!bWarnedNoBody)
	{
		bWarnedNoBody = true;
		UE_LOG(LogMobWater, Warning,
			TEXT("%s has nothing to float: %s does not simulate physics. Buoyancy is forces on a rigid ")
			TEXT("body, so a kinematic one has nothing for it to act on."),
			*GetPathNameSafe(GetOwner()), *GetNameSafe(Body));
	}

	Body = nullptr;
	return nullptr;
}

void UMobWaterBuoyancyComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	Submersion = 0.f;
	bInWater = false;

	UPrimitiveComponent* Floated = ResolveBody();
	if (!Floated || Pontoons.Num() == 0)
	{
		return;
	}

	const FTransform Transform = GetComponentTransform();

	TArray<FVector> Points;
	Points.Reserve(Pontoons.Num());
	for (const FMobWaterPontoon& Pontoon : Pontoons)
	{
		Points.Add(Transform.TransformPosition(Pontoon.Offset));
	}

	// One call for the whole array rather than one per point, which is what it was shaped for: the
	// pontoons are a metre apart and in the same body every frame but the one a hull crosses an edge.
	TArray<FMobWaterInfo> Infos;
	UMobWaterStatics::GetWaterInfoAtLocations(this, Points, Infos);

	// Held up by its own weight, so a heavy body and a light one of the same size both settle at the
	// same depth. The physical alternative needs a displaced volume, and a sphere's volume is not the
	// volume of the hull it stands for - so it would have to be retuned every time a pontoon moved.
	const float Mass = Floated->GetMass();
	const float Gravity = FMath::Abs(GetWorld() ? GetWorld()->GetGravityZ() : -980.f);
	const float PerPontoon = Mass * Gravity / static_cast<float>(Pontoons.Num());

	/** This pontoon's share of the body's momentum, per unit of speed. */
	const float Share = Mass / static_cast<float>(Pontoons.Num());

	FVector SurfaceNormal = FVector::ZeroVector;
	int32 Wet = 0;

	for (int32 Index = 0; Index < Pontoons.Num(); ++Index)
	{
		FMobWaterPontoon& Pontoon = Pontoons[Index];
		const FMobWaterInfo& Info = Infos.IsValidIndex(Index) ? Infos[Index] : FMobWaterInfo();

		Pontoon.bInWater = Info.bValid;
		Pontoon.SurfaceZ = Info.SurfaceZ;
		Pontoon.Submersion = 0.f;

		if (!Info.bValid)
		{
			continue;
		}

		const FVector Point = Points[Index];
		const float Radius = FMath::Max(Pontoon.Radius, 1.f);

		// Linear across the pontoon's own height rather than a sphere cap's actual volume. The cap
		// is barely different in the middle of the range and it is zero-gradient at both ends, which
		// makes a body resting exactly at the waterline take a long time to find it.
		Pontoon.Submersion = FMath::Clamp(
			(Info.SurfaceZ - (static_cast<float>(Point.Z) - Radius)) / (2.f * Radius), 0.f, 1.f);

		if (Pontoon.Submersion <= 0.f)
		{
			continue;
		}

		++Wet;
		Submersion += Pontoon.Submersion;
		SurfaceNormal += Info.Normal;

		const FVector Velocity = Floated->GetPhysicsLinearVelocityAtPoint(Point);

		FVector Force = FVector(0.0, 0.0, BuoyancyCoefficient * Pontoon.Submersion * PerPontoon);

		// Damping is per pontoon and scaled by how much of it is under, so a body half out of the
		// water is half damped rather than fully damped by the half that is in air.
		const float Damped = Pontoon.Submersion * PerPontoon / FMath::Max(Gravity, 1.f);

		// No damping term may be larger than the one that brings its share of the body to a stop this
		// frame. Past that it reverses the velocity into a larger one, and next frame it reverses that
		// - a boat that was floating quietly leaves the level on the first hitch. Buoyancy is a stiff
		// spring, and a stiff spring integrated forwards is only stable while the frame is short.
		const float Stoppable = DeltaTime > UE_SMALL_NUMBER
			? Share / DeltaTime : TNumericLimits<float>::Max();

		const float Rise = static_cast<float>(Velocity.Z);
		Force.Z -= Brake(Rise * VerticalDamping * Damped, Rise, Stoppable);

		const FVector2D Flow = Info.FlowVelocity * FlowStrength;
		const float Along = static_cast<float>(Flow.X) - static_cast<float>(Velocity.X);
		const float Across = static_cast<float>(Flow.Y) - static_cast<float>(Velocity.Y);

		Force.X += Brake(Along * LateralDamping * Damped, Along, Stoppable);
		Force.Y += Brake(Across * LateralDamping * Damped, Across, Stoppable);

		Floated->AddForceAtLocation(Force, Point);
	}

	if (Wet == 0)
	{
		return;
	}

	bInWater = true;
	Submersion /= static_cast<float>(Pontoons.Num());

	// Asked for as an acceleration rather than a torque, so it does not have to know the body's own
	// inertia - which means it must not carry the mass either, or a heavy boat is damped a hundred
	// times harder than a light one and spins itself apart.
	//
	// Held under what stops the rotation this frame, for the same reason the linear damping is.
	const FVector AngularVelocity = Floated->GetPhysicsAngularVelocityInRadians();
	const float Spin = DeltaTime > UE_SMALL_NUMBER
		? FMath::Min(AngularDamping * Submersion, 1.f / DeltaTime) : AngularDamping * Submersion;

	Floated->AddTorqueInRadians(-AngularVelocity * Spin, NAME_None, true);

	if (bAlignToSurface && AlignStrength > 0.f)
	{
		SurfaceNormal = SurfaceNormal.GetSafeNormal();

		// The rotation that would lay the body flat on the water, applied as a torque rather than a
		// transform. A body whose orientation is written directly is a body that has stopped being
		// simulated, and everything it is resting on stops working.
		const FVector Up = Floated->GetComponentTransform().GetUnitAxis(EAxis::Z);
		const FVector Axis = FVector::CrossProduct(Up, SurfaceNormal);

		Floated->AddTorqueInRadians(Axis * AlignStrength * Submersion, NAME_None, true);
	}

#if !UE_BUILD_SHIPPING
	if (GMobWaterBuoyancyDebug > 0)
	{
		for (int32 Index = 0; Index < Pontoons.Num(); ++Index)
		{
			const FMobWaterPontoon& Pontoon = Pontoons[Index];
			const FVector Point = Points[Index];

			DrawDebugSphere(GetWorld(), Point, Pontoon.Radius, 12,
				Pontoon.Submersion > 0.f ? FColor::Cyan : FColor::Silver, false, -1.f, 0, 1.f);

			if (Pontoon.bInWater)
			{
				DrawDebugPoint(GetWorld(), FVector(Point.X, Point.Y, Pontoon.SurfaceZ), 12.f,
					FColor::White, false, -1.f);
			}
		}

		if (GMobWaterBuoyancyDebug > 1)
		{
			UE_LOG(LogMobWater, Log, TEXT("%s submersion %.3f at Z %.2f"),
				*GetNameSafe(GetOwner()), Submersion, Floated->GetComponentLocation().Z);
		}
	}
#endif
}
