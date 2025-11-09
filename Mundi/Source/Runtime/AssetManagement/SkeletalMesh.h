#pragma once
#include "ResourceBase.h"
#include "Enums.h"
//#include "SkinnedMeshComponent.h"
#include <d3d11.h>

// Forward declarations
class USkeletalMeshComponent;


struct FSkeletalMeshAsset
{
    FString PathFileName;
    
    TArray<FSkinnedVertex> Vertices;
    TArray<uint32> Indices;

    // Material Sections
    TArray<FGroupInfo> GroupInfos;
    bool bHasMaterial = false;
    
    FReferenceSkeleton RefSkeleton;
};

class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    USkeletalMesh() = default;
    virtual ~USkeletalMesh() override;
    
    void Load(const FString& InFilePath, ID3D11Device* InDevice);
    void Load(FSkeletalMeshAsset* InData, ID3D11Device* InDevice);

    // Accessors - GPU Resources
    ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
    ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
    uint32 GetVertexCount() const { return VertexCount; }
    uint32 GetIndexCount() const { return IndexCount; }
    uint32 GetVertexStride() const { return sizeof(FNormalVertex); }

    // Accessors - CPU Data
    const FString& GetAssetPathFileName() const { return SkeletalMeshAsset ? SkeletalMeshAsset->PathFileName : FilePath; }
    FSkeletalMeshAsset* GetSkeletalMeshAsset() const { return SkeletalMeshAsset; }
    void SetSkeletalMeshAsset(FSkeletalMeshAsset* InAsset) { SkeletalMeshAsset = InAsset; }

    const TArray<FSkinnedVertex>& GetVertices() const { return SkeletalMeshAsset->Vertices; }
    const TArray<uint32>& GetIndices() const { return SkeletalMeshAsset->Indices; }
    const FReferenceSkeleton& GetReferenceSkeleton() const { return SkeletalMeshAsset->RefSkeleton; }
    const TArray<FGroupInfo>& GetMeshGroupInfo() const { return SkeletalMeshAsset->GroupInfos; }
    bool HasMaterial() const { return SkeletalMeshAsset->bHasMaterial; }
    uint64 GetMeshGroupCount() const { return SkeletalMeshAsset->GroupInfos.size(); }

    FAABB GetLocalBound() const { return LocalBound; }

    // Dynamic Buffer Management (CPU Skinning)
    bool CreateDynamicGPUResources(ID3D11Device* Device);
    bool UpdateVertexBuffer(ID3D11DeviceContext* Context, const TArray<FNormalVertex>& NewVertices);
    void SetUseDynamicBuffer(bool bDynamic) { bUseDynamicBuffer = bDynamic; }
    bool UsesDynamicBuffer() const { return bUseDynamicBuffer; }

private:
    void CreateIndexBuffer(FSkeletalMeshAsset* InSkeletalMeshAsset, ID3D11Device* InDevice);
    void CreateLocalBound(const FSkeletalMeshAsset* InSkeletalMeshAsset);
    void ReleaseResources();

private:
    FString FilePath;

    // GPU Resources
    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;
    uint32 VertexCount = 0;
    uint32 IndexCount = 0;

    // Dynamic Buffer flag (CPU Skinning)
    bool bUseDynamicBuffer = false;

    // CPU ���
    FSkeletalMeshAsset* SkeletalMeshAsset = nullptr;

    // \� AABB
    FAABB LocalBound;
};
