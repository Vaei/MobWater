// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterExclusionComponent.h"
#include "MobWaterTimeSource.h"
#include "MobWaterWaves.h"
#include "Subsystems/WorldSubsystem.h"
#include "MobWaterSubsystem.generated.h"

class UMaterialParameterCollection;
class UMobWaterSpectrum;
class UMobWaterWavePreset;

/**
 * Everything about the water that is not one body's business.
 *
 * Chiefly the clock. Water time is the one value that has to be the same on every machine, and it is
 * the same value that reaches the material and the physics query, because the moment those two read
 * different clocks the buoyancy sits slightly off the surface it is drawn on and no amount of tuning
 * the waves will close it.
 */
UCLASS()
class MOBWATER_API UMobWaterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMobWaterSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin UWorldSubsystem Interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UWorldSubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Waves dragged around in the viewport have to move, not only played ones. */
	virtual bool IsTickableInEditor() const override { return true; }
	//~ End FTickableGameObject Interface

	/**
	 * Where water time comes from.
	 *
	 * Bind a project's own synchronised clock. Left unbound this uses the game state's server world
	 * time, which is corrected in steps - and a step in the clock is a step in every wave at once.
	 */
	void SetTimeSource(const FMobWaterTimeSource& InSource) { TimeSource = InSource; }
	void ClearTimeSource() { TimeSource.Unbind(); }
	bool HasTimeSource() const { return TimeSource.IsBound(); }

	/**
	 * The instant every wave is evaluated at, folded into the loop period.
	 *
	 * Folded rather than raw because a session long enough leaves a float no fraction to work with,
	 * and the waves would quantise into steps while nothing about the clock looked wrong.
	 */
	UFUNCTION(BlueprintPure, Category="Water")
	float GetWaterTime() const { return WaterTime; }

	/** The unfolded value, for anything that has to reason about the clock rather than the waves. */
	double GetRawWaterTime() const { return RawWaterTime; }

	/**
	 * The waves this world uses where no body says otherwise.
	 *
	 * Bodies of water carry their own from Phase 3 on; until then this is what the query answers from.
	 */
	const FMobWaterWaveParams& GetDefaultWaves() const { return DefaultWaves; }

	UFUNCTION(BlueprintCallable, Category="Water")
	void SetDefaultWavePreset(UMobWaterWavePreset* Preset);

	/** Writes the wave set and the clock into the collection every water material reads. */
	void PublishWaves(const FMobWaterWaveParams& Params) const;

	/**
	 * Tells every ocean material how the baked sea state is laid out.
	 *
	 * One at a time for a whole world, because a collection is one set of values and the geometry has
	 * to reach the vertex shader somehow. Two oceans on two different spectra is not a case this
	 * supports, and the second one to register says so in the log rather than quietly drawing the
	 * first one's sea at its own scale.
	 */
	void SetSpectrum(const UMobWaterSpectrum* InSpectrum);

	const UMobWaterSpectrum* GetSpectrum() const { return Spectrum.Get(); }

	void PublishSpectrum() const;

	/**
	 * How bright the reflected sky is, and which way it is turned.
	 *
	 * Rotation is in turns, so lining a sky up with a sun is a fraction rather than a multiple of pi.
	 *
	 * Call this wherever the level already tells its backdrop which sky it is using - the water is
	 * reflecting the same sky and has no way to find that out for itself. The texture is a material
	 * parameter rather than a collection one, because a collection cannot hold a texture; the
	 * settings carry it and Set Up Water writes it into the instances.
	 */
	UFUNCTION(BlueprintCallable, Category="Water")
	void SetReflection(float Intensity, float Rotation);

	void RegisterBody(class UMobWaterComponent* Body);
	void UnregisterBody(class UMobWaterComponent* Body);

	/**
	 * The body of water over a point, or null.
	 *
	 * The deepest one wins where two overlap, because that is the one something falling in would
	 * end up in - a shallow trough drawn across a lake is decoration on it, not a separate pool.
	 */
	class UMobWaterComponent* FindBodyAt(const FVector& Location) const;

	int32 GetBodyCount() const { return Bodies.Num(); }

	void RegisterExclusion(class UMobWaterExclusionComponent* Exclusion);
	void UnregisterExclusion(class UMobWaterExclusionComponent* Exclusion);

	/**
	 * How much water is excluded at a point, 0 none and 1 all of it.
	 *
	 * Answered from every registered volume, not only the four the shader can see, so a query and a
	 * pixel can disagree about a distant hull - the query is right and the pixel is out of slots.
	 */
	UFUNCTION(BlueprintPure, Category="Water", meta=(WorldContext="WorldContextObject"))
	static float GetExclusionAtLocation(const UObject* WorldContextObject, const FVector& Location);

	/** The same, without the lookup, for the query path that already has the subsystem in hand. */
	float GetExclusionAt(const FVector& Location) const;

	void RegisterDisturber(class UMobWaterDisturbanceComponent* Disturber);
	void UnregisterDisturber(class UMobWaterDisturbanceComponent* Disturber);

	/** Empties the ripple field back to still. */
	UFUNCTION(BlueprintCallable, Category="Water")
	void ClearRipples();

	/**
	 * Pushes the surface at a point, once.
	 *
	 * For anything that happens rather than anything that stands there - an impact, a footfall, a
	 * thrown stone. A disturbance component is for something with a position that persists.
	 */
	UFUNCTION(BlueprintCallable, Category="Water", meta=(WorldContext="WorldContextObject"))
	static void AddRipple(const UObject* WorldContextObject, const FVector& Location, float Radius, float Strength);

	/** Where the ripple field is centred, and how wide it is. It reaches half of that. */
	bool GetRippleField(FVector2D& OutOrigin, float& OutExtent) const;

	/**
	 * Where the water is being looked at from, and which way along the ground.
	 *
	 * The forward is zero when there is no player controller, which is an editor viewport with nothing
	 * playing - the renderer reports where it drew from and not which way it was facing.
	 */
	bool GetViewLocation(FVector& OutLocation, FVector* OutForward = nullptr) const;

	/** How many times this has ticked. A subsystem that exists and one that runs look identical. */
	uint64 GetTickCount() const { return TickCount; }

	/** How many disturbers were stamped into the field on the last frame that drew it. */
	int32 GetLastStampCount() const { return LastStampCount; }

	/** What is published right now. Read by mob.Water.Debug. */
	void DumpState() const;

