#include "pch.h"
#include "USlateManager.h"

#include "CameraActor.h"
#include "Windows/SWindow.h"
#include "Windows/SSplitterV.h"
#include "Windows/SDetailsWindow.h"
#include "Windows/SControlPanel.h"
#include "Windows/ControlPanelWindow.h"
#include "Windows/SViewportWindow.h"
#include "Windows/ConsoleWindow.h"
#include "Windows/SConsolePanel.h"
#include "Widgets/MainToolbarWidget.h"
#include "Widgets/ConsoleWidget.h"
#include "FViewportClient.h"
#include "UIManager.h"
#include "GlobalConsole.h"

IMPLEMENT_CLASS(USlateManager)

USlateManager& USlateManager::GetInstance()
{
    static USlateManager* Instance = nullptr;
    if (Instance == nullptr)
    {
        Instance = NewObject<USlateManager>();
    }
    return *Instance;
}
#include "FViewportClient.h"

extern float CLIENTWIDTH;
extern float CLIENTHEIGHT;

SViewportWindow* USlateManager::ActiveViewport;

void USlateManager::SaveSplitterConfig()
{
    if (!TopPanel) return;

    EditorINI["TopPanel"] = std::to_string(TopPanel->SplitRatio);
    EditorINI["LeftRootPanel"] = std::to_string(LeftRootPanel->SplitRatio);
    EditorINI["LeftTop"] = std::to_string(LeftTop->SplitRatio);
    EditorINI["LeftBottom"] = std::to_string(LeftBottom->SplitRatio);
    EditorINI["LeftPanel"] = std::to_string(LeftPanel->SplitRatio);
    EditorINI["RightPanel"] = std::to_string(RightPanel->SplitRatio);
    EditorINI["ConsoleVisible"] = bIsConsoleVisible ? "1" : "0";
}

void USlateManager::LoadSplitterConfig()
{
    if (!TopPanel) return;

    if (EditorINI.Contains("TopPanel"))
        TopPanel->SplitRatio = std::stof(EditorINI["TopPanel"]);
    if (EditorINI.Contains("LeftRootPanel"))
        LeftRootPanel->SplitRatio = std::stof(EditorINI["LeftRootPanel"]);
    if (EditorINI.Contains("LeftTop"))
        LeftTop->SplitRatio = std::stof(EditorINI["LeftTop"]);
    if (EditorINI.Contains("LeftBottom"))
        LeftBottom->SplitRatio = std::stof(EditorINI["LeftBottom"]);
    if (EditorINI.Contains("LeftPanel"))
        LeftPanel->SplitRatio = std::stof(EditorINI["LeftPanel"]);
    if (EditorINI.Contains("RightPanel"))
        RightPanel->SplitRatio = std::stof(EditorINI["RightPanel"]);
    if (EditorINI.Contains("ConsoleVisible"))
        bIsConsoleVisible = (EditorINI["ConsoleVisible"] == "1");
}

USlateManager::USlateManager()
{
    for (int i = 0; i < 4; i++)
        Viewports[i] = nullptr;
}

USlateManager::~USlateManager()
{
    Shutdown();
}

