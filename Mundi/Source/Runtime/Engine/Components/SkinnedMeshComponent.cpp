#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "JsonSerializer.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
    MARK_AS_COMPONENT("스킨드 메시 컴포넌트", "스킨드 메시의 기본 기능을 제공합니다.")
    //PROPERTY(SkeletalMesh, EPropertyType::SkeletalMesh, "Mesh", "렌더링할 스켈레탈 메시")
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent()
{
    bCanEverTick = true;
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
    SkeletalMesh = nullptr;
    MaterialSlots.Empty();
}

void USkinnedMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();
}

void USkinnedMeshComponent::BeginPlay()
{
    Super::BeginPlay();
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // 본 변환 업데이트
    UpdateBoneTransforms();
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    SkeletalMesh = InSkeletalMesh;

    if (SkeletalMesh)
    {
        const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetReferenceSkeleton();
        int32 BoneCount = RefSkeleton.Bones.Num();

        BoneMatrices.SetNum(BoneCount);
        BoneSpaceTransforms.SetNum(BoneCount);

        // Material Slots 초기화
        uint64 GroupCount = SkeletalMesh->GetMeshGroupCount();
        MaterialSlots.resize(static_cast<size_t>(GroupCount));

        UE_LOG("SetSkeletalMesh: %s (Bones: %d, Groups: %llu)",
            SkeletalMesh->GetAssetPathFileName().c_str(),
            BoneCount,
            GroupCount);
    }
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
    if ((int32)InSectionIndex < MaterialSlots.Num())
    {
        return MaterialSlots[InSectionIndex];
    }
    return nullptr;
}

void USkinnedMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
    if ((int32)InElementIndex >= MaterialSlots.Num())
    {
        MaterialSlots.resize(InElementIndex + 1);
    }
    MaterialSlots[InElementIndex] = InNewMaterial;
}

void USkinnedMeshComponent::UpdateBoneTransforms()
{
    if (!SkeletalMesh)
        return;

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetReferenceSkeleton();

    if (RefSkeleton.Bones.Num() == 0)
        return;

    // 배열 크기 확인
    if (BoneMatrices.Num() != RefSkeleton.Bones.Num())
    {
        BoneMatrices.SetNum(RefSkeleton.Bones.Num());
        BoneSpaceTransforms.SetNum(RefSkeleton.Bones.Num());
    }

    // 루트 본부터 재귀적으로 계산
    for (int32 i = 0; i < RefSkeleton.Bones.Num(); ++i)
    {
        if (RefSkeleton.Bones[i].ParentIndex == -1)
        {
            ComputeBoneTransform(i, FMatrix::Identity());
        }
    }
}

void USkinnedMeshComponent::ComputeBoneTransform(int32 BoneIndex, const FMatrix& ParentTransform)
{
    if (!SkeletalMesh)
        return;

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetReferenceSkeleton();

    if (BoneIndex < 0 || BoneIndex >= RefSkeleton.Bones.Num())
        return;

    const FBoneInfo& Bone = RefSkeleton.Bones[BoneIndex];

    // World Transform = LocalTransform * ParentTransform (row-major)
    FMatrix WorldTransform = Bone.LocalTransform * ParentTransform;
    BoneSpaceTransforms[BoneIndex] = WorldTransform;

    // Skinning Matrix = InverseBindPose * WorldTransform
    // 정점을 Bind Pose 기준으로 변환한 후, 현재 포즈로 변환
    BoneMatrices[BoneIndex] = Bone.InverseBindPose * WorldTransform;

    // 자식 본 재귀 처리
    for (int32 i = 0; i < RefSkeleton.Bones.Num(); ++i)
    {
        if (RefSkeleton.Bones[i].ParentIndex == BoneIndex)
        {
            ComputeBoneTransform(i, WorldTransform);
        }
    }
}

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    // TODO: SkeletalMesh 경로 직렬화
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}
