#include "pch.h"
#include "SkeletalMeshViewportWidget.h"
#include "ImGui/imgui.h"
#include "RenderManager.h"
#include "Renderer.h"
#include "RHIDevice.h"
#include "World.h"
#include "CameraActor.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"

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
    // TODO: 실제 3D 렌더링 구현
    // 현재는 임시로 텍스트 정보만 표시

    ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

    // 배경색 표시 (3D Viewport처럼 보이게)
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    ImVec2 p_min = ImGui::GetCursorScreenPos();
    ImVec2 p_max = ImVec2(p_min.x + ViewportSize.x, p_min.y + ViewportSize.y);
    DrawList->AddRectFilled(p_min, p_max, IM_COL32(45, 45, 48, 255));

    // SkeletalMesh 정보 표시
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 20);
    ImGui::Indent(20);

    if (CurrentSkeletalMesh != nullptr)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SkeletalMesh Loaded");
        ImGui::Spacing();

        // Skeleton 정보
        if (CurrentSkeletalMesh->GetSkeleton() != nullptr)
        {
            USkeleton* Skeleton = CurrentSkeletalMesh->GetSkeleton();
            ImGui::Text("Bone Count: %d", Skeleton->GetBoneCount());
            ImGui::Text("Vertex Count: %d", CurrentSkeletalMesh->GetVertexCount());
            ImGui::Text("Index Count: %d", CurrentSkeletalMesh->GetIndexCount());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "TODO: 3D Viewport 구현");
        ImGui::TextWrapped("현재는 임시로 텍스트 정보만 표시합니다.");
        ImGui::TextWrapped("3D 렌더링을 위해서는 PreviewWorld, Camera, RenderTarget 설정이 필요합니다.");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No SkeletalMesh");
        ImGui::Spacing();
        ImGui::TextWrapped("Property에서 SkeletalMesh를 선택하고 '뷰어' 버튼을 눌러주세요.");
    }

    ImGui::Unindent(20);
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

void USkeletalMeshViewportWidget::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    CurrentSkeletalMesh = InMesh;

    // TODO: PreviewActor에 SkeletalMesh 설정
    // if (PreviewActor && CurrentSkeletalMesh)
    // {
    //     PreviewActor->GetSkeletalMeshComponent()->SetSkeletalMesh(CurrentSkeletalMesh);
    // }
}

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