#include "pch.h"
#include "SkeletalMesh.h"
#include "FBXImporter.h"
#include "D3D11RHI.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
	ReleaseResources();

	if (SkeletalMeshData)
	{
		delete SkeletalMeshData;
		SkeletalMeshData = nullptr;
	}
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
	assert(InDevice);

	// FBX 파일에서 스켈레탈 메시 로드
	UFBXImporter& Importer = UFBXImporter::GetInstance();
	SkeletalMeshData = Importer.LoadFBXSkeletalMesh(InFilePath);

	if (SkeletalMeshData && !SkeletalMeshData->Vertices.empty() && !SkeletalMeshData->Indices.empty())
	{
		CreateVertexBuffer(SkeletalMeshData, InDevice);
		CreateIndexBuffer(SkeletalMeshData, InDevice);
		VertexCount = static_cast<uint32>(SkeletalMeshData->Vertices.size());
		IndexCount = static_cast<uint32>(SkeletalMeshData->Indices.size());
	}
}

void USkeletalMesh::Load(FSkeletalMeshData* InData, ID3D11Device* InDevice)
{
	assert(InDevice);
	assert(InData);

	if (SkeletalMeshData)
	{
		delete SkeletalMeshData;
	}
	SkeletalMeshData = InData;

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

	if (!SkeletalMeshData->Vertices.empty() && !SkeletalMeshData->Indices.empty())
	{
		CreateVertexBuffer(SkeletalMeshData, InDevice);
		CreateIndexBuffer(SkeletalMeshData, InDevice);
		VertexCount = static_cast<uint32>(SkeletalMeshData->Vertices.size());
		IndexCount = static_cast<uint32>(SkeletalMeshData->Indices.size());
	}
}

void USkeletalMesh::CreateVertexBuffer(FSkeletalMeshData* InData, ID3D11Device* InDevice)
{
	// FSkeletalMeshData를 FVertexDynamic 형태로 변환
	TArray<FVertexDynamic> VertexArray;
	VertexArray.Reserve(InData->Vertices.size());

	for (size_t i = 0; i < InData->Vertices.size(); ++i)
	{
		FVertexDynamic Vertex;
		Vertex.Position = InData->Vertices[i];
		Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 기본 색상

		if (i < InData->UV.size())
		{
			Vertex.UV = InData->UV[i];
		}
		else
		{
			Vertex.UV = FVector2D(0.0f, 0.0f);
		}

		if (i < InData->Normal.size())
		{
			Vertex.Normal = InData->Normal[i];
		}
		else
		{
			Vertex.Normal = FVector(0.0f, 1.0f, 0.0f);
		}

		Vertex.Tangent = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

		VertexArray.Add(Vertex);
	}

	// 버텍스 버퍼 생성
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = static_cast<UINT>(sizeof(FVertexDynamic) * VertexArray.Num());
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = VertexArray.GetData();

	HRESULT hr = InDevice->CreateBuffer(&bufferDesc, &initData, &VertexBuffer);
	assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMeshData* InData, ID3D11Device* InDevice)
{
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * InData->Indices.size());
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = InData->Indices.data();

	HRESULT hr = InDevice->CreateBuffer(&bufferDesc, &initData, &IndexBuffer);
	assert(SUCCEEDED(hr));
}

void USkeletalMesh::ReleaseResources()
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
