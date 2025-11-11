#pragma once

#include "ResourceBase.h"
#include "Skeleton.h"
#include "Enums.h"
#include "FbxImportOptions.h"
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

	// Binary serialization operators
	friend class FWindowsBinWriter;
	friend class FWindowsBinReader;
	friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkinnedVertex& Vertex);
	friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkinnedVertex& Vertex);
};

/**
 * Skeletal Mesh 데이터 구조체 (Serialization 전용)
 * FBX Import 결과를 담는 순수 데이터 컨테이너
 * USkeletalMesh::Load() 함수에서 이 데이터를 받아서 UObject로 변환
 */
struct FSkeletalMesh
{
	// Mesh 데이터
	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;
	TArray<int32> VertexToControlPointMap;

	// Material 정보 (병합 후 처리용)
	TArray<int32> PolygonMaterialIndices;  // 각 Polygon(Triangle)의 Material Index
	TArray<FString> MaterialNames;          // Material 이름 배열

	TArray<FGroupInfo> GroupInfos;


	// Skeleton 데이터
	USkeleton* Skeleton = nullptr;

	// 기본 생성자
	FSkeletalMesh() = default;

	// 이동 생성자
	FSkeletalMesh(FSkeletalMesh&& Other) noexcept
		: Vertices(std::move(Other.Vertices))
		, Indices(std::move(Other.Indices))
		, VertexToControlPointMap(std::move(Other.VertexToControlPointMap))
		, PolygonMaterialIndices(std::move(Other.PolygonMaterialIndices))
		, MaterialNames(std::move(Other.MaterialNames))
		, GroupInfos(std::move(Other.GroupInfos))
		, Skeleton(Other.Skeleton)
	{
		Other.Skeleton = nullptr;
	}

	// 이동 대입 연산자
	FSkeletalMesh& operator=(FSkeletalMesh&& Other) noexcept
	{
		if (this != &Other)
		{
			Vertices = std::move(Other.Vertices);
			Indices = std::move(Other.Indices);
			VertexToControlPointMap = std::move(Other.VertexToControlPointMap);
			PolygonMaterialIndices = std::move(Other.PolygonMaterialIndices);
			MaterialNames = std::move(Other.MaterialNames);
			GroupInfos = std::move(Other.GroupInfos);
			Skeleton = Other.Skeleton;
			Other.Skeleton = nullptr;
		}
		return *this;
	}

	// 복사 방지
	FSkeletalMesh(const FSkeletalMesh&) = delete;
	FSkeletalMesh& operator=(const FSkeletalMesh&) = delete;

	// 유효성 검사
	bool IsValid() const
	{
		return !Vertices.empty() && !Indices.empty() && Skeleton != nullptr;
	}

