// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
#include "Modules/ModuleManager.h"

class SWidget;

MOBWATEREDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogMobWaterEditor, Log, All);

class FMobWaterEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Everything needed to be able to drop a body of water in and have water.
	 *
	 * Generate leaves a project with assets and no idea that they exist: the settings still point at
	 * nothing, so a placed pool renders blank and looks broken rather than unconfigured. This does the
	 * generate, then the wiring, then refreshes whatever is already in the level.
	 */
	static void SetUpWater();

	/** Authors the collection, the master material and its instances. */
	static void GenerateMaterials();

	/** Points the settings at the generated assets and saves them. Returns what it could not find. */
	static TArray<FString> AssignGeneratedAssets();

	/**
	 * Writes a sky onto every water instance.
	 *
	 * A collection cannot hold a texture, so the one thing a project cannot change from a single
	 * place is which sky the water reflects. This is that single place.
	 */
	static void ApplyReflectionTexture(class UTexture* Texture);

	/** A toast, so a failure is seen rather than only logged. */
	static void Notify(const FText& Message, bool bSuccess);

	/**
	 * Opens the Rewind Debugger with the level's ocean already the thing being debugged.
	 *
	 * Recording is left alone deliberately. What the tracks are for is comparing two machines, so a
	 * record started by whoever opened the panel is a recording of one - and starting one here would
	 * throw away whatever had already been captured.
	 */
	static void DebugOceanInRewindDebugger();

	/** Whether the Rewind Debugger's tab is registered, which is whether the plugin is enabled. */
	static bool HasRewindDebugger();

private:
	void RegisterMenus();
	TSharedRef<SWidget> BuildMenu();

	/** Adds a category to the Place Actors panel, one entry per shape. */
	void RegisterPlacement();

	/** Drops a body of water in front of the perspective viewport and selects it. */
	static void PlaceWater(EMobWaterShape Shape);

	/** Where a placed body lands: in front of the camera, or at the origin with no viewport. */
	static FVector PlacementLocation();

	/** Asserts the contract the material and the component both claim to meet. */
	/**
	 * Solves a Phillips sea and bakes it.
	 *
	 * Its own entry rather than part of a generate, because it is minutes of arithmetic and nothing
	 * about authoring materials changes the sea. A generate only runs it when there is no sea at all.
	 */
	static void BakeSpectrum();

	static void VerifyContract();

	/** Instructions, permutations and texture memory, measured rather than claimed. */
	static void ReportCost();

	/** Opens the collection the water reads its waves and its clock out of. */
	static void OpenCollection();
	static bool HasCollection();

	/** Opens the material instance the selected body is drawing with, foam texture and all. */
	static void OpenSelectedBodyMaterial();
	static bool HasSelectedBody();

	/**
	 * Saves the selected body's look as a preset asset, wherever the dialog is pointed.
	 *
	 * Anywhere rather than the plugin's own folder: a look belongs to the project that tuned it, and
	 * a preset written inside MobWater is one a regenerate is entitled to overwrite.
	 */
	static void SaveLookPreset();

	/**
	 * Selects the level's ocean.
	 *
	 * An ocean is kept centred on the view and has no bank, so there is rarely anywhere to click that
	 * is not already something else. Without this it is only reachable through the outliner.
	 */
	static void SelectOcean();
	static bool HasOcean();

	/**
	 * Writes a settings object to the project's ini, checking it out first.
	 *
	 * Returns false if it could not be written, which the caller has to report - a settings write
	 * that fails quietly leaves an editor session where everything works and a next session where
	 * nothing does, and nothing in between says why.
	 */
	static bool SaveDefaultConfig(UObject* Settings);

	/**
	 * Whether Python is here.
	 *
	 * Asked separately from whether anything can be generated, because Python is only needed to
	 * generate: water whose material has already been authored runs without it.
	 */
	static bool IsPythonAvailable();

	/** Runs a snippet with the plugin's Python directory on the path, and toasts the outcome. */
	static bool RunPython(const FString& Snippet, const FText& DoneMessage);

	static bool IsToolbarMenuEnabled();
	static void HideToolbarMenu();
	static void OpenSettings();

	/** Surfaces the missing material the settings could not resolve, so it is not only in the log. */
	static void OnMaterialMissing(EMobWaterShape Shape);

	/** Held so a live coding reload rebinds rather than notifying twice. */
	FDelegateHandle MissingMaterialHandle;
};
