// Copyright (c) Jared Taylor

#include "MobWaterExclusionVisualizer.h"

#include "MobWaterExclusionComponent.h"
#include "SceneManagement.h"

void FMobWaterExclusionVisualizer::DrawVisualization(const UActorComponent* Component,
	const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UMobWaterExclusionComponent* Exclusion = Cast<UMobWaterExclusionComponent>(Component);
	if (!Exclusion)
	{
		return;
	}

	const FTransform Transform = Exclusion->GetComponentTransform();
	const FVector Centre = Transform.GetLocation();

	// Yaw only. The volume is a plan view, so drawing its pitch and roll would suggest it has a top
	// and a bottom, which is exactly the thing about it people get wrong.
	const FRotator Yaw(0.f, Transform.Rotator().Yaw, 0.f);

	const float HalfX = FMath::Max(static_cast<float>(Exclusion->Extent.X), 1.f);
	const float HalfY = FMath::Max(static_cast<float>(Exclusion->Extent.Y), 1.f);

	// Faded by how much water it actually removes, so a volume turned down reads as turned down.
	const FLinearColor Colour = FLinearColor(0.15f, 0.75f, 1.f).CopyWithNewOpacity(1.f) * FMath::Max(Exclusion->Strength, 0.15f);

	const bool bRadial = Exclusion->Shape == EMobWaterExclusionShape::Disc
		|| Exclusion->Shape == EMobWaterExclusionShape::Sphere;

	auto Corner = [&](float X, float Y)
	{
		return Centre + Yaw.RotateVector(FVector(X, Y, 0.f));
	};

	if (bRadial)
	{
		const float Radius = FMath::Min(HalfX, HalfY);
		DrawCircle(PDI, Centre, FVector::ForwardVector, FVector::RightVector, Colour, Radius, 48,
			SDPG_Foreground, 2.f);

		// The inner ring is where the softness has run out and the water is fully gone. Two rings
		// rather than one, because the edge is a fade and a single ring says it is a wall.
		const float Inner = FMath::Max(Radius - FMath::Max(Exclusion->EdgeSoftness, 1.f), 1.f);
		DrawCircle(PDI, Centre, FVector::ForwardVector, FVector::RightVector, Colour * 0.5f, Inner, 32,
			SDPG_Foreground, 1.f);
	}
	else
	{
		const FVector A = Corner(-HalfX, -HalfY);
		const FVector B = Corner(HalfX, -HalfY);
		const FVector C = Corner(HalfX, HalfY);
		const FVector D = Corner(-HalfX, HalfY);

		PDI->DrawLine(A, B, Colour, SDPG_Foreground, 2.f);
		PDI->DrawLine(B, C, Colour, SDPG_Foreground, 2.f);
		PDI->DrawLine(C, D, Colour, SDPG_Foreground, 2.f);
		PDI->DrawLine(D, A, Colour, SDPG_Foreground, 2.f);

		const float Soft = FMath::Max(Exclusion->EdgeSoftness, 1.f);
		const float InnerX = FMath::Max(HalfX - Soft, 1.f);
		const float InnerY = FMath::Max(HalfY - Soft, 1.f);

		const FVector IA = Corner(-InnerX, -InnerY);
		const FVector IB = Corner(InnerX, -InnerY);
		const FVector IC = Corner(InnerX, InnerY);
		const FVector ID = Corner(-InnerX, InnerY);

		PDI->DrawLine(IA, IB, Colour * 0.5f, SDPG_Foreground, 1.f);
		PDI->DrawLine(IB, IC, Colour * 0.5f, SDPG_Foreground, 1.f);
		PDI->DrawLine(IC, ID, Colour * 0.5f, SDPG_Foreground, 1.f);
		PDI->DrawLine(ID, IA, Colour * 0.5f, SDPG_Foreground, 1.f);
	}
}
