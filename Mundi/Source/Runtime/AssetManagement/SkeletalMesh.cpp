#include "pch.h"
#include "SkeletalMesh.h"
#include "FbxImporter.h"
#include "FbxManager.h"
#include "GlobalConsole.h"
#include "Archive.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "ObjectFactory.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
	ReleaseGPUResources();
}

void USkeletalMesh::SetVertices(const TArray<FSkinnedVertex>& InVertices)
{
	Vertices = InVertices;
	VertexCount = static_cast<uint32>(Vertices.size());

	UE_LOG("[SkeletalMesh] Set %u vertices", VertexCount);
}

void USkeletalMesh::SetIndices(const TArray<uint32>& InIndices)
{
	Indices = InIndices;
	IndexCount = static_cast<uint32>(Indices.size());

	UE_LOG("[SkeletalMesh] Set %u indices (%u triangles)", IndexCount, IndexCount / 3);
}

bool USkeletalMesh::CreateGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		UE_LOG("[error] CreateGPUResources failed: Device is null");
		return false;
	}

	if (Vertices.empty() || Indices.empty())
	{
		UE_LOG("[error] CreateGPUResources failed: No vertex/index data");
		return false;
	}

	// 기존 리소스 정리
	ReleaseGPUResources();

	// === Dual-Buffer Approach ===
	// CPU는 FSkinnedVertex (80 bytes)를 유지하고,
	// GPU는 FNormalVertex (64 bytes)로 변환하여 전송
	// 이를 통해 UberLit.hlsl 셰이더와 호환 가능 + GPU 메모리 절약

	// 1. FSkinnedVertex → FNormalVertex 변환
	TArray<FNormalVertex> GPUVertices;
	GPUVertices.reserve(VertexCount);

	for (const FSkinnedVertex& SkinnedVert : Vertices)
	{
		FNormalVertex NormalVert;
		NormalVert.pos = SkinnedVert.Position;
		NormalVert.normal = SkinnedVert.Normal;
		NormalVert.tex = SkinnedVert.UV;
		NormalVert.Tangent = SkinnedVert.Tangent;
		NormalVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 기본 흰색

		GPUVertices.push_back(NormalVert);
	}

	// 2. Vertex Buffer 생성 (FNormalVertex 기준)
	D3D11_BUFFER_DESC VbDesc = {};
	VbDesc.Usage = D3D11_USAGE_DEFAULT;
	VbDesc.ByteWidth = sizeof(FNormalVertex) * VertexCount;
	VbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	VbDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA VbData = {};
	VbData.pSysMem = GPUVertices.data();

	HRESULT Hr = Device->CreateBuffer(&VbDesc, &VbData, &VertexBuffer);
	if (FAILED(Hr))
	{
		UE_LOG("[error] Failed to create Vertex Buffer");
		return false;
	}

	// 3. Index Buffer 생성
	D3D11_BUFFER_DESC IbDesc = {};
	IbDesc.Usage = D3D11_USAGE_DEFAULT;
	IbDesc.ByteWidth = sizeof(uint32) * IndexCount;
	IbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	IbDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA IbData = {};
	IbData.pSysMem = Indices.data();

	Hr = Device->CreateBuffer(&IbDesc, &IbData, &IndexBuffer);
	if (FAILED(Hr))
	{
		UE_LOG("[error] Failed to create Index Buffer");
		ReleaseGPUResources();
		return false;
	}

	UE_LOG("[SkeletalMesh] GPU resources created successfully (%u vertices, %u indices)",
		VertexCount, IndexCount);
	UE_LOG("[SkeletalMesh] Dual-buffer approach: CPU (80 bytes/vertex), GPU (64 bytes/vertex)");

	return true;
}

