#include "pch.h"
#include "BoneDebugComponent.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "Renderer.h"
#include "Vector.h"

IMPLEMENT_CLASS(UBoneDebugComponent)

BEGIN_PROPERTIES(UBoneDebugComponent)
	MARK_AS_COMPONENT("Bone 디버그 컴포넌트", "SkeletalMesh의 Bone 구조를 시각화합니다.")
	ADD_PROPERTY(bool, bShowBones, "Show Bones", true, "Bone 팔면체 표시 여부")
	ADD_PROPERTY(bool, bShowJoints, "Show Joints", true, "Joint Sphere 표시 여부")
	ADD_PROPERTY(float, BoneScale, "Bone Scale", true, "Bone 팔면체 크기 비율 (0.01 ~ 0.2)")
	ADD_PROPERTY(float, JointRadius, "Joint Radius", true, "Joint Sphere 반지름 (0.005 ~ 0.1)")
END_PROPERTIES()

UBoneDebugComponent::UBoneDebugComponent()
	: USceneComponent()
	, SkeletalMeshComponent(nullptr)
	, BoneColor(0.0f, 0.05f, 0.15f, 1.0f) // 거의 검은색에 가까운 매우 짙은 남색
	, JointColor(0.0f, 0.05f, 0.15f, 1.0f) // 거의 검은색에 가까운 매우 짙은 남색
	, BoneScale(0.05f)
	, JointRadius(0.02f)
	, JointSegments(8)
	, bShowBones(true)
	, bShowJoints(true)
{
	bCanEverTick = false; // 렌더링은 RenderDebugVolume에서 처리
}

void UBoneDebugComponent::OnRegister(UWorld* InWorld)
{
	Super::OnRegister(InWorld);
}

void UBoneDebugComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}

void UBoneDebugComponent::SetSkeletalMeshComponent(USkeletalMeshComponent* InComponent)
{
	SkeletalMeshComponent = InComponent;
}

