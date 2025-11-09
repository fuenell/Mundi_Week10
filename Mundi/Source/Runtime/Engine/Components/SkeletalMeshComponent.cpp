#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "Renderer.h"
#include "JsonSerializer.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "Material.h"
#include "Shader.h"
#include "ResourceManager.h"
#include "RenderSettings.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
    MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 CPU 스키닝으로 렌더링합니다.")

END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
    bCanEverTick = true;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    // Note: USkeletalMesh가 자체 VertexBuffer를 관리하므로 여기서 해제하지 않음
    SkinnedVertices.Empty();
}

void USkeletalMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Note: USkeletalMesh가 자체 VertexBuffer를 관리하므로 여기서 생성하지 않음
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!SkeletalMesh)
        return;

    UE_LOG("[DEBUG TICK] SkeletalMeshComponent::TickComponent - Mesh=%s, BoneCount=%d",
        SkeletalMesh->GetAssetPathFileName().c_str(), GetBoneMatrices().Num());

    // CPU 스키닝 수행
    PerformCPUSkinning();

    // 첫 3개 정점 위치 확인
    if (SkinnedVertices.Num() >= 3)
    {
        UE_LOG("[DEBUG TICK] First 3 skinned vertices: [0]=(%f,%f,%f), [1]=(%f,%f,%f), [2]=(%f,%f,%f)",
            SkinnedVertices[0].Position.X, SkinnedVertices[0].Position.Y, SkinnedVertices[0].Position.Z,
            SkinnedVertices[1].Position.X, SkinnedVertices[1].Position.Y, SkinnedVertices[1].Position.Z,
            SkinnedVertices[2].Position.X, SkinnedVertices[2].Position.Y, SkinnedVertices[2].Position.Z);
    }

    // USkeletalMesh의 VertexBuffer 업데이트 (FVertexDynamic와 FNormalVertex는 메모리 레이아웃이 동일)
    if (SkinnedVertices.Num() > 0 && SkeletalMesh->UsesDynamicBuffer())
    {
        ID3D11DeviceContext* Context = GEngine.GetRHIDevice()->GetDeviceContext();
        const TArray<FNormalVertex>& NormalVertices = reinterpret_cast<const TArray<FNormalVertex>&>(SkinnedVertices);
        bool bUpdateSuccess = SkeletalMesh->UpdateVertexBuffer(Context, NormalVertices);
        UE_LOG("[DEBUG TICK] UpdateVertexBuffer result: %s", bUpdateSuccess ? "SUCCESS" : "FAILED");
    }

    // AABB 재계산 필요 표시
    bAABBDirty = true;
}

void USkeletalMeshComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);
}

void USkeletalMeshComponent::OnUnregister()
{
    // Note: USkeletalMesh가 자체 VertexBuffer를 관리하므로 여기서 해제하지 않음
    Super::OnUnregister();
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    UE_LOG("[SkeletalMeshComponent] SetSkeletalMesh called");

    // 부모 클래스에서 본 행렬, 머티리얼 슬롯 초기화
    USkinnedMeshComponent::SetSkeletalMesh(InSkeletalMesh);

    // 새 메쉬로 CPU 스키닝 준비
    if (SkeletalMesh)
    {
        UE_LOG("[SkeletalMeshComponent] Setting mesh: %s", SkeletalMesh->GetAssetPathFileName().c_str());
        UE_LOG("[DEBUG INIT] BoneCount after parent SetSkeletalMesh: %d", GetBoneMatrices().Num());

        // SkinnedVertices 크기 초기화
        VertexCount = SkeletalMesh->GetVertexCount();
        SkinnedVertices.SetNum(VertexCount);

        // 초기 본 변환 업데이트 (TickComponent 전에 한번 수행)
        UpdateBoneTransforms();
        UE_LOG("[DEBUG INIT] UpdateBoneTransforms completed");

        // 초기 CPU 스키닝 수행 및 버퍼 업데이트
        PerformCPUSkinning();
        UE_LOG("[DEBUG INIT] First skinned vertex: (%f,%f,%f)",
            SkinnedVertices[0].Position.X, SkinnedVertices[0].Position.Y, SkinnedVertices[0].Position.Z);

        if (SkeletalMesh->UsesDynamicBuffer())
        {
            ID3D11DeviceContext* Context = GEngine.GetRHIDevice()->GetDeviceContext();
            const TArray<FNormalVertex>& NormalVertices = reinterpret_cast<const TArray<FNormalVertex>&>(SkinnedVertices);
            bool bUpdateSuccess = SkeletalMesh->UpdateVertexBuffer(Context, NormalVertices);
            UE_LOG("[DEBUG INIT] Initial UpdateVertexBuffer result: %s", bUpdateSuccess ? "SUCCESS" : "FAILED");
        }

        UE_LOG("[SkeletalMeshComponent] Mesh set successfully. VertexBuffer = %p", SkeletalMesh->GetVertexBuffer());
    }
    else
    {
        UE_LOG("[SkeletalMeshComponent] Mesh is null");
        SkinnedVertices.Empty();
    }
}

