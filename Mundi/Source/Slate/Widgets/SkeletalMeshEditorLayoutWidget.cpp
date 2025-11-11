#include "pch.h"
#include "SkeletalMeshEditorLayoutWidget.h"
#include "SkeletalMeshViewportWidget.h"
#include "BoneHierarchyWidget.h"
#include "BoneDetailWidget.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(USkeletalMeshEditorLayoutWidget)

void USkeletalMeshEditorLayoutWidget::RenderWidget()
{
	// 전체 윈도우 영역 크기 가져오기
	ImVec2 WindowSize = ImGui::GetContentRegionAvail();

	// 좌우 분할
	float LeftWidth = WindowSize.x * LeftRightSplitRatio;
	float RightWidth = WindowSize.x * (1.0f - LeftRightSplitRatio);

	// === 좌측 패널: Viewport ===
	ImGui::BeginChild("ViewportPanel", ImVec2(LeftWidth, 0), true, ImGuiWindowFlags_NoScrollbar);
	{
		if (ViewportWidget != nullptr)
		{
			ViewportWidget->RenderWidget();
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Viewport Widget not initialized");
		}
	}
	ImGui::EndChild();

	// 좌우 구분선
	ImGui::SameLine();

	// === 우측 패널: Hierarchy + Detail ===
	ImGui::BeginGroup();
	{
		// 우측 패널의 상하 분할
		float RightTopHeight = WindowSize.y * RightTopBottomSplitRatio;
		float RightBottomHeight = WindowSize.y * (1.0f - RightTopBottomSplitRatio);

		// 우측 상단: Bone Hierarchy
		ImGui::BeginChild("HierarchyPanel", ImVec2(RightWidth, RightTopHeight), true);
		{
			if (HierarchyWidget != nullptr)
			{
				HierarchyWidget->RenderWidget();
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Hierarchy Widget not initialized");
			}
		}
		ImGui::EndChild();

		// 우측 하단: Bone Detail
		ImGui::BeginChild("DetailPanel", ImVec2(RightWidth, RightBottomHeight), true);
		{
			if (DetailWidget != nullptr)
			{
				DetailWidget->RenderWidget();
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Detail Widget not initialized");
			}
		}
		ImGui::EndChild();
	}
	ImGui::EndGroup();
}
