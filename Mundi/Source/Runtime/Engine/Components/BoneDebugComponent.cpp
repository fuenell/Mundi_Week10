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
	, BoneColor(0.0f, 1.0f, 0.0f, 1.0f) // 녹색
	, JointColor(1.0f, 0.0f, 0.0f, 1.0f) // 빨간색
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

	// Component의 World Matrix 가져오기
	FMatrix ComponentWorldMatrix = SkeletalMeshComponent->GetWorldMatrix();

	// 라인 배열 준비
	TArray<FVector> StartPoints;
	TArray<FVector> EndPoints;
	TArray<FVector4> Colors;

	int32 BoneCount = Skeleton->GetBoneCount();

	// 디버깅: 본 계층 구조 로그 (한 번만)
	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		UE_LOG("[BoneDebugComponent] Bone hierarchy for rendering:");
		for (int32 i = 0; i < BoneCount; i++)
		{
			const FBoneInfo& BoneInfo = Skeleton->GetBone(i);
			UE_LOG("  Bone[%d]: %s, ParentIndex=%d", i, BoneInfo.Name.c_str(), BoneInfo.ParentIndex);
		}
		bLoggedOnce = true;
	}

	// 각 Bone에 대해 시각화
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; BoneIndex++)
	{
		const FBoneInfo& BoneInfo = Skeleton->GetBone(BoneIndex);

		// Bone의 World 위치 계산 (Bind Pose 사용)
		FMatrix BoneWorldMatrix = BoneInfo.GlobalBindPoseMatrix * ComponentWorldMatrix;
		FVector BoneWorldPos = FVector(
			BoneWorldMatrix.M[3][0],
			BoneWorldMatrix.M[3][1],
			BoneWorldMatrix.M[3][2]
		);

		// Joint Sphere 그리기
		if (bShowJoints)
		{
			GenerateJointSphere(BoneWorldPos, JointRadius,
				StartPoints, EndPoints, Colors);
		}

		// Bone 팔면체 그리기 (유효한 부모가 있을 때만)
		// 루트 본이 아니고(BoneIndex > 0), 부모 인덱스가 유효하고, 자기 자신이 아닐 때
		// "_end" 본은 제외 (더미 엔드 포인트)
		if (bShowBones &&
			BoneIndex > 0 &&
			BoneInfo.ParentIndex >= 0 &&
			BoneInfo.ParentIndex < BoneCount &&
			BoneInfo.ParentIndex != BoneIndex &&
			BoneInfo.Name.find("_end") == std::string::npos)
		{
			// 부모 본의 World 위치 계산
			const FBoneInfo& ParentBoneInfo = Skeleton->GetBone(BoneInfo.ParentIndex);
			FMatrix ParentWorldMatrix = ParentBoneInfo.GlobalBindPoseMatrix * ComponentWorldMatrix;
			FVector ParentWorldPos = FVector(
				ParentWorldMatrix.M[3][0],
				ParentWorldMatrix.M[3][1],
				ParentWorldMatrix.M[3][2]
			);

			// 부모에서 현재 본으로 팔면체 그리기
			GenerateBoneOctahedron(ParentWorldPos, BoneWorldPos, BoneScale,
				StartPoints, EndPoints, Colors);
		}
	}

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
	OutStartPoints.push_back(V0); OutEndPoints.push_back(V1); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(V1); OutEndPoints.push_back(V2); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(V2); OutEndPoints.push_back(V3); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(V3); OutEndPoints.push_back(V0); OutColors.push_back(BoneColor);

	// 시작점으로 향하는 피라미드
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V0); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V1); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V2); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(Start); OutEndPoints.push_back(V3); OutColors.push_back(BoneColor);

	// 끝점으로 향하는 피라미드
	OutStartPoints.push_back(End); OutEndPoints.push_back(V0); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V1); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V2); OutColors.push_back(BoneColor);
	OutStartPoints.push_back(End); OutEndPoints.push_back(V3); OutColors.push_back(BoneColor);
}

void UBoneDebugComponent::GenerateJointSphere(
	const FVector& Center,
	float Radius,
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
			OutColors.push_back(JointColor);
		}
	}
}
