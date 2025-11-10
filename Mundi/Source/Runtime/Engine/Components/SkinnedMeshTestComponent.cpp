#include "pch.h"
#include "SkinnedMeshTestComponent.h"
#include "Skeleton.h"
#include "SkeletalMesh.h"
#include "GlobalConsole.h"

IMPLEMENT_CLASS(USkinnedMeshTestComponent)

void USkinnedMeshTestComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (!bTestEnabled || !TargetComponent)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = TargetComponent->GetSkeletalMesh();
	if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
	{
		return;
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	int32 BoneCount = Skeleton->GetBoneCount();

	if (TestBoneIndex < 0 || TestBoneIndex >= BoneCount)
	{
		return;
	}

	// 회전 각도 누적
	AccumulatedRotation += RotationSpeed * DeltaTime;

	// 360도를 넘으면 초기화
	if (AccumulatedRotation >= 360.0f)
	{
		AccumulatedRotation -= 360.0f;
	}

	// Bone Transform 생성 (Z축 기준 회전)
	float RadianAngle = AccumulatedRotation * (3.14159265f / 180.0f);
	FTransform RotationTransform;
	RotationTransform.SetRotation(FQuat::CreateFromAxisAngle(FVector(0, 0, 1), RadianAngle));

	// Bone Transform 설정
	TargetComponent->SetBoneTransform(TestBoneIndex, RotationTransform);

	// 디버그 로그 (1초마다)
	static float LogTimer = 0.0f;
	LogTimer += DeltaTime;
	if (LogTimer >= 1.0f)
	{
		const FBoneInfo& BoneInfo = Skeleton->GetBone(TestBoneIndex);
		UE_LOG("[SkinnedMeshTest] Rotating bone '%s' (index %d) by %.1f degrees",
			BoneInfo.Name.c_str(), TestBoneIndex, AccumulatedRotation);
		LogTimer = 0.0f;
	}
}
