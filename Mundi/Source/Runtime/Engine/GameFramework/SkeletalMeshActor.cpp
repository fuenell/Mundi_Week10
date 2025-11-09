#include "pch.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "ObjectFactory.h"

IMPLEMENT_CLASS(ASkeletalMeshActor)

BEGIN_PROPERTIES(ASkeletalMeshActor)
	MARK_AS_SPAWNABLE("스켈레탈 메시", "스켈레탈 메시를 렌더링하는 액터입니다.")
END_PROPERTIES()

ASkeletalMeshActor::ASkeletalMeshActor()
{
	ObjectName = "Skeletal Mesh Actor";
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");

	// 루트 교체
	RootComponent = SkeletalMeshComponent;
}

void ASkeletalMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

ASkeletalMeshActor::~ASkeletalMeshActor()
{

}

FAABB ASkeletalMeshActor::GetBounds() const
{
	USkeletalMeshComponent* CurrentSMC = Cast<USkeletalMeshComponent>(RootComponent);
	if (CurrentSMC)
	{
		return CurrentSMC->GetWorldAABB();
	}

	return FAABB();
}

void ASkeletalMeshActor::SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	SkeletalMeshComponent = InSkeletalMeshComponent;
}

void ASkeletalMeshActor::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	for (UActorComponent* Component : OwnedComponents)
	{
		if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
		{
			SkeletalMeshComponent = SkeletalMeshComp;
			break;
		}
	}
}

void ASkeletalMeshActor::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	if (bInIsLoading)
	{
		SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RootComponent);
	}
}
