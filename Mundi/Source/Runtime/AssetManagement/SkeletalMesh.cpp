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

	// CPU Skinning을 위해 원본 정점 데이터 저장
	OriginalVertices = VertexArray;
	SkinnedVertices = VertexArray;

	// 버텍스 버퍼 생성 (CPU Skinning을 위해 DYNAMIC으로 생성)
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = static_cast<UINT>(sizeof(FVertexDynamic) * VertexArray.Num());
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

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

void USkeletalMesh::UpdateCPUSkinning(const TArray<FMatrix>& BoneTransforms, ID3D11DeviceContext* DeviceContext)
{
	if (!SkeletalMeshData || !DeviceContext || OriginalVertices.IsEmpty())
	{
		return;
	}

	const FSkeleton& Skeleton = SkeletalMeshData->Skeleton;
	const TArray<FBoneWeight>& BoneWeights = SkeletalMeshData->BoneWeights;

	// 각 정점을 스키닝
	for (int32 VertexIndex = 0; VertexIndex < OriginalVertices.Num(); ++VertexIndex)
	{
		const FVertexDynamic& OriginalVertex = OriginalVertices[VertexIndex];
		FVertexDynamic& SkinnedVertex = SkinnedVertices[VertexIndex];

		// 본 가중치가 없으면 원본 정점 사용
		if (VertexIndex >= BoneWeights.Num())
		{
			SkinnedVertex = OriginalVertex;
			continue;
		}

		const FBoneWeight& Weight = BoneWeights[VertexIndex];

		// 스키닝된 위치와 노멀 초기화
		FVector SkinnedPos = FVector::Zero();
		FVector SkinnedNormal = FVector::Zero();

		// 최대 4개의 본 영향 계산
		for (int32 i = 0; i < 4; ++i)
		{
			if (Weight.Weights[i] > 0.0f)
			{
				int32 BoneIndex = Weight.BoneIndices[i];
				if (BoneIndex >= 0 && BoneIndex < BoneTransforms.Num())
				{
					const FMatrix& BoneMatrix = BoneTransforms[BoneIndex];

					// 위치 변환 (Point, w=1)
					FVector4 Pos4 = FVector4::FromPoint(OriginalVertex.Position);
					FVector4 TransformedPos4 = Pos4 * BoneMatrix;
					FVector TransformedPos(TransformedPos4.X, TransformedPos4.Y, TransformedPos4.Z);
					SkinnedPos += TransformedPos * Weight.Weights[i];

					// 노멀 변환 (Direction, w=0)
					FVector4 Normal4 = FVector4::FromDirection(OriginalVertex.Normal);
					FVector4 TransformedNormal4 = Normal4 * BoneMatrix;
					FVector TransformedNormal(TransformedNormal4.X, TransformedNormal4.Y, TransformedNormal4.Z);
					SkinnedNormal += TransformedNormal * Weight.Weights[i];
				}
			}
		}

		// 결과 저장
		SkinnedVertex.Position = SkinnedPos;
		SkinnedVertex.Normal = SkinnedNormal.GetNormalized();

		// UV와 Color는 변하지 않음
		SkinnedVertex.UV = OriginalVertex.UV;
		SkinnedVertex.Color = OriginalVertex.Color;
		SkinnedVertex.Tangent = OriginalVertex.Tangent;
	}

	// 버퍼 업데이트
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr = DeviceContext->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (SUCCEEDED(hr))
	{
		memcpy(mappedResource.pData, SkinnedVertices.GetData(), sizeof(FVertexDynamic) * SkinnedVertices.Num());
		DeviceContext->Unmap(VertexBuffer, 0);
	}
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
