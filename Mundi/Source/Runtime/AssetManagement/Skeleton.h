#pragma once

#include "ResourceBase.h"

/**
 * Bone 정보 구조체
 * Skeletal Mesh의 각 Bone에 대한 정보를 저장
 */
struct FBoneInfo
{
	// Bone 이름
	FString Name;

	// 부모 Bone 인덱스 (-1인 경우 Root Bone)
	int32 ParentIndex;

	// Bind Pose Transform (Local Space)
	// 초기 상태에서 부모 본 기준의 상대 Transform
	FTransform BindPoseRelativeTransform;

	// Global Bind Pose Matrix (Global Space)
	// FBX Cluster의 GetTransformLinkMatrix()에서 추출
	// CPU Skinning 시 이 값을 직접 사용
	FMatrix GlobalBindPoseMatrix;

	// Inverse Bind Pose Matrix (Global Space)
	// Skinning 시 사용 (Vertex를 Bone Space로 변환)
	FMatrix InverseBindPoseMatrix;

	// 기본 생성자
	FBoneInfo()
		: Name("")
		, ParentIndex(-1)
		, BindPoseRelativeTransform(FTransform())
		, GlobalBindPoseMatrix(FMatrix::Identity())
		, InverseBindPoseMatrix(FMatrix::Identity())
	{
	}

	// 생성자
	FBoneInfo(const FString& InName, int32 InParentIndex)
		: Name(InName)
		, ParentIndex(InParentIndex)
		, BindPoseRelativeTransform(FTransform())
		, GlobalBindPoseMatrix(FMatrix::Identity())
		, InverseBindPoseMatrix(FMatrix::Identity())
	{
	}

	// Binary serialization operators
	friend class FWindowsBinWriter;
	friend class FWindowsBinReader;
	friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FBoneInfo& Bone);
	friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FBoneInfo& Bone);
};

/**
 * Skeleton 클래스
 * Skeletal Mesh의 Bone 계층 구조를 관리
 * Unreal Engine의 USkeleton과 유사한 구조
 *
 * 주요 기능:
 * - Bone Hierarchy 관리 (Parent-Child 관계)
 * - Bone 이름/인덱스 검색
 * - Bind Pose 저장 및 관리
 */
class USkeleton : public UResourceBase
{
public:
	DECLARE_CLASS(USkeleton, UResourceBase)

	USkeleton() = default;
	~USkeleton() override = default;

	// === Bone 관리 ===

	/**
	 * Bone 추가
	 * @param BoneName - Bone 이름
	 * @param ParentIndex - 부모 Bone 인덱스 (-1인 경우 Root)
	 * @return 추가된 Bone의 인덱스
	 */
	int32 AddBone(const FString& BoneName, int32 ParentIndex);

	/**
	 * Bone 개수 반환
	 * @return Bone 개수
	 */
	int32 GetBoneCount() const { return static_cast<int32>(Bones.size()); }

	/**
	 * Bone 정보 가져오기 (인덱스)
	 * @param BoneIndex - Bone 인덱스
	 * @return Bone 정보 참조
	 */
	const FBoneInfo& GetBone(int32 BoneIndex) const;

	/**
	 * Bone 정보 가져오기 (이름)
	 * @param BoneName - Bone 이름
	 * @return Bone 정보 참조
	 */
	const FBoneInfo& GetBone(const FString& BoneName) const;

	/**
	 * Bone 인덱스 찾기
	 * @param BoneName - Bone 이름
	 * @return Bone 인덱스 (-1인 경우 찾지 못함)
	 */
	int32 FindBoneIndex(const FString& BoneName) const;

	/**
	 * Root Bone 인덱스 반환
	 * @return Root Bone 인덱스 (ParentIndex == -1)
	 */
	int32 GetRootBoneIndex() const;

	/**
	 * Bone의 자식 Bone 인덱스 목록 가져오기
	 * @param BoneIndex - 부모 Bone 인덱스
	 * @return 자식 Bone 인덱스 배열
	 */
	TArray<int32> GetChildBones(int32 BoneIndex) const;

	/**
	 * Bone 계층 구조를 보기 좋은 트리 형태로 로그 출력
	 */
	void LogBoneHierarchy() const;

	// === Bind Pose 관리 ===

	/**
	 * Bone의 Bind Pose Transform 설정 (Local Space)
	 * @param BoneIndex - Bone 인덱스
	 * @param Transform - Bind Pose Transform
	 */
	void SetBindPoseTransform(int32 BoneIndex, const FTransform& Transform);

	/**
	 * Bone의 Global Bind Pose Matrix 설정 (Global Space)
	 * FBX Cluster의 GetTransformLinkMatrix()에서 추출
	 * @param BoneIndex - Bone 인덱스
	 * @param Matrix - Global Bind Pose Matrix
	 */
	void SetGlobalBindPoseMatrix(int32 BoneIndex, const FMatrix& Matrix);

	/**
	 * Bone의 Inverse Bind Pose Matrix 설정 (Global Space)
	 * @param BoneIndex - Bone 인덱스
	 * @param Matrix - Inverse Bind Pose Matrix
	 */
	void SetInverseBindPoseMatrix(int32 BoneIndex, const FMatrix& Matrix);

	/**
	 * Bind Pose Transform 계산 완료 후 호출
	 * 필요한 경우 추가 처리 수행
	 */
	void FinalizeBones();

	// === Serialization ===

	/**
	 * Skeleton 직렬화/역직렬화
	 * @param bIsLoading - true인 경우 로드, false인 경우 저장
	 * @param InOutHandle - JSON 핸들
	 */
	void Serialize(bool bIsLoading, JSON& InOutHandle) override;

private:
	void LogBoneHierarchyRecursive(int32 BoneIndex, int32 Depth) const;

	// Bone 배열 (인덱스 순서대로 저장)
	TArray<FBoneInfo> Bones;

	// Bone 이름 → 인덱스 맵 (빠른 검색용)
	TMap<FString, int32> BoneNameToIndexMap;
};
