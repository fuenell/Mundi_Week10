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

	// === Duplication ===

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(UBoneDebugComponent)

private:
	/**
	 * Bone 팔면체 와이어프레임 생성
	 * @param Start - Bone 시작 위치 (부모)
	 * @param End - Bone 끝 위치 (자식)
	 * @param Scale - 팔면체 크기 비율
	 * @param OutStartPoints - 라인 시작점 배열 (출력)
	 * @param OutEndPoints - 라인 끝점 배열 (출력)
	 * @param OutColors - 라인 색상 배열 (출력)
	 */
	void GenerateBoneOctahedron(
		const FVector& Start,
		const FVector& End,
		float Scale,
		TArray<FVector>& OutStartPoints,
		TArray<FVector>& OutEndPoints,
		TArray<FVector4>& OutColors) const;

	/**
	 * Joint Sphere 와이어프레임 생성
	 * @param Center - Sphere 중심 위치
	 * @param Radius - Sphere 반지름
	 * @param OutStartPoints - 라인 시작점 배열 (출력)
	 * @param OutEndPoints - 라인 끝점 배열 (출력)
	 * @param OutColors - 라인 색상 배열 (출력)
	 */
	void GenerateJointSphere(
		const FVector& Center,
		float Radius,
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
};
