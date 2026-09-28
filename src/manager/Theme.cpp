#include "manager/Theme.h"

#include "core/Bidi.h"
#include "core/I18n.h"
#include "manager/ManagerResources.h"

#include <windows.h>
#include <d3d11.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

ImVec4 Rgb(unsigned int hex, float alpha = 1.0f) {
  return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f, alpha);
}

ImU32 U32(unsigned int hex, float alpha = 1.0f) { return ImGui::GetColorU32(Rgb(hex, alpha)); }

// Mixes towards white (t > 0) or black (t < 0), for hover and pressed states.
unsigned int Shade(unsigned int hex, float t) {
  const auto channel = [&](int shift) {
    const float c = static_cast<float>((hex >> shift) & 0xFF);
    const float target = t > 0.0f ? 255.0f : 0.0f;
    return static_cast<unsigned int>(std::lround(c + (target - c) * std::fabs(t))) << shift;
  };
  return channel(16) | channel(8) | channel(0);
}

fs::path FontsDirectory() {
  wchar_t buffer[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, buffer))) {
    return fs::path(buffer);
  }
  return fs::path(L"C:\\Windows\\Fonts");
}

struct Blob {
  const void* data = nullptr;
  size_t size = 0;
};

// An RCDATA resource out of this executable. Resource memory lives as long as
// the module, so the atlas can point into it without copying.
Blob Resource(int id) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!found) return {};
  HGLOBAL loaded = LoadResource(module, found);
  if (!loaded) return {};
  return Blob{LockResource(loaded), SizeofResource(module, found)};
}

// System font files read once per atlas build and shared between the faces
// that merge them. A CJK collection is 10-20 MB, and reading it once for each
// of four faces would quadruple that for nothing.
std::map<std::wstring, std::vector<char>>& FileCache() {
  static std::map<std::wstring, std::vector<char>> cache;
  return cache;
}

const std::vector<char>* SystemFont(const wchar_t* name) {
  auto& cache = FileCache();
  if (auto it = cache.find(name); it != cache.end()) {
    return it->second.empty() ? nullptr : &it->second;
  }
  std::vector<char> bytes;
  std::ifstream file(FontsDirectory() / name, std::ios::binary);
  if (file) bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  auto& slot = cache[name];
  slot = std::move(bytes);
  return slot.empty() ? nullptr : &slot;
}

// What the display faces are asked for: Latin with both extension blocks
// (Turkish lives in Extended-A), and the punctuation the interface uses.
const ImWchar* DisplayRanges() {
  static const ImWchar ranges[] = {
      0x0020, 0x024F,
      0x2010, 0x205E,
      0,
  };
  return ranges;
}

// Everything the interface can draw in the current language: a fixed base,
// plus every character that actually appears in its translations and in the
// names of the languages on the picker. Built from the text rather than from
// ImGui's prepared CJK ranges, which would rasterise tens of thousands of
// glyphs the interface never says.
ImVector<ImWchar>& InterfaceRanges() {
  static ImVector<ImWchar> ranges;
  return ranges;
}

void BuildInterfaceRanges(Language language) {
  static const ImWchar base[] = {
      0x0020, 0x024F,   // Latin, Latin-1, Extended-A and -B
      0x0370, 0x03FF,   // Greek, for the odd symbol
      0x0400, 0x052F,   // Cyrillic and Cyrillic Supplement
      0x2010, 0x206F,   // dashes, quotation marks, the ellipsis, primes
      0x20AC, 0x20AC,   // euro
      0x2190, 0x21FF,   // arrows
      0x2212, 0x2265,   // minus, approximately, less/greater-or-equal
      0x25A0, 0x25FF,   // squares and circles
      0,
  };
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(base);
  for (size_t i = 0; i < kLanguageCount; ++i) {
    const auto each = static_cast<Language>(i);
    builder.AddText(ArabicForDisplay(NativeLanguageName(each)).c_str());
  }
  const bool rtl = IsRightToLeft(language);
  for (size_t row = 0; row < TranslationCount(language); ++row) {
    const char* english = nullptr;
    const char* translated = nullptr;
    TranslationAt(language, row, english, translated);
    if (rtl) {
      builder.AddText(ArabicForDisplay(translated).c_str());
    } else {
      builder.AddText(translated);
    }
  }
  InterfaceRanges().clear();
  builder.BuildRanges(&InterfaceRanges());
}

