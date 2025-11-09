#pragma once
#include "SkinnedMeshComponent.h"
#include "VertexData.h"
#include "AABB.h"
#include <d3d11.h>

class UShader;
class FSceneView;
struct FMeshBatchElement;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
    GENERATED_REFLECTION_BODY()

    USkeletalMeshComponent();

protected:
    ~USkeletalMeshComponent() override;

public:
    // Lifecycle
    void InitializeComponent() override;
    void BeginPlay() override;
    void TickComponent(float DeltaTime) override;

    // Registration
    void OnRegister(UWorld* InWorld) override;
    void OnUnregister() override;

    // Mesh setup
    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;

    // Rendering
    void CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatches, const FSceneView* View) override;
    FAABB GetWorldAABB() const override;

    // Serialization
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(USkeletalMeshComponent)

private:
    // CPU 스키닝 핵심 로직
    void PerformCPUSkinning();

    // Dynamic Vertex Buffer 관리
    void CreateDynamicVertexBuffer();
    void UpdateDynamicVertexBuffer();
    void ReleaseDynamicVertexBuffer();

private:
    // CPU 스키닝 결과 버퍼
    TArray<FVertexDynamic> SkinnedVertices;

    // Dynamic Vertex Buffer (매 프레임 업데이트)
    ID3D11Buffer* DynamicVertexBuffer = nullptr;
    uint32 VertexCount = 0;

    // Cached AABB (스키닝 후 업데이트)
    FAABB CachedWorldAABB;
    bool bAABBDirty = true;
};
