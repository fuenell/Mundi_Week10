#include "pch.h"
#include "SkeletalMeshViewerWindow.h"
#include "SkeletalMesh.h"
#include "Enums.h"
#include "ImGui/imgui.h"
#include "ResourceManager.h"

IMPLEMENT_CLASS(USkeletalMeshViewerWindow)

USkeletalMeshViewerWindow::USkeletalMeshViewerWindow()
	: CurrentSkeletalMesh(nullptr)
	, SelectedBoneIndex(-1)
	, bShowBoneNames(true)
	, bShowGizmo(true)
{
}

void USkeletalMeshViewerWindow::Initialize()
{
	Super::Initialize();

	FUIWindowConfig& Config = GetMutableConfig();
	Config.WindowTitle = "Skeletal Mesh Viewer";
	Config.DefaultSize = ImVec2(800, 600);
}

void USkeletalMeshViewerWindow::RenderContent()
{
	ImGui::BeginChild("SkeletalMeshViewerContent");

	const FUIWindowConfig& Config = GetConfig();

	// 메뉴 바
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("View"))
		{
			ImGui::Checkbox("Show Bone Names", &bShowBoneNames);
			ImGui::Checkbox("Show Gizmo", &bShowGizmo);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// SkeletalMesh 선택 콤보박스
	RenderSkeletalMeshSelector();

	ImGui::Separator();

	// 메인 컨텐츠
	if (CurrentSkeletalMesh && CurrentSkeletalMesh->GetSkeletalMeshData())
	{
		ImGui::Columns(2, "MainColumns", true);

		// 왼쪽: Bone Hierarchy Tree
		ImGui::BeginChild("BoneHierarchy", ImVec2(0, 0), true);
		ImGui::Text("Bone Hierarchy");
		ImGui::Separator();
		RenderBoneHierarchyTree();
		ImGui::EndChild();

		ImGui::NextColumn();

		// 오른쪽: Bone Details
		ImGui::BeginChild("BoneDetails", ImVec2(0, 0), true);
		ImGui::Text("Bone Details");
		ImGui::Separator();
		RenderBoneDetails();
		ImGui::EndChild();

		ImGui::Columns(1);
	}
	else
	{
		ImGui::Text("No Skeletal Mesh selected.");
		ImGui::Text("Please select a skeletal mesh from the dropdown above.");
	}

	ImGui::EndChild();
}

void USkeletalMeshViewerWindow::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	CurrentSkeletalMesh = InSkeletalMesh;
	SelectedBoneIndex = -1;
}

void USkeletalMeshViewerWindow::SelectBone(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;
}

void USkeletalMeshViewerWindow::RenderBoneHierarchyTree()
{
	if (!CurrentSkeletalMesh || !CurrentSkeletalMesh->GetSkeletalMeshData())
	{
		return;
	}

	const FSkeleton& Skeleton = CurrentSkeletalMesh->GetSkeleton();

	// 루트 본들을 찾아서 렌더링 (ParentIndex가 -1인 본)
	for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
	{
		const FBoneInfo& Bone = Skeleton.Bones[i];
		if (Bone.ParentIndex == -1)
		{
			RenderBoneTreeNode(i, Skeleton);
		}
	}
}

void USkeletalMeshViewerWindow::RenderBoneTreeNode(int32 BoneIndex, const FSkeleton& Skeleton)
{
	const FBoneInfo& Bone = Skeleton.Bones[BoneIndex];

	// 자식 본이 있는지 확인
	bool bHasChildren = false;
	for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
	{
		if (Skeleton.Bones[i].ParentIndex == BoneIndex)
		{
			bHasChildren = true;
			break;
		}
	}

	// Tree Node 플래그 설정
	ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (!bHasChildren)
	{
		nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (SelectedBoneIndex == BoneIndex)
	{
		nodeFlags |= ImGuiTreeNodeFlags_Selected;
	}

	// 본 이름 표시
	FString boneName = Bone.Name;
	bool bNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)BoneIndex, nodeFlags, boneName.c_str());

	// 클릭 시 선택
	if (ImGui::IsItemClicked())
	{
		SelectBone(BoneIndex);
	}

	// 자식 노드 렌더링
	if (bNodeOpen && bHasChildren)
	{
		for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
		{
			if (Skeleton.Bones[i].ParentIndex == BoneIndex)
			{
				RenderBoneTreeNode(i, Skeleton);
			}
		}
		ImGui::TreePop();
	}
}

