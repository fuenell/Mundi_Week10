#include "pch.h"
#include "BoneHierarchyWidget.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "ImGui/imgui.h"
#include "Windows/SkeletalMeshEditorWindow.h"

IMPLEMENT_CLASS(UBoneHierarchyWidget)

UBoneHierarchyWidget::UBoneHierarchyWidget()
	: UWidget("BoneHierarchy")
	, TargetSkeletalMesh(nullptr)
	, SelectedBoneIndex(-1)
{
}

void UBoneHierarchyWidget::Initialize()
{
	Super::Initialize();

	// 초기 상태 설정
	SelectedBoneIndex = -1;
	BoneVisibility.clear();
}

void UBoneHierarchyWidget::RenderWidget()
{
	Super::RenderWidget();

	// SkeletalMesh가 없으면 메시지 표시
	if (TargetSkeletalMesh == nullptr)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No SkeletalMesh Loaded");
		return;
	}

	// Skeleton 가져오기
	USkeleton* Skeleton = TargetSkeletalMesh->GetSkeleton();
	if (Skeleton == nullptr)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No Skeleton Data");
		return;
	}

	// Bone이 없으면 메시지 표시
	int32 BoneCount = Skeleton->GetBoneCount();
	if (BoneCount == 0)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No Bones in Skeleton");
		return;
	}

	// 헤더 정보 표시
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();
	ImGui::Text("Total Bones: %d", BoneCount);
	ImGui::Separator();

	// 스크롤 가능한 영역 시작
	ImGui::BeginChild("BoneTreeScrollRegion", ImVec2(0, 0), false);

	// Root Bone부터 재귀적으로 렌더링
	int32 RootBoneIndex = Skeleton->GetRootBoneIndex();
	if (RootBoneIndex >= 0)
	{
		RenderBoneTree(RootBoneIndex, Skeleton);
	}
	else
	{
		// Root Bone이 없는 경우, 모든 Bone을 순회하며 ParentIndex가 -1인 Bone들을 찾음
		for (int32 i = 0; i < BoneCount; ++i)
		{
			const FBoneInfo& BoneInfo = Skeleton->GetBone(i);
			if (BoneInfo.ParentIndex == -1)
			{
				RenderBoneTree(i, Skeleton);
			}
		}
	}

	ImGui::EndChild();
}

void UBoneHierarchyWidget::Update()
{
	Super::Update();
}

void UBoneHierarchyWidget::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	TargetSkeletalMesh = InMesh;

	// Bone 가시성 배열 초기화
	if (TargetSkeletalMesh != nullptr && TargetSkeletalMesh->GetSkeleton() != nullptr)
	{
		int32 BoneCount = TargetSkeletalMesh->GetSkeleton()->GetBoneCount();
		BoneVisibility.clear();
		BoneVisibility.resize(BoneCount, true);
	}

	// 선택 초기화
	ClearSelection();
}

void UBoneHierarchyWidget::SetSelectedBoneIndex(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;
}

void UBoneHierarchyWidget::ClearSelection()
{
	SelectedBoneIndex = -1;
}

void UBoneHierarchyWidget::RenderBoneTree(int32 BoneIndex, USkeleton* Skeleton)
{
	if (Skeleton == nullptr || BoneIndex < 0 || BoneIndex >= Skeleton->GetBoneCount())
	{
		return;
	}

	// Bone 정보 가져오기
	const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);

	// TreeNode 플래그 설정 - DefaultOpen 추가로 기본적으로 열린 상태
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;

	// 선택된 Bone이면 Selected 플래그 추가
	if (SelectedBoneIndex == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	// 자식이 없으면 Leaf 플래그 추가
	if (!HasChildren(BoneIndex, Skeleton))
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	// TreeNode 렌더링
	bool bIsOpen = ImGui::TreeNodeEx(
		(void*)(intptr_t)BoneIndex,
		Flags,
		"%s [%d]",
		BoneInfo.Name.c_str(),
		BoneIndex
	);

	// 클릭 처리
	if (ImGui::IsItemClicked())
	{
		// 델리게이트 호출
		SkeletalMeshEditorWindow->OnBoneSelected.Broadcast(BoneIndex);	// SetSelectedBoneIndex 가 호출됨
	}

	// 자식 Bone 렌더링
	if (bIsOpen && HasChildren(BoneIndex, Skeleton))
	{
		TArray<int32> ChildBones = Skeleton->GetChildBones(BoneIndex);
		for (int32 ChildIndex : ChildBones)
		{
			RenderBoneTree(ChildIndex, Skeleton);
		}

		ImGui::TreePop();
	}
}

bool UBoneHierarchyWidget::HasChildren(int32 BoneIndex, USkeleton* Skeleton) const
{
	if (Skeleton == nullptr || BoneIndex < 0)
	{
		return false;
	}

	TArray<int32> ChildBones = Skeleton->GetChildBones(BoneIndex);
	return !ChildBones.empty();
}
