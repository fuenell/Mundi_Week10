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
 * Skeletal Mesh 렌더링의 Base 컴포넌트 (레거시 - Unreal Engine 5 구조)
 *
 * 담당 기능:
 * - 렌더링 인터페이스만 제공
 * - Material 슬롯 관리 (부모 UMeshComponent에서)
 *
 * 상속 구조:
 * - USkeletalMeshComponent: 실제 SkeletalMesh, Bone Transform, Animation 담당
 */
class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkinnedMeshComponent();

protected:
	~USkinnedMeshComponent() override;

public:
	virtual void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View) override;

	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

	virtual FAABB GetWorldAABB() const override;

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkinnedMeshComponent)

protected:
	void OnTransformUpdated() override;
};