void USkeletalMeshViewerWindow::RenderBoneDetails()
{
	if (!CurrentSkeletalMesh || !CurrentSkeletalMesh->GetSkeletalMeshData() || SelectedBoneIndex < 0)
	{
		ImGui::Text("No bone selected.");
		return;
	}

	const FSkeleton& Skeleton = CurrentSkeletalMesh->GetSkeleton();
	if (SelectedBoneIndex >= Skeleton.Bones.Num())
	{
		ImGui::Text("Invalid bone index.");
		return;
	}

	const FBoneInfo& Bone = Skeleton.Bones[SelectedBoneIndex];

	// 본 정보 표시
	ImGui::Text("Bone Name: %s", Bone.Name.c_str());
	ImGui::Text("Bone Index: %d", SelectedBoneIndex);
	ImGui::Text("Parent Index: %d", Bone.ParentIndex);

	if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Skeleton.Bones.Num())
	{
		ImGui::Text("Parent Name: %s", Skeleton.Bones[Bone.ParentIndex].Name.c_str());
	}

	ImGui::Separator();

	// Local Transform 표시
	ImGui::Text("Local Transform:");
	const DirectX::XMFLOAT4X4& LocalTransform = Bone.LocalTransform;
	for (int32 row = 0; row < 4; ++row)
	{
		ImGui::Text("  %.3f  %.3f  %.3f  %.3f",
			LocalTransform.m[row][0],
			LocalTransform.m[row][1],
			LocalTransform.m[row][2],
			LocalTransform.m[row][3]);
	}

	ImGui::Separator();

	// Global Bind Pose 표시
	ImGui::Text("Global Bind Pose:");
	const DirectX::XMFLOAT4X4& GlobalBindPose = Bone.GlobalBindPose;
	for (int32 row = 0; row < 4; ++row)
	{
		ImGui::Text("  %.3f  %.3f  %.3f  %.3f",
			GlobalBindPose.m[row][0],
			GlobalBindPose.m[row][1],
			GlobalBindPose.m[row][2],
			GlobalBindPose.m[row][3]);
	}

	// Gizmo 렌더링
	if (bShowGizmo)
	{
		ImGui::Separator();
		ImGui::Text("Gizmo rendering (TODO: Implement in 3D view)");
	}
}

void USkeletalMeshViewerWindow::RenderBoneTransformGizmo()
{
	// TODO: 3D 뷰에서 선택된 본의 Transform을 Gizmo로 렌더링
	// 이 기능은 SceneRenderer나 별도의 Debug Draw 시스템과 연동 필요
}

