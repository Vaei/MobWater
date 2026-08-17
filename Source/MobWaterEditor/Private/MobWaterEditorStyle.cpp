// Copyright (c) Jared Taylor

#include "MobWaterEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FMobWaterEditorStyle::StyleSet;

FName FMobWaterEditorStyle::GetStyleSetName()
{
	static const FName StyleName(TEXT("MobWaterEditorStyle"));
	return StyleName;
}

const ISlateStyle& FMobWaterEditorStyle::Get()
{
	return *StyleSet;
}

void FMobWaterEditorStyle::Register()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobWater"));
	if (!Plugin.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	// 16 square is what a toolbar entry draws at; anything larger is downsampled every frame.
	StyleSet->Set(GetMenuIconName(),
		new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon64"), TEXT(".png")), FVector2D(16.f, 16.f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FMobWaterEditorStyle::Unregister()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}
