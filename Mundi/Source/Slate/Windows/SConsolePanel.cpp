#include "pch.h"
#include "SConsolePanel.h"
#include "ConsoleWindow.h"
#include "ImGui/imgui.h"

SConsolePanel::SConsolePanel()
{
}

SConsolePanel::~SConsolePanel()
{
}

void SConsolePanel::Initialize(UConsoleWindow* InConsoleWindow)
{
	ConsoleWindow = InConsoleWindow;
}

void SConsolePanel::OnRender()
{
	if (!ConsoleWindow)
		return;

	// Set up ImGui window to fit the console panel area
	ImGui::SetNextWindowPos(ImVec2(Rect.Min.X, Rect.Min.Y));
	ImGui::SetNextWindowSize(ImVec2(GetWidth(), GetHeight()));

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

	if (ImGui::Begin("##ConsolePanel", nullptr, flags))
	{
		// Render console title
		ImGui::TextColored(ImVec4(0.52f, 0.88f, 0.75f, 1.0f), "Console");
		ImGui::Separator();

		// Render console widget
		ConsoleWindow->RenderWidget();
	}
	ImGui::End();

	ImGui::PopStyleVar(3);
}

void SConsolePanel::OnUpdate(float DeltaSeconds)
{
	if (ConsoleWindow)
	{
		ConsoleWindow->Update();
	}
}
