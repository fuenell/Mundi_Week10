#pragma once
#include "SceneComponent.h"

class USkeletalMeshComponent;
class URenderer;

/**
 * @brief Bone 시각화를 위한 디버그 컴포넌트
 *
 * SkeletalMesh의 Bone 구조를 시각적으로 표시합니다.
 * - 각 Bone을 팔면체(Octahedron) 와이어프레임으로 표시
 * - 각 Joint(연결점)를 Sphere 와이어프레임으로 표시
 * - Renderer의 Line Batch 시스템을 사용하여 효율적으로 렌더링
 */
class UBoneDebugComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UBoneDebugComponent, USceneComponent)
	GENERATED_REFLECTION_BODY()

	UBoneDebugComponent();
	virtual ~UBoneDebugComponent() override = default;

	// === Lifecycle ===

	/**
	 * 컴포넌트 등록
	 */
	void OnRegister(UWorld* InWorld) override;

	// === Debug Rendering ===

	/**
	 * 디버그 볼륨 렌더링
	 * SceneRenderer에서 자동으로 호출됩니다.
	 */
	void RenderDebugVolume(URenderer* Renderer) const override;

	// === Configuration ===

	/**
	 * SkeletalMeshComponent 설정
	 * @param InComponent - 시각화할 SkeletalMeshComponent
	 */
	void SetSkeletalMeshComponent(USkeletalMeshComponent* InComponent);

	/**
	 * Bone 색상 설정
	 * @param InColor - RGBA 색상 (기본: 녹색)
	 */
	void SetBoneColor(const FVector4& InColor) { BoneColor = InColor; }

	/**
	 * Joint 색상 설정
	 * @param InColor - RGBA 색상 (기본: 빨간색)
	 */
	void SetJointColor(const FVector4& InColor) { JointColor = InColor; }

	/**
	 * Bone 스케일 설정
	 * @param InScale - Bone 길이 대비 팔면체 크기 비율 (0.01 ~ 0.2)
	 */
	void SetBoneScale(float InScale) { BoneScale = InScale; }

	/**
	 * Joint 반지름 설정
	 * @param InRadius - Sphere 반지름 (0.005 ~ 0.1)
	 */
	void SetJointRadius(float InRadius) { JointRadius = InRadius; }

	/**
	 * Joint Sphere 세그먼트 수 설정
	 * @param InSegments - 원 당 세그먼트 수 (6, 8, 12, 16)
	 */
	void SetJointSegments(int32 InSegments) { JointSegments = InSegments; }

	// === Visibility ===

	/**
	 * Bone 가시성 설정
	 */
	void SetBonesVisible(bool bVisible) { bShowBones = bVisible; }

	/**
	 * Joint 가시성 설정
	 */
	void SetJointsVisible(bool bVisible) { bShowJoints = bVisible; }

	/**
	 * Bone 가시성 조회
	 */
	bool AreBonesVisible() const { return bShowBones; }

	/**
	 * Joint 가시성 조회
	 */
	bool AreJointsVisible() const { return bShowJoints; }

	// === Bone Highlighting ===

	/**
	 * 피킹된 본 인덱스 설정 (하이라이팅용)
	 * @param InBoneIndex - 피킹된 본 인덱스 (-1: 선택 없음)
	 */
	void SetPickedBoneIndex(int32 InBoneIndex) { PickedBoneIndex = InBoneIndex; }

	/**
	 * 피킹된 본 인덱스 조회
	 */
	int32 GetPickedBoneIndex() const { return PickedBoneIndex; }

	// === Duplication ===

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(UBoneDebugComponent)

private:
	/**
	 * Bone 팔면체 와이어프레임 생성
	 * @param Start - Bone 시작 위치 (부모)
	 * @param End - Bone 끝 위치 (자식)
	 * @param Scale - 팔면체 크기 비율
	 * @param Color - 본 색상 (하이라이팅용)
	 * @param OutStartPoints - 라인 시작점 배열 (출력)
	 * @param OutEndPoints - 라인 끝점 배열 (출력)
	 * @param OutColors - 라인 색상 배열 (출력)
	 */
	void GenerateBoneOctahedron(
		const FVector& Start,
		const FVector& End,
		float Scale,
		const FVector4& Color,
		TArray<FVector>& OutStartPoints,
		TArray<FVector>& OutEndPoints,
		TArray<FVector4>& OutColors) const;

	/**
	 * Joint Sphere 와이어프레임 생성
	 * @param Center - Sphere 중심 위치
	 * @param Radius - Sphere 반지름
	 * @param Color - 조인트 색상 (하이라이팅용)
	 * @param OutStartPoints - 라인 시작점 배열 (출력)
	 * @param OutEndPoints - 라인 끝점 배열 (출력)
	 * @param OutColors - 라인 색상 배열 (출력)
	 */
	void GenerateJointSphere(
		const FVector& Center,
		float Radius,
		const FVector4& Color,
		TArray<FVector>& OutStartPoints,
		TArray<FVector>& OutEndPoints,
		TArray<FVector4>& OutColors) const;

private:
	// === References ===

	// 부모 SkeletalMeshComponent 참조
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

	// === Visual Properties ===

	// Bone 색상 (기본: 녹색)
	FVector4 BoneColor = FVector4(0.0f, 1.0f, 0.0f, 1.0f);

	// Joint 색상 (기본: 빨간색)
	FVector4 JointColor = FVector4(1.0f, 0.0f, 0.0f, 1.0f);

	// Bone 스케일 (Bone 길이 대비 팔면체 크기)
	float BoneScale = 0.05f;

	// Joint 반지름
	float JointRadius = 0.02f;

	// Joint Sphere 세그먼트 수 (원 당)
	int32 JointSegments = 8;

	// === Visibility Flags ===

	// Bone 표시 여부
	bool bShowBones = true;

	// Joint 표시 여부
	bool bShowJoints = true;

	// === Highlight State ===

	// 피킹된 본 인덱스 (-1: 선택 없음)
	int32 PickedBoneIndex = -1;

	// === Highlight Colors ===

	// 선택된 조인트와 본 색상 (초록색)
	FVector4 SelectedColor = FVector4(0.0f, 1.0f, 0.0f, 1.0f);

	// 부모 본 색상 (주황색)
	FVector4 ParentBoneColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);

	// 자식 조인트와 본 색상 (흰색)
	FVector4 ChildColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
};
