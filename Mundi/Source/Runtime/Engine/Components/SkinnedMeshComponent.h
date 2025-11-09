#pragma once
#include "MeshComponent.h"
#include "Enums.h"
#include "AABB.h"

class USkeletalMesh;
class USkeleton;
class UShader;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FSceneCompData;

/**
 * USkinnedMeshComponent
 * Skeletal Mesh의 기본 렌더링 컴포넌트 (애니메이션 제외)
 * Unreal Engine의 USkinnedMeshComponent와 유사한 구조
 *
 * 담당 기능:
 * - SkeletalMesh 설정 및 렌더링
 * - Material 슬롯 관리
 * - Skeleton 참조
 *
 * 상속 구조:
 * - USkeletalMeshComponent: 애니메이션 재생 기능 추가 (Phase 6+)
 */
class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkinnedMeshComponent();

protected:
	~USkinnedMeshComponent() override;
	void ClearDynamicMaterials();

public:
	void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

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

	UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
	void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

	UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex);

	const TArray<UMaterialInterface*> GetMaterialSlots() const { return MaterialSlots; }

	void SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture);
	void SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value);
	void SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value);

	FAABB GetWorldAABB() const override;

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkinnedMeshComponent)

	// === Bone Transform 관리 (CPU Skinning용) ===

	/**
	 * Bone Transforms 업데이트
	 * 현재는 Bind Pose만 사용하지만, 향후 Animation 시스템에서 사용
	 */
	void UpdateBoneTransforms();

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

protected:
	void OnTransformUpdated() override;
	void MarkWorldPartitionDirty();

protected:
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<UMaterialInterface*> MaterialSlots;

	// 동적 머티리얼 인스턴스 (런타임에 생성되는 머티리얼)
	TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;

	// === Bone Transform 데이터 (CPU Skinning용) ===

	// Runtime Bone Transforms (Component Space)
	// InverseBindPose * BoneTransform 형태로 저장
	TArray<FMatrix> BoneMatrices;

	// Bone Transform 업데이트 필요 여부
	bool bNeedsBoneTransformUpdate = true;
};