bool USkeletalMesh::CreateDynamicGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		UE_LOG("[error] CreateDynamicGPUResources: Device is null");
		return false;
	}

	if (Vertices.empty() || Indices.empty())
	{
		UE_LOG("[error] CreateDynamicGPUResources: No vertex/index data");
		return false;
	}

	// 기존 리소스 정리
	ReleaseGPUResources();

	// GPU Vertices 준비 (초기 데이터 - Bind Pose)
	TArray<FNormalVertex> GPUVertices;
	GPUVertices.reserve(VertexCount);

	for (const FSkinnedVertex& SkinnedVert : Vertices)
	{
		FNormalVertex NormalVert;
		NormalVert.pos = SkinnedVert.Position;
		NormalVert.normal = SkinnedVert.Normal;
		NormalVert.tex = SkinnedVert.UV;
		NormalVert.Tangent = SkinnedVert.Tangent;
		NormalVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

		GPUVertices.push_back(NormalVert);
	}

	// Dynamic Vertex Buffer 생성
	D3D11_BUFFER_DESC VbDesc = {};
	VbDesc.Usage = D3D11_USAGE_DYNAMIC;              // Dynamic!
	VbDesc.ByteWidth = sizeof(FNormalVertex) * VertexCount;
	VbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	VbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU Write 허용

	D3D11_SUBRESOURCE_DATA VbData = {};
	VbData.pSysMem = GPUVertices.data();

	HRESULT Hr = Device->CreateBuffer(&VbDesc, &VbData, &VertexBuffer);
	if (FAILED(Hr))
	{
		UE_LOG("[error] Failed to create Dynamic Vertex Buffer");
		return false;
	}

	// Index Buffer는 Static으로 유지 (변경 없음)
	D3D11_BUFFER_DESC IbDesc = {};
	IbDesc.Usage = D3D11_USAGE_DEFAULT;  // Static
	IbDesc.ByteWidth = sizeof(uint32) * IndexCount;
	IbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	IbDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA IbData = {};
	IbData.pSysMem = Indices.data();

	Hr = Device->CreateBuffer(&IbDesc, &IbData, &IndexBuffer);
	if (FAILED(Hr))
	{
		UE_LOG("[error] Failed to create Index Buffer");
		ReleaseGPUResources();
		return false;
	}

	bUseDynamicBuffer = true;

	UE_LOG("[SkeletalMesh] Dynamic GPU resources created (%u vertices, %u indices)",
		VertexCount, IndexCount);

	return true;
}

bool USkeletalMesh::UpdateVertexBuffer(ID3D11DeviceContext* Context, const TArray<FNormalVertex>& NewVertices)
{
	if (!Context || !VertexBuffer)
	{
		UE_LOG("[error] UpdateVertexBuffer: Invalid context or buffer");
		return false;
	}

	if (!bUseDynamicBuffer)
	{
		UE_LOG("[error] UpdateVertexBuffer: Not a dynamic buffer");
		return false;
	}

	if (NewVertices.size() != VertexCount)
	{
		UE_LOG("[error] UpdateVertexBuffer: Vertex count mismatch");
		return false;
	}

	// Map Vertex Buffer
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	HRESULT Hr = Context->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
	if (FAILED(Hr))
	{
		UE_LOG("[error] UpdateVertexBuffer: Failed to map buffer");
		return false;
	}

	// Vertex 데이터 복사
	memcpy(MappedResource.pData, NewVertices.data(), sizeof(FNormalVertex) * VertexCount);

	// Unmap
	Context->Unmap(VertexBuffer, 0);

	return true;
}

void USkeletalMesh::ReleaseGPUResources()
{
	if (VertexBuffer)
	{
		VertexBuffer->Release();
		VertexBuffer = nullptr;
	}

	if (IndexBuffer)
	{
		IndexBuffer->Release();
		IndexBuffer = nullptr;
	}
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, const FFbxImportOptions& InOptions)
{
	if (!InDevice)
	{
		UE_LOG("[error] USkeletalMesh::Load failed: Device is null");
		return;
	}

	if (InFilePath.empty())
	{
		UE_LOG("[error] USkeletalMesh::Load failed: Empty file path");
		return;
	}

	// ═══════════════════════════════════════════════════════════
	// FBX Skeletal Mesh: Delegate to FFbxManager (like OBJ pattern)
	// ═══════════════════════════════════════════════════════════
	FSkeletalMesh* MeshData = FFbxManager::LoadFbxSkeletalMeshAsset(InFilePath);

	if (!MeshData || !MeshData->IsValid())
	{
		UE_LOG("[error] FFbxManager failed to load skeletal mesh: %s", InFilePath.c_str());
		return;
	}

	UE_LOG("[SkeletalMesh] Loaded from FFbxManager (Vertices: %zu, Indices: %zu, Bones: %zu)",
		MeshData->Vertices.size(), MeshData->Indices.size(),
		MeshData->Skeleton ? MeshData->Skeleton->GetBoneCount() : 0);

	// ═══════════════════════════════════════════════════════════
	// Move data from FSkeletalMesh to USkeletalMesh
	// IMPORTANT: Use std::move to transfer ownership and avoid double-free
	// (FFbxManager owns FSkeletalMesh*, but we need to take ownership of the data)
	// ═══════════════════════════════════════════════════════════
	Vertices = std::move(MeshData->Vertices);
	Indices = std::move(MeshData->Indices);
	VertexToControlPointMap = std::move(MeshData->VertexToControlPointMap);
	GroupInfos = std::move(MeshData->GroupInfos);
	MaterialNames = std::move(MeshData->MaterialNames);
	Skeleton = MeshData->Skeleton;
	CacheFilePath = std::move(MeshData->CacheFilePath);

	VertexCount = static_cast<uint32>(Vertices.size());
	IndexCount = static_cast<uint32>(Indices.size());

	if (Skeleton)
	{
		UE_LOG("[SkeletalMesh] Bone hierarchy for %s", InFilePath.c_str());
		Skeleton->LogBoneHierarchy();
	}

	// ═══════════════════════════════════════════════════════════
	// Create Dynamic GPU resources for CPU Skinning
	// ═══════════════════════════════════════════════════════════
	if (!CreateDynamicGPUResources(InDevice))
	{
		UE_LOG("[error] USkeletalMesh::Load failed: Failed to create GPU resources");
		return;
	}

	UE_LOG("[SkeletalMesh] Load completed successfully (Dynamic Buffer for CPU Skinning)");
}

