#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "Shader.h"
#include "Texture.h"
#include "ResourceManager.h"
#include "World.h"
#include "WorldPartitionManager.h"
#include "JsonSerializer.h"
#include "CameraComponent.h"
#include "MeshBatchElement.h"
#include "Material.h"
#include "SceneView.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
	MARK_AS_COMPONENT("스킨드 메시 컴포넌트", "스켈레탈 메시를 렌더링하는 기본 컴포넌트입니다.")
	// ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
	ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent()
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
	ClearDynamicMaterials();
}

void USkinnedMeshComponent::ClearDynamicMaterials()
{
	for (UMaterialInstanceDynamic* MID : DynamicMaterialInstances)
	{
		delete MID;
	}
	DynamicMaterialInstances.Empty();
	MaterialSlots.Empty();
}

void USkinnedMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshData())
	{
		return;
	}

	// 스켈레탈 메시는 섹션이 없으므로 하나의 배치로 처리
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
			UE_LOG("USkinnedMeshComponent: 머티리얼이 없거나 셰이더가 없어서 기본 머티리얼 사용 section %u.\n", SectionIndex);
			Material = UResourceManager::GetInstance().GetDefaultMaterial();
			if (Material)
			{
				Shader = Material->GetShader();
			}
			if (!Material || !Shader)
			{
				UE_LOG("USkinnedMeshComponent: 기본 머티리얼이 없습니다.\n");
				return { nullptr, nullptr };
			}
		}
		return { Material, Shader };
	};

	uint32 IndexCount = SkeletalMesh->GetIndexCount();
	uint32 StartIndex = 0;

	if (IndexCount == 0)
	{
		return;
	}

	auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(0);
	if (!MaterialToUse || !ShaderToUse)
	{
		return;
	}

	FMeshBatchElement BatchElement;
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
	BatchElement.VertexBuffer = SkeletalMesh->GetVertexBuffer();
	BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();
	BatchElement.VertexStride = SkeletalMesh->GetVertexStride();
	BatchElement.IndexCount = IndexCount;
	BatchElement.StartIndex = StartIndex;
	BatchElement.BaseVertexIndex = 0;
	BatchElement.WorldMatrix = GetWorldMatrix();
	BatchElement.ObjectID = InternalIndex;
	BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	OutMeshBatchElements.Add(BatchElement);
}

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
	ClearDynamicMaterials();

	SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);
	if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshData())
	{
		// 기본적으로 하나의 머티리얼 슬롯만 생성
		MaterialSlots.SetNum(1);

		// 기본 머티리얼 설정
		UMaterialInterface* DefaultMaterial = UResourceManager::GetInstance().GetDefaultMaterial();
		MaterialSlots[0] = DefaultMaterial;

		MarkWorldPartitionDirty();
	}
	else
	{
		SkeletalMesh = nullptr;
	}
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	ClearDynamicMaterials();

	SkeletalMesh = InSkeletalMesh;
	if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshData())
	{
		// 기본적으로 하나의 머티리얼 슬롯만 생성
		MaterialSlots.SetNum(1);

		// 기본 머티리얼 설정
		UMaterialInterface* DefaultMaterial = UResourceManager::GetInstance().GetDefaultMaterial();
		MaterialSlots[0] = DefaultMaterial;

		MarkWorldPartitionDirty();
	}
	else
	{
		SkeletalMesh = nullptr;
	}
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
	if (static_cast<uint32>(MaterialSlots.Num()) <= InSectionIndex)
	{
		return nullptr;
	}

	UMaterialInterface* FoundMaterial = MaterialSlots[InSectionIndex];

	if (!FoundMaterial)
	{
		UE_LOG("GetMaterial: Failed to find material Section %d\n", InSectionIndex);
		return nullptr;
	}

	return FoundMaterial;
}

void USkinnedMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
	if (InElementIndex >= static_cast<uint32>(MaterialSlots.Num()))
	{
		UE_LOG("out of range InMaterialSlotIndex: %d\n", InElementIndex);
		return;
	}

	UMaterialInterface* OldMaterial = MaterialSlots[InElementIndex];

	if (OldMaterial == InNewMaterial)
	{
		return;
	}

	if (OldMaterial != nullptr)
	{
		UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(OldMaterial);
		if (OldMID)
		{
			int32 RemovedCount = DynamicMaterialInstances.Remove(OldMID);

			if (RemovedCount > 0)
			{
				delete OldMID;
			}
			else
			{
				UE_LOG("Warning: SetMaterial is replacing a MID that was not tracked by DynamicMaterialInstances.\n");
				delete OldMID;
			}
		}
	}

	MaterialSlots[InElementIndex] = InNewMaterial;
}

