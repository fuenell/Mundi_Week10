#include "pch.h"
#include "SkeletalMeshComponent.h"
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

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "애니메이션 재생이 가능한 스켈레탈 메시 컴포넌트입니다.")
	ADD_PROPERTY(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
	// CPU Skinning을 위해 Tick 활성화
	bCanEverTick = true;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = InSkeletalMesh;

	// Material 슬롯 초기화
	MaterialSlots.Empty();
	if (SkeletalMesh)
	{
		// 다중 Material 지원: SkeletalMesh의 모든 Material 로드
		const TArray<FString>& MaterialNames = SkeletalMesh->GetMaterialNames();

		if (!MaterialNames.empty())
		{
			// 모든 Material을 MaterialSlots에 추가
			// Static Mesh Component와 동일한 패턴: SetMaterialByName으로 Load 수행
			MaterialSlots.resize(MaterialNames.size());
			for (int i = 0; i < MaterialNames.size(); ++i)
			{
				SetMaterialByName(i, MaterialNames[i]);
			}
		}
		else
		{
			// 레거시 지원: MaterialNames가 비어있으면 단일 MaterialName 사용
			const FString& MaterialName = SkeletalMesh->GetMaterialName();
			MaterialSlots.resize(1);
			SetMaterialByName(0, MaterialName);
		}

		// Bone Transform 업데이트
		if (SkeletalMesh->GetSkeleton())
		{
			UpdateBoneTransforms();
		}
	}

	MarkWorldPartitionDirty();
}

USkeleton* USkeletalMeshComponent::GetSkeleton() const
{
	if (SkeletalMesh)
	{
		return SkeletalMesh->GetSkeleton();
	}
	return nullptr;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
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

void USkeletalMeshComponent::UpdateBoneTransforms()
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
	}

	bNeedsBoneTransformUpdate = false;
}

void USkeletalMeshComponent::SetBoneTransform(int32 BoneIndex, const FTransform& Transform)
{
	// TODO: 향후 Animation 시스템에서 사용
	// 현재는 플래그만 설정
	bNeedsBoneTransformUpdate = true;
}

void USkeletalMeshComponent::PerformCPUSkinning()
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
}

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
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
		return;
	}

	// Material별 Section 처리 (StaticMeshComponent와 동일)
	const TArray<FGroupInfo>& MeshGroupInfos = SkeletalMesh->GetMeshGroupInfo();

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
			// 기본 머티리얼 사용
			Material = UResourceManager::GetInstance().GetDefaultMaterial();
			if (Material)
			{
				Shader = Material->GetShader();
			}
			if (!Material || !Shader)
			{
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
		{
			continue;
		}

		auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
		if (!MaterialToUse || !ShaderToUse)
		{
			continue;
		}

		FMeshBatchElement BatchElement;

		// View 모드 전용 매크로와 머티리얼 개인 매크로를 결합
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
		BatchElement.VertexBuffer = VertexBuffer;
		BatchElement.IndexBuffer = IndexBuffer;
		BatchElement.VertexStride = SkeletalMesh->GetVertexStride();
		BatchElement.IndexCount = IndexCount;
		BatchElement.StartIndex = StartIndex;
		BatchElement.BaseVertexIndex = 0;
		BatchElement.WorldMatrix = GetWorldMatrix();
		BatchElement.ObjectID = InternalIndex;
		BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		OutMeshBatchElements.Add(BatchElement);
	}
}

FAABB USkeletalMeshComponent::GetWorldAABB() const
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

void USkeletalMeshComponent::MarkWorldPartitionDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldPartitionManager* Partition = World->GetPartitionManager())
		{
			Partition->MarkDirty(this);
		}
	}
}

void USkeletalMeshComponent::DuplicateSubObjects()
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
