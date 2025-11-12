#pragma once
#include "SkinnedMeshComponent.h"

class UBoneDebugComponent;

/**
 * USkeletalMeshComponent
 * Animation 재생이 가능한 Skeletal Mesh 컴포넌트
 * Unreal Engine 5의 USkeletalMeshComponent와 유사한 구조
 *
 * 담당 기능:
 * - SkeletalMesh 관리
 * - Bone Transform 계산
 * - CPU Skinning
 * - Animation 재생 (향후 Phase 6+)
 * - GPU Skinning (향후)
 * - IK (향후)
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
	// === SkeletalMesh 관리 ===

	/**
	 * SkeletalMesh 설정
	 * @param InSkeletalMesh - 설정할 SkeletalMesh 객체
	 */
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

	/**
	 * SkeletalMesh 가져오기
	 * @return 현재 설정된 SkeletalMesh
	 */
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	/**
	 * Skeleton 가져오기
	 * @return SkeletalMesh의 Skeleton (SkeletalMesh가 없으면 nullptr)
	 */
	USkeleton* GetSkeleton() const;

	// === Component Lifecycle ===

	/**
	 * 매 프레임 업데이트
	 * Bone Transform 업데이트 및 CPU Skinning 수행
	 * @param DeltaTime - 프레임 시간 (초)
	 */
	void TickComponent(float DeltaTime) override;

	// === Bone Transform 관리 (CPU Skinning용) ===

	/**
	 * Bone Transforms 업데이트
	 * 현재는 Bind Pose만 사용하지만, 향후 Animation 시스템에서 사용
	 */
	void ResetBoneTransforms();

	/**
	 * Bone Matrices 가져오기 (Component Space)
	 * @return Bone Matrices 배열 (읽기 전용)
	 */
	const TArray<FMatrix>& GetBoneMatrices() const { return BoneMatrices; }

	/**
	 * 특정 Bone의 Transform 설정 (향후 Animation용)
	 * @param BoneIndex - Bone 인덱스
	 * @param Transform - 설정할 Transform
	 */
	void SetBoneTransform(int32 BoneIndex, const FTransform& Transform);

	// === CPU Skinning 관리 ===

	/**
	 * CPU Skinning 수행
	 * Bone Transforms를 사용하여 Vertex 위치 계산
	 */
	void PerformCPUSkinning();

	void StartUpdateBoneRecursive();

	/**
	 * CPU Skinning 활성화/비활성화
	 * @param bEnable - true인 경우 활성화
	 */
	void SetEnableCPUSkinning(bool bEnable) { bEnableCPUSkinning = bEnable; }

	/**
	 * CPU Skinning 활성화 여부 확인
	 * @return CPU Skinning 활성화 여부
	 */
	bool IsCPUSkinningEnabled() const { return bEnableCPUSkinning; }

	// === Bone Debug Visualization ===

	/**
	 * Bone 디버그 시각화 활성화/비활성화
	 * @param bShow - true인 경우 Bone을 시각화
	 */
	void SetShowBoneDebug(bool bShow);

	/**
	 * Bone 디버그 시각화 활성화 여부 확인
	 * @return Bone 디버그 시각화 활성화 여부
	 */
	bool IsShowBoneDebug() const;

	/**
	 * BoneDebugComponent 가져오기
	 * @return BoneDebugComponent (nullptr 가능)
	 */
	UBoneDebugComponent* GetBoneDebugComponent() const { return BoneDebugComponent; }

	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	FAABB GetWorldAABB() const override;
	
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkeletalMeshComponent)


	void MoveBone(int BoneIndex, const FTransform& Transform);

	void UpdateBoneRecursive(int32 BoneIndex, const FMatrix& ParentAnimatedTransform);

	FTransform GetBoneWorldTransform(int32 BoneIndex);

protected:
	void MarkWorldPartitionDirty();

private:
	// SkeletalMesh 참조
	USkeletalMesh* SkeletalMesh = nullptr;

	// === Bone Transform 데이터 (CPU Skinning용) ===

	// Runtime Bone Transforms (Component Space)
	// InverseBindPose * BoneTransform 형태로 저장
	TArray<FMatrix> BoneMatrices;

	// Bone Transform 업데이트 필요 여부
	bool bNeedsBoneTransformUpdate = true;

	// === CPU Skinning 데이터 ===

	// CPU Skinning 결과 (GPU 전송용 - FNormalVertex 형식)
	TArray<FNormalVertex> SkinnedVertices;

	// CPU Skinning 활성화 여부
	bool bEnableCPUSkinning = true;

	// === Bone Debug Visualization ===

	// Bone 디버그 시각화 컴포넌트
	UBoneDebugComponent* BoneDebugComponent = nullptr;

	// TODO: Animation 관련 멤버 변수들 (Phase 6+)
	// UAnimSequence* CurrentAnimation = nullptr;
	// float CurrentAnimationTime = 0.0f;
	
	// 사용자가 'SetBoneTransform'으로 설정한 커스텀 로컬 트랜스폼을
	// BoneIndex를 키로 하여 저장하는 맵(Map)입니다.
	TMap<int32, FTransform> CustomBoneLocalTransform;
};
