#include "pch.h"
#include "SkeletalMesh.h"
#include "MeshLoader.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
    ReleaseResources();
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    FilePath = InFilePath;

    // Load FBX skeletal mesh
    FSkeletalMeshAsset* LoadedAsset = UMeshLoader::GetInstance().LoadFBXSkeletalMesh(InFilePath);
    if (LoadedAsset)
    {
        Load(LoadedAsset, InDevice);
    }
    else
    {
        UE_LOG("Failed to load FBX skeletal mesh: %s", InFilePath.c_str());
    }
}

void USkeletalMesh::Load(FSkeletalMeshAsset* InData, ID3D11Device* InDevice)
{
    if (!InData || !InDevice)
        return;

    ReleaseResources();

    SkeletalMeshAsset = InData;
    VertexCount = static_cast<uint32>(SkeletalMeshAsset->Vertices.size());
    IndexCount = static_cast<uint32>(SkeletalMeshAsset->Indices.size());

    CreateIndexBuffer(SkeletalMeshAsset, InDevice);

    // Local Bound 계산
    CreateLocalBound(SkeletalMeshAsset);

    UE_LOG("USkeletalMesh loaded: %s (Vertices: %u, Indices: %u, Bones: %u)",
        SkeletalMeshAsset->PathFileName.c_str(),
        VertexCount,
        IndexCount,
        SkeletalMeshAsset->RefSkeleton.Bones.Num());
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMeshAsset* InSkeletalMeshAsset, ID3D11Device* InDevice)
{
    if (InSkeletalMeshAsset->Indices.empty())
        return;

    D3D11_BUFFER_DESC ibd = {};
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.ByteWidth = sizeof(uint32) * static_cast<UINT>(InSkeletalMeshAsset->Indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA iinitData = {};
    iinitData.pSysMem = InSkeletalMeshAsset->Indices.data();

    HRESULT hr = InDevice->CreateBuffer(&ibd, &iinitData, &IndexBuffer);
    if (FAILED(hr))
    {
        UE_LOG("Failed to create index buffer for SkeletalMesh: %s", InSkeletalMeshAsset->PathFileName.c_str());
    }
}

void USkeletalMesh::CreateLocalBound(const FSkeletalMeshAsset* InSkeletalMeshAsset)
{
    if (InSkeletalMeshAsset->Vertices.empty())
    {
        LocalBound = FAABB(FVector::Zero(), FVector::Zero());
        return;
    }

    FVector Min = InSkeletalMeshAsset->Vertices[0].Position;
    FVector Max = InSkeletalMeshAsset->Vertices[0].Position;

    for (const FSkinnedVertex& Vertex : InSkeletalMeshAsset->Vertices)
    {
        Min = Min.ComponentMin(Vertex.Position);
        Max = Max.ComponentMax(Vertex.Position);
    }

    LocalBound = FAABB(Min, Max);
}

void USkeletalMesh::ReleaseResources()
{
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    VertexCount = 0;
    IndexCount = 0;
}