void USkeletalMesh::Serialize(bool bIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bIsLoading, InOutHandle);

	if (bIsLoading)
	{
		// TODO: SkeletalMesh 로드 구현 (Phase 5 - Editor 통합 시)
		UE_LOG("[SkeletalMesh] Serialize (Load): Not implemented yet");
	}
	else
	{
		// TODO: SkeletalMesh 저장 구현 (Phase 5 - Editor 통합 시)
		UE_LOG("[SkeletalMesh] Serialize (Save): Not implemented yet");
	}
}

// ═══════════════════════════════════════════════════════════
// FSkinnedVertex 바이너리 직렬화
// ═══════════════════════════════════════════════════════════

FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkinnedVertex& Vertex)
{
	// POD 데이터를 일괄 쓰기
	Writer.Serialize((void*)&Vertex.Position, sizeof(FVector));
	Writer.Serialize((void*)&Vertex.Normal, sizeof(FVector));
	Writer.Serialize((void*)&Vertex.Tangent, sizeof(FVector4));
	Writer.Serialize((void*)&Vertex.UV, sizeof(FVector2D));
	Writer.Serialize((void*)Vertex.BoneIndices, sizeof(int32) * 4);
	Writer.Serialize((void*)Vertex.BoneWeights, sizeof(float) * 4);

	return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkinnedVertex& Vertex)
{
	Reader.Serialize(&Vertex.Position, sizeof(FVector));
	Reader.Serialize(&Vertex.Normal, sizeof(FVector));
	Reader.Serialize(&Vertex.Tangent, sizeof(FVector4));
	Reader.Serialize(&Vertex.UV, sizeof(FVector2D));
	Reader.Serialize(Vertex.BoneIndices, sizeof(int32) * 4);
	Reader.Serialize(Vertex.BoneWeights, sizeof(float) * 4);

	return Reader;
}

// ═══════════════════════════════════════════════════════════
// FSkeletalMesh 바이너리 직렬화
// ═══════════════════════════════════════════════════════════

FWindowsBinWriter& operator<<(FWindowsBinWriter& Writer, const FSkeletalMesh& Mesh)
{
	// 매직 넘버와 버전 쓰기
	uint32 MagicNumber = 0x46425843;  // "FBXC" (hex)
	uint32 Version = 1;
	uint8 TypeFlag = 1;  // 0 = StaticMesh, 1 = SkeletalMesh
	Writer << MagicNumber;
	Writer << Version;
	Writer << TypeFlag;

	// 정점 개수와 데이터 쓰기
	uint32 VertexCount = static_cast<uint32>(Mesh.Vertices.size());
	Writer << VertexCount;
	for (const FSkinnedVertex& Vertex : Mesh.Vertices)
	{
		Writer << Vertex;  // FSkinnedVertex의 operator<< 사용
	}

	// 인덱스 개수와 데이터 쓰기
	uint32 IndexCount = static_cast<uint32>(Mesh.Indices.size());
	Writer << IndexCount;
	Writer.Serialize((void*)Mesh.Indices.data(), IndexCount * sizeof(uint32));

	// 스켈레톤 데이터(본) 쓰기 - USkeleton이 있으면 추출
	if (Mesh.Skeleton != nullptr)
	{
		uint32 BoneCount = static_cast<uint32>(Mesh.Skeleton->GetBoneCount());
		Writer << BoneCount;
		for (int32 i = 0; i < static_cast<int32>(BoneCount); ++i)
		{
			const FBoneInfo& Bone = Mesh.Skeleton->GetBone(i);
			Writer << Bone;  // FBoneInfo의 operator<< 사용
		}
	}
	else
	{
		uint32 BoneCount = 0;
		Writer << BoneCount;
	}

	// 머티리얼 이름 쓰기
	uint32 MaterialCount = static_cast<uint32>(Mesh.MaterialNames.size());
	Writer << MaterialCount;
	for (const FString& MaterialName : Mesh.MaterialNames)
	{
		Serialization::WriteString(Writer, MaterialName);
	}

	// 머티리얼 그룹 쓰기
	uint32 GroupCount = static_cast<uint32>(Mesh.GroupInfos.size());
	Writer << GroupCount;
	for (const FGroupInfo& Group : Mesh.GroupInfos)
	{
		Writer << const_cast<FGroupInfo&>(Group);  // FGroupInfo의 operator<< 사용
	}

	return Writer;
}

