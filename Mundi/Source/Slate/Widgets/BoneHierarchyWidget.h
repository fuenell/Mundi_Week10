#pragma once
#include "Widget.h"
#include "Delegates.h"

class USkeletalMesh;
class USkeleton;

/**
 * @brief Bone 계층 구조를 트리 형태로 표시하는 위젯
 *
 * Skeletal Mesh의 Bone 계층 구조를 ImGui TreeNode를 사용하여 표시하고,
 * 사용자가 Bone을 선택할 수 있도록 합니다.
 */
class UBoneHierarchyWidget : public UWidget
{
public:
	DECLARE_CLASS(UBoneHierarchyWidget, UWidget)

	UBoneHierarchyWidget();
	~UBoneHierarchyWidget() override = default;

	// === Widget Lifecycle ===

	/**
	 * 위젯 초기화
	 */
	void Initialize() override;

	/**
	 * 위젯 렌더링
	 * ImGui를 사용하여 Bone 계층 구조를 트리 형태로 표시
	 */
	void RenderWidget() override;

	/**
	 * 위젯 업데이트
	 */
	void Update() override;

	// === SkeletalMesh 관리 ===

	/**
	 * 표시할 SkeletalMesh 설정
	 * @param InMesh - SkeletalMesh 객체
	 */
	void SetSkeletalMesh(USkeletalMesh* InMesh);

	/**
	 * 현재 설정된 SkeletalMesh 가져오기
	 * @return SkeletalMesh 객체 (nullptr 가능)
	 */
	USkeletalMesh* GetSkeletalMesh() const { return TargetSkeletalMesh; }

	// === Bone 선택 관리 ===

	/**
	 * 선택된 Bone 인덱스 가져오기
	 * @return Bone 인덱스 (-1인 경우 선택되지 않음)
	 */
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }

	/**
	 * Bone 선택
	 * @param BoneIndex - 선택할 Bone 인덱스
	 */
	void SetSelectedBoneIndex(int32 BoneIndex);

	/**
	 * Bone 선택 해제
	 */
	void ClearSelection();

	// === 델리게이트 ===

	/**
	 * Bone이 선택되었을 때 호출되는 델리게이트
	 * int32: 선택된 Bone 인덱스
	 */
	TDelegate<int32> OnBoneSelected;

private:
	/**
	 * Bone 트리를 재귀적으로 렌더링
	 * @param BoneIndex - 렌더링할 Bone 인덱스
	 * @param Skeleton - Skeleton 객체
	 */
	void RenderBoneTree(int32 BoneIndex, USkeleton* Skeleton);

	/**
	 * 특정 Bone이 자식 Bone을 가지고 있는지 확인
	 * @param BoneIndex - 확인할 Bone 인덱스
	 * @param Skeleton - Skeleton 객체
	 * @return 자식 Bone이 있으면 true
	 */
	bool HasChildren(int32 BoneIndex, USkeleton* Skeleton) const;

private:
	// 표시할 SkeletalMesh
	USkeletalMesh* TargetSkeletalMesh = nullptr;

	// 선택된 Bone 인덱스 (-1: 선택 없음)
	int32 SelectedBoneIndex = -1;

	// Bone 가시성 (향후 확장용)
	TArray<bool> BoneVisibility;
};
