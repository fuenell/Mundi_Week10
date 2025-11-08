#include "pch.h"
#include "SkeletalMesh.h"
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
		OutputDebugStringA("[SkeletalMesh] CreateGPUResources failed: Device is null\n");
		return false;
	}

	if (Vertices.empty() || Indices.empty())
	{
		OutputDebugStringA("[SkeletalMesh] CreateGPUResources failed: No vertex/index data\n");
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
	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.ByteWidth = sizeof(FNormalVertex) * VertexCount;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA vbData = {};
	vbData.pSysMem = GPUVertices.data();

	HRESULT hr = Device->CreateBuffer(&vbDesc, &vbData, &VertexBuffer);
	if (FAILED(hr))
	{
		OutputDebugStringA("[SkeletalMesh] Failed to create Vertex Buffer\n");
		return false;
	}

	// 3. Index Buffer 생성
	D3D11_BUFFER_DESC ibDesc = {};
	ibDesc.Usage = D3D11_USAGE_DEFAULT;
	ibDesc.ByteWidth = sizeof(uint32) * IndexCount;
	ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA ibData = {};
	ibData.pSysMem = Indices.data();

	hr = Device->CreateBuffer(&ibDesc, &ibData, &IndexBuffer);
	if (FAILED(hr))
	{
		OutputDebugStringA("[SkeletalMesh] Failed to create Index Buffer\n");
		ReleaseGPUResources();
		return false;
	}

	UE_LOG("[SkeletalMesh] GPU resources created successfully (%u vertices, %u indices)",
		VertexCount, IndexCount);
	UE_LOG("[SkeletalMesh] Dual-buffer approach: CPU (80 bytes/vertex), GPU (64 bytes/vertex)");

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
