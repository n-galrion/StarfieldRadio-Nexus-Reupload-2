#include "pch.h"
#include "RadioMenu.h"
#include "RadioPlayer.h"
#include "DXHook.h"
#include "imgui.h"
#include "fmt/format.h"

namespace RadioMenu {

void ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Starfield-inspired dark blue theme
    colors[ImGuiCol_WindowBg]           = ImVec4(0.04f, 0.06f, 0.10f, 0.94f);
    colors[ImGuiCol_Border]             = ImVec4(0.20f, 0.40f, 0.60f, 0.50f);
    colors[ImGuiCol_TitleBg]            = ImVec4(0.06f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.08f, 0.14f, 0.24f, 1.00f);
    colors[ImGuiCol_Text]               = ImVec4(0.85f, 0.90f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.12f, 0.22f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.25f, 0.50f, 0.75f, 1.00f);
    colors[ImGuiCol_FrameBg]            = ImVec4(0.08f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.12f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.15f, 0.28f, 0.45f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.30f, 0.65f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.12f, 0.22f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.25f, 0.50f, 0.75f, 1.00f);
    colors[ImGuiCol_Separator]          = ImVec4(0.20f, 0.35f, 0.55f, 0.50f);
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.04f, 0.06f, 0.10f, 0.80f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.20f, 0.35f, 0.55f, 1.00f);

    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.ItemSpacing = ImVec2(8, 6);
    style.FramePadding = ImVec2(8, 4);
}

void Render()
{
    RadioPlayer* radio = g_RadioPlayer;
    if (!radio) return;

    bool menuOpen = true;
    ImGui::SetNextWindowSize(ImVec2(420, 460), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("GALACTIC RADIO", &menuOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!menuOpen) DXHook::SetMenuOpen(false);
        return;
    }

    if (!menuOpen) {
        DXHook::SetMenuOpen(false);
        ImGui::End();
        return;
    }

    auto stationNames = radio->GetStationNames();
    int currentIdx = radio->GetStationIndex();
    bool isPlaying = radio->GetIsPlaying();
    float volume = radio->GetVolume();

    // --- Now Playing ---
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.70f, 0.95f, 1.0f));
    ImGui::Text("NOW PLAYING");
    ImGui::PopStyleColor();

    if (currentIdx >= 0 && currentIdx < static_cast<int>(stationNames.size())) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::TextWrapped("%s", stationNames[currentIdx].c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("No station selected");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Playback Controls ---
    float buttonWidth = 50.0f;
    float totalWidth = buttonWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
    float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

    if (ImGui::Button("<<", ImVec2(buttonWidth, 30))) {
        radio->PostCommand(RadioPlayer::Command::PrevStation);
    }
    ImGui::SameLine();
    if (ImGui::Button(isPlaying ? "||" : ">", ImVec2(buttonWidth, 30))) {
        radio->PostCommand(RadioPlayer::Command::TogglePlayer);
    }
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(buttonWidth, 30))) {
        radio->PostCommand(RadioPlayer::Command::NextStation);
    }

    ImGui::Spacing();

    // --- Volume ---
    float volumeNorm = volume / 1000.0f * 100.0f;
    ImGui::Text("Volume");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50);
    if (ImGui::SliderFloat("##volume", &volumeNorm, 0.0f, 100.0f, "%.0f%%")) {
        radio->PostSetVolume(volumeNorm / 100.0f * 1000.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Station List ---
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.70f, 0.95f, 1.0f));
    ImGui::Text("STATIONS");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::BeginChild("StationList", ImVec2(0, 0), ImGuiChildFlags_Border);
    for (int i = 0; i < static_cast<int>(stationNames.size()); i++) {
        bool isSelected = (i == currentIdx);
        std::string label = stationNames[i];
        if (isSelected && isPlaying) {
            label = "> " + label;
        }

        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_None)) {
            radio->PostSelectStation(i);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace RadioMenu
