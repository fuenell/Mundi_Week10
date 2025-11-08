#pragma once
#include "Actor.h"
#include "Enums.h"

class USkeletalMeshComponent;
class USkeletalMesh;

/**
 * ASkeletalMeshActor
 * Skeletal Mesh를 렌더링하는 액터
 * Unreal Engine의 ASkeletalMeshActor와 유사한 구조
 *
 * 현재 Phase 5 구현:
 * - SkeletalMeshComponent를 사용하여 Skeletal Mesh 렌더링
 *
 * 향후 확장 (Phase 6+):
 * - Animation 재생
 * - Bone Transform 조작
 * - IK (Inverse Kinematics)
 */
class ASkeletalMeshActor : public AActor
{
public:
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
	GENERATED_REFLECTION_BODY()

	ASkeletalMeshActor();
	virtual void Tick(float DeltaTime) override;

protected:
	~ASkeletalMeshActor() override;

public:
	virtual FAABB GetBounds() const override;

	/**
	 * SkeletalMeshComponent 가져오기
	 * @return SkeletalMeshComponent 포인터
	 */
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	/**
	 * SkeletalMeshComponent 설정
	 * @param InSkeletalMeshComponent - 설정할 SkeletalMeshComponent
	 */
	void SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent);

	/**
	 * SkeletalMesh 설정 (편의 함수)
	 * @param InSkeletalMesh - 설정할 SkeletalMesh
	 */
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

	// ───── 복사 관련 ────────────────────────────
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(ASkeletalMeshActor)

	// Serialize
	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

protected:
	USkeletalMeshComponent* SkeletalMeshComponent;
};
