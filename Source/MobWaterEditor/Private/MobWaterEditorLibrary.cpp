// Copyright (c) Jared Taylor

#include "MobWaterEditorLibrary.h"

#include "AssetCompilingManager.h"
#include "MobWaterEditor.h"

void UMobWaterEditorLibrary::FinishAssetCompilation()
{
	FAssetCompilingManager::Get().FinishAllCompilation();
}

bool UMobWaterEditorLibrary::HasRewindDebugger()
{
	return FMobWaterEditorModule::HasRewindDebugger();
}

void UMobWaterEditorLibrary::DebugOceanInRewindDebugger()
{
	FMobWaterEditorModule::DebugOceanInRewindDebugger();
}
