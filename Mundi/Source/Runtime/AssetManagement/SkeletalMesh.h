#pragma once

#include "ResourceBase.h"
#include "Skeleton.h"
#include "Enums.h"
#include <d3d11.h>

/**
 * Bone Influence 구조체
 * 각 정점에 영향을 주는 Bone과 가중치 정보
 */
struct FBoneInfluence
{
	// Bone 인덱스
	int32 BoneIndex;

	// 가중치 (0.0 ~ 1.0)
	float Weight;

	FBoneInfluence()
		: BoneIndex(-1)
		, Weight(0.0f)
	{
	}

	FBoneInfluence(int32 InBoneIndex, float InWeight)
		: BoneIndex(InBoneIndex)
		, Weight(InWeight)
	{
	}
};

/**
 * Skinned Vertex 구조체
 * Skeletal Mesh의 정점 데이터 (GPU 전송용)
 *
 * 최대 4개의 Bone Influence 지원 (일반적인 게임 엔진 표준)
 */
struct FSkinnedVertex
{
	// 위치 (Local Space)
	FVector Position;

	// 법선 (Local Space)
	FVector Normal;

	// UV 좌표
	FVector2D UV;

	// Tangent (접선)
	FVector4 Tangent;

	// Bone 인덱스 (최대 4개)
	int32 BoneIndices[4];

	// Bone 가중치 (최대 4개, 합이 1.0)
	float BoneWeights[4];

	FSkinnedVertex()
		: Position(FVector(0, 0, 0))
		, Normal(FVector(0, 0, 1))
		, UV(FVector2D(0, 0))
		, Tangent(FVector4(1, 0, 0, 1))
	{
		for (int i = 0; i < 4; ++i)
		{
			BoneIndices[i] = 0;
			BoneWeights[i] = 0.0f;
		}
	}
};

/**
 * Skeletal Mesh 클래스
 * Bone에 의해 변형되는 Mesh 데이터를 관리
 * Unreal Engine의 USkeletalMesh와 유사한 구조
 *
 * 주요 기능:
 * - Skinned Vertex 데이터 관리 (Bone Weights 포함)
 * - Skeleton 참조
 * - GPU 버퍼 관리 (Vertex Buffer, Index Buffer)
 */
class USkeletalMesh : public UResourceBase
{
public:
	DECLARE_CLASS(USkeletalMesh, UResourceBase)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	// === Skeleton 관리 ===

	/**
	 * Skeleton 설정
	 * @param InSkeleton - Skeleton 객체
	 */
	void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }

	/**
	 * Skeleton 가져오기
	 * @return Skeleton 객체
	 */
	USkeleton* GetSkeleton() const { return Skeleton; }

	// === Mesh 데이터 관리 ===

	/**
	 * Skinned Vertex 데이터 설정
	 * @param InVertices - Vertex 배열
	 */
	void SetVertices(const TArray<FSkinnedVertex>& InVertices);

	/**
	 * Index 데이터 설정
	 * @param InIndices - Index 배열
	 */
	void SetIndices(const TArray<uint32>& InIndices);

	/**
	 * Vertex 개수 반환
	 * @return Vertex 개수
	 */
	uint32 GetVertexCount() const { return VertexCount; }

	/**
	 * Index 개수 반환
	 * @return Index 개수
	 */
	uint32 GetIndexCount() const { return IndexCount; }

	// === GPU 리소스 관리 ===

	/**
	 * GPU 버퍼 생성 (Vertex Buffer, Index Buffer)
	 * @param Device - D3D11 Device
	 * @return 성공 여부
	 */
	bool CreateGPUResources(ID3D11Device* Device);

	/**
	 * Vertex Buffer 가져오기
	 * @return D3D11 Vertex Buffer
	 */
	ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }

	/**
	 * Index Buffer 가져오기
	 * @return D3D11 Index Buffer
	 */
	ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }

	/**
	 * Vertex Stride 반환
	 * @return Vertex 크기 (바이트)
	 */
	uint32 GetVertexStride() const { return sizeof(FSkinnedVertex); }

	// === Serialization ===

	/**
	 * SkeletalMesh 직렬화/역직렬화
	 * @param bIsLoading - true인 경우 로드, false인 경우 저장
	 * @param InOutHandle - JSON 핸들
	 */
	void Serialize(bool bIsLoading, JSON& InOutHandle) override;

private:
	/**
	 * GPU 리소스 정리
	 */
	void ReleaseGPUResources();

private:
	// Skeleton 참조
	USkeleton* Skeleton = nullptr;

	// CPU Mesh 데이터
	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;

	// GPU 리소스
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	uint32 VertexCount = 0;
	uint32 IndexCount = 0;
};
