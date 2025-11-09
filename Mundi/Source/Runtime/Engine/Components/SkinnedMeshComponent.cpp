#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "Shader.h"
#include "Texture.h"
#include "ResourceManager.h"
#include "World.h"
#include "WorldPartitionManager.h"
#include "JsonSerializer.h"
#include "MeshBatchElement.h"
#include "Material.h"
#include "SceneView.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
	MARK_AS_COMPONENT("스킨드 메시 컴포넌트", "본이 있는 스켈레탈 메시를 렌더링하는 기본 컴포넌트입니다.")
	// TODO: EPropertyType::SkeletalMesh 추가 시 활성화
	// ADD_PROPERTY(EPropertyType::SkeletalMesh, SkeletalMesh, "Skeletal Mesh", true)
	ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent()
{
	// 기본 SkeletalMesh는 설정하지 않음 (Import 시 설정됨)
	SkeletalMesh = nullptr;
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
	// 생성된 동적 머티리얼 인스턴스 해제
	ClearDynamicMaterials();
}

void USkinnedMeshComponent::ClearDynamicMaterials()
{
	// 1. 생성된 동적 머티리얼 인스턴스 해제
	for (UMaterialInstanceDynamic* MID : DynamicMaterialInstances)
	{
		delete MID;
	}
	DynamicMaterialInstances.Empty();

	// 2. 머티리얼 슬롯 배열도 비웁니다.
	MaterialSlots.Empty();
}

void USkinnedMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	if (!SkeletalMesh)
	{
		UE_LOG("USkinnedMeshComponent::CollectMeshBatches - No SkeletalMesh");
		return;
	}

	// SkeletalMesh에서 렌더링 데이터 가져오기
	ID3D11Buffer* VertexBuffer = SkeletalMesh->GetVertexBuffer();
	ID3D11Buffer* IndexBuffer = SkeletalMesh->GetIndexBuffer();

	if (!VertexBuffer || !IndexBuffer)
	{
		UE_LOG("USkinnedMeshComponent::CollectMeshBatches - No GPU buffers (VB=%p, IB=%p)", VertexBuffer, IndexBuffer);
		return;
	}

	// Material 결정 (현재는 기본 Material 사용)
	UMaterialInterface* Material = GetMaterial(0);
	UShader* Shader = nullptr;

	if (Material && Material->GetShader())
	{
		Shader = Material->GetShader();
	}
	else
	{
		// 기본 머티리얼 사용
		Material = UResourceManager::GetInstance().GetDefaultMaterial();
		if (Material)
		{
			Shader = Material->GetShader();
		}
		if (!Material || !Shader)
		{
			UE_LOG("USkinnedMeshComponent: 기본 머티리얼이 없습니다.");
			return;
		}
	}

	// Mesh Batch Element 생성
	FMeshBatchElement BatchElement;

	// View 모드 전용 매크로와 머티리얼 개인 매크로를 결합
	TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
	if (0 < Material->GetShaderMacros().Num())
	{
		ShaderMacros.Append(Material->GetShaderMacros());
	}
	FShaderVariant* ShaderVariant = Shader->GetOrCompileShaderVariant(ShaderMacros);

	if (ShaderVariant)
	{
		BatchElement.VertexShader = ShaderVariant->VertexShader;
		BatchElement.PixelShader = ShaderVariant->PixelShader;
		BatchElement.InputLayout = ShaderVariant->InputLayout;
	}
	else
	{
		UE_LOG("USkinnedMeshComponent::CollectMeshBatches - ShaderVariant is nullptr");
		return;
	}

	BatchElement.Material = Material;
	BatchElement.VertexBuffer = VertexBuffer;
	BatchElement.IndexBuffer = IndexBuffer;
	BatchElement.VertexStride = SkeletalMesh->GetVertexStride();
	BatchElement.IndexCount = SkeletalMesh->GetIndexCount();
	BatchElement.StartIndex = 0;
	BatchElement.BaseVertexIndex = 0;
	BatchElement.WorldMatrix = GetWorldMatrix();
	BatchElement.ObjectID = InternalIndex;
	BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	OutMeshBatchElements.Add(BatchElement);
}

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	if (bInIsLoading)
	{
		// TODO: SkeletalMesh 로드 구현 (Phase 5 - Editor 통합 시)
		// 현재는 수동으로 SetSkeletalMesh()를 호출하여 설정
	}
	else
	{
		// TODO: SkeletalMesh 저장 구현 (Phase 5 - Editor 통합 시)
	}
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = InSkeletalMesh;

	// Material 슬롯 초기화 (기본 머티리얼 사용)
	MaterialSlots.Empty();
	if (SkeletalMesh)
	{
		// 현재는 단일 Material만 지원 (향후 다중 Material 지원 예정)
		UMaterialInterface* DefaultMat = UResourceManager::GetInstance().GetDefaultMaterial();
		MaterialSlots.push_back(DefaultMat);

		UE_LOG("USkinnedMeshComponent: SkeletalMesh set with %d vertices, %d indices",
			SkeletalMesh->GetVertexCount(), SkeletalMesh->GetIndexCount());

		// Bone Transform 업데이트
		if (SkeletalMesh->GetSkeleton())
		{
			UpdateBoneTransforms();
		}
	}

	MarkWorldPartitionDirty();
}