// The CJK face each language is drawn in, first. Han characters are shared
// between Chinese and Japanese but drawn differently in each, so Japanese gets
// a Japanese font rather than whichever came first. The rest follow for the
// language picker's own names.
std::vector<std::pair<const wchar_t*, int>> CjkFallbacks(Language language) {
  std::vector<std::pair<const wchar_t*, int>> list;
  switch (language) {
    case Language::Japanese:
      list = {{L"YuGothM.ttc", 0}, {L"meiryo.ttc", 0}, {L"msgothic.ttc", 0}};
      break;
    case Language::Korean:
      list = {{L"malgun.ttf", 0}, {L"gulim.ttc", 0}};
      break;
    default:
      break;
  }
  list.push_back({L"msyh.ttc", 0});
  list.push_back({L"simsun.ttc", 0});
  list.push_back({L"malgun.ttf", 0});
  return list;
}

struct FaceSpec {
  int resource;       // embedded display face, 0 for a system face
  const wchar_t* system;
  float size;         // at scale 1
};

struct ThemeFaces {
  FaceSpec body, heading, title, caption;
  bool serifHeadings;   // fall back to Georgia rather than Segoe for headings
};

ThemeFaces FacesFor(ThemeId theme) {
  switch (theme) {
    case ThemeId::QuestLog:
      return {{IDR_FONT_ALEGREYA, nullptr, 18.0f},
              {IDR_FONT_IM_FELL_SC, nullptr, 23.0f},
              {IDR_FONT_IM_FELL_SC, nullptr, 34.0f},
              {IDR_FONT_ALEGREYA, nullptr, 15.0f},
              true};
    case ThemeId::Dragonflight:
      return {{IDR_FONT_INTER, nullptr, 16.0f},
              {IDR_FONT_MARCELLUS_SC, nullptr, 21.0f},
              {IDR_FONT_MARCELLUS_SC, nullptr, 30.0f},
              {IDR_FONT_INTER, nullptr, 13.5f},
              true};
    case ThemeId::Stormwind:
    default:
      return {{IDR_FONT_ALEGREYA_SANS, nullptr, 18.0f},
              {IDR_FONT_CINZEL, nullptr, 21.0f},
              {IDR_FONT_CINZEL, nullptr, 30.0f},
              {IDR_FONT_ALEGREYA_SANS, nullptr, 15.0f},
              true};
  }
}

// One face: the display font for the Latin it was drawn for, then the system
// fonts merged under it for everything else. ImGui never overwrites a glyph a
// face already has, so the order is the order of preference.
ImFont* AddFace(const FaceSpec& spec, float px, bool serifFallback, bool withCjk,
                Language language) {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  ImFont* font = nullptr;

  if (spec.resource) {
    const Blob blob = Resource(spec.resource);
    if (blob.data) {
      ImFontConfig config;
      config.FontDataOwnedByAtlas = false;   // resource memory, not ours to free
      font = atlas->AddFontFromMemoryTTF(const_cast<void*>(blob.data),
                                         static_cast<int>(blob.size), px, &config,
                                         DisplayRanges());
    }
  }

  const ImWchar* everything = InterfaceRanges().Data;
  const auto merge = [&](const wchar_t* file, int index, const ImWchar* ranges) {
    const std::vector<char>* bytes = SystemFont(file);
    if (!bytes) return;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;   // the cache owns it until Build()
    config.MergeMode = font != nullptr;
    config.FontNo = index;
    ImFont* added = atlas->AddFontFromMemoryTTF(const_cast<char*>(bytes->data()),
                                                static_cast<int>(bytes->size()), px, &config,
                                                ranges);
    if (!font) font = added;
  };

  if (serifFallback) merge(L"georgia.ttf", 0, everything);
  merge(L"segoeui.ttf", 0, everything);
  if (withCjk) {
    for (const auto& [file, index] : CjkFallbacks(language)) merge(file, index, everything);
  }
  return font;
}

// ------------------------------------------------------------------- art

struct Art {
  ComPtr<ID3D11ShaderResourceView> stone, parchment, slate, emblem;
  float tile = 512.0f;
};

Art& TheArt() {
  static Art art;
  return art;
}

