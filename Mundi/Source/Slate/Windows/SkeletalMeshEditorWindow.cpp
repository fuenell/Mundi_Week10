#include "pch.h"
#include "SkeletalMeshEditorWindow.h"
#include "Widgets/SkeletalMeshViewportWidget.h"
#include "Widgets/BoneHierarchyWidget.h"
#include "Widgets/BoneDetailWidget.h"
#include "Widgets/SkeletalMeshEditorLayoutWidget.h"
#include "SkeletalMesh.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(USkeletalMeshEditorWindow)

USkeletalMeshEditorWindow::USkeletalMeshEditorWindow()
	: UUIWindow()
	, CurrentSkeletalMesh(nullptr)
	, LayoutWidget(nullptr)
	, ViewportWidget(nullptr)
	, HierarchyWidget(nullptr)
	, DetailWidget(nullptr)
{
	// 윈도우 기본 설정
	FUIWindowConfig Config;
	Config.WindowTitle = "Skeletal Mesh Viewer";
	Config.DefaultSize = ImVec2(1280, 720);
	Config.MinSize = ImVec2(800, 600);
	Config.MaxSize = ImVec2(1920, 1080);
	Config.bResizable = true;
	Config.bMovable = true;
	Config.bCollapsible = true;
	Config.InitialState = EUIWindowState::Visible;
	Config.UpdateWindowFlags();

	SetConfig(Config);
}

void USkeletalMeshEditorWindow::Initialize()
{
	Super::Initialize();

	// Viewport Widget 생성 (좌측 - 3D Preview)
	ViewportWidget = NewObject<USkeletalMeshViewportWidget>();
	if (ViewportWidget != nullptr)
	{
		ViewportWidget->Initialize();

		ViewportWidget->SetSkeletalMeshEditorWindow(this);
	}

	// Bone Hierarchy Widget 생성 (우측 상단 - Bone Tree)
	HierarchyWidget = NewObject<UBoneHierarchyWidget>();
	if (HierarchyWidget != nullptr)
	{
		HierarchyWidget->Initialize();

		HierarchyWidget->SetSkeletalMeshEditorWindow(this);
	}

	// Bone Detail Widget 생성 (우측 하단 - Transform Editor)
	DetailWidget = NewObject<UBoneDetailWidget>();
	if (DetailWidget != nullptr)
	{
		DetailWidget->Initialize();

		DetailWidget->SetSkeletalMeshEditorWindow(this);
	}

	// Layout Widget 생성 및 설정
	LayoutWidget = NewObject<USkeletalMeshEditorLayoutWidget>();
	if (LayoutWidget != nullptr)
	{
		LayoutWidget->SetViewportWidget(ViewportWidget);
		LayoutWidget->SetHierarchyWidget(HierarchyWidget);
		LayoutWidget->SetDetailWidget(DetailWidget);

		// Layout Widget을 UIWindow에 추가
		AddWidget(LayoutWidget);
	}

	// Bone 선택 델리게이트 연결
	OnBoneSelected.AddDynamic(this, &USkeletalMeshEditorWindow::OnBoneSelectedEvent);

	OnBoneUpdated.AddDynamic(this, &USkeletalMeshEditorWindow::OnBoneUpdatedEvent);
}

void USkeletalMeshEditorWindow::Cleanup()
{
	Super::Cleanup();

	// Widget 정리
	if (ViewportWidget != nullptr)
	{
		DeleteObject(ViewportWidget);
		ViewportWidget = nullptr;
	}

	if (HierarchyWidget != nullptr)
	{
		DeleteObject(HierarchyWidget);
		HierarchyWidget = nullptr;
	}

	if (DetailWidget != nullptr)
	{
		DeleteObject(DetailWidget);
		DetailWidget = nullptr;
	}
}

void USkeletalMeshEditorWindow::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	CurrentSkeletalMesh = InMesh;

	// 모든 위젯에 SkeletalMesh 설정
	if (ViewportWidget != nullptr)
	{
		ViewportWidget->SetSkeletalMesh(InMesh);
	}

	if (HierarchyWidget != nullptr)
	{
		HierarchyWidget->SetSkeletalMesh(InMesh);
	}

	if (DetailWidget != nullptr)
	{
		DetailWidget->SetSkeletalMesh(InMesh);
	}

	OnBoneSelected.Broadcast(-1);
}

void USkeletalMeshEditorWindow::OnBoneSelectedEvent(int32 BoneIndex)
{
	// DetailWidget에 선택된 Bone 정보 전달
	if (DetailWidget != nullptr)
	{
		DetailWidget->SetSelectedBone(BoneIndex);
	}

	if (ViewportWidget)
	{
		ViewportWidget->SetSelectedBone(BoneIndex);
	}

	if (HierarchyWidget != nullptr)
	{
		HierarchyWidget->SetSelectedBoneIndex(BoneIndex);
	}
}

void USkeletalMeshEditorWindow::OnBoneUpdatedEvent(int32 BoneIndex)
{
	if (ViewportWidget)
	{
		ViewportWidget->UpdateBone(BoneIndex);
	}
	if (DetailWidget)
	{
		DetailWidget->UpdateBone(BoneIndex);
	}
}