USkeleton* USkinnedMeshComponent::GetSkeleton() const
{
	if (SkeletalMesh)
	{
		return SkeletalMesh->GetSkeleton();
	}
	return nullptr;
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
	if (InSectionIndex < static_cast<uint32>(MaterialSlots.size()))
	{
		return MaterialSlots[InSectionIndex];
	}
	return nullptr;
}

void USkinnedMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
	// 슬롯 범위 확장
	while (MaterialSlots.size() <= InElementIndex)
	{
		MaterialSlots.push_back(nullptr);
	}

	MaterialSlots[InElementIndex] = InNewMaterial;
	MarkWorldPartitionDirty();
}

UMaterialInstanceDynamic* USkinnedMeshComponent::CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex)
{
	UMaterialInterface* CurrentMaterial = GetMaterial(ElementIndex);
	if (!CurrentMaterial)
	{
		return nullptr;
	}

	// 이미 MID인 경우, 그대로 반환
	if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(CurrentMaterial))
	{
		return ExistingMID;
	}

	// 현재 머티리얼을 부모로 하는 새로운 MID를 생성
	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(CurrentMaterial);
	if (NewMID)
	{
		DynamicMaterialInstances.Add(NewMID); // 소멸자에서 해제하기 위해 추적
		SetMaterial(ElementIndex, NewMID);    // 슬롯에 새로 만든 MID 설정
		NewMID->SetFilePath("(Instance) " + CurrentMaterial->GetFilePath());
		return NewMID;
	}

	return nullptr;
}

void USkinnedMeshComponent::SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture)
{
	UMaterialInterface* Mat = GetMaterial(InMaterialSlotIndex);
	if (!Mat)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
	if (!MID)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}

	if (MID)
	{
		MID->SetTextureParameterValue(Slot, Texture);
	}
}

void USkinnedMeshComponent::SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value)
{
	UMaterialInterface* Mat = GetMaterial(InMaterialSlotIndex);
	if (!Mat)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
	if (!MID)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}

	if (MID)
	{
		MID->SetColorParameterValue(ParameterName, Value);
	}
}

void USkinnedMeshComponent::SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value)
{
	UMaterialInterface* Mat = GetMaterial(InMaterialSlotIndex);
	if (!Mat)
	{
		return;
	}

	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat);
	if (!MID)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}

	if (MID)
	{
		MID->SetScalarParameterValue(ParameterName, Value);
	}
}