ComPtr<ID3D11ShaderResourceView> DecodePng(ID3D11Device* device, IWICImagingFactory* wic,
                                           int resource) {
  const Blob blob = Resource(resource);
  if (!blob.data) return nullptr;

  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(wic->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(blob.data)),
                                          static_cast<DWORD>(blob.size))) ||
      FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
                                          WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return nullptr;
  }
  UINT width = 0, height = 0;
  converter->GetSize(&width, &height);
  std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
  if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                   pixels.data()))) {
    return nullptr;
  }

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{pixels.data(), width * 4, 0};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device->CreateTexture2D(&desc, &init, &texture)) ||
      FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
    return nullptr;
  }
  return view;
}

ImTextureID Tex(const ComPtr<ID3D11ShaderResourceView>& view) {
  return (ImTextureID)(uintptr_t)view.Get();
}

void Diamond(ImDrawList* draw, ImVec2 c, float r, ImU32 fill, ImU32 core) {
  draw->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                      ImVec2(c.x - r, c.y), fill);
  draw->AddCircleFilled(c, r * 0.35f, core);
}

}  // namespace

// ----------------------------------------------------------------- names

const char* TagForTheme(ThemeId theme) {
  switch (theme) {
    case ThemeId::QuestLog: return "questlog";
    case ThemeId::Dragonflight: return "dragonflight";
    default: return "stormwind";
  }
}

bool ParseThemeTag(std::string_view tag, ThemeId& out) {
  for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
    if (tag == TagForTheme(static_cast<ThemeId>(i))) {
      out = static_cast<ThemeId>(i);
      return true;
    }
  }
  return false;
}

const char* ThemeDisplayName(ThemeId theme) {
  switch (theme) {
    case ThemeId::QuestLog: return "Quest log";
    case ThemeId::Dragonflight: return "Dragonflight";
    default: return "Stormwind";
  }
}

// ---------------------------------------------------------------- colours

ThemeColors CurrentThemeColors(ThemeId theme) {
  switch (theme) {
    case ThemeId::QuestLog:
      return ThemeColors{
          0x7A5A2E,   // brown ink
          0x2E6B12,   // uncommon, darkened to read on parchment
          0xA04A00,   // legendary
          0xA3201A,
          0x5A1A0C,   // the quest title's deep red
          0x6A2C9A,   // epic
          0x2E1F10,   // ink
          0x6B5234,   // faded ink
          0xE8D5AC,
          0xDCC594,
          0x8E1B12,   // wax
          0x5E0F09,
          0xF3D9A4,
          true,
      };
    case ThemeId::Dragonflight:
      return ThemeColors{
          0xC8AA6E, 0x1EFF00, 0xFF8000, 0xFF5A5A, 0xF4DFA8, 0xC98BFF,
          0xE8E0CE, 0x8C8676, 0x12151C, 0x181C26,
          0xC8AA6E,   // a gold slab with dark text, as the modern UI does it
          0xF4DFA8, 0x12151C, false,
      };
    case ThemeId::Stormwind:
    default:
      return ThemeColors{
          0xC8AA6E,   // the game's bronze-gold border colour
          0x1EFF00,   // uncommon
          0xFF8000,   // legendary
          0xFF4A4A,   // brighter than the game's red, which vanishes on stone
          0xFFD100,   // the game's own heading yellow
          0xA335EE,   // epic
          0xE8E0CE,   // parchment
          0x9C9380,
          0x0B0A08, 0x13110D,
          0x6B0F0A,   // the red of the game's dialog buttons
          0xC8AA6E, 0xFFD100, false,
      };
  }
}

// ------------------------------------------------------------------ fonts

