#include "pch.h"
#include "SkeletalMesh.h"
#include "FbxImporter.h"
#include "GlobalConsole.h"

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

	UE_LOG("[SkeletalMesh] Loading FBX file: %s", InFilePath.c_str());

	// 1. FBX Importer 생성
	FFbxImporter FbxImporter;

	// 2. FBX Type Detection (Phase 0 추가)
	EFbxImportType FbxType = FbxImporter.DetectFbxType(InFilePath);

	if (FbxType == EFbxImportType::StaticMesh)
	{
		UE_LOG("[error] USkeletalMesh::Load failed: FBX file '%s' is a StaticMesh (has no skeleton or skinning)", InFilePath.c_str());
		UE_LOG("[error] Use UStaticMesh::Load() instead for static meshes");
		return;
	}

	if (FbxType == EFbxImportType::Animation)
	{
		UE_LOG("[error] USkeletalMesh::Load failed: FBX file '%s' is an Animation file (no mesh data)", InFilePath.c_str());
		UE_LOG("[error] Animation-only FBX files are not supported yet");
		return;
	}

	UE_LOG("[SkeletalMesh] FBX Type confirmed: SkeletalMesh");

	// 3. FBX 데이터 Import
	FSkeletalMesh MeshData;

	if (!FbxImporter.ImportSkeletalMesh(InFilePath, InOptions, MeshData))
	{
		UE_LOG("[error] USkeletalMesh::Load failed: FBX Import error - %s", FbxImporter.GetLastError().c_str());
		return;
	}

	if (!MeshData.IsValid())
	{
		UE_LOG("[error] USkeletalMesh::Load failed: Invalid mesh data from FBX");
		return;
	}

	UE_LOG("[SkeletalMesh] FBX Import successful (Vertices: %zu, Indices: %zu)",
		MeshData.Vertices.size(), MeshData.Indices.size());

	// 2. Move mesh data to this object
	Vertices = std::move(MeshData.Vertices);
	Indices = std::move(MeshData.Indices);
	VertexToControlPointMap = std::move(MeshData.VertexToControlPointMap);
	GroupInfos = std::move(MeshData.GroupInfos);
	Skeleton = MeshData.Skeleton;

	VertexCount = static_cast<uint32>(Vertices.size());
	IndexCount = static_cast<uint32>(Indices.size());

	UE_LOG("[SkeletalMesh] Mesh data loaded successfully (Groups: %zu)", GroupInfos.size());

	if (Skeleton)
	{
		UE_LOG("[SkeletalMesh] Bone hierarchy for %s", InFilePath.c_str());
		Skeleton->LogBoneHierarchy();
	}

	// 2.5. Extract Materials from FBX (FBX Scene이 아직 열려있음)
	// ExtractMaterials()에서 Material 생성 → ResourceManager 등록 → Material 이름 저장
	if (!FbxImporter.ExtractMaterialsFromScene(this))
	{
		UE_LOG("[warning] Failed to extract materials from FBX, using default material");
		// Material 추출 실패는 치명적이지 않으므로 계속 진행
	}

	// 3. Create Dynamic GPU resources for CPU Skinning
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
