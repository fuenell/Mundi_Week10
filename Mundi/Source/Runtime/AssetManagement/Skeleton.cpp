#include "pch.h"
#include "Skeleton.h"
#include "GlobalConsole.h"

IMPLEMENT_CLASS(USkeleton)

int32 USkeleton::AddBone(const FString& BoneName, int32 ParentIndex)
{
	// 중복 체크
	if (BoneNameToIndexMap.find(BoneName) != BoneNameToIndexMap.end())
	{
		UE_LOG("[Skeleton] Bone already exists: %s", BoneName.c_str());
		return BoneNameToIndexMap[BoneName];
	}

	// 부모 인덱스 유효성 검사
	if (ParentIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid parent index %d for bone %s", ParentIndex, BoneName.c_str());
		return -1;
	}

	// Bone 추가
	int32 newIndex = static_cast<int32>(Bones.size());
	Bones.push_back(FBoneInfo(BoneName, ParentIndex));
	BoneNameToIndexMap[BoneName] = newIndex;

	UE_LOG("[Skeleton] Added bone [%d]: %s (Parent: %d)", newIndex, BoneName.c_str(), ParentIndex);

	return newIndex;
}

const FBoneInfo& USkeleton::GetBone(int32 BoneIndex) const
{
	static FBoneInfo invalidBone;

	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid bone index: %d", BoneIndex);
		return invalidBone;
	}

	return Bones[BoneIndex];
}

const FBoneInfo& USkeleton::GetBone(const FString& BoneName) const
{
	int32 index = FindBoneIndex(BoneName);
	return GetBone(index);
}

int32 USkeleton::FindBoneIndex(const FString& BoneName) const
{
	auto it = BoneNameToIndexMap.find(BoneName);
	if (it != BoneNameToIndexMap.end())
	{
		return it->second;
	}

	return -1;
}

int32 USkeleton::GetRootBoneIndex() const
{
	for (size_t i = 0; i < Bones.size(); ++i)
	{
		if (Bones[i].ParentIndex == -1)
		{
			return static_cast<int32>(i);
		}
	}

	return -1;
}

TArray<int32> USkeleton::GetChildBones(int32 BoneIndex) const
{
	TArray<int32> children;

	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return children;
	}

	for (size_t i = 0; i < Bones.size(); ++i)
	{
		if (Bones[i].ParentIndex == BoneIndex)
		{
			children.push_back(static_cast<int32>(i));
		}
	}

	return children;
}

void USkeleton::SetBindPoseTransform(int32 BoneIndex, const FTransform& Transform)
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid bone index for SetBindPoseTransform: %d", BoneIndex);
		return;
	}

	Bones[BoneIndex].BindPoseTransform = Transform;
}

void USkeleton::SetInverseBindPoseMatrix(int32 BoneIndex, const FMatrix& Matrix)
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid bone index for SetInverseBindPoseMatrix: %d", BoneIndex);
		return;
	}

	Bones[BoneIndex].InverseBindPoseMatrix = Matrix;
}

void USkeleton::FinalizeBones()
{
	UE_LOG("[Skeleton] Finalized %zu bones", Bones.size());

	// Bone Hierarchy 출력 (디버깅용)
	for (size_t i = 0; i < Bones.size(); ++i)
	{
		UE_LOG("  [%zu] %s (Parent: %d)", i, Bones[i].Name.c_str(), Bones[i].ParentIndex);
	}
}

void USkeleton::Serialize(bool bIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bIsLoading, InOutHandle);

	if (bIsLoading)
	{
		// TODO: Skeleton 로드 구현 (Phase 5 - Editor 통합 시)
		UE_LOG("[Skeleton] Serialize (Load): Not implemented yet");
	}
	else
	{
		// TODO: Skeleton 저장 구현 (Phase 5 - Editor 통합 시)
		UE_LOG("[Skeleton] Serialize (Save): Not implemented yet");
	}
}
