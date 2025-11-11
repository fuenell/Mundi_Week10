#include "pch.h"
#include "SkeletalMeshViewportWidget.h"
#include "ImGui/imgui.h"
#include "RenderManager.h"
#include "Renderer.h"
#include "D3D11RHI.h"
#include "World.h"
#include "CameraActor.h"
#include "SkeletalMesh.h"
#include "Skeleton.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "CameraComponent.h"
#include "SceneView.h"
#include "DirectionalLightActor.h"
#include "DirectionalLightComponent.h"
#include "AmbientLightActor.h"
#include "AmbientLightComponent.h"
#include "Grid/GridActor.h"
#include <d3d11.h>

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
}

USkeletalMeshViewportWidget::~USkeletalMeshViewportWidget()
{
    ReleaseRenderTexture();
    
    // PreviewWorld 정리
    if (PreviewWorld)
    {
        // PreviewWorld의 모든 액터는 World 소멸 시 자동 정리됨
        delete PreviewWorld;
        PreviewWorld = nullptr;
    }
    
    PreviewCamera = nullptr;
    PreviewActor = nullptr;
}

void USkeletalMeshViewportWidget::Initialize()
{
    UWidget::Initialize();

    UE_LOG("[SkeletalMeshViewport] Initialize() called");

    // === 완전히 새로운 PreviewWorld 생성 (PIE처럼) ===
    PreviewWorld = NewObject<UWorld>();
    if (PreviewWorld)
    {
        UE_LOG("[SkeletalMeshViewport] PreviewWorld created successfully");
        // Level만 생성 (Grid/Gizmo 없이)
        PreviewWorld->CreateLevel();
        PreviewWorld->bPie = false; // Preview 모드

        // === PreviewCamera 생성 ===
        PreviewCamera = PreviewWorld->SpawnActor<ACameraActor>();
        if (PreviewCamera)
        {
            UCameraComponent* CameraComp = PreviewCamera->GetCameraComponent();
            if (CameraComp)
            {
                // 카메라를 SkeletalMesh가 잘 보이는 위치에 배치
                // Y축 음수 방향에서 원점을 바라보도록 설정 (180도 회전)
                CameraComp->SetWorldLocation(FVector(5.0f, 0.0f, -5.0f));  // 뒤쪽 위

                // 뒤에서 아래로 보는 방향으로 카메라 회전
                FQuat CameraRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, -45.0f, 180.0f));
                CameraComp->SetWorldRotation(CameraRotation);

                CameraComp->SetFOV(90.0f);
            }
        }

        // === SkeletalMeshActor 생성 (나중에 SetSkeletalMesh에서 메시 설정) ===
        PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
        if (PreviewActor)
        {
            // 원점에 배치
            PreviewActor->SetActorLocation(FVector::Zero());
            PreviewActor->SetActorRotation(FQuat::Identity());
        }

        // === DirectionalLight 생성 (조명이 없으면 아무것도 안 보임) ===
        PreviewLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
        if (PreviewLight)
        {
            UDirectionalLightComponent* LightComp = PreviewLight->GetLightComponent();
            if (LightComp)
            {
                // 위에서 아래로 비추는 조명 (약간 앞쪽에서)
                PreviewLight->SetActorRotation(FQuat::MakeFromEulerZYX(FVector(45.0f, 0.0f, 0.0f)));
                LightComp->SetIntensity(1.0f);
                LightComp->SetLightColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        // === AmbientLight 생성 (전체적인 밝기 제공) ===
        PreviewAmbientLight = PreviewWorld->SpawnActor<AAmbientLightActor>();
        if (PreviewAmbientLight)
        {
            UAmbientLightComponent* AmbientComp = PreviewAmbientLight->GetLightComponent();
            if (AmbientComp)
            {
                AmbientComp->SetIntensity(0.3f);  // 약간의 앰비언트
                AmbientComp->SetLightColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        // === Grid 생성 (바닥 참조용) ===
        PreviewGrid = PreviewWorld->SpawnActor<AGridActor>();
        if (PreviewGrid)
        {
            PreviewGrid->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
        }
    }
}

void USkeletalMeshViewportWidget::Update()
{
    UWidget::Update();

    // PreviewWorld 틱 (애니메이션 등을 위해)
    if (PreviewWorld)
    {
        // TODO: PreviewWorld->Tick(DeltaTime); 필요 시 추가
    }
}

void USkeletalMeshViewportWidget::RenderWidget()
{
    ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

    if (ViewportSize.x < 32.0f || ViewportSize.y < 32.0f)
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Viewport too small");
        return;
    }

    int32 Width = static_cast<int32>(ViewportSize.x);
    int32 Height = static_cast<int32>(ViewportSize.y);

    // === STEP 1: RenderTarget 생성 (첫 호출 시) ===
    if (!SceneRTV || !SceneSRV || !SceneDSV)
    {
        if (!CreateRenderTarget(Width, Height))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to create RenderTarget");
            return;
        }
        bNeedsRedraw = true;
    }

    // 뷰포트 크기가 변경되었는지 확인
    FVector2D CurrentViewportSize(static_cast<float>(Width), static_cast<float>(Height));
    if (CurrentViewportSize != LastViewportSize)
    {
        // 크기가 변경되면 RenderTarget 재생성
        ReleaseRenderTexture();
        if (!CreateRenderTarget(Width, Height))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to recreate RenderTarget");
            return;
        }
        bNeedsRedraw = true;
        LastViewportSize = CurrentViewportSize;
    }

    // === STEP 2: PreviewWorld를 RTV에 렌더링 (변경이 있을 때만) ===
    if (bNeedsRedraw)
    {
        UE_LOG("[SkeletalMeshViewport] Rendering PreviewWorld (bNeedsRedraw=true)");
        if (!RenderPreviewWorldToRTV(Width, Height))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to render");
            UE_LOG("[SkeletalMeshViewport] RenderPreviewWorldToRTV() failed!");
            return;
        }
        bNeedsRedraw = false;  // 렌더링 완료
        UE_LOG("[SkeletalMeshViewport] Rendering complete");
    }

    // === STEP 3: SRV를 ImGui::Image로 표시 ===
    if (SceneSRV)
    {
        ImTextureID TextureID = (ImTextureID)SceneSRV;

        // D3D11 좌표계 → ImGui 좌표계 (Y축 반전)
        ImGui::Image(TextureID, ViewportSize,
            ImVec2(0, 1),  // uv_min
            ImVec2(1, 0)   // uv_max
        );

        HandleViewportInput(FVector2D(ViewportSize.x, ViewportSize.y));
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Initializing...");
    }
}

