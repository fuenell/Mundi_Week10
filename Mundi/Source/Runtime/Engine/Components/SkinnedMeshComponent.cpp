#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "World.h"
#include "WorldPartitionManager.h"
#include "JsonSerializer.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
	MARK_AS_COMPONENT("스킨드 메시 컴포넌트", "스켈레탈 메시 렌더링의 기본 컴포넌트입니다 (레거시).")
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent()
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	// Base 클래스는 렌더링하지 않음
	// USkeletalMeshComponent에서 오버라이드하여 구현
}

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);
}

FAABB USkinnedMeshComponent::GetWorldAABB() const
{
	// Base 클래스는 원점 기준 빈 AABB 반환
	// USkeletalMeshComponent에서 오버라이드하여 구현
	const FVector Origin = GetWorldTransform().TransformPosition(FVector());
	return FAABB(Origin, Origin);
}

void USkinnedMeshComponent::OnTransformUpdated()
{
	Super::OnTransformUpdated();
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
