// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UActorFactory;

/**
 * A menu row that can be dragged into the level, the way the Place Actors panel's rows can.
 *
 * A plain menu entry only clicks. Dragging needs a widget that starts a drag operation the level
 * viewport understands, and the panel's own row widget is private to the PlacementMode module.
 */
class SMobWaterMenuEntry : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMobWaterMenuEntry) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, ToolTip)
	SLATE_END_ARGS()

	/**
	 * The factory decides what is actually spawned; the class is only what a content browser style
	 * drag carries. Both are given rather than derived, so one row serves every shape the menu grows.
	 */
	void Construct(const FArguments& InArgs, UActorFactory* InFactory, const FSlateBrush* InIcon,
		UClass* InActorClass);

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;

private:
	const FSlateBrush* GetBorder() const;

	const FSlateBrush* Icon = nullptr;
	TWeakObjectPtr<UClass> ActorClass;
	TWeakObjectPtr<UActorFactory> Factory;

	const FButtonStyle* Style = nullptr;
	bool bIsPressed = false;
};
