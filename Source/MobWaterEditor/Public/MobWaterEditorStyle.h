// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FMobWaterEditorStyle
{
public:
	static FName GetStyleSetName();
	static const ISlateStyle& Get();

	static void Register();
	static void Unregister();

	static FName GetMenuIconName()
	{
		static const FName Name(TEXT("Mob.WaterMenuIcon"));
		return Name;
	}

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
