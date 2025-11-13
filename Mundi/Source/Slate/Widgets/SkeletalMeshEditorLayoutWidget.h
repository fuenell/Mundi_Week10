#pragma once
#include "Widget.h"

class USkeletalMeshViewportWidget;
class UBoneHierarchyWidget;
class UBoneDetailWidget;

/**
 * @brief SkeletalMesh Editor의 3패널 레이아웃 위젯
 *
 * 좌측: Viewport (70%)
 * 우측 상단: Bone Hierarchy (60%)
 * 우측 하단: Bone Detail (40%)
 */
class USkeletalMeshEditorLayoutWidget : public UWidget
{
public:
	DECLARE_CLASS(USkeletalMeshEditorLayoutWidget, UWidget)

	USkeletalMeshEditorLayoutWidget() = default;
	virtual ~USkeletalMeshEditorLayoutWidget() = default;

	void RenderWidget() override;
	void Update(float DeltaTime) override;

	// Widget 설정
	void SetViewportWidget(USkeletalMeshViewportWidget* InWidget) { ViewportWidget = InWidget; }
	void SetHierarchyWidget(UBoneHierarchyWidget* InWidget) { HierarchyWidget = InWidget; }
	void SetDetailWidget(UBoneDetailWidget* InWidget) { DetailWidget = InWidget; }

private:
	USkeletalMeshViewportWidget* ViewportWidget = nullptr;
	UBoneHierarchyWidget* HierarchyWidget = nullptr;
	UBoneDetailWidget* DetailWidget = nullptr;

	// 레이아웃 비율
	float LeftRightSplitRatio = 0.7f;
	float RightTopBottomSplitRatio = 0.6f;
};
