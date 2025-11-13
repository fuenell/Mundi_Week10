#pragma once
#include "Widgets/Widget.h"

// RHI 리소스 전방 선언
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11DepthStencilView;

class ACameraActor;
class ASkeletalMeshActor;
class ADirectionalLightActor;
class AAmbientLightActor;
class AGridActor;
class UWorld;
class UBoneDebugComponent;
class UBoneGizmoProxyComponent;
class AGizmoActor;
class USkeletalMeshEditorWindow;
struct FBonePicking;

// 스켈레탈 메시 뷰어의 뷰포트 위젯입니다.
class USkeletalMeshViewportWidget : public UWidget
{
public:
	DECLARE_CLASS(USkeletalMeshViewportWidget, UWidget)

	USkeletalMeshViewportWidget();
	virtual ~USkeletalMeshViewportWidget();

	// 위젯 초기화 (리소스 생성)
	void Initialize() override;

	// 위젯 렌더링 (ImGui::Image 호출)
	void RenderWidget() override;

	// 위젯 업데이트 (입력 처리 등)
	void Update(float DeltaTime) override;

	/**
	 * 표시할 SkeletalMesh 설정
	 * @param InMesh - SkeletalMesh 객체
	 */
	void SetSkeletalMesh(class USkeletalMesh* InMesh);

	/**
	 * 현재 SkeletalMesh 가져오기
	 * @return SkeletalMesh 객체
	 */
	class USkeletalMesh* GetSkeletalMesh() const { return CurrentSkeletalMesh; }

	/**
	 * Bone 시각화 토글
	 * @param bVisible - true: 시각화 활성화, false: 비활성화
	 */
	void SetBoneVisualizationEnabled(bool bVisible);

	/**
	 * Bone 시각화 상태 가져오기
	 */
	bool IsBoneVisualizationEnabled() const;

	// 선택된 본 기즈모 표시
	void SetSelectedBone(int32 BoneIndex);

	void UpdateGizmo(int32 BoneIndex);
	
	void UpdateBone(int32 BoneIndex);

	void SetSkeletalMeshEditorWindow(USkeletalMeshEditorWindow* InSkeletalMeshEditorWindow) { SkeletalMeshEditorWindow = InSkeletalMeshEditorWindow; }

private:
	/** GlobalBindPoseMatrix를 재귀적으로 재계산 (BindPoseRelativeTransform 변경 시) */
	void RecalculateGlobalBindPoseMatrixRecursive(class USkeleton* Skeleton, int32 BoneIndex);
	/** RenderTarget 생성 (RTV, SRV, DSV) */
	bool CreateRenderTarget(int32 Width, int32 Height);

	/** PreviewWorld를 RTV에 렌더링하고 SRV로 변환 */
	bool RenderPreviewWorldToRTV(int32 Width, int32 Height);

	/** RTV/SRV/DSV 리소스를 해제합니다. */
	void ReleaseRenderTexture();

	/** 뷰포트 내의 ImGui 입력을 처리합니다. */
	void HandleViewportInput(FVector2D ViewportSize);

	/** 본 피킹 처리 */
	void HandleBonePicking(const FVector2D& ViewportSize, const FVector2D& LocalMousePos);

private:
	// --- Render-to-Texture 리소스 ---
	ID3D11RenderTargetView* SceneRTV = nullptr;
	ID3D11ShaderResourceView* SceneSRV = nullptr;
	ID3D11DepthStencilView* SceneDSV = nullptr;
	ID3D11RenderTargetView* DummyIdRTV = nullptr;  // IdBuffer 대체용 더미 (슬롯 1)
	// (픽킹용 ID 타겟도 필요)
	//FIntPoint TextureSize = { 0, 0 };ㅔㄴ스

	// --- 프리뷰 씬(Scene) ---
	UWorld* PreviewWorld = nullptr;
private:
	ACameraActor* PreviewCamera = nullptr;
	ASkeletalMeshActor* PreviewActor = nullptr;
	ADirectionalLightActor* PreviewLight = nullptr;
	AAmbientLightActor* PreviewAmbientLight = nullptr;
	AGridActor* PreviewGrid = nullptr;
	USceneComponent* BoneTransformComp = nullptr;

	// 현재 표시 중인 SkeletalMesh
	class USkeletalMesh* CurrentSkeletalMesh = nullptr;

	// 렌더링 최적화
	bool bNeedsRedraw = true;  // 다시 그려야 하는지 여부
	FVector2D LastViewportSize = FVector2D::Zero();  // 이전 뷰포트 크기

	// 카메라 회전 상태 (입력 처리용)
	float CameraYawDeg = 0.0f;
	float CameraPitchDeg = 0.0f;

	// --- Bone Visualization & Picking ---

	// Bone 디버그 컴포넌트 (본 시각화)
	UBoneDebugComponent* BoneDebugComponent = nullptr;

	// Bone Gizmo Proxy 컴포넌트 (피킹된 본에 기즈모 부착)
	UBoneGizmoProxyComponent* BoneGizmoProxyComponent = nullptr;

	// 현재 활성화된 기즈모
	AGizmoActor* CurrentGizmo = nullptr;

	// 피킹된 본 인덱스 (-1 if no bone)
	int32 PickedBoneIndex = -1;

	// Bone 시각화 활성화 여부
	bool bBoneVisualizationEnabled = true;

	bool bWasHovering = false;

	USkeletalMeshEditorWindow* SkeletalMeshEditorWindow;

	int32 CurrentBoneIndex = -1;
};