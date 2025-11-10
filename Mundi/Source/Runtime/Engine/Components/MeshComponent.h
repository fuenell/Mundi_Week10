#pragma once
#include "PrimitiveComponent.h"

class UShader;

class UMeshComponent : public UPrimitiveComponent
{
public:
    DECLARE_CLASS(UMeshComponent, UPrimitiveComponent)
    GENERATED_REFLECTION_BODY()
    
    UMeshComponent();

protected:
    ~UMeshComponent() override;
    void ClearDynamicMaterials();

public:
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(UMeshComponent)

    bool IsCastShadows() { return bCastShadows; }
    
    void SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial) override;

    const TArray<UMaterialInterface*> GetMaterialSlots() const { return MaterialSlots; }

    UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;

    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(uint32 ElementIndex);

    void SetMaterialTextureByUser(const uint32 InMaterialSlotIndex, EMaterialTextureSlot Slot, UTexture* Texture);
    void SetMaterialColorByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, const FLinearColor& Value);
    void SetMaterialScalarByUser(const uint32 InMaterialSlotIndex, const FString& ParameterName, float Value);

    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
protected:
    TArray<UMaterialInterface*> MaterialSlots;
    TArray<UMaterialInstanceDynamic*> DynamicMaterialInstances;
    
private:
    bool bCastShadows = true;   // TODO: 프로퍼티로 추가 필요
};