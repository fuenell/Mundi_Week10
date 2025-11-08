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

	// Vertex Buffer 생성
	D3D11_BUFFER_DESC vbDesc = {};
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.ByteWidth = sizeof(FSkinnedVertex) * VertexCount;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA vbData = {};
	vbData.pSysMem = Vertices.data();

	HRESULT hr = Device->CreateBuffer(&vbDesc, &vbData, &VertexBuffer);
	if (FAILED(hr))
	{
		OutputDebugStringA("[SkeletalMesh] Failed to create Vertex Buffer\n");
		return false;
	}

	// Index Buffer 생성
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
