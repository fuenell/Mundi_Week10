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
#include "ObjectFactory.h"
#include "StaticMeshActor.h"
#include "BoneDebugComponent.h"
#include "BoneGizmoProxyComponent.h"
#include "Gizmo/GizmoActor.h"
#include "Picking.h"
#include <d3d11.h>

IMPLEMENT_CLASS(USkeletalMeshViewportWidget)

USkeletalMeshViewportWidget::USkeletalMeshViewportWidget()
{
}

USkeletalMeshViewportWidget::~USkeletalMeshViewportWidget()
{
    ReleaseRenderTexture();

    // PreviewWorld 정리 (ObjectFactory::DeleteObject 사용)
    if (PreviewWorld)
    {
        UE_LOG("[SkeletalMeshViewport] Destroying PreviewWorld via ObjectFactory::DeleteObject");
        ObjectFactory::DeleteObject(PreviewWorld);
        PreviewWorld = nullptr;
    }

    PreviewCamera = nullptr;
    PreviewActor = nullptr;
}

void USkeletalMeshViewportWidget::Initialize()
{
    UWidget::Initialize();

    UE_LOG("[SkeletalMeshViewport] Initialize() called");

    // 이미 초기화되었으면 리턴
    if (PreviewWorld)
    {
        UE_LOG("[SkeletalMeshViewport] WARNING: Already initialized! Skipping...");
        return;
    }

    // === 완전히 새로운 PreviewWorld 생성 (PIE처럼) ===
    PreviewWorld = NewObject<UWorld>();
    if (PreviewWorld)
    {
        PreviewWorld->Initialize();
        UE_LOG("[SkeletalMeshViewport] PreviewWorld created: World=%p, LightManager=%p",
            PreviewWorld, PreviewWorld->GetLightManager());
        UE_LOG("[SkeletalMeshViewport] BEFORE spawning lights - DirLights: %d, AmbientLights: %d",
            PreviewWorld->GetLightManager()->GetDirectionalLightList().Num(),
            PreviewWorld->GetLightManager()->GetAmbientLightList().Num());

        // 빈 레벨 생성 (기본 라이트 없이)
        PreviewWorld->SetLevel(ULevelService::CreateDefaultLevel());
        PreviewWorld->bPie = false; // Tick을 실행하지 않기 위해 false

        // PreviewWorld의 LightManager를 초기화 (다른 World의 라이트가 섞이지 않도록)
        PreviewWorld->GetLightManager()->ClearAllLightList();

        // === RenderSettings에서 Billboard ShowFlag 끄고 Grid ShowFlag 켜기 ===
        PreviewWorld->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Billboard);
        PreviewWorld->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Grid);

        // 디버그: Unlit 모드로 시작 (라이팅 없이 텍스처만 표시)
        // PreviewWorld->GetRenderSettings().SetViewMode(EViewMode::VMI_Unlit);

        // === PreviewCamera 생성 ===
        PreviewCamera = PreviewWorld->SpawnActor<ACameraActor>();
        if (PreviewCamera)
        {
            UCameraComponent* CameraComp = PreviewCamera->GetCameraComponent();
            if (CameraComp)
            {
                // 카메라를 SkeletalMesh가 잘 보이는 위치에 배치
                CameraComp->SetWorldLocation(FVector(5.0f, 0.0f, 2.0f));  // 뒤쪽 위

                // 초기 카메라 회전 설정
                CameraPitchDeg = 0.0f;     // Initialize에서 설정한 Pitch
                CameraYawDeg = 180.0f;      // Initialize에서 설정한 Yaw

                // Pitch/Yaw로 쿼터니언 생성 (HandleViewportInput과 동일한 방식)
                float RadPitch = DegreesToRadians(CameraPitchDeg);
                float RadYaw = DegreesToRadians(CameraYawDeg);
                FQuat YawQuat = FQuat::FromAxisAngle(FVector(0, 0, 1), RadYaw);
                FQuat PitchQuat = FQuat::FromAxisAngle(FVector(0, 1, 0), RadPitch);
                FQuat RollQuat = FQuat::FromAxisAngle(FVector(1, 0, 0), 0);
                FQuat CameraRotation = YawQuat * PitchQuat * RollQuat;
                CameraRotation.Normalize();

                CameraComp->SetWorldRotation(CameraRotation);
                CameraComp->SetFOV(90.0f);
            }
        }

        // === SkeletalMeshActor 생성 (나중에 SetSkeletalMesh에서 메시 설정) ===
        PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
        if (PreviewActor)
        {
            PreviewActor->SetActorLocation(FVector::Zero());
        }

        // === DirectionalLight 생성 (조명이 없으면 아무것도 안 보임) ===
        PreviewLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
        if (PreviewLight)
        {
            UDirectionalLightComponent* LightComp = PreviewLight->GetLightComponent();
            if (LightComp)
            {
                // 위에서 아래로 비추는 조명 (약간 앞쪽에서)
                PreviewLight->SetActorRotation(FQuat::MakeFromEulerZYX(FVector(0.0f, 90.0f, 0.0f)));
                LightComp->SetIntensity(1.0f);  // 강도 증가 (1.0)
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
                AmbientComp->SetIntensity(0.3f);  // 강도 증가 (0.3)
                AmbientComp->SetLightColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
            }
        }

        // === 초기화 완료 후 라이트 카운트 확인 ===
        UE_LOG("[SkeletalMeshViewport] PreviewWorld=%p, LightManager=%p",
            PreviewWorld, PreviewWorld->GetLightManager());
        UE_LOG("[SkeletalMeshViewport] AFTER spawning all actors - DirLights: %d, AmbientLights: %d",
            PreviewWorld->GetLightManager()->GetDirectionalLightList().Num(),
            PreviewWorld->GetLightManager()->GetAmbientLightList().Num());

        // 등록된 라이트의 정보 출력
        if (PreviewWorld->GetLightManager()->GetAmbientLightList().Num() > 0)
        {
            UAmbientLightComponent* Light = PreviewWorld->GetLightManager()->GetAmbientLightList()[0];
            UE_LOG("[SkeletalMeshViewport] AmbientLight[0] - Component=%p, GetWorld()=%p, Intensity=%.2f",
                Light, Light->GetWorld(), Light->GetIntensity());
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
        //UE_LOG("[SkeletalMeshViewport] Rendering PreviewWorld (bNeedsRedraw=true)");
        if (!RenderPreviewWorldToRTV(Width, Height))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Failed to render");
            UE_LOG("[SkeletalMeshViewport] RenderPreviewWorldToRTV() failed!");
            return;
        }
        bNeedsRedraw = false;  // 렌더링 완료
        //UE_LOG("[SkeletalMeshViewport] Rendering complete");
    }

    // === STEP 3: SRV를 ImGui::Image로 표시 ===
    if (SceneSRV)
    {
        ImTextureID TextureID = (ImTextureID)SceneSRV;

        // D3D11 좌표계와 ImGui 좌표계 모두 (0,0)이 Top-Left이므로 반전 불필요
        ImGui::Image(TextureID, ViewportSize,
            ImVec2(0, 0),  // uv_min
            ImVec2(1, 1)   // uv_max
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
    //UE_LOG("[SkeletalMeshViewport] After OMSetRenderTargets: RTV=%p, DSV=%p", CheckRTV, CheckDSV);
    if (CheckRTV) CheckRTV->Release();
    if (CheckDSV) CheckDSV->Release();

    D3D11_VIEWPORT Viewport = {};
    Viewport.Width = static_cast<float>(Width);
    Viewport.Height = static_cast<float>(Height);
    Viewport.MinDepth = 0.0f;
    Viewport.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &Viewport);

    //UE_LOG("[SkeletalMeshViewport] About to call RenderSceneForView");

    // [DEBUG] PreviewWorld 상태 체크
    if (PreviewWorld)
    {
        //int32 ActorCount = PreviewWorld->GetActors().Num();
        //UE_LOG("[SkeletalMeshViewport] PreviewWorld has %d actors", ActorCount);

        // 라이트 체크
        //if (PreviewLight)
        //{
        //    UDirectionalLightComponent* LightComp = PreviewLight->GetLightComponent();
        //    if (LightComp)
        //    {
        //        UE_LOG("[SkeletalMeshViewport] DirectionalLight - Intensity: %.2f, Registered: %d",
        //            LightComp->GetIntensity(), LightComp->IsRegistered());
        //    }
        //}

        //if (PreviewAmbientLight)
        //{
        //    UAmbientLightComponent* AmbientComp = PreviewAmbientLight->GetLightComponent();
        //    if (AmbientComp)
        //    {
        //        UE_LOG("[SkeletalMeshViewport] AmbientLight - Intensity: %.2f, Registered: %d",
        //            AmbientComp->GetIntensity(), AmbientComp->IsRegistered());
        //    }
        //}

        //if (PreviewGrid)
        //{
        //    UE_LOG("[SkeletalMeshViewport] Grid location: (%.1f, %.1f, %.1f)",
        //        PreviewGrid->GetActorLocation().X, PreviewGrid->GetActorLocation().Y, PreviewGrid->GetActorLocation().Z);

        //    ULineComponent* LineComp = PreviewGrid->GetLineComponent();
        //    if (LineComp)
        //    {
        //        UE_LOG("[SkeletalMeshViewport] Grid LineComponent - Registered: %d, LineCount: %d",
        //            LineComp->IsRegistered(), LineComp->GetLines().Num());
        //    }
        //}

        //if (PreviewActor)
        //{
        //    UE_LOG("[SkeletalMeshViewport] SkeletalMeshActor location: (%.1f, %.1f, %.1f)",
        //        PreviewActor->GetActorLocation().X, PreviewActor->GetActorLocation().Y, PreviewActor->GetActorLocation().Z);

        //    USkeletalMeshComponent* SkelComp = PreviewActor->GetSkeletalMeshComponent();
        //    if (SkelComp && SkelComp->GetSkeletalMesh())
        //    {
        //        UE_LOG("[SkeletalMeshViewport] SkeletalMesh assigned: %s, Registered: %d",
        //            SkelComp->GetSkeletalMesh()->GetName().c_str(), SkelComp->IsRegistered());
        //    }
        //    else
        //    {
        //        UE_LOG("[SkeletalMeshViewport] WARNING: No SkeletalMesh assigned!");
        //    }
        //}
    }

    // === PreviewWorld를 렌더링 ===
    UCameraComponent* CameraComp = PreviewCamera->GetCameraComponent();
    if (CameraComp)
    {
        FVector CamLoc = CameraComp->GetWorldLocation();
        FQuat CamRot = CameraComp->GetWorldRotation();
        //UE_LOG("[SkeletalMeshViewport] Camera location: (%.1f, %.1f, %.1f), FOV: %.1f",
        //    CamLoc.X, CamLoc.Y, CamLoc.Z, CameraComp->GetFOV());

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
        // 렌더링 직전에 PreviewWorld의 LightManager를 강제로 업데이트 (메인 World가 덮어쓴 LightBuffer 복구)
        PreviewWorld->GetLightManager()->SetDirtyFlag();

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
    const float MouseSensitivity = 0.25f;  // CameraActor와 동일
    const float CameraMoveSpeed = 0.05f;    // 이동 속도
    
    // === 우클릭: 회전 + WASD 이동 ===
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        // 마우스 회전 (CameraActor의 ProcessCameraRotation과 동일한 방식)
        ImVec2 MouseDelta = io.MouseDelta;
        if (FMath::Abs(MouseDelta.x) > 0.1f || FMath::Abs(MouseDelta.y) > 0.1f)
        {
            // Yaw/Pitch 누적 (멤버 변수 사용)
            CameraYawDeg += MouseDelta.x * MouseSensitivity;
            CameraPitchDeg += MouseDelta.y * MouseSensitivity;

            // Pitch 제한 (짐벌락 방지)
            CameraPitchDeg = std::clamp(CameraPitchDeg, -89.0f, 89.0f);

            // 축별 쿼터니언 생성 (축을 반대로 시도)
            float RadYaw = DegreesToRadians(CameraYawDeg);
            float RadPitch = DegreesToRadians(CameraPitchDeg);

            FQuat YawQuat = FQuat::FromAxisAngle(FVector(0, 0, 1), RadYaw);      // Z축 회전
            FQuat PitchQuat = FQuat::FromAxisAngle(FVector(0, 1, 0), RadPitch);  // Y축 회전
            FQuat RollQuat = FQuat::FromAxisAngle(FVector(1, 0, 0), 0); // X축 회전 (180도 뒤집기)

            // Yaw * Pitch 순서로 합성
            FQuat FinalRotation = YawQuat * PitchQuat * RollQuat;
            FinalRotation.Normalize();

            CameraComp->SetWorldRotation(FinalRotation);

            // 카메라 회전 각도 출력
            // UE_LOG("[SkeletalMeshViewport] Camera Rotation - Pitch: %.2f, Yaw: %.2f", CameraPitchDeg, CameraYawDeg);

            bInputChanged = true;
        }

        // WASD 이동 (CameraActor의 ProcessCameraMovement와 동일한 방식)
        FVector Move = FVector::Zero();

        // DirectX LH 기준: Right=+Y, Up=+Z, Forward=+X
        const FQuat Quat = CameraComp->GetWorldRotation();
        const FVector Right = Quat.RotateVector(FVector(0, 1, 0)).GetNormalized();
        const FVector Up = Quat.RotateVector(FVector(0, 0, 1)).GetNormalized();
        const FVector Forward = Quat.RotateVector(FVector(1, 0, 0)).GetNormalized();

        if (ImGui::IsKeyDown(ImGuiKey_W)) Move += Forward;  // 전진
        if (ImGui::IsKeyDown(ImGuiKey_S)) Move -= Forward;  // 후진
        if (ImGui::IsKeyDown(ImGuiKey_D)) Move += Right;    // 오른쪽
        if (ImGui::IsKeyDown(ImGuiKey_A)) Move -= Right;    // 왼쪽
        if (ImGui::IsKeyDown(ImGuiKey_E)) Move += Up;       // 위
        if (ImGui::IsKeyDown(ImGuiKey_Q)) Move -= Up;       // 아래

        // 이동 적용
        if (Move.SizeSquared() > 0.0f)
        {
            Move = Move.GetNormalized() * CameraMoveSpeed;
            FVector Location = CameraComp->GetWorldLocation();
            FVector NewLocation = Location + Move;
            CameraComp->SetWorldLocation(NewLocation);

            //UE_LOG("[SkeletalMeshViewport] Camera Move - Location: (%.2f, %.2f, %.2f)",
            //    NewLocation.X, NewLocation.Y, NewLocation.Z);

            bInputChanged = true;
        }
    }
    
    // === 좌클릭: 본 피킹 (본 시각화가 활성화된 경우만) ===
    if (bBoneVisualizationEnabled && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Get local mouse position within viewport
        ImVec2 MousePos = ImGui::GetMousePos();
        ImVec2 ViewportMin = ImGui::GetItemRectMin();
        FVector2D LocalMousePos(MousePos.x - ViewportMin.x, MousePos.y - ViewportMin.y);

        // Perform bone picking
        HandleBonePicking(ViewportSize, LocalMousePos);
        bInputChanged = true;
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

// ========================================
// Bone Visualization & Picking
// ========================================

void USkeletalMeshViewportWidget::SetBoneVisualizationEnabled(bool bVisible)
{
    bBoneVisualizationEnabled = bVisible;

    UE_LOG("[SkeletalMeshViewport] SetBoneVisualizationEnabled called: bVisible=%d", bVisible);

    if (!PreviewActor)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: PreviewActor is null!");
        return;
    }

    USkeletalMeshComponent* SkelMeshComp = PreviewActor->GetSkeletalMeshComponent();
    if (!SkelMeshComp)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: SkeletalMeshComponent is null!");
        return;
    }

    USkeletalMesh* SkeletalMesh = SkelMeshComp->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: SkeletalMesh is null!");
        return;
    }

    USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    if (!Skeleton)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: Skeleton is null!");
        return;
    }

    UE_LOG("[SkeletalMeshViewport] SkeletalMesh has %d bones", Skeleton->GetBoneCount());

    // BoneDebugComponent가 없으면 생성
    if (!BoneDebugComponent && bVisible)
    {
        UE_LOG("[SkeletalMeshViewport] Creating BoneDebugComponent...");
        BoneDebugComponent = NewObject<UBoneDebugComponent>();
        if (BoneDebugComponent)
        {
            BoneDebugComponent->SetupAttachment(SkelMeshComp);
            BoneDebugComponent->SetSkeletalMeshComponent(SkelMeshComp);
            BoneDebugComponent->RegisterComponent(PreviewWorld);  // RegisterComponent를 호출해야 bRegistered=true가 됨
            UE_LOG("[SkeletalMeshViewport] BoneDebugComponent created and attached successfully");
            UE_LOG("[SkeletalMeshViewport] BoneDebugComponent registered: %d", BoneDebugComponent->IsRegistered());
        }
        else
        {
            UE_LOG("[SkeletalMeshViewport] ERROR: Failed to create BoneDebugComponent!");
        }
    }

    // 시각화 활성화/비활성화
    if (BoneDebugComponent)
    {
        BoneDebugComponent->SetBonesVisible(bVisible);
        BoneDebugComponent->SetJointsVisible(bVisible);
        UE_LOG("[SkeletalMeshViewport] BoneDebugComponent visibility set: Bones=%d, Joints=%d",
            BoneDebugComponent->AreBonesVisible(), BoneDebugComponent->AreJointsVisible());
    }

    bNeedsRedraw = true;
}

