#include "pch.h"
#include "SkeletalMeshViewportWidget.h"
#include "ImGui/imgui.h"
#include "RenderManager.h"
#include "Renderer.h"
#include "RHIDevice.h"
#include "World.h"
#include "CameraActor.h"
// ... (기타 필요한 헤더)

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
    // Initialize()에서 리소스 생성
}

USkeletalMeshViewportWidget::~USkeletalMeshViewportWidget()
{
    //Cleanup(); // 소멸 시 리소스 해제
}

void USkeletalMeshViewportWidget::Initialize()
{
    UWidget::Initialize();

    // TODO: GEngine->GetPreviewWorld() 등에서 프리뷰 월드 가져오기
    // PreviewWorld = ...
    // PreviewCamera = PreviewWorld->SpawnActor<ACameraActor>();
    // PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
}

//void USkeletalMeshViewportWidget::Cleanup()
//{
//    ReleaseRenderTexture();
//    // TODO: PreviewWorld에서 PreviewActor와 PreviewCamera 파괴
//}

void USkeletalMeshViewportWidget::Update()
{
    UWidget::Update();
    // (Update 로직이 필요하다면 이곳에)
}

void USkeletalMeshViewportWidget::RenderWidget()
{
    //// 1. 뷰포트로 사용할 영역의 크기를 가져옴 (창 크기 100%)
    //ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
    //FIntPoint NewTextureSize = { (int)ViewportSize.x, (int)ViewportSize.y };

    //// 2. 텍스처(RTV/SRV) 크기 갱신
    //UpdateRenderTexture(NewTextureSize);

    //// 3. 프리뷰 씬 렌더링 (RTV에 그리기)
    //RenderPreviewScene(NewTextureSize);

    //// 4. 렌더링된 텍스처를 ImGui 창에 이미지로 표시
    //if (SceneSRV)
    //{
    //    // Y축 뒤집힘 문제 해결 (UV (0,1) -> (1,0))
    //    ImGui::Image((void*)SceneSRV, ViewportSize, ImVec2(0, 1), ImVec2(1, 0));
    //}

    //// 5. ImGui 입력 처리 (카메라 이동, 픽킹)
    //HandleViewportInput(FVector2D(ViewportSize.x, ViewportSize.y));
}

// --- (이하 구현은 이전 답변과 동일) ---

//void USkeletalMeshViewportWidget::RenderPreviewScene(FIntPoint TargetSize)
//{
//    if (TargetSize.X <= 0 || TargetSize.Y <= 0) return;
//    if (!SceneRTV || !PreviewCamera || !PreviewWorld) return;
//
//    URenderer* Renderer = URenderManager::GetInstance().GetRenderer();
//    if (Renderer)
//    {
//        FSceneView PreviewView(PreviewCamera->GetCameraComponent(), TargetSize.X, TargetSize.Y, &PreviewWorld->GetRenderSettings());
//
//        // TODO: URenderer에 RenderSceneToTarget 구현
//        // Renderer->RenderSceneToTarget(
//        //     PreviewWorld, 
//        //     &PreviewView, 
//        //     SceneRTV, 
//        //     SceneDSV
//        // ); 
//    }
//}

void USkeletalMeshViewportWidget::HandleViewportInput(FVector2D ViewportSize)
{
    // ImGui::IsItemHovered()는 ImGui::Image() 바로 뒤에 호출되어야 합니다.
    if (ImGui::IsItemHovered())
    {
        ImGuiIO& io = ImGui::GetIO();
        // TODO: io.MouseWheel로 카메라 줌
        // TODO: io.MouseDown[0] (좌클릭)으로 픽킹
        // TODO: io.MouseDown[1] (우클릭) + io.MouseDelta로 카메라 회전
    }
}

//void USkeletalMeshViewportWidget::UpdateRenderTexture(FIntPoint NewSize)
//{
//    if (NewSize.X <= 0 || NewSize.Y <= 0) return;
//    if (TextureSize.X == NewSize.X && TextureSize.Y == NewSize.Y) return;
//
//    ReleaseRenderTexture();
//
//    // TODO: GEngine->GetRHIDevice()를 통해 RTV, SRV, DSV 생성
//    // ...
//
//    TextureSize = NewSize;
//}
//
//void USkeletalMeshViewportWidget::ReleaseRenderTexture()
//{
//    SAFE_RELEASE(SceneRTV);
//    SAFE_RELEASE(SceneSRV);
//    SAFE_RELEASE(SceneDSV);
//    TextureSize = { 0, 0 };
//}