void UBoneDebugComponent::RenderDebugVolume(URenderer* Renderer) const
{
	// --- 1. 유효성 검사 (Guard Clauses) ---

	// Renderer 또는 SkeletalMeshComponent가 없으면 렌더링 안 함
	if (!Renderer || !SkeletalMeshComponent)
		return;

	// Bone/Joint가 모두 숨겨져 있으면 렌더링 안 함
	if (!bShowBones && !bShowJoints)
		return;

	// SkeletalMesh 가져오기
	USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();
	if (!SkeletalMesh)
		return;

	// Skeleton 가져오기
	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton)
		return;

	int32 BoneCount = Skeleton->GetBoneCount();
	if (BoneCount == 0)
		return;

	// --- 2. 데이터 준비 ---

	// Component의 World Matrix (최종 월드 변환용)
	const FMatrix& ComponentWorldMatrix = SkeletalMeshComponent->GetWorldMatrix();

	// (B⁻¹ * A)
	// SkelComp의 Tick에서 계산된 '최종 스키닝 행렬' 배열을 가져옵니다.
	const TArray<FMatrix>& BoneMatricesRef = SkeletalMeshComponent->GetBoneMatrices();

	// 스키닝 행렬 배열이 스켈레톤과 크기가 맞는지 확인
	if (BoneMatricesRef.Num() != BoneCount)
	{
		// 아직 Tick이 돌지 않았거나, 배열이 준비되지 않았습니다.
		return;
	}

	// 디버그 라인을 저장할 임시 버퍼
	TArray<FVector> StartPoints;
	TArray<FVector> EndPoints;
	TArray<FVector4> Colors;

	// (디버깅 로그 - 필요한 경우 주석 해제)
	// static bool bLoggedOnce = false;
	// if (!bLoggedOnce)
	// {
	// 	UE_LOG("[BoneDebugComponent] Bone hierarchy for rendering:");
	// 	for (int32 i = 0; i < BoneCount; i++)
	// 	{
	// 		const FBoneInfo& BoneInfo = Skeleton->GetBone(i);
	// 		UE_LOG("  Bone[%d]: %s, ParentIndex=%d", i, BoneInfo.Name.c_str(), BoneInfo.ParentIndex);
	// 	}
	// 	bLoggedOnce = true;
	// }

	// --- 3. 하이라이팅 정보 준비 ---

	// 피킹된 본의 부모 인덱스
	int32 ParentOfPickedBone = -1;
	if (PickedBoneIndex >= 0 && PickedBoneIndex < BoneCount)
	{
		const FBoneInfo& PickedBoneInfo = Skeleton->GetBone(PickedBoneIndex);
		ParentOfPickedBone = PickedBoneInfo.ParentIndex;
	}

	// 자식 본 목록 구축 (피킹된 본의 모든 자손을 찾기 위해)
	TArray<bool> IsChildOfPicked;
	IsChildOfPicked.resize(BoneCount, false);

	if (PickedBoneIndex >= 0 && PickedBoneIndex < BoneCount)
	{
		// BFS로 모든 자식 본을 찾습니다
		TArray<int32> Queue;
		Queue.push_back(PickedBoneIndex);

		while (!Queue.empty())
		{
			int32 CurrentIndex = Queue[0];
			Queue.erase(Queue.begin());

			// 모든 본을 순회하며 현재 본의 자식을 찾습니다
			for (int32 i = 0; i < BoneCount; i++)
			{
				const FBoneInfo& BoneInfo = Skeleton->GetBone(i);
				if (BoneInfo.ParentIndex == CurrentIndex)
				{
					IsChildOfPicked[i] = true;
					Queue.push_back(i);
				}
			}
		}
	}

	// --- 4. 뼈 순회 및 라이브 포즈 역산 (Reconstruction) ---

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; BoneIndex++)
	{
		const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);

		// [★핵심 로직: 라이브 포즈 역산★]
		// A = B * (B⁻¹ * A)
		// A = GlobalBindPoseMatrix * FinalSkinMatrix

		// B (T-포즈 글로벌 행렬)
		const FMatrix& GlobalBindPoseMatrix = BoneInfo.GlobalBindPoseMatrix;
		// B⁻¹ * A (최종 스키닝 행렬)
		const FMatrix& FinalSkinMatrix = BoneMatricesRef[BoneIndex];

		// A (컴포넌트 로컬 공간 기준 "라이브 포즈" 행렬)
		FMatrix BoneLocalAnimatedMatrix = GlobalBindPoseMatrix * FinalSkinMatrix;

		// "라이브 월드 행렬" 계산 (로컬 * 월드)
		FMatrix BoneWorldMatrix = BoneLocalAnimatedMatrix * ComponentWorldMatrix;

		// 최종 월드 위치 추출
		FVector BoneWorldPos = FVector(
			BoneWorldMatrix.M[3][0],
			BoneWorldMatrix.M[3][1],
			BoneWorldMatrix.M[3][2]
		);

		// --- 5. 색상 결정 (하이라이팅) ---
		FVector4 CurrentJointColor = JointColor;
		FVector4 CurrentBoneColor = BoneColor;

		// 피킹된 본인 경우 -> 초록색
		if (BoneIndex == PickedBoneIndex)
		{
			CurrentJointColor = SelectedColor;
			CurrentBoneColor = SelectedColor;
		}
		// 피킹된 본의 자식인 경우 -> 흰색
		else if (IsChildOfPicked[BoneIndex])
		{
			CurrentJointColor = ChildColor;
			CurrentBoneColor = ChildColor;
		}

		// --- 6. 그리기 (Joints) ---
		if (bShowJoints)
		{
			GenerateJointSphere(BoneWorldPos, JointRadius, CurrentJointColor,
				StartPoints, EndPoints, Colors);
		}

		// --- 7. 그리기 (Bones) ---
		// 루트 본(ParentIndex < 0)을 제외하고, 유효한 부모가 있는 뼈만 선을 그립니다.
		if (bShowBones &&
			BoneInfo.ParentIndex >= 0 &&
			BoneInfo.ParentIndex < BoneCount &&
			BoneInfo.Name.find("_end") == std::string::npos) // (End-Site 뼈 제외)
		{
			// 부모 본의 '라이브 월드 위치'도 동일하게 역산합니다.

			// B_parent
			const FBoneInfo& ParentBoneInfo = Skeleton->GetBone(BoneInfo.ParentIndex);
			const FMatrix& ParentGlobalBindPoseMatrix = ParentBoneInfo.GlobalBindPoseMatrix;
			// (B⁻¹ * A)_parent
			const FMatrix& ParentFinalSkinMatrix = BoneMatricesRef[BoneInfo.ParentIndex];

			// A_parent = B_parent * (B⁻¹ * A)_parent
			FMatrix ParentLocalAnimatedMatrix = ParentGlobalBindPoseMatrix * ParentFinalSkinMatrix;

			// 최종 월드 행렬
			FMatrix ParentWorldMatrix = ParentLocalAnimatedMatrix * ComponentWorldMatrix;

			// 최종 월드 위치
			FVector ParentWorldPos = FVector(
				ParentWorldMatrix.M[3][0],
				ParentWorldMatrix.M[3][1],
				ParentWorldMatrix.M[3][2]
			);

			// 본 색상 결정
			FVector4 BoneColorToUse = CurrentBoneColor;
			if (BoneInfo.ParentIndex == PickedBoneIndex)
			{
				// 피킹된 조인트가 소유한 본 -> 초록색
				BoneColorToUse = SelectedColor;
			}
			else if (BoneIndex == PickedBoneIndex && ParentOfPickedBone >= 0)
			{
				// 피킹된 조인트로 연결되는 부모 본 (부모 조인트에서 피킹된 조인트로 가는 본) -> 주황색
				BoneColorToUse = ParentBoneColor;
			}

			// 부모에서 현재 본으로 팔면체(선) 그리기
			GenerateBoneOctahedron(ParentWorldPos, BoneWorldPos, BoneScale, BoneColorToUse,
				StartPoints, EndPoints, Colors);
		}
	} // (End of for loop)

	// --- 8. 최종 렌더링 호출 ---

	// 모든 라인을 한 번에 렌더링
	if (!StartPoints.empty())
	{
		Renderer->AddLines(StartPoints, EndPoints, Colors);
	}
}

