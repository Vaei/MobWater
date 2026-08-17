// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterInsightsTraceModule.h"
#include "MobWaterTracks.h"
#include "Modules/ModuleInterface.h"

/**
 * The Rewind Debugger's view of the water.
 *
 * Its own module rather than part of MobWaterEditor, because everything it touches is the Rewind
 * Debugger's - and a project that does not have GameplayInsights enabled should be short a track
 * rather than short an editor.
 */
class FMobWaterRewindModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

private:
	FMobWaterInsightsTraceModule TraceModule;

	MobWaterRewind::FMobWaterStateTrackCreator StateTrackCreator;
	MobWaterRewind::FMobWaterBodyTrackCreator BodyTrackCreator;
	MobWaterRewind::FMobWaterQueryTrackCreator QueryTrackCreator;
};