void USkeletalMeshComponent::PerformCPUSkinning()
{
    if (!SkeletalMesh)
        return;

    const TArray<FSkinnedVertex>& SourceVertices = SkeletalMesh->GetVertices();
    const TArray<FMatrix>& Bones = GetBoneMatrices();

    if (SourceVertices.Num() == 0 || Bones.Num() == 0)
        return;

    // 결과 버퍼 크기 확인
    if (SkinnedVertices.Num() != SourceVertices.Num())
    {
        SkinnedVertices.SetNum(SourceVertices.Num());
    }

    // 각 정점마다 CPU 스키닝 수행
    for (int32 VertexIdx = 0; VertexIdx < SourceVertices.Num(); ++VertexIdx)
    {
        const FSkinnedVertex& SrcVertex = SourceVertices[VertexIdx];
        FVector SkinnedPos = FVector::Zero();
        FVector SkinnedNormal = FVector::Zero();

        // 4개 본의 영향 합산
        for (int32 i = 0; i < 4; ++i)
        {
            uint32 BoneIdx = SrcVertex.BoneIndices[i];
            float Weight = SrcVertex.BoneWeights[i];

            if (Weight > 0.0f && BoneIdx < static_cast<uint32>(Bones.Num()))
            {
                // 프로젝트는 row-vector 방식 (V * M)
                FVector TransformedPos = SrcVertex.Position * Bones[BoneIdx];
                FVector TransformedNormal = SrcVertex.Normal * Bones[BoneIdx];

                SkinnedPos += TransformedPos * Weight;
                SkinnedNormal += TransformedNormal * Weight;
            }
        }

        // 결과 저장
        SkinnedVertices[VertexIdx].Position = SkinnedPos;
        SkinnedVertices[VertexIdx].Normal = SkinnedNormal.GetNormalized();
        SkinnedVertices[VertexIdx].UV = SrcVertex.UV;
        SkinnedVertices[VertexIdx].Tangent = SrcVertex.Tangent;
        SkinnedVertices[VertexIdx].Color = FVector4(1, 1, 1, 1);
    }
}

void USkeletalMeshComponent::CreateDynamicVertexBuffer()
{
    if (!SkeletalMesh || DynamicVertexBuffer)
        return;

    VertexCount = SkeletalMesh->GetVertexCount();
    if (VertexCount == 0)
        return;

    SkinnedVertices.SetNum(VertexCount);

    // Dynamic Vertex Buffer 생성
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = sizeof(FVertexDynamic) * VertexCount;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // Device는 GEngine을 통해 얻음
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = Device->CreateBuffer(&desc, nullptr, &DynamicVertexBuffer);
    if (FAILED(hr))
    {
        UE_LOG("Failed to create dynamic vertex buffer for SkeletalMeshComponent");
        DynamicVertexBuffer = nullptr;
    }
    else
    {
        UE_LOG("Dynamic vertex buffer created for SkeletalMeshComponent: %u vertices", VertexCount);
    }
}

