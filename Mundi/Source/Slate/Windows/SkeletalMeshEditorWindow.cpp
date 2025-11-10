#include "pch.h"
#include "SkeletalMeshEditorWindow.h"
#include "Widgets/SkeletalMeshViewportWidget.h" // [중요] 새 위젯 경로

IMPLEMENT_CLASS(USkeletalMeshEditorWindow)

USkeletalMeshEditorWindow::USkeletalMeshEditorWindow()
{
	// 윈도우 기본 설정 (제목, 크기 등)
	FUIWindowConfig Config = GetMutableConfig();
	Config.WindowTitle = "Skeletal Mesh Viewer";
	Config.DefaultSize = ImVec2(800, 600);
	Config.bResizable = true;
	SetConfig(Config);
}

void USkeletalMeshEditorWindow::Initialize()
{
	// 1. UUIWindow의 기본 초기화 (필수)
	UUIWindow::Initialize();

	// 2. 뷰포트 위젯을 생성합니다.
	ViewportWidget = NewObject<USkeletalMeshViewportWidget>();
	ViewportWidget->SetName("SkeletalMeshViewport");

	// 3. 생성된 위젯을 이 윈도우에 추가합니다.
	//    이제 이 윈도우의 RenderWidget()이 ViewportWidget->RenderWidget()을 호출합니다.
	AddWidget(ViewportWidget);
}
