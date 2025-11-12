#pragma once
#include "SceneComponent.h"

class USkeletalMeshComponent;

/**
 * @brief 피킹된 본(Bone)에 기즈모를 부착하기 위한 프록시 컴포넌트
 *
 * - 타겟 본의 트랜스폼을 추적하여 기즈모 위치를 동기화
 * - 기즈모 조작 시 타겟 본의 트랜스폼을 업데이트
 * - 이동, 회전, 스케일 모드 지원
 */
class UBoneGizmoProxyComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UBoneGizmoProxyComponent, USceneComponent)
	GENERATED_REFLECTION_BODY()

	UBoneGizmoProxyComponent();
	virtual ~UBoneGizmoProxyComponent() override = default;

	// === Lifecycle ===

	/**
	 * 컴포넌트 틱
	 * 매 프레임마다 타겟 본의 트랜스폼과 동기화
	 */
	void TickComponent(float DeltaTime) override;

	// === Configuration ===

	/**
	 * 타겟 본 설정
	 * @param InSkeletalMeshComponent - 타겟 스켈레탈 메시 컴포넌트
	 * @param InBoneIndex - 타겟 본 인덱스
	 */
	void SetTargetBone(USkeletalMeshComponent* InSkeletalMeshComponent, int32 InBoneIndex);

	/**
	 * 타겟 본 인덱스 가져오기
	 */
	int32 GetTargetBoneIndex() const { return TargetBoneIndex; }

	/**
	 * 타겟 스켈레탈 메시 컴포넌트 가져오기
	 */
	USkeletalMeshComponent* GetTargetSkeletalMeshComponent() const { return TargetSkeletalMeshComponent; }

	/**
	 * 본 트랜스폼 동기화 활성화/비활성화
	 * @param bEnable - true: 본 트랜스폼을 추적, false: 추적 안 함
	 */
	void SetSyncWithBone(bool bEnable) { bSyncWithBone = bEnable; }

	/**
	 * 본 트랜스폼 동기화 여부 가져오기
	 */
	bool IsSyncWithBone() const { return bSyncWithBone; }

	// === Transform Update ===

	/**
	 * 프록시 컴포넌트의 트랜스폼이 변경되었을 때 타겟 본 업데이트
	 * 기즈모 조작 시 호출됨
	 */
	void UpdateTargetBoneTransform();

	// === Duplication ===

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(UBoneGizmoProxyComponent)

private:
	/**
	 * 타겟 본의 월드 트랜스폼 가져오기
	 */
	FTransform GetTargetBoneWorldTransform() const;

private:
	// === References ===

	// 타겟 스켈레탈 메시 컴포넌트
	USkeletalMeshComponent* TargetSkeletalMeshComponent = nullptr;

	// 타겟 본 인덱스 (-1 if no bone)
	int32 TargetBoneIndex = -1;

	// === Sync Settings ===

	// 본 트랜스폼과 동기화 여부
	// true: 매 틱마다 본 트랜스폼을 따라감
	// false: 프록시 트랜스폼을 본에 적용만 함
	bool bSyncWithBone = true;
};
