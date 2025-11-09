#include "pch.h"
#include "SkeletalMeshComponent.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링하고 애니메이션을 재생하는 컴포넌트입니다.")
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	// TODO: 애니메이션 데이터 복제 추가 예정
}
