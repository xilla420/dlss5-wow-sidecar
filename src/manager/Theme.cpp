#include "manager/Theme.h"

#include <imgui.h>

namespace sidecar {
namespace {

ImVec4 Rgb(unsigned int hex, float alpha = 1.0f) {
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                alpha);
}

}  // namespace

ThemeColors CurrentThemeColors(bool dark) {
  if (dark) {
    return ThemeColors{0x43BAB4, 0x43BAB4, 0xE0A33A, 0xE05A4F};
  }
  return ThemeColors{0x0C6B69, 0x0C6B69, 0xA8720C, 0xB3372B};
}

void ApplySidecarTheme(bool dark) {
  if (dark) {
    ImGui::StyleColorsDark();
  } else {
    ImGui::StyleColorsLight();
  }

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ChildRounding = 4.0f;
  style.TabRounding = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.WindowPadding = ImVec2(16.0f, 16.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.CellPadding = ImVec2(10.0f, 6.0f);
  style.WindowBorderSize = 0.0f;
  style.FrameBorderSize = 0.0f;

  const ThemeColors colors = CurrentThemeColors(dark);
  ImVec4* c = style.Colors;

  if (dark) {
    c[ImGuiCol_WindowBg] = Rgb(0x14181A);
    c[ImGuiCol_ChildBg] = Rgb(0x191E21);
    c[ImGuiCol_PopupBg] = Rgb(0x191E21);
    c[ImGuiCol_FrameBg] = Rgb(0x232A2D);
    c[ImGuiCol_FrameBgHovered] = Rgb(0x2C3438);
    c[ImGuiCol_FrameBgActive] = Rgb(0x333D42);
    c[ImGuiCol_Text] = Rgb(0xE6EAEB);
    c[ImGuiCol_TextDisabled] = Rgb(0x7A8589);
    c[ImGuiCol_Separator] = Rgb(0x2C3438);
    c[ImGuiCol_TableBorderLight] = Rgb(0x2C3438);
    c[ImGuiCol_TableBorderStrong] = Rgb(0x394347);
    c[ImGuiCol_TableHeaderBg] = Rgb(0x1F2528);
  } else {
    c[ImGuiCol_WindowBg] = Rgb(0xF7F8F8);
    c[ImGuiCol_ChildBg] = Rgb(0xFFFFFF);
    c[ImGuiCol_PopupBg] = Rgb(0xFFFFFF);
    c[ImGuiCol_FrameBg] = Rgb(0xEBEEEE);
    c[ImGuiCol_FrameBgHovered] = Rgb(0xE0E4E4);
    c[ImGuiCol_FrameBgActive] = Rgb(0xD5DADA);
    c[ImGuiCol_Text] = Rgb(0x1A1F21);
    c[ImGuiCol_TextDisabled] = Rgb(0x778184);
    c[ImGuiCol_Separator] = Rgb(0xDDE1E1);
    c[ImGuiCol_TableBorderLight] = Rgb(0xE4E8E8);
    c[ImGuiCol_TableBorderStrong] = Rgb(0xCED4D4);
    c[ImGuiCol_TableHeaderBg] = Rgb(0xEDF0F0);
  }

  c[ImGuiCol_Button] = Rgb(colors.accent, 0.14f);
  c[ImGuiCol_ButtonHovered] = Rgb(colors.accent, 0.26f);
  c[ImGuiCol_ButtonActive] = Rgb(colors.accent, 0.40f);
  c[ImGuiCol_CheckMark] = Rgb(colors.accent);
  c[ImGuiCol_SliderGrab] = Rgb(colors.accent);
  c[ImGuiCol_SliderGrabActive] = Rgb(colors.accent);
  c[ImGuiCol_Header] = Rgb(colors.accent, 0.18f);
  c[ImGuiCol_HeaderHovered] = Rgb(colors.accent, 0.28f);
  c[ImGuiCol_HeaderActive] = Rgb(colors.accent, 0.38f);
  c[ImGuiCol_TitleBgActive] = Rgb(colors.accent, 0.24f);
}

}  // namespace sidecar
