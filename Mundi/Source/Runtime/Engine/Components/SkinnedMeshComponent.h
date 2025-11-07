#pragma once
#include "MeshComponent.h"
#include "Vector.h"

class USkeletalMesh;
class UMaterialInterface;




class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
    GENERATED_REFLECTION_BODY()

    USkinnedMeshComponent();

protected:
    ~USkinnedMeshComponent() override;

public:
    // Lifecycle
    void InitializeComponent() override;
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;

    // Skeletal Mesh 설정
    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

    // Material 관리
    UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
    void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

    // Bone Transform 업데이트 (애니메이션에서 호출)
    void UpdateBoneTransforms();
    const TArray<FMatrix>& GetBoneMatrices() const { return BoneMatrices; }

    // Serialization
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(USkinnedMeshComponent)

protected:
    // 본 변환 계산 (재귀적으로 부모부터 계산)
    void ComputeBoneTransform(int32 BoneIndex, const FMatrix& ParentTransform);

protected:
    // 스켈레탈 메시 에셋
    USkeletalMesh* SkeletalMesh = nullptr;

    // 본 변환 행렬 (BoneTransform * InverseBindPose)
    // 이 행렬들이 스키닝에 사용됨
    TArray<FMatrix> BoneMatrices;

    // 현재 본 포즈 (애니메이션 적용 전/후)
    // 현재는 Reference Pose만 사용
    TArray<FMatrix> BoneSpaceTransforms;

    // Material Slots
    TArray<UMaterialInterface*> MaterialSlots;
};
