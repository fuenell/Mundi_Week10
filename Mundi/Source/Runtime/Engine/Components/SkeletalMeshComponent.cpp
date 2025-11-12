#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "Shader.h"
#include "Texture.h"
#include "ResourceManager.h"
#include "World.h"
#include "WorldPartitionManager.h"
#include "RenderSettings.h"
#include "JsonSerializer.h"
#include "MeshBatchElement.h"
#include "Material.h"
#include "SceneView.h"
#include "BoneDebugComponent.h"

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "애니메이션 재생이 가능한 스켈레탈 메시 컴포넌트입니다.")
	ADD_PROPERTY(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
{
	// CPU Skinning을 위해 Tick 활성화
	bCanEverTick = true;

	// BoneDebugComponent 생성
	BoneDebugComponent = NewObject<UBoneDebugComponent>();
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetSkeletalMeshComponent(this);
		BoneDebugComponent->SetupAttachment(this);
		BoneDebugComponent->SetBonesVisible(false); // 기본값: 숨김
		BoneDebugComponent->SetJointsVisible(false); // 기본값: 숨김
	}
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = InSkeletalMesh;

	// Material 슬롯 초기화
	MaterialSlots.Empty();
	CustomBoneLocalTransform.clear();
	if (SkeletalMesh)
	{
		// Static Mesh Component와 동일한 패턴: GroupInfos 기반 Material 자동 설정
		const TArray<FGroupInfo>& GroupInfos = SkeletalMesh->GetMeshGroupInfo();

		if (!GroupInfos.empty())
		{
			// GroupInfos에서 Material 로드 (StaticMeshComponent와 동일한 방식)
			MaterialSlots.resize(GroupInfos.size());
			for (int i = 0; i < GroupInfos.size(); ++i)
			{
				SetMaterialByName(i, GroupInfos[i].InitialMaterialName);
			}
		}
		else
		{
			// 레거시 지원: GroupInfos가 비어있으면 MaterialNames 배열 사용
			const TArray<FString>& MaterialNames = SkeletalMesh->GetMaterialNames();
			if (!MaterialNames.empty())
			{
				MaterialSlots.resize(MaterialNames.size());
				for (int i = 0; i < MaterialNames.size(); ++i)
				{
					SetMaterialByName(i, MaterialNames[i]);
				}
			}
			else
			{
				// 최종 레거시 지원: 단일 MaterialName 사용
				const FString& MaterialName = SkeletalMesh->GetMaterialName();
				MaterialSlots.resize(1);
				SetMaterialByName(0, MaterialName);
			}
		}

		// Bone Transform 업데이트
		if (SkeletalMesh->GetSkeleton())
		{
			ResetBoneTransforms();
		}
	}

	MarkWorldPartitionDirty();
}

void USkeletalMeshComponent::ResetBoneTransforms()
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

	// === TEST: Root 본 회전 애니메이션 ===
	// 최상위 본을 Y축 기준으로 한 바퀴 계속 회전

	// 한 바퀴 계속 회전 (360도)
	float RotationRad = DeltaTime * 1.0f;  // 초당 2 라디안 회전 (약 114도/초)

	FTransform RotationMatrix = FTransform(FVector::Zero(), FQuat::MakeFromEulerZYX(FVector(0, 0, RadiansToDegrees(RotationRad))), FVector::One());
	FTransform RotationMatrix1 = FTransform(FVector::Zero(), FQuat::MakeFromEulerZYX(FVector(0, RadiansToDegrees(RotationRad), 0)), FVector::One());

	MoveBone(0, RotationMatrix);
	MoveBone(1, RotationMatrix1);

	if (bNeedsBoneTransformUpdate)
	{
		StartUpdateBoneRecursive();

		// CPU Skinning 수행
		PerformCPUSkinning();

		bNeedsBoneTransformUpdate = false;
	}
}

void USkeletalMeshComponent::StartUpdateBoneRecursive()
{
	USkeleton* Skeleton = GetSkeleton();
	const FBoneInfo& CurrentBoneInfo = Skeleton->GetBone(0);

	// UpdateBoneRecursive(0, CurrentBoneInfo.GlobalBindPoseMatrix);
	UpdateBoneRecursive(0, FMatrix::Identity());
}