ThemeFonts LoadThemeFonts(ThemeId theme, float scale, Language language) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  // Wide rather than tall: a CJK atlas is thousands of glyphs, and ImGui's
  // default width packs them into a texture taller than some GPUs accept.
  io.Fonts->TexDesiredWidth = 4096;
  BuildInterfaceRanges(language);

  // Rasterised at the size they will be drawn at, rather than drawn small and
  // stretched: stretched text at this size is visibly soft. Rounded, because a
  // fractional pixel size puts the baseline between pixels.
  const auto at = [scale](float size) { return std::round(size * scale); };
  const ThemeFaces faces = FacesFor(theme);

  ThemeFonts fonts;
  // Body first, so it becomes ImGui's default face.
  fonts.body = AddFace(faces.body, at(faces.body.size), false, true, language);
  fonts.heading = AddFace(faces.heading, at(faces.heading.size), faces.serifHeadings, true,
                          language);
  fonts.title = AddFace(faces.title, at(faces.title.size), faces.serifHeadings, false,
                        language);
  fonts.caption = AddFace(faces.caption, at(faces.caption.size), false, true, language);
  // Numbers the operator compares against each other have to line up.
  // Consolas, which is not embedded because every Windows install has it; the
  // log it shows is English by design.
  if (const std::vector<char>* consolas = SystemFont(L"consola.ttf")) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    fonts.mono = io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(consolas->data()),
                                                static_cast<int>(consolas->size()), at(16.0f),
                                                &config, DisplayRanges());
  }

  if (!fonts.body) fonts.body = io.Fonts->AddFontDefault();

  // Built now, so the system font files can be let go straight away rather
  // than held for the life of the process.
  io.Fonts->Build();
  io.Fonts->ClearInputData();
  FileCache().clear();
  return fonts;
}

// ------------------------------------------------------------------ style

void ApplySidecarTheme(ThemeId theme) {
  // From a clean style every time: the caller scales sizes afterwards, and a
  // second theme applied over a scaled first would be scaled twice.
  ImGui::GetStyle() = ImGuiStyle();
  ImGui::StyleColorsDark();
  const ThemeColors colors = CurrentThemeColors(theme);
  const bool light = colors.light;

  ImGuiStyle& style = ImGui::GetStyle();
  // The game's frames are square and edged, not rounded and flat. The modern
  // UI allows itself a little radius; the older two do not.
  const float radius = theme == ThemeId::Dragonflight ? 3.0f : 1.0f;
  style.WindowRounding = 0.0f;
  style.ChildRounding = radius;
  style.FrameRounding = radius;
  style.GrabRounding = radius;
  style.PopupRounding = radius;
  style.TabRounding = radius;
  style.ScrollbarRounding = radius;

  style.WindowPadding = ImVec2(0.0f, 0.0f);   // the shell lays out its own bands
  style.FramePadding = ImVec2(12.0f, 7.0f);
  style.ItemSpacing = ImVec2(10.0f, 9.0f);
  style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
  style.CellPadding = ImVec2(12.0f, 8.0f);
  style.IndentSpacing = 20.0f;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 12.0f;

  // Edges everywhere: a WoW panel is defined by its border, not by a shadow.
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.SeparatorTextBorderSize = 1.0f;

  ImVec4* c = style.Colors;
  const unsigned int ink = colors.parchment;

  // The shell window is see-through: the theme's backdrop is drawn behind it.
  c[ImGuiCol_WindowBg] = Rgb(colors.window, 0.0f);
  c[ImGuiCol_ChildBg] = Rgb(colors.panel, light ? 0.45f : 0.72f);
  c[ImGuiCol_PopupBg] = Rgb(colors.panel, 0.98f);
  c[ImGuiCol_MenuBarBg] = Rgb(colors.panel);

  c[ImGuiCol_Text] = Rgb(ink);
  c[ImGuiCol_TextDisabled] = Rgb(colors.muted);
  c[ImGuiCol_TextSelectedBg] = Rgb(colors.accent, 0.35f);

  // Sunken fields, the way the game draws an input slot -- or, on the page, a
  // faint pencilled box.
  if (light) {
    c[ImGuiCol_FrameBg] = Rgb(0x5A3A10, 0.08f);
    c[ImGuiCol_FrameBgHovered] = Rgb(0x5A3A10, 0.14f);
    c[ImGuiCol_FrameBgActive] = Rgb(0x5A3A10, 0.20f);
  } else {
    c[ImGuiCol_FrameBg] = Rgb(0x05060A, 0.85f);
    c[ImGuiCol_FrameBgHovered] = Rgb(Shade(colors.panel, 0.08f), 0.95f);
    c[ImGuiCol_FrameBgActive] = Rgb(Shade(colors.panel, 0.14f), 0.95f);
  }

  const unsigned int border = theme == ThemeId::Dragonflight ? 0x3A3F4E : colors.accent;
  c[ImGuiCol_Border] = Rgb(border, theme == ThemeId::Dragonflight ? 1.0f : (light ? 0.55f : 0.40f));
  c[ImGuiCol_BorderShadow] = Rgb(0x000000, 0.0f);
  c[ImGuiCol_Separator] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_SeparatorHovered] = Rgb(colors.accent, 0.55f);
  c[ImGuiCol_SeparatorActive] = Rgb(colors.goldBright, 0.80f);

  c[ImGuiCol_Button] = Rgb(colors.accent, light ? 0.14f : 0.16f);
  c[ImGuiCol_ButtonHovered] = Rgb(colors.accent, light ? 0.26f : 0.32f);
  c[ImGuiCol_ButtonActive] = Rgb(colors.goldBright, light ? 0.30f : 0.46f);

  c[ImGuiCol_Header] = Rgb(colors.accent, 0.18f);
  c[ImGuiCol_HeaderHovered] = Rgb(colors.accent, 0.28f);
  c[ImGuiCol_HeaderActive] = Rgb(colors.accent, 0.40f);

  c[ImGuiCol_CheckMark] = Rgb(light ? colors.buttonFace : colors.goldBright);
  c[ImGuiCol_SliderGrab] = Rgb(colors.accent);
  c[ImGuiCol_SliderGrabActive] = Rgb(colors.goldBright);

  c[ImGuiCol_Tab] = Rgb(colors.panel);
  c[ImGuiCol_TabHovered] = Rgb(colors.accent, 0.30f);
  c[ImGuiCol_TabSelected] = Rgb(colors.accent, 0.22f);
  c[ImGuiCol_TabSelectedOverline] = Rgb(colors.goldBright);
  c[ImGuiCol_TabDimmed] = Rgb(colors.panel);
  c[ImGuiCol_TabDimmedSelected] = Rgb(colors.accent, 0.16f);

  c[ImGuiCol_TitleBg] = Rgb(colors.window);
  c[ImGuiCol_TitleBgActive] = Rgb(colors.accent, 0.20f);

  c[ImGuiCol_TableHeaderBg] = Rgb(colors.accent, light ? 0.12f : 0.10f);
  c[ImGuiCol_TableBorderStrong] = Rgb(colors.accent, 0.34f);
  c[ImGuiCol_TableBorderLight] = Rgb(colors.accent, 0.16f);
  c[ImGuiCol_TableRowBg] = Rgb(0x000000, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = light ? Rgb(0x5A3A10, 0.05f) : Rgb(0xFFFFFF, 0.022f);

  c[ImGuiCol_ScrollbarBg] = Rgb(colors.window, light ? 0.2f : 0.6f);
  c[ImGuiCol_ScrollbarGrab] = Rgb(colors.accent, 0.35f);
  c[ImGuiCol_ScrollbarGrabHovered] = Rgb(colors.accent, 0.50f);
  c[ImGuiCol_ScrollbarGrabActive] = Rgb(colors.goldBright, 0.60f);

  c[ImGuiCol_PlotHistogram] = Rgb(colors.accent);
  c[ImGuiCol_NavHighlight] = Rgb(colors.goldBright);
  c[ImGuiCol_ModalWindowDimBg] = Rgb(0x000000, light ? 0.45f : 0.72f);
}

