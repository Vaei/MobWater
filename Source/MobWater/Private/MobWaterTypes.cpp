// Copyright (c) Jared Taylor

#include "MobWaterTypes.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

void MobWaterSprite::Apply(UBillboardComponent* Sprite)
{
	if (!Sprite)
	{
		return;
	}

	static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(
		TEXT("/MobWater/Textures/T_MobWaterSprite.T_MobWaterSprite"));

	if (UTexture2D* Texture = Icon.Get())
	{
		Sprite->SetSprite(Texture);
	}
}
#endif

const TCHAR* MobWaterShapeName(EMobWaterShape Shape)
{
	switch (Shape)
	{
	case EMobWaterShape::Box:		return TEXT("Box");
	case EMobWaterShape::Disc:		return TEXT("Disc");
	case EMobWaterShape::Spline:	return TEXT("Spline");
	case EMobWaterShape::Ocean:		return TEXT("Ocean");
	}

	return TEXT("Unknown");
}

FString MobWaterVariant::Suffix(int32 Variant)
{
	// Built in bit order so the name a generator writes and the name a lookup expects cannot disagree
	// about which feature comes first.
	FString Out;

	if (Variant & Ripples)
	{
		Out += TEXT("_Ripples");
	}
	if (Variant & Foam)
	{
		Out += TEXT("_Foam");
	}
	if (Variant & Refraction)
	{
		Out += TEXT("_Refraction");
	}
	if (Variant & FoamTexture)
	{
		Out += TEXT("_FoamTexture");
	}
	if (Variant & Gradient)
	{
		Out += TEXT("_Gradient");
	}

	return Out;
}

FString MobWaterFallVariant::Suffix(int32 Variant)
{
	FString Out;

	if (Variant & Foam)
	{
		Out += TEXT("_Foam");
	}
	if (Variant & Refraction)
	{
		Out += TEXT("_Refraction");
	}
	if (Variant & Gradient)
	{
		Out += TEXT("_Gradient");
	}

	return Out;
}

FString MobWaterUnderwaterVariant::Suffix(int32 Variant)
{
	FString Out;

	if (Variant & Caustics)
	{
		Out += TEXT("_Caustics");
	}
	if (Variant & Window)
	{
		Out += TEXT("_Window");
	}
	if (Variant & Capture)
	{
		Out += TEXT("_Capture");
	}

	return Out;
}
