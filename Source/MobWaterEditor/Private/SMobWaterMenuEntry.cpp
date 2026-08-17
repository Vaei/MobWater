// Copyright (c) Jared Taylor

#include "SMobWaterMenuEntry.h"

#include "ActorFactories/ActorFactory.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorActions.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobWaterEditor"

void SMobWaterMenuEntry::Construct(const FArguments& InArgs, UActorFactory* InFactory,
	const FSlateBrush* InIcon, UClass* InActorClass)
{
	Icon = InIcon;
	ActorClass = InActorClass;
	Factory = InFactory;
	Style = &FAppStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Menu.Button"));

	const float IconSize = FAppStyle::Get().GetFloat(TEXT("Menu.MenuIconSize"));

	ChildSlot
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SBorder)
		.BorderImage(this, &SMobWaterMenuEntry::GetBorder)
		.Cursor(EMouseCursor::GrabHand)
		.ToolTipText(InArgs._ToolTip)
		.Padding(FMargin(10.f, 3.f, 5.f, 3.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(14.f, 0.f, 10.f, 0.f)
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(IconSize)
				.HeightOverride(IconSize)
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text(InArgs._Label)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Image(FAppStyle::Get().GetBrush(TEXT("Icons.DragHandle")))
			]
		]
	];
}

const FSlateBrush* SMobWaterMenuEntry::GetBorder() const
{
	if (bIsPressed)
	{
		return &Style->Pressed;
	}
	return IsHovered() ? &Style->Hovered : &Style->Normal;
}

FReply SMobWaterMenuEntry::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bIsPressed = true;
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMobWaterMenuEntry::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	bIsPressed = false;

	{
		// The spawn and the move are one transaction, so undo does not leave a pool at the origin.
		const FScopedTransaction Transaction(LOCTEXT("PlaceWaterTransaction", "Place Water"));

		AActor* NewActor = FLevelEditorActionCallbacks::AddActor(
			Factory.Get(), FAssetData(ActorClass.Get()), nullptr);

		if (NewActor && GCurrentLevelEditingViewportClient)
		{
			GEditor->MoveActorInFrontOfCamera(*NewActor,
				GCurrentLevelEditingViewportClient->GetViewLocation(),
				GCurrentLevelEditingViewportClient->GetViewRotation().Vector());
		}
	}

	if (!MouseEvent.IsControlDown())
	{
		FSlateApplication::Get().DismissAllMenus();
	}

	return FReply::Handled();
}

FReply SMobWaterMenuEntry::OnDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	bIsPressed = false;

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		return FReply::Handled();
	}

	const FAssetData Asset(ActorClass.Get());

	// Content browser style drags are routed through this when something is listening, and the
	// viewport's own preview only follows the drag it started itself.
	if (FEditorDelegates::OnAssetDragStarted.IsBound())
	{
		TArray<FAssetData> Dragged;
		Dragged.Add(Asset);
		FEditorDelegates::OnAssetDragStarted.Broadcast(Dragged, Factory.Get());
		return FReply::Handled();
	}

	return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(Asset, Factory.Get()));
}

#undef LOCTEXT_NAMESPACE