// -------------------------------------------------------------------- art

bool LoadThemeArt(ID3D11Device* device) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ComPtr<IWICImagingFactory> wic;
  const bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                             CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
  if (ok) {
    Art& art = TheArt();
    art.stone = DecodePng(device, wic.Get(), IDR_ART_STONE);
    art.parchment = DecodePng(device, wic.Get(), IDR_ART_PARCHMENT);
    art.slate = DecodePng(device, wic.Get(), IDR_ART_SLATE);
    art.emblem = DecodePng(device, wic.Get(), IDR_ART_EMBLEM);
  }
  wic.Reset();
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

void DrawThemeBackdrop(ImDrawList* draw, ImVec2 min, ImVec2 max, ThemeId theme, float scale) {
  const ThemeColors colors = CurrentThemeColors(theme);
  draw->AddRectFilled(min, max, U32(colors.window));

  const Art& art = TheArt();
  const ComPtr<ID3D11ShaderResourceView>& texture =
      theme == ThemeId::QuestLog ? art.parchment
      : theme == ThemeId::Dragonflight ? art.slate
                                       : art.stone;
  if (texture) {
    // Tiled by hand: the backend's sampler clamps, so one stretched quad would
    // smear. Tiles are the art's own size times the interface scale, so the
    // grain stays the same size relative to the text.
    const float tile = art.tile * std::max(scale, 1.0f);
    draw->PushClipRect(min, max, true);
    for (float y = min.y; y < max.y; y += tile) {
      for (float x = min.x; x < max.x; x += tile) {
        draw->AddImage(Tex(texture), ImVec2(x, y), ImVec2(x + tile, y + tile));
      }
    }
    draw->PopClipRect();
  }

  // A vignette so the edges recede and the middle, where the reading happens,
  // is the brightest -- scorched edges on the page.
  const float band = std::min(max.x - min.x, max.y - min.y) * 0.18f;
  const unsigned int edge = colors.light ? 0x4A2E0C : 0x000000;
  const float strength = colors.light ? 0.45f : 0.60f;
  const ImU32 dark = U32(edge, strength);
  const ImU32 clear = U32(edge, 0.0f);
  draw->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + band), dark, dark, clear, clear);
  draw->AddRectFilledMultiColor(ImVec2(min.x, max.y - band), max, clear, clear, dark, dark);
  draw->AddRectFilledMultiColor(min, ImVec2(min.x + band, max.y), dark, clear, clear, dark);
  draw->AddRectFilledMultiColor(ImVec2(max.x - band, min.y), max, clear, dark, dark, clear);

  // The modern UI's watermark: the emblem, large and nearly invisible.
  if (theme == ThemeId::Dragonflight && art.emblem) {
    const float size = (max.y - min.y) * 0.62f;
    const ImVec2 at(max.x - size * 0.78f, max.y - size * 0.82f);
    draw->AddImage(Tex(art.emblem), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0),
                   ImVec2(1, 1), U32(colors.accent, 0.045f));
  }
}