protected:
	UMaterialParameterCollection* GetCollection() const;

	void TickClock(float DeltaTime);

	/**
	 * Keeps the surface's glint pointed at whatever the level lights it with.
	 *
	 * This renderer has its local lights turned off and no sky atmosphere, so a lit translucent
	 * surface has almost nothing to reflect and reads as flat dark glass. The glint is most of what
	 * puts the light back, and it needs to know where the sun is to do it.
	 */
	void TickSun();

	/** Advances the wave equation, scrolls it to the view, and stamps whatever is standing in it. */
	void TickRipples(float DeltaTime);

	/**
	 * Gives every local player's camera the plane it looks through when it goes under.
	 *
	 * Done here rather than left to the project because a camera under water with nothing in front of
	 * it does not read as a missing feature - it reads as the water being broken from below.
	 */
	void TickUnderwater();

	/**
	 * Copies the stepped field back into the history with every push waiting on it added.
	 *
	 * Returns whether it drew. False means there was nothing to stamp and the plain copy still has to
	 * happen, because the history is what the next step reads and a step that reads a stale one runs
	 * the wave equation backwards over a frame it has already been through.
	 */
	bool StampDisturbers(class UTextureRenderTarget2D* Source, class UTextureRenderTarget2D* Into, float DeltaTime);

	void SetScalar(FName Name, float Value) const;
	void SetVector(FName Name, const FLinearColor& Value) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<class UMobWaterComponent>> Bodies;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<class UMobWaterDisturbanceComponent>> Disturbers;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<class UMobWaterExclusionComponent>> Exclusions;

	/**
	 * Publishes the volumes water is kept out of, nearest the view first.
	 *
	 * Evaluated rather than stamped because an exclusion is geometry: its edge has to be exactly
	 * where the hull is, and a field that follows the camera and fades at its border cannot hold one.
	 */
	void PublishExclusions();

	/**
	 * Draws the baked mesh outlines nearest the view into their own target.
	 *
	 * Separate from the analytic publish because these are textures, and a texture cannot go in a
	 * parameter collection. Separate from the ripple field because a still pool with a hull in it is
	 * exactly the case that would otherwise lose its hole, ripples being off.
	 */
	void TickMeshExclusions();

	/** Points the outline material at the outlines nearest the view. Answers how many it found. */
	int32 PublishMeshExclusions(class UMaterialInstanceDynamic* Material, const FVector2D& Origin,
		float Extent) const;

	/** Pushes queued by AddRipple, drawn on the next field tick. Location, radius, strength. */
	TArray<FVector4> PendingRipples;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> StepMaterial;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> CopyMaterial;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> StampMaterial;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> ExclusionMaterial;

	/** Where the outline window is centred, snapped to its own texels. */
	FVector2D ExclusionOrigin = FVector2D::ZeroVector;

	/** Whether anything is in the outline target, so a level that loses its last hull clears it once. */
	bool bExclusionFieldDrawn = false;

	/** Where the field is centred, snapped to its own texels. */
	FVector2D FieldOrigin = FVector2D::ZeroVector;
	bool bFieldValid = false;

	/** Time owed to the field, which is stepped at a fixed rate rather than once a frame. */
	float RippleAccumulator = 0.f;

	int32 LastStampCount = -1;

	/** Said once. There are more volumes than slots on every frame, not only the first. */
	bool bWarnedExclusionOverflow = false;

	/** Said once, for the same reason. */
	bool bWarnedStampOverflow = false;

	/** The light being followed. Re-found when it goes away, which is what a level transition is. */
	UPROPERTY(Transient)
	TWeakObjectPtr<class ADirectionalLight> SunLight;

	FVector LastSunDirection = FVector::ZeroVector;
	FLinearColor LastSunColor = FLinearColor::Transparent;

	/** Bound by the project. Unbound means the game state's clock, or local time outside a game. */
	FMobWaterTimeSource TimeSource;

	/** Folded into the loop period, and the only time anything downstream is allowed to read. */
	float WaterTime = 0.f;

	/** What the source last said, before folding. */
	double RawWaterTime = 0.0;

	/**
	 * Accumulated locally, and used only when there is no source and no game state.
	 *
	 * That is the editor viewport, where there is nothing to be in sync with and the water still has
	 * to move.
	 */
	double LocalTime = 0.0;

	UPROPERTY(Transient)
	FMobWaterWaveParams DefaultWaves;

	/** Set when the wave set changes, so the seventeen vector writes happen then rather than always. */
	bool bWavesDirty = true;

	/** The baked sea state this world's oceans are on. */
	TWeakObjectPtr<const UMobWaterSpectrum> Spectrum;

	bool bSpectrumDirty = true;

	/** Said once. A second ocean on a second sea state is wrong on every frame, not only the first. */
	bool bWarnedSpectrumConflict = false;

	uint64 TickCount = 0;
};