bool USkeletalMeshViewportWidget::IsBoneVisualizationEnabled() const
{
    return bBoneVisualizationEnabled;
}

void USkeletalMeshViewportWidget::HandleBonePicking(const FVector2D& ViewportSize, const FVector2D& LocalMousePos)
{
    UE_LOG("[SkeletalMeshViewport] HandleBonePicking: MousePos=(%.2f, %.2f), ViewportSize=(%.2f, %.2f)",
        LocalMousePos.X, LocalMousePos.Y, ViewportSize.X, ViewportSize.Y);

    if (!PreviewActor || !PreviewCamera)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: PreviewActor or PreviewCamera is null!");
        return;
    }

    USkeletalMeshComponent* SkelMeshComp = PreviewActor->GetSkeletalMeshComponent();
    if (!SkelMeshComp)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: SkeletalMeshComponent is null!");
        return;
    }

    // Generate ray from mouse position
    const FMatrix View = PreviewCamera->GetViewMatrix();
    const FMatrix Proj = PreviewCamera->GetProjectionMatrix();
    const FVector CameraWorldPos = PreviewCamera->GetActorLocation();
    const FVector CameraRight = PreviewCamera->GetRight();
    const FVector CameraUp = PreviewCamera->GetUp();
    const FVector CameraForward = PreviewCamera->GetForward();

    FRay Ray = MakeRayFromViewport(View, Proj, CameraWorldPos, CameraRight, CameraUp, CameraForward,
                                   LocalMousePos, ViewportSize);

    UE_LOG("[SkeletalMeshViewport] Ray: Origin=(%.2f, %.2f, %.2f), Direction=(%.2f, %.2f, %.2f)",
        Ray.Origin.X, Ray.Origin.Y, Ray.Origin.Z,
        Ray.Direction.X, Ray.Direction.Y, Ray.Direction.Z);

    // Perform bone picking
    FBonePicking PickingResult = CPickingSystem::PerformBonePicking(SkelMeshComp, Ray);

    if (PickingResult.IsValid())
    {
        UE_LOG("[SkeletalMeshViewport] Bone picked: Index=%d, Type=%d, Distance=%.2f",
            PickingResult.BoneIndex,
            static_cast<int32>(PickingResult.PickingType),
            PickingResult.Distance);

        // Create gizmo for picked bone
        CreateGizmoForBone(PickingResult);
    }
    else
    {
        UE_LOG("[SkeletalMeshViewport] No bone picked");

        // Destroy current gizmo
        DestroyCurrentGizmo();
    }
}

