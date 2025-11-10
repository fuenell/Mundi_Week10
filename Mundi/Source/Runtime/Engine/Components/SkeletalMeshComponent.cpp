#include "pch.h"
#include "SkeletalMeshComponent.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "애니메이션 재생이 가능한 스켈레탈 메시 컴포넌트입니다.")
	// TODO: Animation 관련 프로퍼티 추가 (Phase 6+)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
	// TODO: Animation 관련 초기화 (Phase 6+)
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	// TODO: Animation 관련 리소스 정리 (Phase 6+)
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	// TODO: Animation 관련 복사 (Phase 6+)
}