void USkeletalMeshViewerWindow::RenderBoneGizmosInScene()
{
	if (!bShowGizmo || !CurrentSkeletalMesh || !CurrentSkeletalMesh->GetSkeletalMeshData())
	{
		return;
	}

	const FSkeleton& Skeleton = CurrentSkeletalMesh->GetSkeleton();

	// TODO: DebugDraw 시스템과 연동하여 본 Gizmo 렌더링
	// 각 본의 위치에 좌표축(X=빨강, Y=녹색, Z=파랑)을 그림
	// 선택된 본은 더 크고 밝게 표시

	// 구현 예시 (DebugDraw API가 있다고 가정):
	/*
	for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
	{
		const FBoneInfo& Bone = Skeleton.Bones[i];
		DirectX::XMMATRIX BoneMatrix = DirectX::XMLoadFloat4x4(&Bone.GlobalBindPose);

		// 본 위치 추출
		DirectX::XMVECTOR Position = BoneMatrix.r[3];

		// 좌표축 방향 추출
		DirectX::XMVECTOR XAxis = DirectX::XMVector3Normalize(BoneMatrix.r[0]);
		DirectX::XMVECTOR YAxis = DirectX::XMVector3Normalize(BoneMatrix.r[1]);
		DirectX::XMVECTOR ZAxis = DirectX::XMVector3Normalize(BoneMatrix.r[2]);

		float Scale = (i == SelectedBoneIndex) ? GizmoScale * 2.0f : GizmoScale;

		// X축 (빨강)
		DebugDraw::DrawLine(Position, Position + XAxis * Scale, FColor(1, 0, 0, 1));

		// Y축 (녹색)
		DebugDraw::DrawLine(Position, Position + YAxis * Scale, FColor(0, 1, 0, 1));

		// Z축 (파랑)
		DebugDraw::DrawLine(Position, Position + ZAxis * Scale, FColor(0, 0, 1, 1));

		// 본 이름 표시 (옵션)
		if (bShowBoneNames)
		{
			DebugDraw::DrawText(Position, Bone.Name, FColor(1, 1, 1, 1));
		}
	}
	*/
}

void USkeletalMeshViewerWindow::CacheSkeletalMeshList()
{
	CachedSkeletalMeshPaths.Empty();
	CachedSkeletalMeshItems.Empty();

	UResourceManager& ResMgr = UResourceManager::GetInstance();
	CachedSkeletalMeshPaths = ResMgr.GetAllFilePaths<USkeletalMesh>();

	// "None" 옵션 추가
	CachedSkeletalMeshPaths.Insert("", 0);
	CachedSkeletalMeshItems.Add("None");

	// 경로 -> const char* 변환
	for (int32 i = 1; i < CachedSkeletalMeshPaths.Num(); ++i)
	{
		CachedSkeletalMeshItems.Add(CachedSkeletalMeshPaths[i].c_str());
	}

	// 현재 메시가 설정되어 있으면 인덱스 찾기
	if (CurrentSkeletalMesh)
	{
		FString CurrentPath = CurrentSkeletalMesh->GetFilePath();
		for (int32 i = 0; i < CachedSkeletalMeshPaths.Num(); ++i)
		{
			if (CachedSkeletalMeshPaths[i] == CurrentPath)
			{
				SelectedMeshIndex = i;
				break;
			}
		}
	}
	else
	{
		SelectedMeshIndex = 0; // None
	}
}

void USkeletalMeshViewerWindow::RenderSkeletalMeshSelector()
{
	// 캐시가 비어있으면 새로 로드
	if (CachedSkeletalMeshItems.IsEmpty())
	{
		CacheSkeletalMeshList();
	}

	if (CachedSkeletalMeshItems.IsEmpty())
	{
		ImGui::Text("No Skeletal Meshes available");
		return;
	}

	ImGui::Text("Skeletal Mesh:");
	ImGui::SameLine();

	ImGui::SetNextItemWidth(400);
	if (ImGui::Combo("##SkeletalMeshCombo", &SelectedMeshIndex, CachedSkeletalMeshItems.GetData(), CachedSkeletalMeshItems.Num()))
	{
		// 선택이 변경됨
		if (SelectedMeshIndex == 0)
		{
			// None 선택
			CurrentSkeletalMesh = nullptr;
			SelectedBoneIndex = -1;
		}
		else if (SelectedMeshIndex > 0 && SelectedMeshIndex < CachedSkeletalMeshPaths.Num())
		{
			// SkeletalMesh 로드
			const FString& MeshPath = CachedSkeletalMeshPaths[SelectedMeshIndex];
			USkeletalMesh* LoadedMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(MeshPath);
			if (LoadedMesh)
			{
				SetSkeletalMesh(LoadedMesh);
			}
		}
	}

	// Refresh 버튼
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		CacheSkeletalMeshList();
	}
}
