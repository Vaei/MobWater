// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MobWaterEditorUserSettings.generated.h"

/**
 * One developer's editor preferences.
 *
 * EditorPerProjectUserSettings rather than a checked-in config, so one person hiding the toolbar
 * menu never lands on anyone else.
 */
UCLASS(Config=EditorPerProjectUserSettings, meta=(DisplayName="Mob Water (Editor)"))
class MOBWATEREDITOR_API UMobWaterEditorUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Whether the Water button appears on the level editor toolbar. */
	UPROPERTY(EditAnywhere, Config, Category="Toolbar")
	bool bShowToolbarMenu = true;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