FWindowsBinReader& operator>>(FWindowsBinReader& Reader, FSkeletalMesh& Mesh)
{
	// 매직 넘버 읽기 및 검증
	uint32 MagicNumber, Version;
	uint8 TypeFlag;
	Reader << MagicNumber;
	Reader << Version;
	Reader << TypeFlag;

	if (MagicNumber != 0x46425843)
	{
		UE_LOG("[error] Invalid FBX cache file (bad magic number)");
		return Reader;
	}

	if (Version != 1)
	{
		UE_LOG("[error] Unsupported FBX cache version: %d", Version);
		return Reader;
	}

	if (TypeFlag != 1)
	{
		UE_LOG("[error] Invalid FBX cache type flag (expected SkeletalMesh=1, got %d)", TypeFlag);
		return Reader;
	}

	// 정점 읽기
	uint32 VertexCount;
	Reader << VertexCount;
	Mesh.Vertices.clear();
	Mesh.Vertices.reserve(VertexCount);
	for (uint32 i = 0; i < VertexCount; ++i)
	{
		FSkinnedVertex Vertex;
		Reader >> Vertex;
		Mesh.Vertices.push_back(Vertex);
	}

	// 인덱스 읽기
	uint32 IndexCount;
	Reader << IndexCount;
	Mesh.Indices.resize(IndexCount);
	Reader.Serialize(Mesh.Indices.data(), IndexCount * sizeof(uint32));

	// 스켈레톤 데이터(본) 읽기 - 이후 USkeleton 생성 필요
	uint32 BoneCount;
	Reader << BoneCount;

	if (BoneCount > 0)
	{
		// USkeleton 생성 및 채우기
		Mesh.Skeleton = ObjectFactory::NewObject<USkeleton>();

		for (uint32 i = 0; i < BoneCount; ++i)
		{
			FBoneInfo Bone;
			Reader >> Bone;

			// 스켈레톤에 본 추가
			int32 BoneIndex = Mesh.Skeleton->AddBone(Bone.Name, Bone.ParentIndex);

			// 본 트랜스폼 설정
			Mesh.Skeleton->SetBindPoseTransform(BoneIndex, Bone.BindPoseTransform);
			Mesh.Skeleton->SetGlobalBindPoseMatrix(BoneIndex, Bone.GlobalBindPoseMatrix);
			Mesh.Skeleton->SetInverseBindPoseMatrix(BoneIndex, Bone.InverseBindPoseMatrix);
		}
	}

	// 머티리얼 이름 읽기
	uint32 MaterialCount;
	Reader << MaterialCount;
	Mesh.MaterialNames.clear();
	Mesh.MaterialNames.reserve(MaterialCount);
	for (uint32 i = 0; i < MaterialCount; ++i)
	{
		FString MaterialName;
		Serialization::ReadString(Reader, MaterialName);
		Mesh.MaterialNames.push_back(MaterialName);
	}

	// 머티리얼 그룹 읽기
	uint32 GroupCount;
	Reader << GroupCount;
	Mesh.GroupInfos.clear();
	Mesh.GroupInfos.reserve(GroupCount);
	for (uint32 i = 0; i < GroupCount; ++i)
	{
		FGroupInfo Group;
		Reader << Group;  // FGroupInfo의 operator<< 사용
		Mesh.GroupInfos.push_back(Group);
	}

	return Reader;
}