bool USkeletalMeshViewportWidget::CreateRenderTarget(int32 Width, int32 Height)
{
    ReleaseRenderTexture();

    URenderer* Renderer = URenderManager::GetInstance().GetRenderer();
    if (!Renderer) return false;

    D3D11RHI* RHI = Renderer->GetRHIDevice();
    if (!RHI) return false;

    ID3D11Device* Device = RHI->GetDevice();
    if (!Device) return false;

    // === Color Texture 생성 (RTV + SRV) ===
    ID3D11Texture2D* ColorTexture = nullptr;
    D3D11_TEXTURE2D_DESC ColorDesc = {};
    ColorDesc.Width = Width;
    ColorDesc.Height = Height;
    ColorDesc.MipLevels = 1;
    ColorDesc.ArraySize = 1;
    ColorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ColorDesc.SampleDesc.Count = 1;
    ColorDesc.Usage = D3D11_USAGE_DEFAULT;
    ColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&ColorDesc, nullptr, &ColorTexture)))
        return false;

    // RTV 생성
    if (FAILED(Device->CreateRenderTargetView(ColorTexture, nullptr, &SceneRTV)))
    {
        ColorTexture->Release();
        return false;
    }

    // SRV 생성
    if (FAILED(Device->CreateShaderResourceView(ColorTexture, nullptr, &SceneSRV)))
    {
        ColorTexture->Release();
        return false;
    }

    ColorTexture->Release(); // RTV, SRV가 참조 유지

    // === Dummy IdBuffer Texture 생성 (RTV) - Slot 1용 ===
    ID3D11Texture2D* DummyIdTexture = nullptr;
    D3D11_TEXTURE2D_DESC DummyIdDesc = ColorDesc;  // ColorTexture와 같은 크기
    DummyIdDesc.Format = DXGI_FORMAT_R32_UINT;  // ID는 uint
    DummyIdDesc.BindFlags = D3D11_BIND_RENDER_TARGET;  // SRV 불필요

    if (FAILED(Device->CreateTexture2D(&DummyIdDesc, nullptr, &DummyIdTexture)))
        return false;

    // Dummy IdBuffer RTV 생성
    if (FAILED(Device->CreateRenderTargetView(DummyIdTexture, nullptr, &DummyIdRTV)))
    {
        DummyIdTexture->Release();
        return false;
    }

    DummyIdTexture->Release(); // RTV가 참조 유지

    // === Depth Texture 생성 (DSV) ===
    ID3D11Texture2D* DepthTexture = nullptr;
    D3D11_TEXTURE2D_DESC DepthDesc = {};
    DepthDesc.Width = Width;
    DepthDesc.Height = Height;
    DepthDesc.MipLevels = 1;
    DepthDesc.ArraySize = 1;
    DepthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DepthDesc.SampleDesc.Count = 1;
    DepthDesc.Usage = D3D11_USAGE_DEFAULT;
    DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(Device->CreateTexture2D(&DepthDesc, nullptr, &DepthTexture)))
        return false;

    // DSV 생성
    if (FAILED(Device->CreateDepthStencilView(DepthTexture, nullptr, &SceneDSV)))
    {
        DepthTexture->Release();
        return false;
    }

    DepthTexture->Release(); // DSV가 참조 유지

    return true;
}