void USkeletalMeshComponent::UpdateDynamicVertexBuffer()
{
    if (!DynamicVertexBuffer || SkinnedVertices.Num() == 0)
        return;

    // D3D11_MAP_WRITE_DISCARD로 버퍼 업데이트
    ID3D11DeviceContext* Context = GEngine.GetRHIDevice()->GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = Context->Map(DynamicVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, SkinnedVertices.data(), sizeof(FVertexDynamic) * SkinnedVertices.Num());
        Context->Unmap(DynamicVertexBuffer, 0);
    }
    else
    {
        UE_LOG("Failed to map dynamic vertex buffer for SkeletalMeshComponent");
    }
}

void USkeletalMeshComponent::ReleaseDynamicVertexBuffer()
{
    if (DynamicVertexBuffer)
    {
        DynamicVertexBuffer->Release();
        DynamicVertexBuffer = nullptr;
    }
    SkinnedVertices.Empty();
}

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatches, const FSceneView* View)
{
    UE_LOG("[DEBUG] SkeletalMeshComponent::CollectMeshBatches CALLED!");

    // ShowFlag 체크 (나중에 RenderSettings 구현 시 추가)
    // if (!RenderSettings->IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
    //     return;

    if (!SkeletalMesh)
    {
        UE_LOG("[SkeletalMeshComponent] CollectMeshBatches: SkeletalMesh is null");
        return;
    }

    // USkeletalMesh의 VertexBuffer 사용 (StaticMesh 패턴)
    ID3D11Buffer* VertexBuffer = SkeletalMesh->GetVertexBuffer();
    ID3D11Buffer* IndexBuffer = SkeletalMesh->GetIndexBuffer();

    UE_LOG("[DEBUG] VB=%p, IB=%p, Stride=%u", VertexBuffer, IndexBuffer, SkeletalMesh->GetVertexStride());

    if (!VertexBuffer || !IndexBuffer)
    {
        UE_LOG("[SkeletalMeshComponent] CollectMeshBatches: SkeletalMesh buffers are null! (VB=%p, IB=%p)", VertexBuffer, IndexBuffer);
        return;
    }

    UE_LOG("[SkeletalMeshComponent] CollectMeshBatches: Processing mesh %s", SkeletalMesh->GetAssetPathFileName().c_str());

    const TArray<FGroupInfo>& MeshGroupInfos = SkeletalMesh->GetMeshGroupInfo();

    // StaticMeshComponent 패턴 따라 구현
    auto DetermineMaterialAndShader = [&](uint32 SectionIndex) -> TPair<UMaterialInterface*, UShader*>
    {
        UMaterialInterface* Material = GetMaterial(SectionIndex);
        UShader* Shader = nullptr;

        if (Material && Material->GetShader())
        {
            Shader = Material->GetShader();
        }
        else
        {
            Material = UResourceManager::GetInstance().GetDefaultMaterial();
            if (Material)
            {
                Shader = Material->GetShader();
            }
            if (!Material || !Shader)
            {
                UE_LOG("SkeletalMeshComponent: No default material available");
                return { nullptr, nullptr };
            }
        }
        return { Material, Shader };
    };

    const bool bHasSections = !MeshGroupInfos.IsEmpty();
    const uint32 NumSectionsToProcess = bHasSections ? static_cast<uint32>(MeshGroupInfos.size()) : 1;

    for (uint32 SectionIndex = 0; SectionIndex < NumSectionsToProcess; ++SectionIndex)
    {
        uint32 IndexCount = 0;
        uint32 StartIndex = 0;

        if (bHasSections)
        {
            const FGroupInfo& Group = MeshGroupInfos[SectionIndex];
            IndexCount = Group.IndexCount;
            StartIndex = Group.StartIndex;
        }
        else
        {
            IndexCount = SkeletalMesh->GetIndexCount();
            StartIndex = 0;
        }

        if (IndexCount == 0)
            continue;

        auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
        if (!MaterialToUse || !ShaderToUse)
        {
            UE_LOG("[SkeletalMeshComponent] Section %u: Material or Shader is null", SectionIndex);
            continue;
        }

        FMeshBatchElement BatchElement;

        // Shader Variant (View 매크로 + Material 매크로)
        TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
        if (0 < MaterialToUse->GetShaderMacros().Num())
        {
            ShaderMacros.Append(MaterialToUse->GetShaderMacros());
        }
        FShaderVariant* ShaderVariant = ShaderToUse->GetOrCompileShaderVariant(ShaderMacros);

        if (ShaderVariant)
        {
            BatchElement.VertexShader = ShaderVariant->VertexShader;
            BatchElement.PixelShader = ShaderVariant->PixelShader;
            BatchElement.InputLayout = ShaderVariant->InputLayout;
            //UE_LOG("[SkeletalMeshComponent] Section %u: ShaderVariant OK - VS=%p, PS=%p", SectionIndex, ShaderVariant->VertexShader, ShaderVariant->PixelShader);
        }
        else
        {
            UE_LOG("[SkeletalMeshComponent] Section %u: ShaderVariant is NULL!", SectionIndex);
        }

        BatchElement.Material = MaterialToUse;
        BatchElement.VertexBuffer = VertexBuffer;  // USkeletalMesh의 VertexBuffer 사용
        BatchElement.IndexBuffer = IndexBuffer;
        BatchElement.VertexStride = SkeletalMesh->GetVertexStride();  // sizeof(FNormalVertex)
        BatchElement.IndexCount = IndexCount;
        BatchElement.StartIndex = StartIndex;
        BatchElement.BaseVertexIndex = 0;
        BatchElement.WorldMatrix = GetWorldMatrix();
        BatchElement.ObjectID = InternalIndex;
        BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        FMatrix WorldMtx = GetWorldMatrix();
        UE_LOG("[DEBUG] WorldMatrix: Pos=(%f,%f,%f), Scale=(%f,%f,%f)",
            WorldMtx.M[3][0], WorldMtx.M[3][1], WorldMtx.M[3][2],
            WorldMtx.M[0][0], WorldMtx.M[1][1], WorldMtx.M[2][2]);

        UE_LOG("[DEBUG] Adding batch: VS=%p, PS=%p, VB=%p, IB=%p, Stride=%u, IndexCount=%u",
            BatchElement.VertexShader, BatchElement.PixelShader,
            BatchElement.VertexBuffer, BatchElement.IndexBuffer,
            BatchElement.VertexStride, BatchElement.IndexCount);
        OutMeshBatches.Add(BatchElement);
    }

    UE_LOG("[DEBUG] CollectMeshBatches completed: Total batches added = %d", NumSectionsToProcess);
}

FAABB USkeletalMeshComponent::GetWorldAABB() const
{
    if (bAABBDirty && SkinnedVertices.Num() > 0)
    {
        // 스키닝된 정점으로 AABB 계산
        FVector Min = SkinnedVertices[0].Position;
        FVector Max = SkinnedVertices[0].Position;

        for (const FVertexDynamic& Vertex : SkinnedVertices)
        {
            Min = Min.ComponentMin(Vertex.Position);
            Max = Max.ComponentMax(Vertex.Position);
        }

        const_cast<FAABB&>(CachedWorldAABB) = FAABB(Min, Max);
        const_cast<bool&>(bAABBDirty) = false;
    }

    return CachedWorldAABB;
}

void USkeletalMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}
