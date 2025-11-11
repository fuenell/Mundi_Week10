#pragma once
#include "UIWindow.h"

class USkeletalMesh;
class USkeletalMeshViewportWidget;
class UBoneHierarchyWidget;
class UBoneDetailWidget;
class USkeletalMeshEditorLayoutWidget;

/**
 * @brief SkeletalMesh 에디터 윈도우
 *
 * SkeletalMesh를 3D 뷰포트에 표시하고,
 * Bone 계층 구조와 선택된 Bone의 상세 정보를 편집할 수 있는 통합 에디터 윈도우입니다.
 *
 * 레이아웃:
 * ┌─────────────────────────────┬─────────────────┐
 * │                             │                 │
 * │  SkeletalMesh Viewport      │  Bone           │
 * │  (3D Preview)               │  Hierarchy      │
 * │                             │  (우측 상단)     │
 * │                             │                 │
 * │                             ├─────────────────┤
 * │                             │                 │
 * │                             │  Bone           │
 * │                             │  Detail         │
 * │                             │  (우측 하단)     │
 * └─────────────────────────────┴─────────────────┘
 */
class USkeletalMeshEditorWindow : public UUIWindow
{
public:
	DECLARE_CLASS(USkeletalMeshEditorWindow, UUIWindow)

	USkeletalMeshEditorWindow();
	~USkeletalMeshEditorWindow() = default;

	// === Window Lifecycle ===

	/**
	 * 윈도우 초기화
	 * Widget들을 생성하고 레이아웃을 구성합니다.
	 */
	void Initialize() override;

	/**
	 * 윈도우 정리
	 */
	void Cleanup() override;

	// === SkeletalMesh 관리 ===

	/**
	 * 표시할 SkeletalMesh 설정
	 * @param InMesh - SkeletalMesh 객체
	 */
	void SetSkeletalMesh(USkeletalMesh* InMesh);

	/**
	 * 현재 표시 중인 SkeletalMesh 가져오기
	 * @return SkeletalMesh 객체 (nullptr 가능)
	 */
	USkeletalMesh* GetSkeletalMesh() const { return CurrentSkeletalMesh; }

private:
	/**
	 * Bone 선택 이벤트 처리
	 * BoneHierarchyWidget에서 Bone이 선택되면 호출됩니다.
	 * @param BoneIndex - 선택된 Bone 인덱스
	 */
	void OnBoneSelected(int32 BoneIndex);

private:
	// 현재 표시 중인 SkeletalMesh
	USkeletalMesh* CurrentSkeletalMesh = nullptr;

	// === Widget들 ===

	// 레이아웃 Widget (3패널 관리)
	USkeletalMeshEditorLayoutWidget* LayoutWidget = nullptr;

	// 3D Viewport Widget
	USkeletalMeshViewportWidget* ViewportWidget = nullptr;

	// Bone 계층 구조 Widget
	UBoneHierarchyWidget* HierarchyWidget = nullptr;

	// Bone 상세 정보 Widget
	UBoneDetailWidget* DetailWidget = nullptr;
};