bool USkeletalMeshViewportWidget::RenderPreviewWorldToRTV(int32 Width, int32 Height)
{
    if (!PreviewWorld || !PreviewCamera || !SceneRTV || !SceneDSV)
        return false;

    URenderer* Renderer = URenderManager::GetInstance().GetRenderer();
    if (!Renderer) return false;

    D3D11RHI* RHI = Renderer->GetRHIDevice();
    ID3D11DeviceContext* Context = RHI->GetDeviceContext();

    // === 현재 RenderTarget 상태 백업 ===
    ID3D11RenderTargetView* OldRTV = nullptr;
    ID3D11DepthStencilView* OldDSV = nullptr;
    Context->OMGetRenderTargets(1, &OldRTV, &OldDSV);

    UINT NumViewports = 1;
    D3D11_VIEWPORT OldViewport = {};
    Context->RSGetViewports(&NumViewports, &OldViewport);

    // === 우리의 RTV/DSV로 전환 ===
    const float ClearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    Context->ClearRenderTargetView(SceneRTV, ClearColor);
    Context->ClearDepthStencilView(SceneDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // MRT (Multiple Render Targets): slot 0에 SceneRTV, slot 1에 DummyIdRTV
    ID3D11RenderTargetView* RTVs[2] = { SceneRTV, DummyIdRTV };
    Context->OMSetRenderTargets(2, RTVs, SceneDSV);

    // 디버그: RTV가 제대로 설정되었는지 확인
    ID3D11RenderTargetView* CheckRTV = nullptr;
    ID3D11DepthStencilView* CheckDSV = nullptr;
    Context->OMGetRenderTargets(1, &CheckRTV, &CheckDSV);
    UE_LOG("[SkeletalMeshViewport] After OMSetRenderTargets: RTV=%p, DSV=%p", CheckRTV, CheckDSV);
    if (CheckRTV) CheckRTV->Release();
    if (CheckDSV) CheckDSV->Release();

    D3D11_VIEWPORT Viewport = {};
    Viewport.Width = static_cast<float>(Width);
    Viewport.Height = static_cast<float>(Height);
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &Viewport);

    UE_LOG("[SkeletalMeshViewport] About to call RenderSceneForView");

    // [DEBUG] PreviewWorld 상태 체크
    if (PreviewWorld)
    {
        int32 ActorCount = PreviewWorld->GetActors().Num();
        UE_LOG("[SkeletalMeshViewport] PreviewWorld has %d actors", ActorCount);

        if (PreviewGrid)
            UE_LOG("[SkeletalMeshViewport] Grid location: (%.1f, %.1f, %.1f)",
                PreviewGrid->GetActorLocation().X, PreviewGrid->GetActorLocation().Y, PreviewGrid->GetActorLocation().Z);

        if (PreviewActor)
        {
            UE_LOG("[SkeletalMeshViewport] SkeletalMeshActor location: (%.1f, %.1f, %.1f)",
                PreviewActor->GetActorLocation().X, PreviewActor->GetActorLocation().Y, PreviewActor->GetActorLocation().Z);

            USkeletalMeshComponent* SkelComp = PreviewActor->GetSkeletalMeshComponent();
            if (SkelComp && SkelComp->GetSkeletalMesh())
            {
                UE_LOG("[SkeletalMeshViewport] SkeletalMesh assigned: %s", SkelComp->GetSkeletalMesh()->GetName().c_str());
            }
            else
            {
                UE_LOG("[SkeletalMeshViewport] WARNING: No SkeletalMesh assigned!");
            }
        }
    }

    // === PreviewWorld를 렌더링 ===
    UCameraComponent* CameraComp = PreviewCamera->GetCameraComponent();
    if (CameraComp)
    {
        FVector CamLoc = CameraComp->GetWorldLocation();
        FQuat CamRot = CameraComp->GetWorldRotation();
        UE_LOG("[SkeletalMeshViewport] Camera location: (%.1f, %.1f, %.1f), FOV: %.1f",
            CamLoc.X, CamLoc.Y, CamLoc.Z, CameraComp->GetFOV());

        FMinimalViewInfo ViewInfo;
        ViewInfo.ViewLocation = CamLoc;
        ViewInfo.ViewRotation = CamRot;
        ViewInfo.FieldOfView = CameraComp->GetFOV();
        ViewInfo.ZoomFactor = CameraComp->GetZoomFactor();
        ViewInfo.NearClip = CameraComp->GetNearClip();
        ViewInfo.FarClip = CameraComp->GetFarClip();
        ViewInfo.ProjectionMode = CameraComp->GetProjectionMode();
        ViewInfo.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
        ViewInfo.ViewRect.MinX = 0;
        ViewInfo.ViewRect.MinY = 0;
        ViewInfo.ViewRect.MaxX = Width;
        ViewInfo.ViewRect.MaxY = Height;

        FSceneView SceneView(&ViewInfo, &PreviewWorld->GetRenderSettings());

        // External RenderTarget 사용 설정 (SceneRenderer가 RTV를 전환하지 않도록)
        SceneView.bUseExternalRenderTarget = true;

        // **핵심**: PreviewWorld만 렌더링 (메인 World가 아님!)
        Renderer->RenderSceneForView(PreviewWorld, &SceneView, nullptr);
    }

    // === 원래 RenderTarget으로 복원 ===
    Context->OMSetRenderTargets(1, &OldRTV, OldDSV);
    Context->RSSetViewports(1, &OldViewport);

    if (OldRTV) OldRTV->Release();
    if (OldDSV) OldDSV->Release();

    return true;
}

