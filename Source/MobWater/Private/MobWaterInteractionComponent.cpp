// Copyright (c) Jared Taylor

#include "MobWaterInteractionComponent.h"

#include "MobWaterDisturbanceComponent.h"
#include "MobWaterModule.h"
#include "MobWaterStatics.h"
#include "MobWaterSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

UMobWaterInteractionComponent::UMobWaterInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMobWaterInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bMakeRipples && GetOwner())
	{
		Disturbance = NewObject<UMobWaterDisturbanceComponent>(GetOwner(), TEXT("MobWaterDisturbance"));
		if (Disturbance)
		{
			Disturbance->Radius = RippleRadius;
			Disturbance->Strength = RippleStrength;
			Disturbance->SetupAttachment(GetOwner()->GetRootComponent());
			Disturbance->RegisterComponent();

			// Off until the owner is actually in water. A disturber standing on dry land still
			// stamps the field, and the ripples appear in whatever water happens to be nearby.
			Disturbance->SetActive(false);
		}
	}
}

FVector UMobWaterInteractionComponent::GetMeasureLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	return Owner->GetActorLocation() + Owner->GetActorRotation().RotateVector(MeasureOffset);
}

void UMobWaterInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector Measure = GetMeasureLocation();

	FMobWaterInfo Info;
	const bool bFound = UMobWaterStatics::GetWaterInfoAtLocation(this, Measure, Info);

	// Below the surface as well as inside the body's footprint. A pool on a balcony is not water the
	// character standing under the balcony is in.
	const bool bNowInWater = bFound && Info.ImmersionDepth > 0.f;

	LastInfo = Info;

	if (bNowInWater != bInWater)
	{
		const float Speed = FMath::Abs(Owner->GetVelocity().Z);
		const FVector At(Measure.X, Measure.Y, bFound ? Info.SurfaceZ : Measure.Z);

		bInWater = bNowInWater;

		if (bInWater)
		{
			OnEnteredWater.Broadcast(At, Speed);
		}
		else
		{
			OnExitedWater.Broadcast(At, Speed);
		}

		// Scaled by how fast it crossed, so stepping in is a ring and falling in is a column.
		Splash(At, FMath::GetMappedRangeValueClamped(
			FVector2f(SplashSpeedRange), FVector2f(SplashStrengthRange), Speed));
	}

	if (Disturbance)
	{
		Disturbance->SetActive(bInWater);
	}

	TickWadeSplash(Measure, Info, bFound);

	if (!bDriveMovement)
	{
		return;
	}

	const bool bShouldSwim = bInWater && Info.ImmersionDepth >= SwimDepth;
	if (bShouldSwim != bSwimming)
	{
		bSwimming = bShouldSwim;
		ApplySwimming(bSwimming);
	}

	if (!bSwimming)
	{
		const float Range = FMath::Max(SwimDepth - WadeDepth, 1.f);
		const float Immersion = bInWater
			? FMath::Clamp((Info.ImmersionDepth - WadeDepth) / Range, 0.f, 1.f)
			: 0.f;

		ApplyWade(Immersion);
	}
}

void UMobWaterInteractionComponent::TickWadeSplash(const FVector& Measure, const FMobWaterInfo& Info, bool bFound)
{
	const AActor* Owner = GetOwner();

	if (!bInWater || bSwimming || WadeSplashDistance <= 0.f || !Owner)
	{
		bHasWadeMark = false;
		return;
	}

	const FVector Here(Measure.X, Measure.Y, 0.0);

	if (!bHasWadeMark)
	{
		WadeMark = Here;
		bHasWadeMark = true;
		return;
	}

	if (FVector::DistSquared2D(Here, WadeMark) < FMath::Square(WadeSplashDistance))
	{
		return;
	}

	WadeMark = Here;

	// Scaled by how deep it is as well as how fast. Ankle deep is a scuff and thigh deep is a shove,
	// and a splash that ignores the difference reads as the same puddle at every depth.
	const float Immersion = FMath::Clamp(Info.ImmersionDepth / FMath::Max(SwimDepth, 1.f), 0.f, 1.f);
	const float Speed = static_cast<float>(Owner->GetVelocity().Size2D());

	const float Strength = FMath::GetMappedRangeValueClamped(
		FVector2f(SplashSpeedRange), FVector2f(SplashStrengthRange), Speed) * Immersion;

	Splash(FVector(Measure.X, Measure.Y, bFound ? Info.SurfaceZ : Measure.Z), Strength);
}

void UMobWaterInteractionComponent::Splash(const FVector& Location, float Strength)
{
	UE_LOG(LogMobWater, Verbose, TEXT("Splash by %s at %s, strength %.2f"),
		*GetNameSafe(GetOwner()), *Location.ToCompactString(), Strength);

	OnSplash.Broadcast(Location, Strength);

	// The push is negative: something entering water pulls the surface down before it comes back,
	// and a positive impulse makes a mound rise to meet the thing that landed in it.
	UMobWaterSubsystem::AddRipple(this, Location, RippleRadius * SplashRadiusScale, -Strength);
}

void UMobWaterInteractionComponent::ApplyWade_Implementation(float Immersion01)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	if (!bCapturedWalkSpeed)
	{
		OriginalMaxWalkSpeed = Movement->MaxWalkSpeed;
		bCapturedWalkSpeed = true;
	}

	Movement->MaxWalkSpeed = OriginalMaxWalkSpeed * FMath::Lerp(1.f, WadeSpeedScale, Immersion01);
}

void UMobWaterInteractionComponent::ApplySwimming_Implementation(bool bShouldSwim)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	if (bShouldSwim)
	{
		Movement->SetMovementMode(MOVE_Swimming);
		return;
	}

	// Falling rather than walking: leaving the water upwards is a jump out, and putting the character
	// straight into walking here plants it in mid air until the next ground check.
	if (Movement->MovementMode == MOVE_Swimming)
	{
		Movement->SetMovementMode(MOVE_Falling);
	}

	if (bCapturedWalkSpeed)
	{
		Movement->MaxWalkSpeed = OriginalMaxWalkSpeed;
	}
}
