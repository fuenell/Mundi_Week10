#pragma once
#include "SkinnedMeshComponent.h"

/**
 * USkeletalMeshComponent
 * Animation 재생이 가능한 Skeletal Mesh 컴포넌트
 * Unreal Engine의 USkeletalMeshComponent와 유사한 구조
 *
 * 현재 Phase 5 구현:
 * - USkinnedMeshComponent 상속 (기본 렌더링)
 *
 * 향후 확장 (Phase 6+):
 * - Bone Transform 계산
 * - Animation 재생
 * - Skinning (GPU)
 * - IK (Inverse Kinematics)
 */
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkeletalMeshComponent();

protected:
	~USkeletalMeshComponent() override;

public:
	// TODO: Animation 관련 함수들 (Phase 6+)
	// void PlayAnimation(UAnimSequence* Animation);
	// void StopAnimation();
	// void UpdateAnimation(float DeltaTime);

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkeletalMeshComponent)

protected:
	// TODO: Animation 관련 멤버 변수들 (Phase 6+)
	// UAnimSequence* CurrentAnimation = nullptr;
	// float CurrentAnimationTime = 0.0f;
	// TArray<FTransform> BoneTransforms;
};