void UBoneDebugComponent::GenerateBoneOctahedron(
	const FVector& Start,
	const FVector& End,
	float Scale,
	const FVector4& Color,
	TArray<FVector>& OutStartPoints,
	TArray<FVector>& OutEndPoints,
	TArray<FVector4>& OutColors) const
{
	// Bone 방향 벡터 계산
	FVector Direction = End - Start;
	float Length = Direction.Size();

	// Bone 길이가 너무 짧으면 그리지 않음
	if (Length < 0.0001f)
		return;

	Direction = Direction / Length; // 정규화
	float Radius = Length * Scale;

	// Bone 방향에 수직인 좌표계 생성
	FVector Up = FMath::Abs(Direction.Z) < 0.9f ?
		FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
	FVector Right = FVector::Cross(Direction,Up).GetNormalized();
	Up = FVector::Cross(Right, Direction).GetNormalized();

	// 팔면체 정점 생성 (중간에 4개, 양 끝에 1개씩)
	FVector Mid = Start + Direction * (Length * 0.5f);
	FVector V0 = Mid + Right * Radius;
	FVector V1 = Mid + Up * Radius;
	FVector V2 = Mid - Right * Radius;
	FVector V3 = Mid - Up * Radius;

	// 중간 사각형 링
	OutStartPoints.push_back(V0); OutEndPoints.push_back(V1); OutColors.push_back(Color);
	OutStartPoints.push_back(V1); OutEndPoints.push_back(V2); OutColors.push_back(Color);
	OutStartPoints.push_back(V2); OutEndPoints.push_back(V3); OutColors.push_back(Color);
	OutStartPoints.push_back(V3); OutEndPoints.push_back(V0); OutColors.push_back(Color);

	// 시작점으로 향하는 피라미드
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V0); OutColors.push_back(Color);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V1); OutColors.push_back(Color);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V2); OutColors.push_back(Color);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V3); OutColors.push_back(Color);

	// 끝점으로 향하는 피라미드
	OutStartPoints.push_back(End); OutEndPoints.push_back(V0); OutColors.push_back(Color);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V1); OutColors.push_back(Color);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V2); OutColors.push_back(Color);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V3); OutColors.push_back(Color);
}

void UBoneDebugComponent::GenerateJointSphere(
	const FVector& Center,
	float Radius,
	const FVector4& Color,
	TArray<FVector>& OutStartPoints,
	TArray<FVector>& OutEndPoints,
	TArray<FVector4>& OutColors) const
{
	// 3개의 대원(Great Circle)을 그려서 구를 표현 (XY, XZ, YZ 평면)
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		for (int i = 0; i < JointSegments; ++i)
		{
			const float a0 = (static_cast<float>(i) / JointSegments) * TWO_PI;
			const float a1 = (static_cast<float>((i + 1) % JointSegments) / JointSegments) * TWO_PI;

			FVector p0, p1;

			if (Axis == 0) // XY 평면 (Z 고정)
			{
				p0 = Center + FVector(Radius * std::cos(a0), Radius * std::sin(a0), 0.0f);
				p1 = Center + FVector(Radius * std::cos(a1), Radius * std::sin(a1), 0.0f);
			}
			else if (Axis == 1) // XZ 평면 (Y 고정)
			{
				p0 = Center + FVector(Radius * std::cos(a0), 0.0f, Radius * std::sin(a0));
				p1 = Center + FVector(Radius * std::cos(a1), 0.0f, Radius * std::sin(a1));
			}
			else // YZ 평면 (X 고정)
			{
				p0 = Center + FVector(0.0f, Radius * std::cos(a0), Radius * std::sin(a0));
				p1 = Center + FVector(0.0f, Radius * std::cos(a1), Radius * std::sin(a1));
			}

			OutStartPoints.push_back(p0);
			OutEndPoints.push_back(p1);
			OutColors.push_back(Color);
		}
	}
}
