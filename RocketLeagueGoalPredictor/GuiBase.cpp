#include "pch.h"
#include "GuiBase.h"

std::string SettingsWindowBase::GetPluginName() {
	return "Goal Predictor";
}

void SettingsWindowBase::SetImGuiContext(uintptr_t ctx) {
	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

std::string PluginWindowBase::GetMenuName() {
	return "GoalPredictor";
}

std::string PluginWindowBase::GetMenuTitle() {
	return menuTitle_;
}

void PluginWindowBase::SetImGuiContext(uintptr_t ctx) {
	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool PluginWindowBase::ShouldBlockInput() {
	return false;
}

bool PluginWindowBase::IsActiveOverlay() {
	return false;
}

void PluginWindowBase::OnOpen() {
	isWindowOpen_ = true;
}

void PluginWindowBase::OnClose() {
	isWindowOpen_ = false;
}

void PluginWindowBase::Render() {
	if (!isWindowOpen_) {
		_globalCvarManager->executeCommand("togglemenu " + GetMenuName());
		return;
	}

	RenderWindow();
}