void DrawThemeFrame(ImDrawList* draw, ImVec2 min, ImVec2 max, ThemeId theme, float scale) {
  const ThemeColors colors = CurrentThemeColors(theme);
  const float s = std::max(scale, 1.0f);
  switch (theme) {
    case ThemeId::Stormwind: {
      // The Esc menu's double rule, and a gold stud at each corner.
      draw->AddRect(min, max, U32(colors.accent, 0.85f), 0.0f, 0, 2.0f * s);
      const float inset = 4.0f * s;
      draw->AddRect(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset),
                    U32(colors.accent, 0.30f), 0.0f, 0, 1.0f);
      const ImU32 stud = U32(colors.goldBright, 0.95f);
      const ImU32 core = U32(0x2A1A08);
      const float r = 5.0f * s;
      Diamond(draw, min, r, stud, core);
      Diamond(draw, ImVec2(max.x, min.y), r, stud, core);
      Diamond(draw, ImVec2(min.x, max.y), r, stud, core);
      Diamond(draw, max, r, stud, core);
      break;
    }
    case ThemeId::QuestLog: {
      // An inked border with a pen flourish in each corner.
      draw->AddRect(min, max, U32(colors.accent, 0.80f), 0.0f, 0, 1.5f * s);
      const float inset = 3.0f * s;
      draw->AddRect(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset),
                    U32(colors.accent, 0.30f), 0.0f, 0, 1.0f);
      const float len = 16.0f * s;
      const ImU32 inkColour = U32(colors.goldBright, 0.75f);
      const auto flourish = [&](ImVec2 corner, float dx, float dy) {
        draw->AddBezierCubic(ImVec2(corner.x + dx * len, corner.y + dy * 2.0f * s),
                             ImVec2(corner.x + dx * len * 0.4f, corner.y - dy * 4.0f * s),
                             ImVec2(corner.x - dx * 4.0f * s, corner.y + dy * len * 0.4f),
                             ImVec2(corner.x + dx * 2.0f * s, corner.y + dy * len), inkColour,
                             1.4f * s);
        draw->AddCircleFilled(ImVec2(corner.x + dx * 3.0f * s, corner.y + dy * 3.0f * s),
                              2.0f * s, inkColour);
      };
      flourish(min, 1, 1);
      flourish(ImVec2(max.x, min.y), -1, 1);
      flourish(ImVec2(min.x, max.y), 1, -1);
      flourish(max, -1, -1);
      break;
    }
    case ThemeId::Dragonflight:
    default: {
      // A hairline, and gold brackets closing each corner.
      draw->AddRect(min, max, U32(0x3A3F4E), 3.0f * s, 0, 1.0f);
      const float len = 14.0f * s;
      const float w = 2.0f * s;
      const ImU32 gold = U32(colors.accent, 0.90f);
      draw->AddLine(min, ImVec2(min.x + len, min.y), gold, w);
      draw->AddLine(min, ImVec2(min.x, min.y + len), gold, w);
      draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x - len, min.y), gold, w);
      draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), gold, w);
      draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), gold, w);
      draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - len), gold, w);
      draw->AddLine(max, ImVec2(max.x - len, max.y), gold, w);
      draw->AddLine(max, ImVec2(max.x, max.y - len), gold, w);
      break;
    }
  }
}

