#pragma once
#include "Windows/UIWindow.h"

// 스켈레탈 메시 뷰어의 메인 윈도우(창)입니다.
class USkeletalMeshEditorWindow : public UUIWindow
{
public:
	DECLARE_CLASS(USkeletalMeshEditorWindow, UUIWindow)

	USkeletalMeshEditorWindow();
	~USkeletalMeshEditorWindow() = default;

	// 윈도우 초기화 시 뷰포트 위젯을 생성하고 추가합니다.
	void Initialize() override;
	 
protected:
	// 이 창이 소유한 뷰포트 위젯의 포인터
	class USkeletalMeshViewportWidget* ViewportWidget = nullptr;
};