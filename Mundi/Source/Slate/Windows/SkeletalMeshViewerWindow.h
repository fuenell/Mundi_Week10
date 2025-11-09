#pragma once
#include "UIWindow.h"
#include "UEContainer.h"
#include <DirectXMath.h>

class USkeletalMesh;
struct FSkeleton;
struct FBoneInfo;

/**
 * Skeletal Mesh Viewer Window
 * - View and edit multiple skeletal mesh resources in the editor
 * - Display bone hierarchy tree
 * - Render gizmos for selected bones
 */
class USkeletalMeshViewerWindow : public UUIWindow
{
public:
	DECLARE_CLASS(USkeletalMeshViewerWindow, UUIWindow)

	USkeletalMeshViewerWindow();
	virtual void Initialize() override;

	// Skeletal Mesh 관리
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetCurrentSkeletalMesh() const { return CurrentSkeletalMesh; }

	// 본 선택
	void SelectBone(int32 BoneIndex);
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }

	// Gizmo 렌더링 (SceneRenderer와 연동하여 3D View에 그리기)
	void RenderBoneGizmosInScene();

protected:
	void RenderContent();

private:
	void RenderBoneHierarchyTree();
	void RenderBoneDetails();
	void RenderBoneTransformGizmo();
	void RenderSkeletalMeshSelector();

	// 재귀적으로 본 트리를 렌더링
	void RenderBoneTreeNode(int32 BoneIndex, const FSkeleton& Skeleton);

	// SkeletalMesh 목록 캐싱
	void CacheSkeletalMeshList();

private:
	// 현재 로드된 스켈레탈 메시
	USkeletalMesh* CurrentSkeletalMesh = nullptr;

	// 선택된 본 인덱스 (-1은 선택 없음)
	int32 SelectedBoneIndex = -1;

	// UI 상태
	bool bShowBoneNames = true;
	bool bShowGizmo = true;

	// Gizmo 설정
	float GizmoScale = 10.0f;
	bool bShowBoneAxes = true;

	// SkeletalMesh 리스트 캐시
	TArray<FString> CachedSkeletalMeshPaths;
	TArray<const char*> CachedSkeletalMeshItems;
	int32 SelectedMeshIndex = -1;
};
