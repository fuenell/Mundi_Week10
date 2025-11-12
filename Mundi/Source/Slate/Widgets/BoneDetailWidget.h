#pragma once
#include "Widget.h"
#include "Vector.h"

class USkeletalMesh;
class USkeleton;
class USkeletalMeshEditorWindow;

/**
 * @brief 선택된 Bone의 상세 정보를 표시하고 편집하는 위젯
 *
 * Bone의 Transform(Position, Rotation, Scale) 정보를 표시하고
 * 사용자가 직접 편집할 수 있는 UI를 제공합니다.
 */
class UBoneDetailWidget : public UWidget
{
public:
	DECLARE_CLASS(UBoneDetailWidget, UWidget)

	UBoneDetailWidget();
	~UBoneDetailWidget() override = default;

	// === Widget Lifecycle ===

	/**
	 * 위젯 초기화
	 */
	void Initialize() override;

	/**
	 * 위젯 렌더링
	 * ImGui를 사용하여 Bone Transform 정보를 표시하고 편집
	 */
	void RenderWidget() override;

	/**
	 * 위젯 업데이트
	 */
	void Update() override;

	// === SkeletalMesh 관리 ===

	/**
	 * 표시할 SkeletalMesh 설정
	 * @param InMesh - SkeletalMesh 객체
	 */
	void SetSkeletalMesh(USkeletalMesh* InMesh);

	/**
	 * 현재 설정된 SkeletalMesh 가져오기
	 * @return SkeletalMesh 객체 (nullptr 가능)
	 */
	USkeletalMesh* GetSkeletalMesh() const { return TargetSkeletalMesh; }

	// === Bone 선택 관리 ===

	/**
	 * 표시할 Bone 설정
	 * @param BoneIndex - Bone 인덱스 (-1인 경우 선택 해제)
	 */
	void SetSelectedBone(int32 BoneIndex);

	/**
	 * 현재 선택된 Bone 인덱스 가져오기
	 * @return Bone 인덱스 (-1인 경우 선택되지 않음)
	 */
	int32 GetSelectedBoneIndex() const { return CurrentBoneIndex; }

	/**
	 * Bone 선택 해제
	 */
	void ClearSelection();

	void SetSkeletalMeshEditorWindow(USkeletalMeshEditorWindow* InSkeletalMeshEditorWindow) { SkeletalMeshEditorWindow = InSkeletalMeshEditorWindow; }

private:
	/**
	 * Bone Transform 데이터를 로드하여 편집용 변수에 저장
	 */
	void LoadBoneTransform();

	/**
	 * 편집된 Bone Transform을 SkeletalMesh에 적용
	 */
	void ApplyBoneTransform();

	/**
	 * Transform 속성 렌더링 (Position, Rotation, Scale)
	 * @param Label - 속성 라벨 (예: "Position")
	 * @param Value - 편집할 Vector 값
	 * @param DragSpeed - 드래그 속도
	 * @param bIsRotation - Rotation 속성인지 여부 (Degree 단위로 표시)
	 */
	void RenderTransformProperty(const char* Label, FVector& Value, float DragSpeed = 0.1f, bool bIsRotation = false);

	/**
	 * Matrix를 Position, Rotation(Euler), Scale로 분해
	 * @param Matrix - 분해할 Matrix
	 * @param OutPosition - 출력 Position
	 * @param OutRotation - 출력 Rotation (Euler Angles, Degrees)
	 * @param OutScale - 출력 Scale
	 */
	void DecomposeMatrix(const FMatrix& Matrix, FVector& OutPosition, FVector& OutRotation, FVector& OutScale);

	/**
	 * Position, Rotation(Euler), Scale로부터 Matrix 생성
	 * @param Position - Position
	 * @param Rotation - Rotation (Euler Angles, Degrees)
	 * @param Scale - Scale
	 * @return 생성된 Matrix
	 */
	FMatrix ComposeMatrix(const FVector& Position, const FVector& Rotation, const FVector& Scale);

private:
	// 수정을 위한 USkeletalMeshComponent
	USkeletalMeshComponent* TargetComponent;

	// 표시할 SkeletalMesh
	USkeletalMesh* TargetSkeletalMesh = nullptr;

	// 현재 선택된 Bone 인덱스 (-1: 선택 없음)
	int32 CurrentBoneIndex = -1;

	// Bone 이름
	FString BoneName;

	// === 편집용 Transform 데이터 ===

	// Position (Local Space)
	FVector BonePosition;

	// Rotation (Euler Angles, Degrees)
	FVector BoneRotation;

	// Scale
	FVector BoneScale;

	// Transform이 수정되었는지 여부
	bool bIsTransformModified = false;

	USkeletalMeshEditorWindow* SkeletalMeshEditorWindow;
};
