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

    // Create Index Buffer
    CreateIndexBuffer(SkeletalMeshAsset, InDevice);

    // Create Dynamic Vertex Buffer (CPU Skinning)
    CreateDynamicGPUResources(InDevice);

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

    VertexCount = 0;
    IndexCount = 0;
}

bool USkeletalMesh::CreateDynamicGPUResources(ID3D11Device* Device)
{
    if (!Device || !SkeletalMeshAsset || SkeletalMeshAsset->Vertices.empty())
    {
        UE_LOG("[error] CreateDynamicGPUResources: Invalid parameters");
        return false;
    }

    // FSkinnedVertex -> FNormalVertex 변환 (Bind Pose)
    TArray<FNormalVertex> InitialVertices;
    InitialVertices.resize(SkeletalMeshAsset->Vertices.size());

    for (size_t i = 0; i < SkeletalMeshAsset->Vertices.size(); i++)
    {
        const FSkinnedVertex& SrcVert = SkeletalMeshAsset->Vertices[i];
        FNormalVertex& DstVert = InitialVertices[i];

        DstVert.pos = SrcVert.Position;
        DstVert.normal = SrcVert.Normal;
        DstVert.tex = SrcVert.UV;
        DstVert.Tangent = SrcVert.Tangent;
        DstVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Dynamic Vertex Buffer 생성
    D3D11_BUFFER_DESC vbd = {};
    vbd.Usage = D3D11_USAGE_DYNAMIC;  // Dynamic Buffer
    vbd.ByteWidth = sizeof(FNormalVertex) * static_cast<UINT>(InitialVertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU에서 쓰기 가능

    D3D11_SUBRESOURCE_DATA vinitData = {};
    vinitData.pSysMem = InitialVertices.data();

    HRESULT hr = Device->CreateBuffer(&vbd, &vinitData, &VertexBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[error] Failed to create dynamic vertex buffer for SkeletalMesh: %s",
               SkeletalMeshAsset->PathFileName.c_str());
        return false;
    }

    bUseDynamicBuffer = true;

    UE_LOG("SkeletalMesh: Created dynamic vertex buffer (%u vertices, %u bytes)",
           static_cast<uint32>(InitialVertices.size()),
           vbd.ByteWidth);

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
        UE_LOG("[error] UpdateVertexBuffer: Vertex count mismatch (Expected: %u, Got: %zu)",
               VertexCount, NewVertices.size());
        return false;
    }

    // Map Vertex Buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = Context->Map(VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr))
    {
        UE_LOG("[error] UpdateVertexBuffer: Failed to map buffer");
        return false;
    }

    // Vertex 데이터 복사
    memcpy(mappedResource.pData, NewVertices.data(), sizeof(FNormalVertex) * VertexCount);

    // Unmap
    Context->Unmap(VertexBuffer, 0);

    return true;
}
