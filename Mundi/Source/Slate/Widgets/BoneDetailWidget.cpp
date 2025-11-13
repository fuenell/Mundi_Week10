#include "pch.h"
#include "BoneDetailWidget.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "Windows/SkeletalMeshEditorWindow.h"
#include "ImGui/imgui.h"
#include "Vector.h"

IMPLEMENT_CLASS(UBoneDetailWidget)

UBoneDetailWidget::UBoneDetailWidget()
	: UWidget("BoneDetail")
	, TargetSkeletalMesh(nullptr)
	, CurrentBoneIndex(-1)
	, BoneName("")
	, BonePosition(0.0f, 0.0f, 0.0f)
	, BoneRotation(0.0f, 0.0f, 0.0f)
	, BoneScale(1.0f, 1.0f, 1.0f)
	, bIsTransformModified(false)
{
}

void UBoneDetailWidget::Initialize()
{
	Super::Initialize();

	// 초기 상태 설정
	ClearSelection();
}

void UBoneDetailWidget::RenderWidget()
{
	Super::RenderWidget();

	// 선택된 Bone이 없으면 메시지 표시
	if (CurrentBoneIndex < 0)
	{
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No Bone Selected");
		ImGui::Separator();
		ImGui::TextWrapped("Select a bone from the Bone Hierarchy to view and edit its properties.");
		return;
	}

	// SkeletalMesh가 없으면 메시지 표시
	if (TargetSkeletalMesh == nullptr || TargetSkeletalMesh->GetSkeleton() == nullptr)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No SkeletalMesh or Skeleton");
		return;
	}

	USkeleton* Skeleton = TargetSkeletalMesh->GetSkeleton();
	if (CurrentBoneIndex >= Skeleton->GetBoneCount())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Invalid Bone Index");
		return;
	}

	// Bone 정보 표시
	ImGui::Text("Bone Details");
	ImGui::Separator();

	// Bone 이름 및 인덱스
	ImGui::Text("Name: %s", BoneName.c_str());
	ImGui::Text("Index: %d", CurrentBoneIndex);

	// 부모 Bone 정보
	const FBoneInfo& BoneInfo = Skeleton->GetBone(CurrentBoneIndex);
	if (BoneInfo.ParentIndex >= 0)
	{
		const FBoneInfo& ParentBone = Skeleton->GetBone(BoneInfo.ParentIndex);
		ImGui::Text("Parent: %s [%d]", ParentBone.Name.c_str(), BoneInfo.ParentIndex);
	}
	else
	{
		ImGui::Text("Parent: None (Root Bone)");
	}

	ImGui::Separator();

	// Transform 편집 영역
	ImGui::Text("Transform (Local Space)");
	ImGui::Separator();

	// Position
	RenderTransformProperty("Position", BonePosition, 0.1f, false);

	ImGui::Spacing();

	// Rotation (Euler Angles, Degrees)
	RenderTransformProperty("Rotation", BoneRotation, 0.5f, true);

	ImGui::Spacing();

	// Scale
	RenderTransformProperty("Scale", BoneScale, 0.01f, false);

	ImGui::Separator();

	// 버튼 영역
	ImGui::Spacing();

	// Apply 버튼
	if (ImGui::Button("Apply Changes", ImVec2(120, 0)))
	{
		ApplyBoneTransform();
		bIsTransformModified = false;
	}

	ImGui::SameLine();

	// Reset 버튼
	if (ImGui::Button("Reset", ImVec2(120, 0)))
	{
		LoadBoneTransform();
		bIsTransformModified = false;
	}

	// 수정 상태 표시
	if (bIsTransformModified)
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "* Transform Modified");

		ApplyBoneTransform();
		bIsTransformModified = false;
	}
}

void UBoneDetailWidget::Update(float DeltaTime)
{
	Super::Update(DeltaTime);
}

void UBoneDetailWidget::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	TargetSkeletalMesh = InMesh;

	// 선택 초기화
	ClearSelection();
}

void UBoneDetailWidget::SetSelectedBone(int32 BoneIndex)
{
	if (TargetSkeletalMesh == nullptr || TargetSkeletalMesh->GetSkeleton() == nullptr)
	{
		ClearSelection();
		return;
	}

	USkeleton* Skeleton = TargetSkeletalMesh->GetSkeleton();
	if (BoneIndex < 0 || BoneIndex >= Skeleton->GetBoneCount())
	{
		ClearSelection();
		return;
	}

	// Bone 선택
	CurrentBoneIndex = BoneIndex;

	// Bone 이름 저장
	const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);
	BoneName = BoneInfo.Name;

	// Transform 데이터 로드
	LoadBoneTransform();

	// 수정 플래그 초기화
	bIsTransformModified = false;
}

void UBoneDetailWidget::ClearSelection()
{
	CurrentBoneIndex = -1;
	BoneName = "";
	BonePosition = FVector(0.0f, 0.0f, 0.0f);
	BoneRotation = FVector(0.0f, 0.0f, 0.0f);
	BoneScale = FVector(1.0f, 1.0f, 1.0f);
	bIsTransformModified = false;
}

