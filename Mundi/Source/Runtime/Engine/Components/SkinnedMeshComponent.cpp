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
	// NOTE: USkeletalMesh* 이런식으로 변수타입이 아니라 enum을 넘기는 방식으로 모두 교체? or 타입을 넘지기 않고 자동으로 enum 타입을 찾아서 저장하록 변경
	ADD_PROPERTY(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
	ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

USkinnedMeshComponent::USkinnedMeshComponent()
{
	// 기본 SkeletalMesh는 설정하지 않음 (Import 시 설정됨)
	SkeletalMesh = nullptr;

	// CPU Skinning을 위해 Tick 활성화
	bCanEverTick = true;
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
	if (Material)
	{
		const FMaterialInfo& Info = Material->GetMaterialInfo();
	}
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

	// Material 슬롯 초기화
	MaterialSlots.Empty();
	if (SkeletalMesh)
	{
		// SkeletalMesh가 Material을 가지고 있으면 사용, 없으면 기본 Material
		UMaterialInterface* Mat = SkeletalMesh->GetMaterial();
		if (!Mat)
		{
			Mat = UResourceManager::GetInstance().GetDefaultMaterial();
			UE_LOG("USkinnedMeshComponent: Using default material (SkeletalMesh has no material)");
		}
		else
		{
			UE_LOG("USkinnedMeshComponent: Using SkeletalMesh's material");
		}
		MaterialSlots.push_back(Mat);

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

// === Component Lifecycle ===

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (!SkeletalMesh || !bEnableCPUSkinning)
	{
		return;
	}

	// Bone Transform 업데이트 (필요시)
	if (bNeedsBoneTransformUpdate)
	{
		UpdateBoneTransforms();
	}

	// CPU Skinning 수행
	PerformCPUSkinning();

	// GPU Buffer 업데이트
	if (SkeletalMesh->UsesDynamicBuffer() && !SkinnedVertices.empty())
	{
		ID3D11DeviceContext* Context = UResourceManager::GetInstance().GetContext();
		SkeletalMesh->UpdateVertexBuffer(Context, SkinnedVertices);
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

	// 디버그: 첫 프레임에만 로그 출력
	static bool bDebugFirstBoneUpdate = true;

	// 각 Bone의 Skinning Matrix 계산
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; BoneIndex++)
	{
		const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);

		// Skinning 공식: SkinnedVertex = OriginalVertex × (InverseBindPose × BoneTransform)
		//
		// Phase 1: Bind Pose만 사용 (Animation은 Phase 6+)
		// BoneTransform = GlobalBindPoseMatrix (FBX Cluster에서 추출)
		//
		// CRITICAL: GlobalBindPoseMatrix를 직접 사용!
		// Local Transform을 누적하여 계산하면 FBX Cluster의 값과 일치하지 않음
		//
		// 이론적으로: InverseBindPose × GlobalBindPose = Identity (Bind Pose 상태)

		// Global Bind Pose Matrix 사용 (FBX Cluster에서 직접 추출한 값)
		// Animation이 있을 경우 이 값을 AnimatedBoneTransform으로 교체
		FMatrix GlobalBoneTransform = BoneInfo.GlobalBindPoseMatrix;

		// Inverse Bind Pose 적용
		// 최종 Bone Matrix = InverseBindPose × GlobalBoneTransform
		BoneMatrices[BoneIndex] = BoneInfo.InverseBindPoseMatrix * GlobalBoneTransform;

		// 디버그: 첫 번째 본 정보 출력
		if (bDebugFirstBoneUpdate && BoneIndex == 0)
		{
			UE_LOG("[Bone Transform Debug] First Bone (Index 0):");
			UE_LOG("  Bone Name: %s", BoneInfo.Name.c_str());
			UE_LOG("  Parent Index: %d", BoneInfo.ParentIndex);

			FMatrix& FinalMatrix = BoneMatrices[BoneIndex];
			UE_LOG("  Final Matrix Row 0: (%.3f, %.3f, %.3f, %.3f)",
				FinalMatrix.M[0][0], FinalMatrix.M[0][1], FinalMatrix.M[0][2], FinalMatrix.M[0][3]);
			UE_LOG("  Final Matrix Row 1: (%.3f, %.3f, %.3f, %.3f)",
				FinalMatrix.M[1][0], FinalMatrix.M[1][1], FinalMatrix.M[1][2], FinalMatrix.M[1][3]);
			UE_LOG("  Final Matrix Row 2: (%.3f, %.3f, %.3f, %.3f)",
				FinalMatrix.M[2][0], FinalMatrix.M[2][1], FinalMatrix.M[2][2], FinalMatrix.M[2][3]);
			UE_LOG("  Final Matrix Row 3: (%.3f, %.3f, %.3f, %.3f)",
				FinalMatrix.M[3][0], FinalMatrix.M[3][1], FinalMatrix.M[3][2], FinalMatrix.M[3][3]);

			// NaN/Infinity 검사
			bool bHasInvalidValue = false;
			for (int Row = 0; Row < 4; Row++)
			{
				for (int Col = 0; Col < 4; Col++)
				{
					float Value = FinalMatrix.M[Row][Col];
					if (std::isnan(Value) || std::isinf(Value))
					{
						UE_LOG("[error] Bone matrix contains NaN or Infinity at M[%d][%d]!", Row, Col);
						bHasInvalidValue = true;
						break;
					}
				}
				if (bHasInvalidValue) break;
			}

			if (!bHasInvalidValue)
			{
				UE_LOG("[Bone Transform Debug] First bone matrix appears valid.");
			}
		}
	}

	bNeedsBoneTransformUpdate = false;

	if (bDebugFirstBoneUpdate)
	{
		UE_LOG("USkinnedMeshComponent: Updated %d bone transforms (first time)", BoneCount);
		bDebugFirstBoneUpdate = false;
	}
}

void USkinnedMeshComponent::SetBoneTransform(int32 BoneIndex, const FTransform& Transform)
{
	// TODO: 향후 Animation 시스템에서 사용
	// 현재는 플래그만 설정
	bNeedsBoneTransformUpdate = true;
}

// === CPU Skinning ===

void USkinnedMeshComponent::PerformCPUSkinning()
{
	if (!SkeletalMesh || !bEnableCPUSkinning)
	{
		return;
	}

	const TArray<FSkinnedVertex>& SourceVertices = SkeletalMesh->GetVerticesRef();
	const TArray<FMatrix>& BoneMatricesRef = GetBoneMatrices();

	if (SourceVertices.empty() || BoneMatricesRef.empty())
	{
		return;
	}

	// Skinned Vertices 준비 (GPU 전송용)
	SkinnedVertices.resize(SourceVertices.size());

	// 디버그: 첫 프레임에만 로그 출력
	static bool bFirstFrame = true;
	if (bFirstFrame)
	{
		UE_LOG("[CPU Skinning] Processing %zu vertices with %zu bones",
			SourceVertices.size(), BoneMatricesRef.size());
		bFirstFrame = false;
	}

	// 각 Vertex Skinning
	for (size_t VertIndex = 0; VertIndex < SourceVertices.size(); VertIndex++)
	{
		const FSkinnedVertex& SrcVert = SourceVertices[VertIndex];
		FNormalVertex& DstVert = SkinnedVertices[VertIndex];

		// Skinning 계산
		FVector SkinnedPos(0, 0, 0);
		FVector SkinnedNormal(0, 0, 0);
		FVector SkinnedTangent(0, 0, 0);

		// 최대 4개의 Bone Influence 적용
		for (int32 i = 0; i < 4; i++)
		{
			int32 BoneIndex = SrcVert.BoneIndices[i];
			float Weight = SrcVert.BoneWeights[i];

			if (Weight > 0.0f && BoneIndex >= 0 && BoneIndex < static_cast<int32>(BoneMatricesRef.size()))
			{
				const FMatrix& BoneMatrix = BoneMatricesRef[BoneIndex];

				// Position Skinning (Affine Transform)
				FVector4 Pos4 = FVector4(SrcVert.Position.X,
					SrcVert.Position.Y,
					SrcVert.Position.Z,
					1.0f);  // w=1 (위치)
				FVector4 TransformedPos = Pos4 * BoneMatrix;
				SkinnedPos += FVector(TransformedPos.X,
					TransformedPos.Y,
					TransformedPos.Z) * Weight;

				// Normal Skinning (3x3 회전만 적용)
				FVector4 Normal4 = FVector4(SrcVert.Normal.X,
					SrcVert.Normal.Y,
					SrcVert.Normal.Z,
					0.0f);  // w=0 (방향)
				FVector4 TransformedNormal = Normal4 * BoneMatrix;
				SkinnedNormal += FVector(TransformedNormal.X,
					TransformedNormal.Y,
					TransformedNormal.Z) * Weight;

				// Tangent Skinning
				FVector4 Tangent4 = FVector4(SrcVert.Tangent.X,
					SrcVert.Tangent.Y,
					SrcVert.Tangent.Z,
					0.0f);  // w=0 (방향)
				FVector4 TransformedTangent = Tangent4 * BoneMatrix;
				SkinnedTangent += FVector(TransformedTangent.X,
					TransformedTangent.Y,
					TransformedTangent.Z) * Weight;
			}
		}

		// 결과 저장 (FNormalVertex 형식)
		DstVert.pos = SkinnedPos;
		DstVert.normal = SkinnedNormal.GetNormalized();
		DstVert.tex = SrcVert.UV;

		// Tangent 저장 (w 성분 유지)
		FVector NormalizedTangent = SkinnedTangent.GetNormalized();
		DstVert.Tangent = FVector4(NormalizedTangent.X,
			NormalizedTangent.Y,
			NormalizedTangent.Z,
			SrcVert.Tangent.W);

		DstVert.color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// 디버그: 첫 프레임에 첫 몇 개 Vertex 확인
	static bool bDebugFirstVertex = true;
	if (bDebugFirstVertex && !SourceVertices.empty())
	{
		// 첫 번째 Vertex 정보 출력
		const FSkinnedVertex& FirstSrcVert = SourceVertices[0];
		const FNormalVertex& FirstDstVert = SkinnedVertices[0];

		UE_LOG("[CPU Skinning Debug] First Vertex Info:");
		UE_LOG("  Original Pos: (%.3f, %.3f, %.3f)",
			FirstSrcVert.Position.X, FirstSrcVert.Position.Y, FirstSrcVert.Position.Z);
		UE_LOG("  Skinned Pos: (%.3f, %.3f, %.3f)",
			FirstDstVert.pos.X, FirstDstVert.pos.Y, FirstDstVert.pos.Z);
		UE_LOG("  Bone Indices: [%d, %d, %d, %d]",
			FirstSrcVert.BoneIndices[0], FirstSrcVert.BoneIndices[1],
			FirstSrcVert.BoneIndices[2], FirstSrcVert.BoneIndices[3]);
		UE_LOG("  Bone Weights: [%.3f, %.3f, %.3f, %.3f]",
			FirstSrcVert.BoneWeights[0], FirstSrcVert.BoneWeights[1],
			FirstSrcVert.BoneWeights[2], FirstSrcVert.BoneWeights[3]);

		// NaN/Infinity 검사
		bool bHasInvalidValue = false;
		if (std::isnan(FirstDstVert.pos.X) || std::isnan(FirstDstVert.pos.Y) || std::isnan(FirstDstVert.pos.Z))
		{
			UE_LOG("[error] CPU Skinning produced NaN values!");
			bHasInvalidValue = true;
		}
		if (std::isinf(FirstDstVert.pos.X) || std::isinf(FirstDstVert.pos.Y) || std::isinf(FirstDstVert.pos.Z))
		{
			UE_LOG("[error] CPU Skinning produced Infinity values!");
			bHasInvalidValue = true;
		}
		if (FirstDstVert.pos.X == 0.0f && FirstDstVert.pos.Y == 0.0f && FirstDstVert.pos.Z == 0.0f)
		{
			UE_LOG("[warning] CPU Skinning produced (0,0,0) position - may be invalid!");
		}

		if (!bHasInvalidValue)
		{
			UE_LOG("[CPU Skinning Debug] First vertex values appear valid.");
		}

		bDebugFirstVertex = false;
	}
}
