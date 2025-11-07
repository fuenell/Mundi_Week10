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
    ReleaseDynamicVertexBuffer();
}

void USkeletalMeshComponent::InitializeComponent()
{
    Super::InitializeComponent();
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    // Dynamic Vertex Buffer 생성
    CreateDynamicVertexBuffer();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // CPU 스키닝 수행 하지만 지금은 매 틱 마다 수행중인데, 나중에 애니메이션을 위해서 지금은 성능 낭비더라도 일단은 넣어둠
    PerformCPUSkinning();

    // Dynamic Vertex Buffer 업데이트
    UpdateDynamicVertexBuffer();

    // AABB 재계산 필요 표시
    bAABBDirty = true;
}

void USkeletalMeshComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);
}

void USkeletalMeshComponent::OnUnregister()
{
    ReleaseDynamicVertexBuffer();
    Super::OnUnregister();
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
    // ShowFlag 체크 (나중에 RenderSettings 구현 시 추가)
    // if (!RenderSettings->IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
    //     return;

    if (!SkeletalMesh || !DynamicVertexBuffer)
        return;

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
            continue;

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
        }

        BatchElement.Material = MaterialToUse;
        BatchElement.VertexBuffer = DynamicVertexBuffer;
        BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();
        BatchElement.VertexStride = sizeof(FVertexDynamic);
        BatchElement.IndexCount = IndexCount;
        BatchElement.StartIndex = StartIndex;
        BatchElement.BaseVertexIndex = 0;
        BatchElement.WorldMatrix = GetWorldMatrix();
        BatchElement.ObjectID = InternalIndex;
        BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        OutMeshBatches.Add(BatchElement);
    }
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