UMaterialInstanceDynamic* USkinnedMeshComponent::CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex)
{
	UMaterialInterface* CurrentMaterial = GetMaterial(ElementIndex);
	if (!CurrentMaterial)
	{
		return nullptr;
	}

	if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(CurrentMaterial))
	{
		return ExistingMID;
	}

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(CurrentMaterial);
	if (NewMID)
	{
		DynamicMaterialInstances.Add(NewMID);
		SetMaterial(ElementIndex, NewMID);
		NewMID->SetFilePath("(Instance) " + CurrentMaterial->GetFilePath());
		return NewMID;
	}

	return nullptr;
}

void USkinnedMeshComponent::SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture)
{
	UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
	if (MID == nullptr)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}
	MID->SetTextureParameterValue(Slot, Texture);
}

void USkinnedMeshComponent::SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value)
{
	UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
	if (MID == nullptr)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}
	MID->SetColorParameterValue(ParameterName, Value);
}

void USkinnedMeshComponent::SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value)
{
	UMaterialInterface* CurrentMaterial = GetMaterial(InMaterialSlotIndex);
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(CurrentMaterial);
	if (MID == nullptr)
	{
		MID = CreateAndSetMaterialInstanceDynamic(InMaterialSlotIndex);
	}
	MID->SetScalarParameterValue(ParameterName, Value);
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

	// TODO: 스켈레탈 메시의 바운딩 박스 계산
	// 지금은 임시로 작은 AABB 반환
	const FVector Origin = WorldTransform.TransformPosition(FVector());
	return FAABB(Origin - FVector(100, 100, 100), Origin + FVector(100, 100, 100));
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

	TMap<UMaterialInstanceDynamic*, UMaterialInstanceDynamic*> OldToNewMIDMap;

	DynamicMaterialInstances.Empty();

	for (int32 i = 0; i < MaterialSlots.Num(); ++i)
	{
		UMaterialInterface* CurrentSlot = MaterialSlots[i];
		UMaterialInstanceDynamic* OldMID = Cast<UMaterialInstanceDynamic>(CurrentSlot);

		if (OldMID)
		{
			UMaterialInstanceDynamic* NewMID = nullptr;

			if (OldToNewMIDMap.Contains(OldMID))
			{
				NewMID = OldToNewMIDMap[OldMID];
			}
			else
			{
				UMaterialInterface* Parent = OldMID->GetParentMaterial();
				if (!Parent)
				{
					MaterialSlots[i] = nullptr;
					continue;
				}

				NewMID = UMaterialInstanceDynamic::Create(Parent);
				NewMID->CopyParametersFrom(OldMID);
				DynamicMaterialInstances.Add(NewMID);
				OldToNewMIDMap.Add(OldMID, NewMID);
			}

			MaterialSlots[i] = NewMID;
		}
	}
}

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	const FString MaterialSlotsKey = "MaterialSlots";

	if (bInIsLoading)
	{
		ClearDynamicMaterials();

		JSON SlotsArrayJson;
		if (FJsonSerializer::ReadArray(InOutHandle, MaterialSlotsKey, SlotsArrayJson, JSON::Make(JSON::Class::Array), false))
		{
			MaterialSlots.SetNum(static_cast<int32>(SlotsArrayJson.size()));

			for (int i = 0; i < SlotsArrayJson.size(); ++i)
			{
				JSON& SlotJson = SlotsArrayJson.at(i);
				if (SlotJson.IsNull())
				{
					MaterialSlots[i] = nullptr;
					continue;
				}

				FString ClassName;
				FJsonSerializer::ReadString(SlotJson, "Type", ClassName, "None", false);

				UMaterialInterface* LoadedMaterial = nullptr;

				if (ClassName == UMaterialInstanceDynamic::StaticClass()->Name)
				{
					UMaterialInstanceDynamic* NewMID = new UMaterialInstanceDynamic();
					NewMID->Serialize(true, SlotJson);
					DynamicMaterialInstances.Add(NewMID);
					LoadedMaterial = NewMID;
				}
				else
				{
					FString AssetPath;
					FJsonSerializer::ReadString(SlotJson, "AssetPath", AssetPath, "", false);
					if (!AssetPath.empty())
					{
						LoadedMaterial = UResourceManager::GetInstance().Load<UMaterial>(AssetPath);
					}
					else
					{
						LoadedMaterial = nullptr;
					}
				}

				MaterialSlots[i] = LoadedMaterial;
			}
		}
	}
	else
	{
		JSON SlotsArrayJson = JSON::Make(JSON::Class::Array);
		for (UMaterialInterface* Mtl : MaterialSlots)
		{
			JSON SlotJson = JSON::Make(JSON::Class::Object);

			if (Mtl == nullptr)
			{
				SlotJson["Type"] = "None";
			}
			else
			{
				SlotJson["Type"] = Mtl->GetClass()->Name;
				Mtl->Serialize(false, SlotJson);
			}

			SlotsArrayJson.append(SlotJson);
		}
		InOutHandle[MaterialSlotsKey] = SlotsArrayJson;
	}
}