	// Binary serialization operators
	friend class FWindowsBinWriter;
	friend class FWindowsBinReader;
	friend FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkeletalMesh& Mesh);
	friend FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkeletalMesh& Mesh);
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
 *
 * Dual-Buffer 구조:
 * - CPU: FSkinnedVertex (80 bytes) - Bone 데이터 포함, CPU Skinning 계산용
 * - GPU: FNormalVertex (64 bytes) - Bone 데이터 제외, 렌더링 전용 (UberLit.hlsl 호환)
 * - 이를 통해 GPU 메모리 절약 및 기존 셰이더 재사용 가능
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

	// === Material 관리 ===

	/**
	 * Material 이름 설정 (FBX Import 시 사용)
	 * @param InMaterialName - Material Asset 이름
	 */
	void SetMaterialName(const FString& InMaterialName) { MaterialName = InMaterialName; }

	/**
	 * Material 이름 추가 (다중 Material 지원)
	 * @param InMaterialName - 추가할 Material Asset 이름
	 */
	void AddMaterialName(const FString& InMaterialName) { MaterialNames.push_back(InMaterialName); }

	/**
	 * Material 이름 가져오기 (레거시 - 단일 Material)
	 * @return Material Asset 이름 (ResourceManager에서 찾을 때 사용)
	 */
	const FString& GetMaterialName() const { return MaterialName; }

	/**
	 * 모든 Material 이름 가져오기 (다중 Material 지원)
	 * @return Material Asset 이름 배열
	 */
	const TArray<FString>& GetMaterialNames() const { return MaterialNames; }

	/**
	 * Material 개수 가져오기
	 * @return Material 개수
	 */
	size_t GetMaterialCount() const { return MaterialNames.size(); }

	/**
	 * Mesh Group 정보 가져오기 (Material별 Index 범위)
	 * @return GroupInfo 배열
	 */
	const TArray<FGroupInfo>& GetMeshGroupInfo() const { return GroupInfos; }

	/**
	 * Mesh Group 개수 가져오기
	 * @return Group 개수
	 */
	uint64 GetMeshGroupCount() const { return GroupInfos.size(); }

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
	 * Vertex to Control Point 매핑 데이터 설정
	 * FBX Import 시 Polygon Vertex Index → Control Point Index 매핑을 저장
	 * @param InMap - 매핑 배열 (각 Vertex의 Control Point Index)
	 */
	void SetVertexToControlPointMap(const TArray<int32>& InMap)
	{
		VertexToControlPointMap = InMap;
	}

	/**
	 * Vertex to Control Point 매핑 데이터 가져오기
	 * @return 매핑 배열 (읽기 전용)
	 */
	const TArray<int32>& GetVertexToControlPointMap() const
	{
		return VertexToControlPointMap;
	}

	/**
	 * Vertices 배열 참조 가져오기 (수정 가능)
	 * ExtractSkinWeights에서 Bone Weights를 적용할 때 사용
	 * @return Vertices 배열 참조
	 */
	TArray<FSkinnedVertex>& GetVerticesRef()
	{
		return Vertices;
	}

	/**
	 * Indices 배열 참조 반환 (Direct Access)
	 * ExtractSkinWeights에서 Winding Order를 수정할 때 사용
	 * @return Indices 배열 참조
	 */
	TArray<uint32>& GetIndicesRef()
	{
		return Indices;
	}

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
	 * Static Buffer (Bind Pose 렌더링용)
	 * @param Device - D3D11 Device
	 * @return 성공 여부
	 */
	bool CreateGPUResources(ID3D11Device* Device);

	/**
	 * Dynamic GPU 버퍼 생성 (CPU Skinning용)
	 * @param Device - D3D11 Device
	 * @return 성공 여부
	 */
	bool CreateDynamicGPUResources(ID3D11Device* Device);

	/**
	 * Vertex Buffer 업데이트 (Dynamic Buffer 전용)
	 * @param Context - D3D11 Device Context
	 * @param NewVertices - 새로운 Vertex 데이터
	 * @return 성공 여부
	 */
	bool UpdateVertexBuffer(ID3D11DeviceContext* Context, const TArray<FNormalVertex>& NewVertices);

	/**
	 * Dynamic Buffer 사용 여부 설정
	 * @param bDynamic - true인 경우 Dynamic Buffer 사용
	 */
	void SetUseDynamicBuffer(bool bDynamic) { bUseDynamicBuffer = bDynamic; }

	/**
	 * Dynamic Buffer 사용 여부 확인
	 * @return Dynamic Buffer 사용 여부
	 */
	bool UsesDynamicBuffer() const { return bUseDynamicBuffer; }

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
	 * Vertex Stride 반환 (GPU 버퍼용)
	 * @return Vertex 크기 (바이트) - FNormalVertex 기준 (64 bytes)
	 *
	 * NOTE: CPU는 FSkinnedVertex(80 bytes)를 유지하지만,
	 *       GPU 버퍼는 FNormalVertex(64 bytes)로 변환하여 전송
	 */
	uint32 GetVertexStride() const { return sizeof(FNormalVertex); }

	// === Loading ===

	/**
	 * FBX 파일로부터 SkeletalMesh 로드
	 * ResourceManager 패턴에 맞춰 설계됨
	 * @param InFilePath - FBX 파일 경로
	 * @param InDevice - D3D11 Device (GPU 리소스 생성용)
	 * @param InOptions - FBX Import 옵션 (기본값 사용 가능)
	 */
	void Load(const FString& InFilePath, ID3D11Device* InDevice, const FFbxImportOptions& InOptions = FFbxImportOptions());

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

	// Material 이름 (레거시 - 단일 Material)
	FString MaterialName;

	// Material 이름 배열 (다중 Material 지원)
	TArray<FString> MaterialNames;

	// Material별 그룹 정보 (다중 Material 지원)
	TArray<FGroupInfo> GroupInfos;

	// CPU Mesh 데이터
	TArray<FSkinnedVertex> Vertices;
	TArray<uint32> Indices;

	// Vertex → Control Point 매핑 (FBX Import용)
	// Skin Weights를 적용할 때 사용
	TArray<int32> VertexToControlPointMap;

	// GPU 리소스
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	uint32 VertexCount = 0;
	uint32 IndexCount = 0;

	// Dynamic Buffer 사용 여부 (CPU Skinning용)
	bool bUseDynamicBuffer = false;
};
