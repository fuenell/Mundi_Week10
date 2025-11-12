#include "pch.h"
#include "BoneGizmoProxyComponent.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"

IMPLEMENT_CLASS(UBoneGizmoProxyComponent)

BEGIN_PROPERTIES(UBoneGizmoProxyComponent)
	MARK_AS_COMPONENT("Bone Gizmo Proxy", "본에 기즈모를 부착하기 위한 프록시 컴포넌트")
	ADD_PROPERTY(int32, TargetBoneIndex, "Target Bone Index", false, "타겟 본 인덱스")
	ADD_PROPERTY(bool, bSyncWithBone, "Sync With Bone", true, "본 트랜스폼과 동기화 여부")
END_PROPERTIES()

UBoneGizmoProxyComponent::UBoneGizmoProxyComponent()
	: USceneComponent()
	, TargetSkeletalMeshComponent(nullptr)
	, TargetBoneIndex(-1)
	, bSyncWithBone(true)
{
	bCanEverTick = true;
}

void UBoneGizmoProxyComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	// 본 트랜스폼과 동기화가 활성화된 경우, 타겟 본의 트랜스폼을 따라감
	if (bSyncWithBone && TargetSkeletalMeshComponent && TargetBoneIndex >= 0)
	{
		FTransform BoneWorldTransform = GetTargetBoneWorldTransform();
		SetWorldTransform(BoneWorldTransform);
	}
}

void UBoneGizmoProxyComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}

void UBoneGizmoProxyComponent::SetTargetBone(USkeletalMeshComponent* InSkeletalMeshComponent, int32 InBoneIndex)
{
	TargetSkeletalMeshComponent = InSkeletalMeshComponent;
	TargetBoneIndex = InBoneIndex;

	// 타겟 본이 설정되면 초기 위치를 본 위치로 설정
	if (TargetSkeletalMeshComponent && TargetBoneIndex >= 0)
	{
		FTransform BoneWorldTransform = GetTargetBoneWorldTransform();
		SetWorldTransform(BoneWorldTransform);
	}
}

void UBoneGizmoProxyComponent::UpdateTargetBoneTransform()
{
	if (!TargetSkeletalMeshComponent || TargetBoneIndex < 0)
		return;

	USkeletalMesh* SkeletalMesh = TargetSkeletalMeshComponent->GetSkeletalMesh();
	if (!SkeletalMesh)
		return;

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
		return;

	// 현재 프록시 컴포넌트의 월드 트랜스폼을 타겟 본에 적용
	FTransform ProxyWorldTransform = GetWorldTransform();

	// 컴포넌트의 월드 트랜스폼을 본 로컬 공간으로 변환
	FMatrix ComponentWorldMatrix = TargetSkeletalMeshComponent->GetWorldMatrix();
	FMatrix InverseComponentWorld = ComponentWorldMatrix.InverseAffine();

	// 프록시의 월드 위치를 본 로컬 공간으로 변환
	FVector4 WorldPos4(ProxyWorldTransform.Translation.X, ProxyWorldTransform.Translation.Y, ProxyWorldTransform.Translation.Z, 1.0f);
	FVector4 LocalPos4 = WorldPos4 * InverseComponentWorld;
	FVector LocalPos(LocalPos4.X, LocalPos4.Y, LocalPos4.Z);

	// TODO: 본의 로컬 트랜스폼 업데이트
	// 현재는 GlobalBindPoseMatrix를 직접 수정할 수 없으므로,
	// 애니메이션 시스템이나 본 조작 시스템이 필요합니다.
	// 이 부분은 추후 애니메이션 시스템 구현 시 완성됩니다.

	UE_LOG("[BoneGizmoProxy] Bone %d transform updated (World: %.2f, %.2f, %.2f)",
		TargetBoneIndex,
		ProxyWorldTransform.Translation.X,
		ProxyWorldTransform.Translation.Y,
		ProxyWorldTransform.Translation.Z);
}

FTransform UBoneGizmoProxyComponent::GetTargetBoneWorldTransform() const
{
	FTransform Result; // Default constructor initializes to identity

	if (!TargetSkeletalMeshComponent || TargetBoneIndex < 0)
		return Result;

	USkeletalMesh* SkeletalMesh = TargetSkeletalMeshComponent->GetSkeletalMesh();
	if (!SkeletalMesh)
		return Result;

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
		return Result;

	if (TargetBoneIndex >= Skeleton->GetBoneCount())
		return Result;

	const FBoneInfo& BoneInfo = Skeleton->GetBone(TargetBoneIndex);

	// 본의 월드 트랜스폼 계산
	FMatrix ComponentWorldMatrix = TargetSkeletalMeshComponent->GetWorldMatrix();
	FMatrix BoneWorldMatrix = BoneInfo.GlobalBindPoseMatrix * ComponentWorldMatrix;

	// FMatrix에서 FTransform으로 변환
	Result.Translation = FVector(
		BoneWorldMatrix.M[3][0],
		BoneWorldMatrix.M[3][1],
		BoneWorldMatrix.M[3][2]
	);

	// 회전 추출 (3x3 회전 행렬에서 쿼터니언으로 변환)
	// 간단한 구현: 단위 쿼터니언 사용
	Result.Rotation = FQuat(0.0f, 0.0f, 0.0f, 1.0f);

	// 스케일 추출 (각 축의 길이)
	FVector XAxis(BoneWorldMatrix.M[0][0], BoneWorldMatrix.M[0][1], BoneWorldMatrix.M[0][2]);
	FVector YAxis(BoneWorldMatrix.M[1][0], BoneWorldMatrix.M[1][1], BoneWorldMatrix.M[1][2]);
	FVector ZAxis(BoneWorldMatrix.M[2][0], BoneWorldMatrix.M[2][1], BoneWorldMatrix.M[2][2]);

	Result.Scale3D = FVector(XAxis.Size(), YAxis.Size(), ZAxis.Size());

	return Result;
}