void USkeletalMeshComponent::PerformCPUSkinning()
{
	if (!SkeletalMesh || !bEnableCPUSkinning)
	{
		return;
	}

	// Show Flag 체크: Skeletal Mesh가 렌더링되지 않으면 CPU Skinning도 건너뛰기
	UWorld* World = GetWorld();
	if (World && !World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
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

	// GPU Buffer 업데이트
	if (SkeletalMesh->UsesDynamicBuffer() && !SkinnedVertices.empty())
	{
		ID3D11DeviceContext* Context = UResourceManager::GetInstance().GetContext();
		SkeletalMesh->UpdateVertexBuffer(Context, SkinnedVertices);
	}
}

// 'SetBoneTransform'은 이제 로컬 트랜스폼을 '오버라이드 맵'에 저장합니다.
void USkeletalMeshComponent::SetBoneTransform(int32 BoneIndex, const FTransform& Transform)
{
	USkeleton* Skeleton = GetSkeleton();
	if (!Skeleton || BoneIndex < 0 || BoneIndex >= Skeleton->GetBoneCount())
	{
		return;
	}

	// 전달받은 '로컬' 트랜스폼을 '커스텀 오버라이드 맵'에 저장합니다.
	// TickComponent가 이 값을 읽어갈 것입니다.
	CustomBoneLocalTransform[BoneIndex] = Transform;
	bNeedsBoneTransformUpdate = true;
}

// 상대 좌표 행렬로 움직이기
void USkeletalMeshComponent::MoveBone(int TargetBoneIndex, const FTransform& Transform)
{
	USkeleton* Skeleton = GetSkeleton();
	if (Skeleton && Skeleton->GetBoneCount() > TargetBoneIndex)
	{
		// 루트 본부터 시작 (부모 행렬은 단위 행렬)
		const FBoneInfo& CurrentBoneInfo = Skeleton->GetBone(TargetBoneIndex);

		if (CustomBoneLocalTransform.find(TargetBoneIndex) != CustomBoneLocalTransform.end())
		{
			// 오버라이드
			CustomBoneLocalTransform[TargetBoneIndex] = CustomBoneLocalTransform[TargetBoneIndex].GetWorldTransform(Transform);
		}
		else
		{
			CustomBoneLocalTransform[TargetBoneIndex] = CurrentBoneInfo.BindPoseRelativeTransform.GetWorldTransform(Transform);
		}

		bNeedsBoneTransformUpdate = true;
	}
}

void USkeletalMeshComponent::UpdateBoneRecursive(int32 BoneIndex, const FMatrix& ParentAnimatedTransform)
{
	USkeleton* Skeleton = GetSkeleton();

	if (BoneIndex < 0 || BoneIndex >= Skeleton->GetBoneCount())
		return;

	const FBoneInfo& CurrentBoneInfo = Skeleton->GetBone(BoneIndex);

	FTransform LocalTransform = CurrentBoneInfo.BindPoseRelativeTransform;

	if (CustomBoneLocalTransform.find(BoneIndex) != CustomBoneLocalTransform.end())
	{
		// 오버라이드
		LocalTransform = CustomBoneLocalTransform[BoneIndex];
	}

	// 로컬 행렬에 부모 행렬을 곱해서 본의 모델위치를 복구 
	FMatrix CurrentAnimatedTransform = LocalTransform.ToMatrix() * ParentAnimatedTransform;

	// 4. (기존과 동일 - 올바른 로직)
	// 최종 스키닝 행렬을 계산합니다.
	// SkinMatrix = InverseBindPose(Global) * Animated(Global)
	if (BoneIndex < BoneMatrices.size())
	{
		BoneMatrices[BoneIndex] = CurrentBoneInfo.InverseBindPoseMatrix * CurrentAnimatedTransform;
	}

	// 5. (기존과 동일 - 올바른 로직)
	// 모든 자식 본 재귀 업데이트
	TArray<int32> ChildBones = Skeleton->GetChildBones(BoneIndex);
	for (int32 ChildIndex : ChildBones)
	{
		// 자식에게는 방금 계산한 '새로운 글로벌 포즈'를 부모 행렬로 넘겨줍니다.
		UpdateBoneRecursive(ChildIndex, CurrentAnimatedTransform);
	}
}

// NOTE: 각 뼈 트랜스폼을 캐싱해서 반환하도록 변경해도 좋음, 일단 매번 부모 노드까지 순회해서 계산
FTransform USkeletalMeshComponent::GetBoneWorldTransform(int32 BoneIndex)
{
	USkeleton* Skeleton = GetSkeleton();

	FTransform Result;

	if (BoneIndex < 0 || BoneIndex >= Skeleton->GetBoneCount())
		return Result;

	// 부모 노드까지 순회
	while (0 <= BoneIndex)
	{
		const FBoneInfo& CurrentBoneInfo = Skeleton->GetBone(BoneIndex);

		FTransform LocalTransform = CurrentBoneInfo.BindPoseRelativeTransform;

		if (CustomBoneLocalTransform.find(BoneIndex) != CustomBoneLocalTransform.end())
		{
			LocalTransform = CustomBoneLocalTransform[BoneIndex];
		}

		Result = LocalTransform.GetWorldTransform(Result);

		BoneIndex = CurrentBoneInfo.ParentIndex;
	}

	return Result;
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

	// BoneDebugComponent 복사
	if (BoneDebugComponent)
	{
		BoneDebugComponent->DuplicateSubObjects();
	}
}

void USkeletalMeshComponent::SetShowBoneDebug(bool bShow)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetBonesVisible(bShow);
		BoneDebugComponent->SetJointsVisible(bShow);
	}
}

bool USkeletalMeshComponent::IsShowBoneDebug() const
{
	if (BoneDebugComponent)
	{
		return BoneDebugComponent->AreBonesVisible() || BoneDebugComponent->AreJointsVisible();
	}
	return false;
}
