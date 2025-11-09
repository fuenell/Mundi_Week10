#pragma once
#include "MeshComponent.h"
#include "Enums.h"
#include "AABB.h"

class USkeletalMesh;
class UShader;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FSceneCompData;

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

	void SetSkeletalMesh(const FString& PathFileName);
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

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

	// CPU Skinning 업데이트
	void UpdateSkinning(ID3D11DeviceContext* DeviceContext);

protected:
	void OnTransformUpdated() override;
	void MarkWorldPartitionDirty();

protected:
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<UMaterialInterface*> MaterialSlots;
	TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;

	// 본 Transform 배열 (현재는 BindPose 사용, 나중에 애니메이션 추가 시 수정)
	TArray<FMatrix> BoneTransforms;
	void CalculateBoneTransforms();
};
