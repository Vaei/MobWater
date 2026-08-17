// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobWaterTypes.h"
#include "Components/SceneComponent.h"
#include "MobWaterExclusionComponent.generated.h"

/** How many exclusion volumes the surface evaluates. Matches MOB_WATER_EXCLUSIONS in the shader. */
#define MOB_WATER_EXCLUSION_SLOTS 4

/** How many mesh silhouettes the ripple field's spare channel carries. Matches the step material. */
#define MOB_WATER_MESH_EXCLUSION_SLOTS 4

/**
 * An area water is kept out of.
 *
 * Evaluated by the surface rather than stamped into the ripple field, deliberately: a hull moves, and
 * a field that follows the camera and fades at its border is the wrong place for something whose edge
 * has to stay exactly where the geometry is. Evaluated, it is arithmetic that cannot smear.
 *
 * Only so many fit, so the ones nearest the view win. That is the same trade the fog blockers in
 * MobLights make, for the same reason: the alternative is a texture, and a texture cannot hold an
 * edge sharp enough for a boat.
 *
 * Mesh is the one shape that is a texture, and it takes the other route: its outline is baked to a
 * mask and stamped into the field's spare channel, because no pair of numbers describes it.
 *
 * Being a plan view, it has no top and no bottom. Water is kept out of a footprint, not out of a
 * storey - a bridge over a lake does not hold water off its deck.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent, DisplayName="Mob Water Exclusion"))
class MOBWATER_API UMobWaterExclusionComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobWaterExclusionComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion")
	EMobWaterExclusionShape Shape = EMobWaterExclusionShape::Box;

	/** Half the volume's size, in world units. A disc or sphere uses X as its radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ForceUnits="cm", EditCondition="Shape != EMobWaterExclusionShape::Mesh"))
	FVector2D Extent = FVector2D(200.0, 400.0);

	/**
	 * The outline to cut, when the shape is a mesh.
	 *
	 * Unset, the first static mesh on this actor is used, which is what a hull with an exclusion
	 * component dropped on it wants. The outline is its plan view in its own space, so this component
	 * has to sit where the mesh sits for the two to line up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Exclusion", meta=(EditCondition="Shape == EMobWaterExclusionShape::Mesh"))
	TObjectPtr<class UStaticMesh> ExclusionMesh;

	/**
	 * How much water it removes. 1 takes it away entirely.
	 *
	 * Less than 1 thins it rather than clearing it, which is what a half-submerged grating wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Strength = 1.f;

	/** How far in from the edge the water fades out rather than stopping, in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ClampMin="0.0", ForceUnits="cm"))
	float EdgeSoftness = 20.f;

	/** Whether this also stops things being counted as submerged inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion")
	bool bBlocksSubmersion = true;

	/** How much water this volume removes at a world point, 0 outside and 1 fully excluded. */
	float GetExclusionAt(const FVector& Location) const;

	/** The two vectors the shader reads this volume as. */
	void PackForShader(FLinearColor& OutA, FLinearColor& OutB) const;

	/** The same, for a mesh, where the third slot carries the mask's scale instead of its shape. */
	void PackMeshForShader(FLinearColor& OutA, FLinearColor& OutB) const;

	/** Whether this one is carved by the field's mask rather than by an analytic slot. */
	UFUNCTION(BlueprintPure, Category="Exclusion")
	bool IsMesh() const { return Shape == EMobWaterExclusionShape::Mesh && Silhouette.Num() > 0; }

	/** The baked outline, uploaded once. Null until there is one, or off the game thread. */
	class UTexture2D* GetSilhouetteTexture() const;

	/** Half the footprint in world units, which for a mesh comes from the outline it was baked from. */
	UFUNCTION(BlueprintPure, Category="Exclusion")
	FVector2D GetWorldExtent() const;

	/**
	 * How far inside the outline the mask can measure, in world units.
	 *
	 * The mask holds distance rather than coverage, so a mesh edge softens over the same centimetres
	 * an analytic one does. Coverage alone cannot: it is one bit per texel, and the only edge it can
	 * make is however wide one texel happens to be, which changes with the size of the thing.
	 */
	float GetSilhouetteSpan() const;

#if WITH_EDITOR
	/** Rasterises the mesh's plan view into the mask, and works out the footprint it covers. */
	void BuildSilhouette();

	/**
	 * The same, on demand.
	 *
	 * A mesh edited after its outline was baked is the case this exists for: nothing about editing a
	 * mesh touches the components that cut with it, so the outline would stay whatever the mesh used
	 * to be until something happened to re-register the component.
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category="Exclusion")
	void RebuildSilhouette();
#endif

protected:
	/** Distance inside the outline, 0 at the edge and 255 at the deepest point the span reaches. */
	UPROPERTY()
	TArray<uint8> Silhouette;

	/** How many texels a side the mask is. */
	UPROPERTY()
	int32 SilhouetteSize = 0;

	/** Half the mesh's plan footprint in its own space, before this component's scale. */
	UPROPERTY()
	FVector2D SilhouetteExtent = FVector2D::ZeroVector;

	/** The mesh the outline was baked from, so a swapped mesh rebuilds rather than drawing the old one. */
	UPROPERTY()
	TObjectPtr<class UStaticMesh> SilhouetteSource;

	/** Uploaded from Silhouette on demand. Transient, because the bytes above are what is saved. */
	UPROPERTY(Transient)
	mutable TObjectPtr<class UTexture2D> SilhouetteTexture;

	/** Bilinear, in the mask's own 0..1 frame, matching the sampler the field reads it with. */
	float SampleSilhouette(const FVector2D& UV) const;

#if WITH_EDITOR
	/** The mesh this component actually cuts with, which is ExclusionMesh or the owner's own. */
	class UStaticMesh* ResolveMesh() const;
#endif
};