void USkeletalMeshViewportWidget::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    CurrentSkeletalMesh = InMesh;

    if (PreviewActor && CurrentSkeletalMesh)
    {
        USkeletalMeshComponent* SkelMeshComp = PreviewActor->GetSkeletalMeshComponent();
        if (SkelMeshComp)
        {
            SkelMeshComp->SetSkeletalMesh(CurrentSkeletalMesh);
            bNeedsRedraw = true;  // 메시가 변경되었으므로 다시 렌더링
        }
    }
}

void USkeletalMeshViewportWidget::HandleViewportInput(FVector2D ViewportSize)
{
    if (!ImGui::IsItemHovered() || !PreviewCamera)
        return;

    ImGuiIO& io = ImGui::GetIO();
    UCameraComponent* CameraComp = PreviewCamera->GetCameraComponent();
    if (!CameraComp) return;

    bool bInputChanged = false;

    // === 마우스 휠: 줌 ===
    if (io.MouseWheel != 0.0f)
    {
        FVector Location = CameraComp->GetWorldLocation();
        FVector Forward = CameraComp->GetForward();
        float ZoomDelta = io.MouseWheel * 20.0f;
        CameraComp->SetWorldLocation(Location + Forward * ZoomDelta);
        bInputChanged = true;
    }

    // === 우클릭: 회전 ===
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        ImVec2 MouseDelta = io.MouseDelta;
        if (FMath::Abs(MouseDelta.x) > 0.1f || FMath::Abs(MouseDelta.y) > 0.1f)
        {
            float DeltaYaw = MouseDelta.x * 0.3f;
            float DeltaPitch = -MouseDelta.y * 0.3f;

            FQuat CurrentRotation = CameraComp->GetWorldRotation();
            FQuat YawRotation = FQuat::MakeFromEulerZYX(FVector(0.0f, 0.0f, DeltaYaw));
            FVector RightVector = CameraComp->GetRight();
            FQuat PitchRotation = FQuat::FromAxisAngle(RightVector, DeltaPitch * (3.14159f / 180.0f));

            CameraComp->SetWorldRotation(YawRotation * PitchRotation * CurrentRotation);
            bInputChanged = true;
        }
    }

    // === 중클릭: 팬 ===
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        ImVec2 MouseDelta = io.MouseDelta;
        if (FMath::Abs(MouseDelta.x) > 0.1f || FMath::Abs(MouseDelta.y) > 0.1f)
        {
            FVector Right = CameraComp->GetRight();
            FVector Up = CameraComp->GetUp();
            FVector Location = CameraComp->GetWorldLocation();
            FVector PanOffset = Right * (-MouseDelta.x * 0.5f) + Up * (MouseDelta.y * 0.5f);
            CameraComp->SetWorldLocation(Location + PanOffset);
            bInputChanged = true;
        }
    }

    // 입력이 있으면 다시 렌더링
    if (bInputChanged)
    {
        bNeedsRedraw = true;
    }
}

void USkeletalMeshViewportWidget::ReleaseRenderTexture()
{
    if (SceneRTV) { SceneRTV->Release(); SceneRTV = nullptr; }
    if (SceneSRV) { SceneSRV->Release(); SceneSRV = nullptr; }
    if (SceneDSV) { SceneDSV->Release(); SceneDSV = nullptr; }
    if (DummyIdRTV) { DummyIdRTV->Release(); DummyIdRTV = nullptr; }
}