void DrawEmblem(ImDrawList* draw, ImVec2 at, float size, ThemeId theme) {
  const Art& art = TheArt();
  if (!art.emblem) return;
  const ThemeColors colors = CurrentThemeColors(theme);
  const unsigned int tint = colors.light ? colors.goldBright : colors.accent;
  draw->AddImage(Tex(art.emblem), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0),
                 ImVec2(1, 1), U32(tint));
}

bool ThemedPrimaryButton(const char* label, ImVec2 size, ThemeId theme, bool danger,
                         float scale) {
  const ThemeColors colors = CurrentThemeColors(theme);
  const float s = std::max(scale, 1.0f);

  ImGui::PushID(label);
  const bool pressed = ImGui::InvisibleButton("##primary", size);
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();

  unsigned int face = colors.buttonFace;
  unsigned int rim = colors.buttonRim;
  unsigned int text = colors.buttonText;
  if (danger) {
    switch (theme) {
      case ThemeId::QuestLog: face = 0x3E2A18; rim = 0x2A1A0C; text = 0xF3D9A4; break;
      case ThemeId::Dragonflight: face = 0x3A1416; rim = 0xFF5A5A; text = 0xFFB0A8; break;
      default: face = 0x2A0A08; rim = 0x8A6D3B; text = 0xF4DFA8; break;
    }
  }
  if (held) {
    face = Shade(face, -0.18f);
  } else if (hovered) {
    face = Shade(face, 0.14f);
  }

  const float rounding = theme == ThemeId::QuestLog ? 7.0f * s
                         : theme == ThemeId::Dragonflight ? 3.0f * s
                                                          : 0.0f;
  // A face lit from above: lighter at the top, darker at the foot.
  const ImU32 top = U32(Shade(face, 0.16f));
  const ImU32 bottom = U32(Shade(face, -0.28f));
  if (rounding > 0.0f) {
    draw->AddRectFilled(min, max, U32(face), rounding);
    draw->AddRectFilledMultiColor(ImVec2(min.x + rounding, min.y + 1),
                                  ImVec2(max.x - rounding, max.y - 1), top, top, bottom, bottom);
  } else {
    draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
  }

  switch (theme) {
    case ThemeId::Stormwind:
      // Gold rim, a dark inner line, and a rim that brightens on hover.
      draw->AddRect(min, max, U32(hovered ? colors.goldBright : rim), 0.0f, 0, 2.0f * s);
      draw->AddRect(ImVec2(min.x + 3 * s, min.y + 3 * s), ImVec2(max.x - 3 * s, max.y - 3 * s),
                    U32(0x000000, 0.55f), 0.0f, 0, 1.0f);
      break;
    case ThemeId::QuestLog:
      // A wax seal: a darker ring pressed into the edge.
      draw->AddRect(min, max, U32(rim), rounding, 0, 3.0f * s);
      draw->AddRect(ImVec2(min.x + 4 * s, min.y + 4 * s), ImVec2(max.x - 4 * s, max.y - 4 * s),
                    U32(Shade(face, 0.25f), 0.6f), rounding * 0.6f, 0, 1.0f);
      break;
    case ThemeId::Dragonflight:
    default:
      draw->AddRect(min, max, U32(hovered ? colors.goldBright : rim, danger ? 0.9f : 0.6f),
                    rounding, 0, 1.0f);
      break;
  }

  const char* end = std::strstr(label, "##");
  const ImVec2 textSize = ImGui::CalcTextSize(label, end);
  const ImVec2 textAt(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f);
  if (theme == ThemeId::Stormwind) {
    // The game's button text carries a hard shadow.
    draw->AddText(ImVec2(textAt.x + 1 * s, textAt.y + 1 * s), U32(0x000000, 0.8f), label, end);
  }
  draw->AddText(textAt, U32(text), label, end);
  return pressed;
}

}  // namespace sidecar
