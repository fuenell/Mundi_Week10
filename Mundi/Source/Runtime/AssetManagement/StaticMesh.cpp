#include "pch.h"
#include "StaticMesh.h"
#include "StaticMeshComponent.h"
#include "ObjManager.h"
#include "FbxImporter.h"
#include "FbxImportOptions.h"
#include "ResourceManager.h"
#include "GlobalConsole.h"
#include <filesystem>

IMPLEMENT_CLASS(UStaticMesh)

UStaticMesh::~UStaticMesh()
{
    ReleaseResources();
}

void UStaticMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    // 파일 확장자 확인
    std::filesystem::path FilePath(InFilePath);
    FString Extension = FilePath.extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

    if (Extension == ".fbx")
    {
        // TODO: [FBX Baking System] 임시 FBX 로드 방식 - FbxManager 구현 시 변경 필요
        // 현재: UStaticMesh::Load()에서 직접 FBX Import 후 new FStaticMesh() 소유
        // 변경 후: FbxManager::LoadFbxStaticMeshAsset(FilePath) 호출하여 캐싱된 FStaticMesh* 참조
        //         (ObjManager::LoadObjStaticMeshAsset 패턴과 동일하게)

        // FBX 파일 Import
        UE_LOG("[StaticMesh] Loading FBX file: %s", InFilePath.c_str());

        // FBX Importer 생성
        FFbxImporter Importer;

        // FBX Type Detection
        EFbxImportType FbxType = Importer.DetectFbxType(InFilePath);

        if (FbxType != EFbxImportType::StaticMesh)
        {
            UE_LOG("[StaticMesh ERROR] FBX file '%s' is not a StaticMesh (detected as SkeletalMesh or Animation)", InFilePath.c_str());
            UE_LOG("[StaticMesh ERROR] Use USkeletalMesh::Load() instead for skeletal meshes");
            return;
        }

        // Import Options 설정
        FFbxImportOptions Options;
        Options.bConvertScene = true;
        Options.bConvertSceneUnit = true;
        Options.bRemoveDegenerates = true;
        Options.ImportScale = 1.0f;

        // StaticMesh 데이터 구조체 생성
        StaticMeshAsset = new FStaticMesh();
        bOwnsStaticMeshAsset = true;  // FBX: UStaticMesh owns the asset

        // FBX Import 수행
        if (!Importer.ImportStaticMesh(InFilePath, Options, *StaticMeshAsset))
        {
            UE_LOG("[StaticMesh ERROR] Failed to import FBX file: %s", Importer.GetLastError().c_str());
            delete StaticMeshAsset;
            StaticMeshAsset = nullptr;
            bOwnsStaticMeshAsset = false;
            return;
        }

        UE_LOG("[StaticMesh] FBX Import Success: %d vertices, %d indices",
            StaticMeshAsset->Vertices.size(),
            StaticMeshAsset->Indices.size());
    }
    else if (Extension == ".obj")
    {
        // OBJ 파일 Load (기존 방식)
        StaticMeshAsset = FObjManager::LoadObjStaticMeshAsset(InFilePath);
    }
    else
    {
        UE_LOG("[StaticMesh ERROR] Unsupported file format: %s", Extension.c_str());
        return;
    }

    // 빈 버텍스, 인덱스로 버퍼 생성 방지
    if (StaticMeshAsset && 0 < StaticMeshAsset->Vertices.size() && 0 < StaticMeshAsset->Indices.size())
    {
        CacheFilePath = StaticMeshAsset->CacheFilePath;
        CreateVertexBuffer(StaticMeshAsset, InDevice, InVertexType);
        CreateIndexBuffer(StaticMeshAsset, InDevice);
        CreateLocalBound(StaticMeshAsset);
        VertexCount = static_cast<uint32>(StaticMeshAsset->Vertices.size());
        IndexCount = static_cast<uint32>(StaticMeshAsset->Indices.size());
    }
}

void UStaticMesh::Load(FMeshData* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    SetVertexType(InVertexType);

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

    CreateVertexBuffer(InData, InDevice, InVertexType);
    CreateIndexBuffer(InData, InDevice);
    CreateLocalBound(InData);

    VertexCount = static_cast<uint32>(InData->Vertices.size());
    IndexCount = static_cast<uint32>(InData->Indices.size());
}

void UStaticMesh::SetVertexType(EVertexLayoutType InVertexType)
{
    VertexType = InVertexType;

    uint32 Stride = 0;
    switch (InVertexType)
    {
    case EVertexLayoutType::PositionColor:
        Stride = sizeof(FVertexSimple);
        break;
    case EVertexLayoutType::PositionColorTexturNormal:
        Stride = sizeof(FVertexDynamic);
        break;
    case EVertexLayoutType::PositionTextBillBoard:
        Stride = sizeof(FBillboardVertexInfo_GPU);
        break;
    case EVertexLayoutType::PositionBillBoard:
        Stride = sizeof(FBillboardVertex);
        break;
    default:
        assert(false && "Unknown vertex type!");
    }

    VertexStride = Stride;
}

void UStaticMesh::CreateVertexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    HRESULT hr;
    hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, *InMeshData, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void UStaticMesh::CreateVertexBuffer(FStaticMesh* InStaticMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    HRESULT hr;
    hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(InDevice, InStaticMesh->Vertices, &VertexBuffer);
    assert(SUCCEEDED(hr));
}

void UStaticMesh::CreateIndexBuffer(FMeshData* InMeshData, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InMeshData, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void UStaticMesh::CreateIndexBuffer(FStaticMesh* InStaticMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InStaticMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}

void UStaticMesh::CreateLocalBound(const FMeshData* InMeshData)
{
    TArray<FVector> Verts = InMeshData->Vertices;
    FVector Min = Verts[0];
    FVector Max = Verts[0];
    for (FVector Vertex : Verts)
    {
        Min = Min.ComponentMin(Vertex);
        Max = Max.ComponentMax(Vertex);
    }
    LocalBound = FAABB(Min, Max);
}

void UStaticMesh::CreateLocalBound(const FStaticMesh* InStaticMesh)
{
    TArray<FNormalVertex> Verts = InStaticMesh->Vertices;
    FVector Min = Verts[0].pos;
    FVector Max = Verts[0].pos;
    for (FNormalVertex Vertex : Verts)
    {
        FVector Pos = Vertex.pos;
        Min = Min.ComponentMin(Pos);
        Max = Max.ComponentMax(Pos);
    }
    LocalBound = FAABB(Min, Max);
}

void UStaticMesh::ReleaseResources()
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

    // TODO: [FBX Baking System] 임시 메모리 누수 방지 코드 - FbxManager 구현 시 삭제 필요
    // 현재: bOwnsStaticMeshAsset 플래그로 FBX는 delete, OBJ는 delete 안함
    // 변경 후: FbxManager가 FStaticMesh* 소유 관리 → UStaticMesh는 delete 불필요 (OBJ와 동일)
    if (bOwnsStaticMeshAsset && StaticMeshAsset)
    {
        delete StaticMeshAsset;
        StaticMeshAsset = nullptr;
        bOwnsStaticMeshAsset = false;
    }
}
