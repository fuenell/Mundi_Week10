#pragma once
#include "Widgets/Widget.h"

// RHI 리소스 전방 선언
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11DepthStencilView;

class ACameraActor;
class ASkeletalMeshActor;
class UWorld;

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
	void Update() override;

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

private:
	/** ImGui 영역 크기에 맞춰 렌더 타겟을 관리합니다. */
	//void UpdateRenderTexture(FIntPoint NewSize);

	/** RTV/SRV 리소스를 해제합니다. */
	void ReleaseRenderTexture();

	/** 뷰포트 내의 ImGui 입력을 처리합니다. */
	void HandleViewportInput(FVector2D ViewportSize);

	/** 프리뷰 씬을 텍스처에 렌더링합니다. */
	//void RenderPreviewScene(FIntPoint TargetSize);

private:
	// --- Render-to-Texture 리소스 ---
	ID3D11RenderTargetView* SceneRTV = nullptr;
	ID3D11ShaderResourceView* SceneSRV = nullptr;
	ID3D11DepthStencilView* SceneDSV = nullptr;
	// (픽킹용 ID 타겟도 필요)
	//FIntPoint TextureSize = { 0, 0 };

	// --- 프리뷰 씬(Scene) ---
	UWorld* PreviewWorld = nullptr;
	ACameraActor* PreviewCamera = nullptr;
	ASkeletalMeshActor* PreviewActor = nullptr;

	// 현재 표시 중인 SkeletalMesh
	class USkeletalMesh* CurrentSkeletalMesh = nullptr;
};