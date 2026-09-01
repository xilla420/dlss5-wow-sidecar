#include "manager/Theme.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <string>

#include <imgui.h>

namespace fs = std::filesystem;

namespace sidecar {
namespace {

ImVec4 Rgb(unsigned int hex, float alpha = 1.0f) {
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                alpha);
}

fs::path FontsDirectory() {
  wchar_t buffer[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, buffer))) {
    return fs::path(buffer);
  }
  return fs::path(L"C:\\Windows\\Fonts");
}

// Loads the first of `candidates` that exists. Returns null rather than
// throwing when none does, and the caller then leaves ImGui on its built-in
// face: a missing font is a cosmetic problem, not a reason for the manager to
// refuse to open.
ImFont* LoadFirst(std::initializer_list<const wchar_t*> candidates, float size) {
  const fs::path directory = FontsDirectory();
  std::error_code ec;
  for (const wchar_t* name : candidates) {
    const fs::path path = directory / name;
    if (!fs::exists(path, ec) || ec) continue;
    if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
            path.string().c_str(), size)) {
      return font;
    }
  }
  return nullptr;
}

}  // namespace

ThemeColors CurrentThemeColors(bool dark) {
  if (dark) {
    return ThemeColors{
        0xC8AA6E,   // the game's bronze-gold border colour
        0x1EFF00,   // uncommon
        0xFF8000,   // legendary
        0xFF4A4A,   // brighter than the game's red, which vanishes on stone
        0xF4DFA8,   // hovered gold
        0xA335EE,   // epic
        0xE8E0CE,   // parchment
    };
  }
  return ThemeColors{0x8A6D3B, 0x1E7A00, 0xB35C00, 0xB3372B,
                     0xC8AA6E, 0x7B2CB5, 0x2A2620};
}

ThemeFonts LoadThemeFonts() {
  ImGuiIO& io = ImGui::GetIO();
  ThemeFonts fonts;

  // Body first, so it becomes ImGui's default face.
  fonts.body = LoadFirst({L"segoeui.ttf", L"tahoma.ttf", L"arial.ttf"}, 17.0f);

  // Georgia stands in for Friz Quadrata: a Roman serif with the same weight in
  // the stems, and present on every Windows install.
  fonts.heading = LoadFirst({L"georgiab.ttf", L"georgia.ttf", L"pala.ttf"}, 21.0f);
  fonts.title = LoadFirst({L"georgiab.ttf", L"georgia.ttf", L"pala.ttf"}, 30.0f);
  fonts.caption = LoadFirst({L"segoeui.ttf", L"tahoma.ttf", L"arial.ttf"}, 14.0f);
  // Numbers the operator compares against each other have to line up.
  fonts.mono = LoadFirst({L"consola.ttf", L"cour.ttf"}, 16.0f);

  if (!fonts.body) io.Fonts->AddFontDefault();
  return fonts;
}

void ApplySidecarTheme(bool dark) {
  ImGui::StyleColorsDark();

  ImGuiStyle& style = ImGui::GetStyle();
  // The game's frames are square and edged, not rounded and flat. Keeping a
  // couple of pixels of radius on the interactive parts stops it reading as a
  // 1998 dialog box.
  style.WindowRounding = 0.0f;
  style.ChildRounding = 2.0f;
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.PopupRounding = 2.0f;
  style.TabRounding = 2.0f;
  style.ScrollbarRounding = 2.0f;

  style.WindowPadding = ImVec2(0.0f, 0.0f);   // the shell lays out its own bands
  style.FramePadding = ImVec2(12.0f, 7.0f);
  style.ItemSpacing = ImVec2(10.0f, 9.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  style.CellPadding = ImVec2(12.0f, 8.0f);
  style.IndentSpacing = 20.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 12.0f;

  // Edges everywhere: a WoW panel is defined by its border, not by a drop
  // shadow.
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.SeparatorTextBorderSize = 1.0f;

  const ThemeColors colors = CurrentThemeColors(dark);
  ImVec4* c = style.Colors;

  // Near-black with a blue cast, which is what the game's panels sit on.
  c[ImGuiCol_WindowBg] = Rgb(0x0B0C12);
  c[ImGuiCol_ChildBg] = Rgb(0x111420);
  c[ImGuiCol_PopupBg] = Rgb(0x0E1018);
  c[ImGuiCol_MenuBarBg] = Rgb(0x0E1018);

  c[ImGuiCol_Text] = Rgb(colors.parchment);
  c[ImGuiCol_TextDisabled] = Rgb(0x8C8676);

  // Sunken fields, the way the game draws an input slot.
  c[ImGuiCol_FrameBg] = Rgb(0x07080C);
  c[ImGuiCol_FrameBgHovered] = Rgb(0x141826);
  c[ImGuiCol_FrameBgActive] = Rgb(0x1B2032);

  // Every border is the bronze rule, at the weight the surface deserves.
  c[ImGuiCol_Border] = Rgb(colors.accent, 0.38f);
  c[ImGuiCol_BorderShadow] = Rgb(0x000000, 0.0f);
  c[ImGuiCol_Separator] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_SeparatorHovered] = Rgb(colors.accent, 0.55f);
  c[ImGuiCol_SeparatorActive] = Rgb(colors.goldBright, 0.80f);

  c[ImGuiCol_Button] = Rgb(colors.accent, 0.16f);
  c[ImGuiCol_ButtonHovered] = Rgb(colors.accent, 0.32f);
  c[ImGuiCol_ButtonActive] = Rgb(colors.goldBright, 0.46f);

  c[ImGuiCol_Header] = Rgb(colors.accent, 0.18f);
  c[ImGuiCol_HeaderHovered] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_HeaderActive] = Rgb(colors.accent, 0.42f);

  c[ImGuiCol_CheckMark] = Rgb(colors.goldBright);
  c[ImGuiCol_SliderGrab] = Rgb(colors.accent);
  c[ImGuiCol_SliderGrabActive] = Rgb(colors.goldBright);

  c[ImGuiCol_Tab] = Rgb(0x111420);
  c[ImGuiCol_TabHovered] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_TabSelected] = Rgb(colors.accent, 0.22f);
  c[ImGuiCol_TabSelectedOverline] = Rgb(colors.goldBright);
  c[ImGuiCol_TabDimmed] = Rgb(0x0E1018);
  c[ImGuiCol_TabDimmedSelected] = Rgb(colors.accent, 0.16f);

  c[ImGuiCol_TitleBg] = Rgb(0x0B0C12);
  c[ImGuiCol_TitleBgActive] = Rgb(colors.accent, 0.20f);

  c[ImGuiCol_TableHeaderBg] = Rgb(0x151A28);
  c[ImGuiCol_TableBorderStrong] = Rgb(colors.accent, 0.34f);
  c[ImGuiCol_TableBorderLight] = Rgb(colors.accent, 0.16f);
  c[ImGuiCol_TableRowBg] = Rgb(0x00000000, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = Rgb(0xFFFFFF, 0.022f);

  c[ImGuiCol_ScrollbarBg] = Rgb(0x07080C);
  c[ImGuiCol_ScrollbarGrab] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_ScrollbarGrabHovered] = Rgb(colors.accent, 0.46f);
  c[ImGuiCol_ScrollbarGrabActive] = Rgb(colors.goldBright, 0.60f);

  c[ImGuiCol_ModalWindowDimBg] = Rgb(0x000000, 0.72f);
}

}  // namespace sidecar
