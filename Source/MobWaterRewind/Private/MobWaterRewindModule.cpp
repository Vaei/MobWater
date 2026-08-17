// Copyright (c) Jared Taylor

#include "MobWaterRewindModule.h"

#include "Features/IModularFeatures.h"
#include "IRewindDebuggerTrackCreator.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/ModuleService.h"

void FMobWaterRewindModule::StartupModule()
{
	IModularFeatures& Features = IModularFeatures::Get();

	Features.RegisterModularFeature(TraceServices::ModuleFeatureName, &TraceModule);
	Features.RegisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &StateTrackCreator);
	Features.RegisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &BodyTrackCreator);
	Features.RegisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &QueryTrackCreator);
}

void FMobWaterRewindModule::ShutdownModule()
{
	IModularFeatures& Features = IModularFeatures::Get();

	Features.UnregisterModularFeature(TraceServices::ModuleFeatureName, &TraceModule);
	Features.UnregisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &StateTrackCreator);
	Features.UnregisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &BodyTrackCreator);
	Features.UnregisterModularFeature(
		RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &QueryTrackCreator);
}

IMPLEMENT_MODULE(FMobWaterRewindModule, MobWaterRewind);