void USkeletalMeshViewportWidget::CreateGizmoForBone(const FBonePicking& PickingResult)
{
    UE_LOG("[SkeletalMeshViewport] CreateGizmoForBone: BoneIndex=%d", PickingResult.BoneIndex);

    if (!PreviewWorld || !PreviewActor)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: PreviewWorld or PreviewActor is null!");
        return;
    }

    USkeletalMeshComponent* SkelMeshComp = PreviewActor->GetSkeletalMeshComponent();
    if (!SkelMeshComp)
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: SkeletalMeshComponent is null!");
        return;
    }

    // Destroy existing gizmo
    DestroyCurrentGizmo();

    // Create BoneGizmoProxyComponent
    UE_LOG("[SkeletalMeshViewport] Creating BoneGizmoProxyComponent...");
    BoneGizmoProxyComponent = NewObject<UBoneGizmoProxyComponent>();

    if (BoneGizmoProxyComponent)
    {
        BoneGizmoProxyComponent->SetupAttachment(PreviewActor->GetRootComponent());
        BoneGizmoProxyComponent->SetTargetBone(SkelMeshComp, PickingResult.BoneIndex);
        BoneGizmoProxyComponent->RegisterComponent(PreviewWorld);  // RegisterComponent 호출
        UE_LOG("[SkeletalMeshViewport] BoneGizmoProxyComponent created and attached");

        // Create Gizmo Actor
        UE_LOG("[SkeletalMeshViewport] Spawning GizmoActor...");
        CurrentGizmo = PreviewWorld->SpawnActor<AGizmoActor>();
        if (CurrentGizmo)
        {
            UE_LOG("[SkeletalMeshViewport] GizmoActor spawned successfully");

            // Attach gizmo root component to proxy component
            USceneComponent* GizmoRoot = CurrentGizmo->GetRootComponent();
            if (GizmoRoot)
            {
                GizmoRoot->SetupAttachment(BoneGizmoProxyComponent);
                UE_LOG("[SkeletalMeshViewport] Gizmo root attached to proxy component");
            }
            else
            {
                UE_LOG("[SkeletalMeshViewport] WARNING: Gizmo root component is null!");
            }

            CurrentGizmo->SetMode(EGizmoMode::Translate); // Default to translate mode
            UE_LOG("[SkeletalMeshViewport] Gizmo created for bone %d", PickingResult.BoneIndex);
        }
        else
        {
            UE_LOG("[SkeletalMeshViewport] ERROR: Failed to spawn GizmoActor!");
        }

        PickedBoneIndex = PickingResult.BoneIndex;
        bNeedsRedraw = true;
    }
    else
    {
        UE_LOG("[SkeletalMeshViewport] ERROR: Failed to create BoneGizmoProxyComponent!");
    }
}

void USkeletalMeshViewportWidget::DestroyCurrentGizmo()
{
    UE_LOG("[SkeletalMeshViewport] DestroyCurrentGizmo called");

    // Destroy gizmo actor
    if (CurrentGizmo)
    {
        UE_LOG("[SkeletalMeshViewport] Destroying GizmoActor...");
        ObjectFactory::DeleteObject(CurrentGizmo);
        CurrentGizmo = nullptr;
    }

    // Destroy proxy component
    if (BoneGizmoProxyComponent)
    {
        UE_LOG("[SkeletalMeshViewport] Destroying BoneGizmoProxyComponent...");
        BoneGizmoProxyComponent->DestroyComponent();
        BoneGizmoProxyComponent = nullptr;
    }

    PickedBoneIndex = -1;
    bNeedsRedraw = true;
    UE_LOG("[SkeletalMeshViewport] Gizmo destroyed");
}
