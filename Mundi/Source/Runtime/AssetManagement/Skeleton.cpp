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
	int32 NewIndex = static_cast<int32>(Bones.size());
	Bones.push_back(FBoneInfo(BoneName, ParentIndex));
	BoneNameToIndexMap[BoneName] = NewIndex;

	UE_LOG("[Skeleton] Added bone [%d]: %s (Parent: %d)", NewIndex, BoneName.c_str(), ParentIndex);

	return NewIndex;
}

const FBoneInfo& USkeleton::GetBone(int32 BoneIndex) const
{
	static FBoneInfo InvalidBone;

	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid bone index: %d", BoneIndex);
		return InvalidBone;
	}

	return Bones[BoneIndex];
}

const FBoneInfo& USkeleton::GetBone(const FString& BoneName) const
{
	int32 BoneIndex = FindBoneIndex(BoneName);
	return GetBone(BoneIndex);
}

int32 USkeleton::FindBoneIndex(const FString& BoneName) const
{
	auto It = BoneNameToIndexMap.find(BoneName);
	if (It != BoneNameToIndexMap.end())
	{
		return It->second;
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
	TArray<int32> Children;

	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return Children;
	}

	for (size_t i = 0; i < Bones.size(); ++i)
	{
		if (Bones[i].ParentIndex == BoneIndex)
		{
			Children.push_back(static_cast<int32>(i));
		}
	}

	return Children;
}

void USkeleton::LogBoneHierarchy() const
{
	if (Bones.empty())
	{
		UE_LOG("[Skeleton] Bone hierarchy is empty");
		return;
	}

	UE_LOG("[Skeleton] Bone Hierarchy (Bones: %zu)", Bones.size());

	int32 RootCount = 0;
	for (size_t BoneIndex = 0; BoneIndex < Bones.size(); ++BoneIndex)
	{
		if (Bones[BoneIndex].ParentIndex == -1)
		{
			++RootCount;
			LogBoneHierarchyRecursive(static_cast<int32>(BoneIndex), 0);
		}
	}

	if (RootCount == 0)
	{
		UE_LOG("[Skeleton] Root bone not found; dumping linear list");
		for (size_t BoneIndex = 0; BoneIndex < Bones.size(); ++BoneIndex)
		{
			const FBoneInfo& Bone = Bones[BoneIndex];
			UE_LOG("  [%zu] %s (Parent: %d)", BoneIndex, Bone.Name.c_str(), Bone.ParentIndex);
		}
	}
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

void USkeleton::SetGlobalBindPoseMatrix(int32 BoneIndex, const FMatrix& Matrix)
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		UE_LOG("[Skeleton] Invalid bone index for SetGlobalBindPoseMatrix: %d", BoneIndex);
		return;
	}

	Bones[BoneIndex].GlobalBindPoseMatrix = Matrix;
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

void USkeleton::LogBoneHierarchyRecursive(int32 BoneIndex, int32 Depth) const
{
	if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
	{
		return;
	}

	const FBoneInfo& Bone = Bones[BoneIndex];
	FString Line;

	if (Depth == 0)
	{
		Line = Bone.Name;
	}
	else
	{
		Line.append(static_cast<size_t>(Depth - 1) * 2, ' ');
		Line += "└";
		Line += Bone.Name;
	}

	UE_LOG("%s", Line.c_str());

	const TArray<int32> ChildBones = GetChildBones(BoneIndex);
	for (int32 ChildIndex : ChildBones)
	{
		LogBoneHierarchyRecursive(ChildIndex, Depth + 1);
	}
}