FAABB USkinnedMeshComponent::GetWorldAABB() const
{
	const FTransform WorldTransform = GetWorldTransform();
	const FMatrix WorldMatrix = GetWorldMatrix();

	if (!SkeletalMesh)
	{
		const FVector Origin = WorldTransform.TransformPosition(FVector());
		return FAABB(Origin, Origin);
	}

	// TODO: USkeletalMesh::GetLocalBound() 구현 후 사용
	// 임시로 고정 크기 bounds 사용 (대략적인 캐릭터 크기)
	const FVector LocalMin = FVector(-1.0f, -1.0f, -1.0f);
	const FVector LocalMax = FVector(1.0f, 1.0f, 1.0f);

	// 8개의 코너를 World Space로 변환
	const FVector LocalCorners[8] = {
		FVector(LocalMin.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMax.Z)
	};

	FVector4 WorldMin4 = FVector4(LocalCorners[0].X, LocalCorners[0].Y, LocalCorners[0].Z, 1.0f) * WorldMatrix;
	FVector4 WorldMax4 = WorldMin4;

	for (int32 CornerIndex = 1; CornerIndex < 8; ++CornerIndex)
	{
		const FVector4 WorldPos = FVector4(LocalCorners[CornerIndex].X,
			LocalCorners[CornerIndex].Y,
			LocalCorners[CornerIndex].Z,
			1.0f) * WorldMatrix;
		WorldMin4 = WorldMin4.ComponentMin(WorldPos);
		WorldMax4 = WorldMax4.ComponentMax(WorldPos);
	}

	FVector WorldMin = FVector(WorldMin4.X, WorldMin4.Y, WorldMin4.Z);
	FVector WorldMax = FVector(WorldMax4.X, WorldMax4.Y, WorldMax4.Z);
	return FAABB(WorldMin, WorldMax);
}

void USkinnedMeshComponent::OnTransformUpdated()
{
	Super::OnTransformUpdated();
	MarkWorldPartitionDirty();
}

void USkinnedMeshComponent::MarkWorldPartitionDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartitionManager* Partition = World->GetPartitionManager())
		{
			Partition->MarkDirty(this);
		}
	}
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	// SkeletalMesh는 공유 리소스이므로 복사하지 않음
	// Material 슬롯만 복사
	TArray<UMaterialInterface*> OriginalMaterials = MaterialSlots;
	MaterialSlots.Empty();
	for (UMaterialInterface* Mat : OriginalMaterials)
	{
		MaterialSlots.push_back(Mat);
	}
}

// === Bone Transform 관리 ===

void USkinnedMeshComponent::UpdateBoneTransforms()
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
	{
		return;
	}

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	int32 BoneCount = Skeleton->GetBoneCount();

	if (BoneCount == 0)
	{
		return;
	}

	// Bone Matrices 초기화
	BoneMatrices.resize(BoneCount);

	// 각 Bone의 Component Space Transform 계산
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; BoneIndex++)
	{
		const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);

		// Phase 1: Bind Pose만 사용 (Animation은 Phase 6+)
		FMatrix LocalMatrix = BoneInfo.BindPoseTransform.ToMatrix();

		// Parent Transform 누적 (계층 구조)
		if (BoneInfo.ParentIndex >= 0 && BoneInfo.ParentIndex < BoneCount)
		{
			FMatrix ParentMatrix = BoneMatrices[BoneInfo.ParentIndex];
			BoneMatrices[BoneIndex] = LocalMatrix * ParentMatrix;
		}
		else
		{
			// Root Bone
			BoneMatrices[BoneIndex] = LocalMatrix;
		}

		// Inverse Bind Pose 적용 (Skinning을 위해)
		BoneMatrices[BoneIndex] = BoneInfo.InverseBindPoseMatrix * BoneMatrices[BoneIndex];
	}

	bNeedsBoneTransformUpdate = false;

	UE_LOG("USkinnedMeshComponent: Updated %d bone transforms", BoneCount);
}

void USkinnedMeshComponent::SetBoneTransform(int32 BoneIndex, const FTransform& Transform)
{
	// TODO: 향후 Animation 시스템에서 사용
	// 현재는 플래그만 설정
	bNeedsBoneTransformUpdate = true;
}
