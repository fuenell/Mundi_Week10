#pragma once

#include "ActorComponent.h"
#include "SkinnedMeshComponent.h"

/**
 * CPU Skinning 테스트용 컴포넌트
 * 특정 Bone을 자동으로 회전시켜서 CPU Skinning이 작동하는지 확인
 */
class USkinnedMeshTestComponent : public UActorComponent
{
public:
	DECLARE_CLASS(USkinnedMeshTestComponent, UActorComponent)

	USkinnedMeshTestComponent() = default;
	~USkinnedMeshTestComponent() override = default;

	/**
	 * 테스트할 SkinnedMeshComponent 설정
	 * @param InComponent - 테스트할 컴포넌트
	 */
	void SetTargetComponent(USkinnedMeshComponent* InComponent)
	{
		TargetComponent = InComponent;
	}

	/**
	 * 회전시킬 Bone 인덱스 설정
	 * @param InBoneIndex - Bone 인덱스
	 */
	void SetTestBoneIndex(int32 InBoneIndex)
	{
		TestBoneIndex = InBoneIndex;
	}

	/**
	 * 회전 속도 설정 (도/초)
	 * @param InSpeed - 회전 속도
	 */
	void SetRotationSpeed(float InSpeed)
	{
		RotationSpeed = InSpeed;
	}

	/**
	 * 테스트 활성화/비활성화
	 * @param bEnable - true인 경우 활성화
	 */
	void SetTestEnabled(bool bEnable)
	{
		bTestEnabled = bEnable;
	}

	// === Component Lifecycle ===

	void TickComponent(float DeltaTime) override;

private:
	// 테스트 대상 컴포넌트
	USkinnedMeshComponent* TargetComponent = nullptr;

	// 테스트할 Bone 인덱스
	int32 TestBoneIndex = 0;

	// 회전 속도 (도/초)
	float RotationSpeed = 90.0f;

	// 누적 회전 각도
	float AccumulatedRotation = 0.0f;

	// 테스트 활성화 여부
	bool bTestEnabled = false;
};