void USlateManager::Initialize(ID3D11Device* InDevice, UWorld* InWorld, const FRect& InRect)
{
    // MainToolbar 생성
    MainToolbar = NewObject<UMainToolbarWidget>();
    MainToolbar->Initialize();

    Device = InDevice;
    World = InWorld;
    Rect = InRect;

    // === 전체 화면: 좌(뷰포트+콘솔) + 우(Control + Details) ===
    TopPanel = new SSplitterH();  // 수평 분할 (좌우)
    TopPanel->SetSplitRatio(0.7f);  // 70% 뷰포트+콘솔, 30% UI
    TopPanel->SetRect(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);

    // === 왼쪽 영역: 상(4뷰포트) + 하(Console) ===
    LeftRootPanel = new SSplitterV();  // 수직 분할 (상하)
    LeftRootPanel->SetSplitRatio(0.75f);  // 75% 뷰포트, 25% 콘솔

    // 왼쪽 상단: 4분할 뷰포트 영역
    LeftPanel = new SSplitterH();  // 수평 분할 (좌우)
    LeftTop = new SSplitterV();    // 수직 분할 (상하)
    LeftBottom = new SSplitterV(); // 수직 분할 (상하)
    LeftPanel->SideLT = LeftTop;
    LeftPanel->SideRB = LeftBottom;

    // 오른쪽: Control + Details (상하 분할)
    RightPanel = new SSplitterV();  // 수직 분할 (상하)
    RightPanel->SetSplitRatio(0.5f);  // 50-50 분할

    ControlPanel = new SControlPanel();
    DetailPanel = new SDetailsWindow();

    RightPanel->SideLT = ControlPanel;   // 위쪽: ControlPanel
    RightPanel->SideRB = DetailPanel;    // 아래쪽: DetailsWindow

    // TopPanel 좌우 배치
    TopPanel->SideLT = LeftRootPanel;  // 왼쪽: 뷰포트+콘솔
    TopPanel->SideRB = RightPanel;     // 오른쪽: Control+Details

    // === 뷰포트 생성 ===
    Viewports[0] = new SViewportWindow();
    Viewports[1] = new SViewportWindow();
    Viewports[2] = new SViewportWindow();
    Viewports[3] = new SViewportWindow();
    MainViewport = Viewports[0];

    Viewports[0]->Initialize(0, 0,
        Rect.GetWidth() / 2, Rect.GetHeight() / 2,
        World, Device, EViewportType::Perspective);

    Viewports[1]->Initialize(Rect.GetWidth() / 2, 0,
        Rect.GetWidth(), Rect.GetHeight() / 2,
        World, Device, EViewportType::Orthographic_Front);

    Viewports[2]->Initialize(0, Rect.GetHeight() / 2,
        Rect.GetWidth() / 2, Rect.GetHeight(),
        World, Device, EViewportType::Orthographic_Left);

    Viewports[3]->Initialize(Rect.GetWidth() / 2, Rect.GetHeight() / 2,
        Rect.GetWidth(), Rect.GetHeight(),
        World, Device, EViewportType::Orthographic_Top);

    World->SetEditorCameraActor(MainViewport->GetViewportClient()->GetCamera());

    // 뷰포트들을 2x2로 연결
    LeftTop->SideLT = Viewports[0];
    LeftTop->SideRB = Viewports[1];
    LeftBottom->SideLT = Viewports[2];
    LeftBottom->SideRB = Viewports[3];

    // === Console Panel 생성 ===
    ConsoleWindow = new UConsoleWindow();
    ConsolePanelWindow = new SConsolePanel();
    if (ConsoleWindow && ConsolePanelWindow)
    {
        ConsolePanelWindow->Initialize(ConsoleWindow);
        UE_LOG("USlateManager: ConsoleWindow created successfully");
        UGlobalConsole::SetConsoleWidget(ConsoleWindow->GetConsoleWidget());
        UE_LOG("USlateManager: GlobalConsole connected to ConsoleWidget");
    }
    else
    {
        UE_LOG("ERROR: Failed to create ConsoleWindow");
    }

    // === LeftRootPanel 상하 배치 (상: LeftPanel(4뷰포트), 하: ConsolePanel) ===
    LeftRootPanel->SideLT = LeftPanel;
    LeftRootPanel->SideRB = ConsolePanelWindow;

    SwitchLayout(EViewportLayoutMode::SingleMain);

    LoadSplitterConfig();

    // 콘솔 초기 상태 설정 (기본적으로 숨김)
    if (!bIsConsoleVisible)
    {
        HideConsole();
    }
}

void USlateManager::SwitchLayout(EViewportLayoutMode NewMode)
{
    if (NewMode == CurrentMode) return;

    if (NewMode == EViewportLayoutMode::FourSplit)
    {
        LeftRootPanel->SideLT = LeftPanel;
    }
    else if (NewMode == EViewportLayoutMode::SingleMain)
    {
        LeftRootPanel->SideLT = MainViewport;
    }

    CurrentMode = NewMode;
}

void USlateManager::SwitchPanel(SWindow* SwitchPanel)
{
    if (LeftRootPanel->SideLT != SwitchPanel) {
        LeftRootPanel->SideLT = SwitchPanel;
        CurrentMode = EViewportLayoutMode::SingleMain;
    }
    else {
        LeftRootPanel->SideLT = LeftPanel;
        CurrentMode = EViewportLayoutMode::FourSplit;
    }
}

void USlateManager::Render()
{
    // 메인 툴바 렌더링 (항상 최상단에)
    MainToolbar->RenderWidget();

    // TopPanel 렌더링 (모든 UI 포함)
    if (TopPanel)
    {
        TopPanel->OnRender();
    }
}

void USlateManager::Update(float DeltaTime)
{
    ProcessInput();
    // MainToolbar 업데이트
    MainToolbar->Update(DeltaTime);

    if (TopPanel)
    {
        // 툴바 높이만큼 아래로 이동 (50px)
        const float toolbarHeight = 50.0f;
        TopPanel->Rect = FRect(0, toolbarHeight, CLIENTWIDTH, CLIENTHEIGHT);
        TopPanel->OnUpdate(DeltaTime);
    }
}

void USlateManager::ProcessInput()
{
    const FVector2D MousePosition = INPUT.GetMousePosition();

    if (INPUT.IsMouseButtonPressed(LeftButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseDown(MousePosition, 0);
        }
    }
    if (INPUT.IsMouseButtonPressed(RightButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseDown(MousePosition, 1);
        }
    }
    if (INPUT.IsMouseButtonReleased(LeftButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseUp(MousePosition, 0);
        }
    }
    if (INPUT.IsMouseButtonReleased(RightButton))
    {
        const FVector2D MousePosition = INPUT.GetMousePosition();
        {
            OnMouseUp(MousePosition, 1);
        }
    }
    OnMouseMove(MousePosition);

    // Alt + ` (억음 부호 키)로 콘솔 토글
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) && ImGui::GetIO().KeyAlt)
    {
        ToggleConsole();
    }

    // 단축키로 기즈모 모드 변경
    if (World->GetGizmoActor())
        World->GetGizmoActor()->ProcessGizmoModeSwitch();
}