void UBoneDetailWidget::LoadBoneTransform()
{
	if (TargetSkeletalMesh == nullptr || CurrentBoneIndex < 0)
	{
		return;
	}

	USkeleton* Skeleton = TargetSkeletalMesh->GetSkeleton();
	if (Skeleton == nullptr || CurrentBoneIndex >= Skeleton->GetBoneCount())
	{
		return;
	}

	// Bone 정보 가져오기
	const FBoneInfo& BoneInfo = Skeleton->GetBone(CurrentBoneIndex);

	// BindPoseTransform에서 Transform 데이터 추출
	const FTransform& BindPose = BoneInfo.BindPoseRelativeTransform;

	// Translation
	BonePosition = BindPose.Translation;

	// Rotation (Quaternion → Euler Angles, Degrees)
	BoneRotation = BindPose.Rotation.ToEulerZYXDeg();

	// Scale
	BoneScale = BindPose.Scale3D;
}

void UBoneDetailWidget::ApplyBoneTransform()
{
	if (TargetSkeletalMesh == nullptr || CurrentBoneIndex < 0)
	{
		return;
	}

	USkeleton* Skeleton = TargetSkeletalMesh->GetSkeleton();
	if (Skeleton == nullptr || CurrentBoneIndex >= Skeleton->GetBoneCount())
	{
		return;
	}

	// Euler Angles → Quaternion
	FQuat Quat = FQuat::MakeFromEulerZYX(BoneRotation);

	// FTransform 생성
	FTransform NewTransform(BonePosition, Quat, BoneScale);

	// Skeleton에 적용
	Skeleton->SetBindPoseTransform(CurrentBoneIndex, NewTransform);

	SkeletalMeshEditorWindow->OnBoneUpdated.Broadcast(CurrentBoneIndex);
	// TODO: SkeletalMeshComponent에 Bone Transform 업데이트 알림
	// 현재는 Skeleton의 BindPose만 수정하고 있으므로,
	// 실시간 프리뷰를 위해서는 SkeletalMeshComponent의 BoneMatrices를 업데이트해야 합니다.
}

void UBoneDetailWidget::RenderTransformProperty(const char* Label, FVector& Value, float DragSpeed, bool bIsRotation)
{
	ImGui::Text("%s", Label);

	// 고유 ID 생성
	char IDBuffer[64];
	snprintf(IDBuffer, sizeof(IDBuffer), "##%s_%d", Label, CurrentBoneIndex);

	// DragFloat3로 편집
	if (ImGui::DragFloat3(IDBuffer, &Value.X, DragSpeed))
	{
		bIsTransformModified = true;

		// Rotation인 경우 각도를 -180 ~ 180 범위로 정규화
		if (bIsRotation)
		{
			Value.X = NormalizeAngleDeg(Value.X);
			Value.Y = NormalizeAngleDeg(Value.Y);
			Value.Z = NormalizeAngleDeg(Value.Z);
		}
	}

	// Rotation인 경우 단위 표시
	if (bIsRotation)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(deg)");
	}
}

void UBoneDetailWidget::DecomposeMatrix(const FMatrix& Matrix, FVector& OutPosition, FVector& OutRotation, FVector& OutScale)
{
	// Translation 추출 (Matrix의 마지막 열)
	OutPosition = FVector(Matrix.M[0][3], Matrix.M[1][3], Matrix.M[2][3]);

	// Scale 추출 (각 축 벡터의 길이)
	FVector ScaleX(Matrix.M[0][0], Matrix.M[1][0], Matrix.M[2][0]);
	FVector ScaleY(Matrix.M[0][1], Matrix.M[1][1], Matrix.M[2][1]);
	FVector ScaleZ(Matrix.M[0][2], Matrix.M[1][2], Matrix.M[2][2]);

	OutScale = FVector(
		ScaleX.Length(),
		ScaleY.Length(),
		ScaleZ.Length()
	);

	// Rotation Matrix 추출 (Scale 제거)
	FMatrix RotationMatrix = Matrix;
	if (OutScale.X > KINDA_SMALL_NUMBER)
	{
		RotationMatrix.M[0][0] /= OutScale.X;
		RotationMatrix.M[1][0] /= OutScale.X;
		RotationMatrix.M[2][0] /= OutScale.X;
	}
	if (OutScale.Y > KINDA_SMALL_NUMBER)
	{
		RotationMatrix.M[0][1] /= OutScale.Y;
		RotationMatrix.M[1][1] /= OutScale.Y;
		RotationMatrix.M[2][1] /= OutScale.Y;
	}
	if (OutScale.Z > KINDA_SMALL_NUMBER)
	{
		RotationMatrix.M[0][2] /= OutScale.Z;
		RotationMatrix.M[1][2] /= OutScale.Z;
		RotationMatrix.M[2][2] /= OutScale.Z;
	}

	// Rotation Matrix → Quaternion → Euler Angles
	// (이 함수는 현재 사용되지 않음 - FTransform을 직접 사용)
	// 필요시 구현 가능
	OutRotation = FVector(0.0f, 0.0f, 0.0f);
}

FMatrix UBoneDetailWidget::ComposeMatrix(const FVector& Position, const FVector& Rotation, const FVector& Scale)
{
	// Euler Angles → Quaternion
	FQuat Quat = FQuat::MakeFromEulerZYX(Rotation);

	// FTransform 생성
	FTransform Transform(Position, Quat, Scale);

	// FTransform → FMatrix
	return Transform.ToMatrix();
}
