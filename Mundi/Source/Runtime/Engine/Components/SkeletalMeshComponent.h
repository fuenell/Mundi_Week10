#pragma once
#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkeletalMeshComponent();

protected:
	~USkeletalMeshComponent() override;

public:
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkeletalMeshComponent)

	// TODO: 애니메이션 관련 기능 추가 예정
};