void USlateManager::OnMouseMove(FVector2D MousePos)
{
    if (ActiveViewport)
    {
        ActiveViewport->OnMouseMove(MousePos);
    }
    else if (TopPanel)
    {
        TopPanel->OnMouseMove(MousePos);
    }
}

void USlateManager::OnMouseDown(FVector2D MousePos, uint32 Button)
{
    if (ActiveViewport)
    {
    }
    else if (TopPanel)
    {
        TopPanel->OnMouseDown(MousePos, Button);

        // 어떤 뷰포트 안에서 눌렸는지 확인
        for (auto* VP : Viewports)
        {
            if (VP && VP->Rect.Contains(MousePos))
            {
                ActiveViewport = VP; // 고정

                // 우클릭인 경우 커서 숨김 및 잠금
                if (Button == 1)
                {
                    INPUT.SetCursorVisible(false);
                    INPUT.LockCursor();
                }
                break;
            }
        }
    }
}

void USlateManager::OnMouseUp(FVector2D MousePos, uint32 Button)
{
    // 우클릭 해제 시 커서 복원 (ActiveViewport와 무관하게 처리)
    if (Button == 1 && INPUT.IsCursorLocked())
    {
        INPUT.SetCursorVisible(true);
        INPUT.ReleaseCursor();
    }

    if (ActiveViewport)
    {
        ActiveViewport->OnMouseUp(MousePos, Button);
        ActiveViewport = nullptr; // 드래그 끝나면 해제
    }
    // NOTE: ActiveViewport가 있더라도 Up 이벤트는 항상 보내주어 드래그 관련 버그를 제거
    if (TopPanel)
    {
        TopPanel->OnMouseUp(MousePos, Button);
    }
}

void USlateManager::OnShutdown()
{
    SaveSplitterConfig();
}

void USlateManager::Shutdown()
{
    // 레이아웃/설정 저장
    SaveSplitterConfig();

    // 콘솔 윈도우 삭제
    if (ConsoleWindow)
    {
        delete ConsoleWindow;
        ConsoleWindow = nullptr;
        UE_LOG("USlateManager: ConsoleWindow destroyed");
    }

    // 콘솔 패널 삭제
    if (ConsolePanelWindow)
    {
        delete ConsolePanelWindow;
        ConsolePanelWindow = nullptr;
    }

    // D3D 컨텍스트를 해제하기 위해 UI 패널과 뷰포트를 명시적으로 삭제
    if (TopPanel) { delete TopPanel; TopPanel = nullptr; }
    if (LeftRootPanel) { delete LeftRootPanel; LeftRootPanel = nullptr; }
    if (LeftTop) { delete LeftTop; LeftTop = nullptr; }
    if (LeftBottom) { delete LeftBottom; LeftBottom = nullptr; }
    if (LeftPanel) { delete LeftPanel; LeftPanel = nullptr; }
    if (RightPanel) { delete RightPanel; RightPanel = nullptr; }

    if (ControlPanel) { delete ControlPanel; ControlPanel = nullptr; }
    if (DetailPanel) { delete DetailPanel; DetailPanel = nullptr; }

    for (int i = 0; i < 4; ++i)
    {
        if (Viewports[i]) { delete Viewports[i]; Viewports[i] = nullptr; }
    }
    MainViewport = nullptr;
    ActiveViewport = nullptr;
}

void USlateManager::SetPIEWorld(UWorld* InWorld)
{
    MainViewport->SetVClientWorld(InWorld);
    // PIE에도 Main Camera Set
    InWorld->SetEditorCameraActor(MainViewport->GetViewportClient()->GetCamera());
}

void USlateManager::ShowConsole()
{
    if (!LeftRootPanel || !ConsolePanelWindow)
        return;

    bIsConsoleVisible = true;

    // 콘솔 패널을 LeftRootPanel의 하단에 연결
    LeftRootPanel->SideRB = ConsolePanelWindow;
    LeftRootPanel->SetSplitRatio(0.75f); // 75% 뷰포트, 25% Console

    // 콘솔을 열 때 스크롤을 가장 아래로 이동
    if (ConsoleWindow && ConsoleWindow->GetConsoleWidget())
    {
        ConsoleWindow->GetConsoleWidget()->SetScrollToBottom();
    }
}

void USlateManager::HideConsole()
{
    if (!LeftRootPanel)
        return;

    bIsConsoleVisible = false;

    // 콘솔 패널을 LeftRootPanel에서 분리 (뷰포트만 표시)
    LeftRootPanel->SideRB = nullptr;
    LeftRootPanel->SetSplitRatio(1.0f); // 100% 뷰포트
}

void USlateManager::ToggleConsole()
{
    if (bIsConsoleVisible)
    {
        HideConsole();
    }
    else
    {
        ShowConsole();
    }
}

void USlateManager::ForceOpenConsole()
{
    if (!bIsConsoleVisible)
    {
        ShowConsole();
    }
}
