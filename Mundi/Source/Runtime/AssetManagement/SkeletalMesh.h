#pragma once
#include "ResourceBase.h"
#include "Enums.h"
#include <d3d11.h>

class USkeletalMeshComponent;

class USkeletalMesh : public UResourceBase
{
public:
	DECLARE_CLASS(USkeletalMesh, UResourceBase)

	USkeletalMesh() = default;
	virtual ~USkeletalMesh() override;

	void Load(const FString& InFilePath, ID3D11Device* InDevice);
	void Load(FSkeletalMeshData* InData, ID3D11Device* InDevice);

	ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
	ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
	uint32 GetVertexCount() const { return VertexCount; }
	uint32 GetIndexCount() const { return IndexCount; }
	uint32 GetVertexStride() const { return VertexStride; }

	FSkeletalMeshData* GetSkeletalMeshData() const { return SkeletalMeshData; }
	const FSkeleton& GetSkeleton() const { return SkeletalMeshData->Skeleton; }

	// CPU Skinning
	void UpdateCPUSkinning(const TArray<FMatrix>& BoneTransforms, ID3D11DeviceContext* DeviceContext);

private:
	void CreateVertexBuffer(FSkeletalMeshData* InData, ID3D11Device* InDevice);
	void CreateIndexBuffer(FSkeletalMeshData* InData, ID3D11Device* InDevice);
	void ReleaseResources();

	// GPU 리소스
	ID3D11Buffer* VertexBuffer = nullptr;
	ID3D11Buffer* IndexBuffer = nullptr;
	uint32 VertexCount = 0;
	uint32 IndexCount = 0;
	uint32 VertexStride = sizeof(FVertexDynamic);

	// CPU 리소스
	FSkeletalMeshData* SkeletalMeshData = nullptr;

	// CPU Skinning용 원본 정점 데이터
	TArray<FVertexDynamic> OriginalVertices;
	TArray<FVertexDynamic> SkinnedVertices;
};
