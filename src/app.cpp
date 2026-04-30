#include "pixelizer/app.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <utility>

namespace pixelizer {
namespace {

constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;
constexpr float kViewportSplitterThickness = 8.0F;
constexpr float kViewportMinimumPaneSize = 180.0F;
constexpr const char* kLospecPaletteCredits[] = {
    "pico-8",
    "dawnbringer-16",
    "dawnbringer-32",
    "shmupy-16",
    "aurora",
    "carnival-32",
    "db-iso22",
    "amiga-pixels-64",
    "2bit-demichrome",
    "windows-95-256-colours",
    "microsoft-windows",
    "commodore64",
    "commodore-vic-20",
    "msx",
    "nintendo-entertainment-system",
    "amstrad-cpc",
    "apple-ii",
};

TextId dither_label(DitherMode mode)
{
    switch (mode) {
    case DitherMode::None:
        return TextId::None;
    case DitherMode::Bayer:
        return TextId::Bayer;
    case DitherMode::BlueNoise:
        return TextId::BlueNoise;
    case DitherMode::FloydSteinberg:
        return TextId::FloydSteinberg;
    case DitherMode::FalseFloydSteinberg:
        return TextId::FalseFloydSteinberg;
    case DitherMode::FilterLite:
        return TextId::FilterLite;
    case DitherMode::ZhigangFan:
        return TextId::ZhigangFan;
    case DitherMode::ShiauFan:
        return TextId::ShiauFan;
    case DitherMode::JarvisJudiceNinke:
        return TextId::JarvisJudiceNinke;
    case DitherMode::Atkinson:
        return TextId::Atkinson;
    case DitherMode::Stucki:
        return TextId::Stucki;
    case DitherMode::Burkes:
        return TextId::Burkes;
    case DitherMode::Sierra:
        return TextId::Sierra;
    case DitherMode::TwoRowSierra:
        return TextId::TwoRowSierra;
    case DitherMode::Riemersma:
        return TextId::Riemersma;
    case DitherMode::ClusterDot4x4:
        return TextId::ClusterDot4x4;
    case DitherMode::ClusterDot8x8:
        return TextId::ClusterDot8x8;
    case DitherMode::Horizontal2x2:
        return TextId::Horizontal2x2;
    case DitherMode::Horizontal8x1:
        return TextId::Horizontal8x1;
    case DitherMode::Horizontal12x4:
        return TextId::Horizontal12x4;
    case DitherMode::Vertical2x2:
        return TextId::Vertical2x2;
    case DitherMode::Vertical1x8:
        return TextId::Vertical1x8;
    case DitherMode::Vertical4x12:
        return TextId::Vertical4x12;
    case DitherMode::Diagonal5x5:
        return TextId::Diagonal5x5;
    }
    return TextId::None;
}

TextId block_mode_label(BlockColorMode mode)
{
    switch (mode) {
    case BlockColorMode::Average:
        return TextId::Average;
    case BlockColorMode::WeightedAverage:
        return TextId::Weighted;
    }
    return TextId::Weighted;
}

const char* bayer_pattern_label(int size)
{
    if (size <= 2) {
        return "2x2";
    }
    if (size <= 4) {
        return "4x4";
    }
    if (size <= 8) {
        return "8x8";
    }
    return "16x16";
}

std::string ensure_png_extension(std::filesystem::path path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".png") {
        path.replace_extension(".png");
    }
    return path.string();
}

float fit_zoom_for_size(int width, int height, ImVec2 available)
{
    if (width <= 0 || height <= 0) {
        return 1.0F;
    }

    const float x_zoom = (available.x - 24.0F) / static_cast<float>(width);
    const float y_zoom = (available.y - 74.0F) / static_cast<float>(height);
    return std::clamp(std::min(x_zoom, y_zoom), 0.05F, 32.0F);
}

float split_size_from_ratio(float ratio, float available_size)
{
    if (available_size <= 1.0F) {
        return available_size;
    }

    const float minimum = std::min(kViewportMinimumPaneSize, available_size * 0.45F);
    return std::clamp(ratio * available_size, minimum, available_size - minimum);
}

float ratio_from_split_size(float split_size, float available_size)
{
    if (available_size <= 1.0F) {
        return 0.5F;
    }

    const float minimum = std::min(kViewportMinimumPaneSize, available_size * 0.45F);
    return std::clamp(split_size / available_size, minimum / available_size, 1.0F - minimum / available_size);
}

float splitter_thickness_for(float available_size)
{
    return std::min(kViewportSplitterThickness, std::max(0.0F, available_size * 0.2F));
}

void remove_vertical_item_spacing()
{
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    ImGui::SetCursorPosY(std::max(0.0F, ImGui::GetCursorPosY() - spacing));
}

bool render_splitter(const char* id, ImVec2 size, ImGuiMouseCursor cursor, float& delta)
{
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(cursor);
    }

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Separator);
    if (active) {
        color = ImGui::GetColorU32(ImGuiCol_SeparatorActive);
    } else if (hovered) {
        color = ImGui::GetColorU32(ImGuiCol_SeparatorHovered);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, color);

    if (!active) {
        delta = 0.0F;
        return false;
    }

    const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
    delta = cursor == ImGuiMouseCursor_ResizeEW ? mouse_delta.x : mouse_delta.y;
    return delta != 0.0F;
}

const char* skip_spaces(const char* text)
{
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text)) != 0) {
        ++text;
    }
    return text;
}

bool consumed_full_number(const char* end, bool allow_percent)
{
    end = skip_spaces(end);
    if (allow_percent && *end == '%') {
        end = skip_spaces(end + 1);
    }
    return *end == '\0';
}

bool parse_number_edit_input(const char* input, bool integer, double& value)
{
    input = skip_spaces(input);
    if (*input == '\0') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    if (integer) {
        const long parsed = std::strtol(input, &end, 10);
        if (end == input || errno == ERANGE || !consumed_full_number(end, false)) {
            return false;
        }
        value = static_cast<double>(parsed);
        return true;
    }

    const double parsed = std::strtod(input, &end);
    if (end == input || errno == ERANGE || !std::isfinite(parsed) || !consumed_full_number(end, true)) {
        return false;
    }

    value = parsed;
    return true;
}

Color32 icon_color_at(int x, int y)
{
    static constexpr std::array<const char*, 16> p_mask = {{
        "................",
        "................",
        "...PPPPPPPPP....",
        "...PPPPPPPPPP...",
        "...PPP....PPP...",
        "...PPP....PPP...",
        "...PPP....PPP...",
        "...PPPPPPPPPP...",
        "...PPPPPPPPP....",
        "...PPP..........",
        "...PPP..........",
        "...PPP..........",
        "...PPP..........",
        "...PPP..........",
        "................",
        "................",
    }};
    static constexpr std::array<std::array<int, 4>, 4> bayer4 = {{
        {{0, 8, 2, 10}},
        {{12, 4, 14, 6}},
        {{3, 11, 1, 9}},
        {{15, 7, 13, 5}},
    }};

    static constexpr Color32 frame{3, 5, 10, 255};
    static constexpr Color32 dark{14, 10, 18, 255};
    static constexpr Color32 light{255, 146, 45, 255};
    static constexpr Color32 letter{246, 252, 255, 255};
    static constexpr Color32 shadow{48, 18, 6, 255};

    if (x == 0 || y == 0 || x == 15 || y == 15) {
        return frame;
    }

    if (p_mask[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 'P') {
        return letter;
    }

    if (x > 0 && y > 0 && p_mask[static_cast<std::size_t>(y - 1)][static_cast<std::size_t>(x - 1)] == 'P') {
        return shadow;
    }

    const float gradient = (static_cast<float>(x) + static_cast<float>(y) * 0.45F) / 21.75F;
    const float threshold = (static_cast<float>(bayer4[static_cast<std::size_t>(y & 3)][static_cast<std::size_t>(x & 3)]) + 0.5F) / 16.0F;
    return gradient > threshold ? light : dark;
}

std::array<float, 3> color_to_rgb_floats(Color32 color)
{
    return {
        color.r / 255.0F,
        color.g / 255.0F,
        color.b / 255.0F,
    };
}

ImVec4 color_to_imgui(Color32 color)
{
    return ImVec4(color.r / 255.0F, color.g / 255.0F, color.b / 255.0F, 1.0F);
}

Color32 color_from_rgb_floats(const std::array<float, 3>& color)
{
    return {
        static_cast<std::uint8_t>(std::lround(std::clamp(color[0], 0.0F, 1.0F) * 255.0F)),
        static_cast<std::uint8_t>(std::lround(std::clamp(color[1], 0.0F, 1.0F) * 255.0F)),
        static_cast<std::uint8_t>(std::lround(std::clamp(color[2], 0.0F, 1.0F) * 255.0F)),
        255,
    };
}

void draw_transparency_swatch(const char* id, ImVec2 size)
{
    ImGui::PushID(id);
    ImGui::Dummy(size);
    ImGui::PopID();

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, max, IM_COL32(255, 255, 255, 255), 1.5F);
    draw_list->AddRect(min, max, IM_COL32(0, 0, 0, 140), 1.5F);
    draw_list->AddLine(
        ImVec2(min.x + 2.0F, max.y - 2.0F),
        ImVec2(max.x - 2.0F, min.y + 2.0F),
        IM_COL32(218, 31, 45, 255),
        2.0F);
}

ImU32 rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return IM_COL32(r, g, b, 255);
}

void draw_flag_border(ImDrawList* draw_list, ImVec2 min, ImVec2 max)
{
    draw_list->AddRect(min, max, IM_COL32(0, 0, 0, 110), 1.5F);
}

void draw_horizontal_stripes(ImDrawList* draw_list, ImVec2 min, ImVec2 size, std::initializer_list<ImU32> colors)
{
    const float stripe_height = size.y / static_cast<float>(colors.size());
    int index = 0;
    for (ImU32 color : colors) {
        const float y0 = min.y + stripe_height * static_cast<float>(index);
        const float y1 = index + 1 == static_cast<int>(colors.size()) ? min.y + size.y : y0 + stripe_height;
        draw_list->AddRectFilled(ImVec2(min.x, y0), ImVec2(min.x + size.x, y1), color, 1.5F);
        ++index;
    }
}

void draw_vertical_stripes(ImDrawList* draw_list, ImVec2 min, ImVec2 size, std::initializer_list<ImU32> colors)
{
    const float stripe_width = size.x / static_cast<float>(colors.size());
    int index = 0;
    for (ImU32 color : colors) {
        const float x0 = min.x + stripe_width * static_cast<float>(index);
        const float x1 = index + 1 == static_cast<int>(colors.size()) ? min.x + size.x : x0 + stripe_width;
        draw_list->AddRectFilled(ImVec2(x0, min.y), ImVec2(x1, min.y + size.y), color, 1.5F);
        ++index;
    }
}

void draw_nordic_cross(ImDrawList* draw_list, ImVec2 min, ImVec2 size, ImU32 base, ImU32 outer, ImU32 inner = 0)
{
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw_list->AddRectFilled(min, max, base, 1.5F);
    const float vertical_x = min.x + size.x * 0.38F;
    const float outer_w = size.x * 0.18F;
    const float outer_h = size.y * 0.28F;
    draw_list->AddRectFilled(ImVec2(vertical_x - outer_w * 0.5F, min.y), ImVec2(vertical_x + outer_w * 0.5F, max.y), outer);
    draw_list->AddRectFilled(ImVec2(min.x, min.y + size.y * 0.5F - outer_h * 0.5F), ImVec2(max.x, min.y + size.y * 0.5F + outer_h * 0.5F), outer);
    if (inner != 0) {
        const float inner_w = outer_w * 0.48F;
        const float inner_h = outer_h * 0.48F;
        draw_list->AddRectFilled(ImVec2(vertical_x - inner_w * 0.5F, min.y), ImVec2(vertical_x + inner_w * 0.5F, max.y), inner);
        draw_list->AddRectFilled(ImVec2(min.x, min.y + size.y * 0.5F - inner_h * 0.5F), ImVec2(max.x, min.y + size.y * 0.5F + inner_h * 0.5F), inner);
    }
}

void draw_language_flag(Language language, ImVec2 min, ImVec2 size)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const ImU32 white = rgb(255, 255, 255);
    const ImU32 black = rgb(26, 26, 26);
    const ImU32 red = rgb(206, 17, 38);
    const ImU32 yellow = rgb(255, 206, 0);

    switch (language) {
    case Language::English:
        draw_list->AddRectFilled(min, max, rgb(1, 33, 105), 1.5F);
        draw_list->AddLine(min, max, white, 4.0F);
        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(min.x, max.y), white, 4.0F);
        draw_list->AddLine(min, max, red, 2.0F);
        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(min.x, max.y), red, 2.0F);
        draw_list->AddRectFilled(ImVec2(min.x + size.x * 0.42F, min.y), ImVec2(min.x + size.x * 0.58F, max.y), white);
        draw_list->AddRectFilled(ImVec2(min.x, min.y + size.y * 0.38F), ImVec2(max.x, min.y + size.y * 0.62F), white);
        draw_list->AddRectFilled(ImVec2(min.x + size.x * 0.46F, min.y), ImVec2(min.x + size.x * 0.54F, max.y), red);
        draw_list->AddRectFilled(ImVec2(min.x, min.y + size.y * 0.44F), ImVec2(max.x, min.y + size.y * 0.56F), red);
        break;
    case Language::Spanish:
        draw_horizontal_stripes(draw_list, min, size, {red, yellow, red});
        break;
    case Language::French:
        draw_vertical_stripes(draw_list, min, size, {rgb(0, 35, 149), white, red});
        break;
    case Language::German:
        draw_horizontal_stripes(draw_list, min, size, {black, rgb(221, 0, 0), rgb(255, 206, 0)});
        break;
    case Language::Danish:
        draw_nordic_cross(draw_list, min, size, rgb(198, 12, 48), white);
        break;
    case Language::Swedish:
        draw_nordic_cross(draw_list, min, size, rgb(0, 106, 167), rgb(254, 204, 0));
        break;
    case Language::Norwegian:
        draw_nordic_cross(draw_list, min, size, rgb(186, 12, 47), white, rgb(0, 32, 91));
        break;
    case Language::Czech:
        draw_horizontal_stripes(draw_list, min, size, {white, rgb(215, 20, 26)});
        draw_list->AddTriangleFilled(min, ImVec2(min.x, max.y), ImVec2(min.x + size.x * 0.48F, min.y + size.y * 0.5F), rgb(17, 69, 126));
        break;
    case Language::Italian:
        draw_vertical_stripes(draw_list, min, size, {rgb(0, 146, 70), white, rgb(206, 43, 55)});
        break;
    case Language::Greek:
        for (int i = 0; i < 9; ++i) {
            const float y0 = min.y + size.y * static_cast<float>(i) / 9.0F;
            const float y1 = min.y + size.y * static_cast<float>(i + 1) / 9.0F;
            draw_list->AddRectFilled(ImVec2(min.x, y0), ImVec2(max.x, y1), (i % 2 == 0) ? rgb(13, 94, 175) : white);
        }
        draw_list->AddRectFilled(min, ImVec2(min.x + size.y * 0.56F, min.y + size.y * 0.56F), rgb(13, 94, 175));
        draw_list->AddRectFilled(ImVec2(min.x + size.y * 0.22F, min.y), ImVec2(min.x + size.y * 0.34F, min.y + size.y * 0.56F), white);
        draw_list->AddRectFilled(ImVec2(min.x, min.y + size.y * 0.22F), ImVec2(min.x + size.y * 0.56F, min.y + size.y * 0.34F), white);
        break;
    case Language::Polish:
        draw_horizontal_stripes(draw_list, min, size, {white, rgb(220, 20, 60)});
        break;
    case Language::Finnish:
        draw_nordic_cross(draw_list, min, size, white, rgb(0, 53, 128));
        break;
    case Language::Ukrainian:
        draw_horizontal_stripes(draw_list, min, size, {rgb(0, 87, 183), rgb(255, 215, 0)});
        break;
    case Language::Russian:
        draw_horizontal_stripes(draw_list, min, size, {white, rgb(0, 57, 166), rgb(213, 43, 30)});
        break;
    case Language::ChineseSimplified:
        draw_list->AddRectFilled(min, max, rgb(222, 41, 16), 1.5F);
        draw_list->AddCircleFilled(ImVec2(min.x + size.x * 0.25F, min.y + size.y * 0.35F), size.y * 0.18F, rgb(255, 222, 0), 12);
        break;
    case Language::ChineseTraditional:
        draw_list->AddRectFilled(min, max, rgb(254, 0, 0), 1.5F);
        draw_list->AddRectFilled(min, ImVec2(min.x + size.x * 0.55F, min.y + size.y * 0.52F), rgb(0, 0, 149));
        draw_list->AddCircleFilled(ImVec2(min.x + size.x * 0.27F, min.y + size.y * 0.26F), size.y * 0.13F, white, 12);
        break;
    case Language::Korean:
        draw_list->AddRectFilled(min, max, white, 1.5F);
        draw_list->AddCircleFilled(ImVec2(min.x + size.x * 0.5F, min.y + size.y * 0.5F), size.y * 0.18F, rgb(205, 46, 58), 16);
        draw_list->AddCircleFilled(ImVec2(min.x + size.x * 0.5F, min.y + size.y * 0.58F), size.y * 0.18F, rgb(0, 71, 160), 16);
        draw_list->AddRectFilled(ImVec2(min.x + size.x * 0.18F, min.y + size.y * 0.22F), ImVec2(min.x + size.x * 0.36F, min.y + size.y * 0.28F), black);
        draw_list->AddRectFilled(ImVec2(min.x + size.x * 0.64F, min.y + size.y * 0.72F), ImVec2(min.x + size.x * 0.82F, min.y + size.y * 0.78F), black);
        break;
    case Language::Japanese:
        draw_list->AddRectFilled(min, max, white, 1.5F);
        draw_list->AddCircleFilled(ImVec2(min.x + size.x * 0.5F, min.y + size.y * 0.5F), size.y * 0.24F, rgb(188, 0, 45), 20);
        break;
    case Language::Count:
        draw_list->AddRectFilled(min, max, rgb(80, 80, 80), 1.5F);
        break;
    }

    draw_flag_border(draw_list, min, max);
}

float language_button_width(Language language)
{
    const LanguageDefinition& definition = language_definition(language);
    return ImGui::CalcTextSize(definition.native_name).x + 54.0F;
}

constexpr float kLanguageOptionRowHeight = 30.0F;

float language_option_width()
{
    float widest_name = 0.0F;
    for (const LanguageDefinition& definition : language_definitions()) {
        widest_name = std::max(widest_name, ImGui::CalcTextSize(definition.native_name).x);
    }
    return widest_name + 48.0F;
}

bool render_language_button(Language language, float width)
{
    const LanguageDefinition& definition = language_definition(language);
    const bool pressed = ImGui::Button("##LanguageButton", ImVec2(width, 0.0F));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float frame_height = max.y - min.y;
    const ImVec2 flag_size(24.0F, 16.0F);
    const ImVec2 flag_min(min.x + 8.0F, min.y + (frame_height - flag_size.y) * 0.5F);
    draw_language_flag(language, flag_min, flag_size);

    const ImVec2 text_size = ImGui::CalcTextSize(definition.native_name);
    const ImVec2 text_pos(flag_min.x + flag_size.x + 8.0F, min.y + (frame_height - text_size.y) * 0.5F);
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), definition.native_name);
    return pressed;
}

bool render_language_option(Language language, bool selected, float width)
{
    const LanguageDefinition& definition = language_definition(language);
    ImGui::PushID(static_cast<int>(language));
    const bool pressed = ImGui::Selectable(
        "##LanguageOption",
        selected,
        ImGuiSelectableFlags_None,
        ImVec2(width, kLanguageOptionRowHeight));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 flag_size(24.0F, 16.0F);
    const ImVec2 flag_min(min.x + 8.0F, min.y + (kLanguageOptionRowHeight - flag_size.y) * 0.5F);
    draw_language_flag(language, flag_min, flag_size);

    const ImVec2 text_size = ImGui::CalcTextSize(definition.native_name);
    const ImVec2 text_pos(flag_min.x + flag_size.x + 8.0F, min.y + (kLanguageOptionRowHeight - text_size.y) * 0.5F);
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), definition.native_name);
    ImGui::PopID();
    return pressed;
}

bool add_font_from_candidates(const std::vector<std::filesystem::path>& candidates, float size, ImFontConfig* config, const ImWchar* ranges)
{
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) {
            continue;
        }
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(candidate.string().c_str(), size, config, ranges)) {
            return true;
        }
    }
    return false;
}

void set_window_icon(SDL_Window* window)
{
    static constexpr int kIconSourceSize = 16;
    static constexpr int kWindowIconSize = 32;
    if (!window) {
        return;
    }

    SDL_Surface* icon = SDL_CreateSurface(kWindowIconSize, kWindowIconSize, SDL_PIXELFORMAT_RGBA32);
    if (!icon) {
        return;
    }

    for (int y = 0; y < kWindowIconSize; ++y) {
        for (int x = 0; x < kWindowIconSize; ++x) {
            const Color32 color = icon_color_at(x * kIconSourceSize / kWindowIconSize, y * kIconSourceSize / kWindowIconSize);
            SDL_WriteSurfacePixel(icon, x, y, color.r, color.g, color.b, color.a);
        }
    }

    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
}

} // namespace

App::App()
{
    status_ = text(TextId::StatusOpenImageToBegin);
}

App::~App()
{
    shutdown();
}

bool App::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        set_status(textf(TextId::StatusSdlInitFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }

    window_ = SDL_CreateWindow("Pixelizer", kInitialWidth, kInitialHeight, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        set_status(textf(TextId::StatusSdlCreateWindowFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    set_window_icon(window_);

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        set_status(textf(TextId::StatusSdlCreateRendererFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.ChildRounding = 4.0F;
    style.GrabRounding = 4.0F;

    configure_fonts();
    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    refresh_palettes();
    return true;
}

int App::run()
{
    running_ = true;
    while (running_) {
        process_events(running_);
        drain_file_commands();
        update_preview_if_needed();
        render_frame();
    }

    return 0;
}

void App::shutdown()
{
    destroy_texture(original_texture_);
    destroy_texture(result_texture_);

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

void App::process_events(bool& running)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)) {
            running = false;
        }
        if (!file_commands_.dialog_open() && event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            file_commands_.submit_drop(event.drop.data, !original_.empty());
        }
    }
}

void App::render_frame()
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    handle_shortcuts();
    render_menu_bar();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menu_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + menu_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - menu_height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool disable_workspace = file_commands_.dialog_open();
    ImGui::Begin("Pixelizer Workspace", nullptr, flags);
    if (disable_workspace) {
        ImGui::BeginDisabled();
    }

    const float control_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.24F, 300.0F, 390.0F);
    ImGui::BeginChild("Controls", ImVec2(control_width, 0), ImGuiChildFlags_Borders);
    render_controls();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Views", ImVec2(0, 0), ImGuiChildFlags_None);
    render_viewports();
    ImGui::EndChild();

    if (disable_workspace) {
        ImGui::EndDisabled();
    }
    ImGui::End();

    render_number_edit_popup();
    render_drop_confirm_popup();
    render_delete_palette_popup();
    render_palette_import_conflict_popup();
    render_palette_import_name_popup();
    render_palette_color_popup();
    render_save_palette_popup();
    render_language_picker_popup();
    render_about_dialog();

    ImGui::Render();

    SDL_SetRenderDrawColor(renderer_, 22, 24, 28, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

void App::render_menu_bar()
{
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    const bool disable_menu = file_commands_.dialog_open();
    if (disable_menu) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button(imgui_label(TextId::OpenImage, "OpenImage").c_str())) {
        request_open_image();
    }
    ImGui::SameLine();
    if (ImGui::Button(imgui_label(TextId::ImportPalette, "ImportPalette").c_str())) {
        request_import_palette();
    }
    ImGui::SameLine();
    if (ImGui::Button(imgui_label(TextId::ExportPng, "ExportPng").c_str())) {
        request_export_png();
    }
    ImGui::SameLine();
    const bool single_viewport = viewport_mode_ == ViewportMode::Single;
    if (ImGui::Button(imgui_label(single_viewport ? TextId::TwoViews : TextId::OneView, "ViewportMode").c_str())) {
        viewport_mode_ = single_viewport ? ViewportMode::Split : ViewportMode::Single;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(single_viewport ? TextId::ShowOriginalAndResult : TextId::ShowOnlyResult));
    }
    if (viewport_mode_ == ViewportMode::Split) {
        ImGui::SameLine();
        const bool side_by_side = viewport_layout_ == ViewportLayout::SideBySide;
        if (ImGui::Button(imgui_label(side_by_side ? TextId::StackViews : TextId::SideBySide, "ViewportLayout").c_str())) {
            viewport_layout_ = viewport_layout_ == ViewportLayout::SideBySide ? ViewportLayout::Stacked : ViewportLayout::SideBySide;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", text(side_by_side ? TextId::ShowResultTop : TextId::ShowOriginalLeft));
        }
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float language_width = language_button_width(language_);
    const float about_width = std::max(
        ImGui::GetFrameHeight(),
        ImGui::CalcTextSize(text(TextId::AboutButtonLabel)).x + style.FramePadding.x * 2.0F);
    const float right_controls_width = language_width + style.ItemSpacing.x + about_width;
    const float right_controls_x = std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowWidth() - right_controls_width - style.WindowPadding.x);
    const float status_width = right_controls_x - ImGui::GetCursorPosX() - style.ItemSpacing.x;
    if (status_width > 24.0F) {
        const ImVec2 status_min = ImGui::GetCursorScreenPos();
        const ImVec2 status_max(status_min.x + status_width, status_min.y + ImGui::GetFrameHeight());
        const ImVec4 clip_rect(status_min.x, status_min.y, status_max.x, status_max.y);
        ImGui::InvisibleButton("##StatusText", ImVec2(status_width, ImGui::GetFrameHeight()));
        ImGui::GetWindowDrawList()->AddText(
            nullptr,
            0.0F,
            ImVec2(status_min.x, status_min.y + ImGui::GetStyle().FramePadding.y),
            ImGui::GetColorU32(ImGuiCol_Text),
            status_.c_str(),
            nullptr,
            0.0F,
            &clip_rect);
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(right_controls_x);
    if (render_language_button(language_, language_width)) {
        open_language_picker_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(imgui_label(TextId::AboutButtonLabel, "AboutButton").c_str(), ImVec2(about_width, 0.0F))) {
        open_about_dialog_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(TextId::AboutButtonTooltip));
    }

    if (disable_menu) {
        ImGui::EndDisabled();
    }
    ImGui::EndMainMenuBar();
}

void App::render_language_picker_popup()
{
    const std::string popup_id = imgui_label(TextId::LanguageWindowTitle, "LanguagePicker");

    if (open_language_picker_) {
        ImGui::OpenPopup(popup_id.c_str());
        open_language_picker_ = false;
    }

    const float option_width = language_option_width();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float side_padding = 24.0F;
    const float popup_width = std::max(300.0F, option_width * 2.0F + style.ItemSpacing.x + side_padding * 2.0F + 18.0F);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popup_height = std::min(390.0F, std::max(260.0F, viewport->WorkSize.y - 24.0F));
    ImGui::SetNextWindowSize(ImVec2(popup_width, popup_height), ImGuiCond_Always);

    bool popup_open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(side_padding, style.WindowPadding.y));
    if (!ImGui::BeginPopupModal(
            popup_id.c_str(),
            &popup_open,
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TextUnformatted(text(TextId::ChooseLanguage));
    ImGui::Separator();

    const auto& languages = language_definitions();
    constexpr std::size_t kLanguageColumnCount = 2;
    const std::size_t rows_per_column = (languages.size() + kLanguageColumnCount - 1U) / kLanguageColumnCount;
    const float list_width = option_width * 2.0F + style.ItemSpacing.x;
    const float full_list_height = static_cast<float>(rows_per_column) * kLanguageOptionRowHeight;
    const float close_button_area = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    const float list_height = std::min(full_list_height, std::max(kLanguageOptionRowHeight * 4.0F, ImGui::GetContentRegionAvail().y - close_button_area));

    ImGui::BeginChild("##LanguageList", ImVec2(list_width, list_height), ImGuiChildFlags_None);
    for (std::size_t column = 0; column < kLanguageColumnCount; ++column) {
        if (column > 0) {
            ImGui::SameLine(0.0F, style.ItemSpacing.x);
        }

        ImGui::BeginGroup();
        for (std::size_t row = 0; row < rows_per_column; ++row) {
            const std::size_t index = row + column * rows_per_column;
            if (index >= languages.size()) {
                break;
            }

            const LanguageDefinition& definition = languages[index];
            if (render_language_option(definition.language, definition.language == language_, option_width)) {
                language_ = definition.language;
                set_status(textf(TextId::StatusLanguageChangedFormat, {{"language", definition.native_name}}));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(imgui_label(TextId::Close, "CloseLanguagePicker").c_str()) || !popup_open) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

void App::render_about_dialog()
{
    const std::string popup_id = imgui_label(TextId::AboutWindowTitle, "AboutDialog");

    if (open_about_dialog_) {
        ImGui::OpenPopup(popup_id.c_str());
        open_about_dialog_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popup_width = std::min(620.0F, std::max(340.0F, viewport->WorkSize.x - 32.0F));
    const float popup_height = std::min(560.0F, std::max(360.0F, viewport->WorkSize.y - 32.0F));
    ImGui::SetNextWindowSize(ImVec2(popup_width, popup_height), ImGuiCond_Appearing);

    bool popup_open = true;
    if (!ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float close_button_area = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    const float content_height = std::max(180.0F, ImGui::GetContentRegionAvail().y - close_button_area);
    auto wrapped_bullet = [](const char* value) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", value);
    };

    ImGui::BeginChild("##AboutContent", ImVec2(0.0F, content_height), ImGuiChildFlags_None);
    ImGui::TextUnformatted("Pixelizer");
    ImGui::Separator();
    ImGui::TextWrapped("%s", text(TextId::AboutProjectCredit));
    ImGui::TextWrapped("%s", text(TextId::AboutProjectLicense));

    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::AboutThirdPartyTitle));
    ImGui::Separator();
    ImGui::TextUnformatted(text(TextId::AboutDependenciesTitle));
    wrapped_bullet(text(TextId::AboutDependencySdl));
    wrapped_bullet(text(TextId::AboutDependencyImGui));
    wrapped_bullet(text(TextId::AboutDependencyStb));

    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::AboutPalettesTitle));
    ImGui::TextWrapped("%s", text(TextId::AboutPalettesCredit));
    for (const char* palette : kLospecPaletteCredits) {
        wrapped_bullet(palette);
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", text(TextId::AboutAssetsCredit));
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(imgui_label(TextId::Close, "CloseAboutDialog").c_str()) || !popup_open) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void App::render_controls()
{
    auto edit = edit_session_.begin_edit();
    ProcessSettings& settings = edit.settings();
    int& selected_palette = edit.selected_palette();

    ImGui::TextUnformatted(text(TextId::Pixelize));
    ImGui::Separator();

    slider_int_direct(TextId::PixelSize, "PixelSize", settings.pixel_size, 1, 128);

    if (ImGui::BeginCombo(imgui_label(TextId::BlockSample, "BlockSample").c_str(), text(block_mode_label(settings.block_color_mode)))) {
        for (BlockColorMode mode : {BlockColorMode::WeightedAverage, BlockColorMode::Average}) {
            const bool selected = settings.block_color_mode == mode;
            if (ImGui::Selectable(text(block_mode_label(mode)), selected)) {
                settings.block_color_mode = mode;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float use_palette_width = ImGui::GetFrameHeight()
        + style.ItemInnerSpacing.x
        + ImGui::CalcTextSize(text(TextId::UsePalette)).x;
    const float preserve_transparency_width = ImGui::GetFrameHeight()
        + style.ItemInnerSpacing.x
        + ImGui::CalcTextSize(text(TextId::PreserveTransparency)).x;
    const bool place_transparency_same_line = use_palette_width
        + style.ItemSpacing.x
        + preserve_transparency_width
        <= ImGui::GetContentRegionAvail().x;

    ImGui::Checkbox(imgui_label(TextId::UsePalette, "UsePalette").c_str(), &settings.use_palette);
    if (place_transparency_same_line) {
        ImGui::SameLine();
    }
    ImGui::Checkbox(imgui_label(TextId::PreserveTransparency, "PreserveTransparency").c_str(), &settings.preserve_transparency);

    if (settings.use_palette) {
        if (selected_palette >= static_cast<int>(palettes_.size())) {
            selected_palette = -1;
        }
        if (!palettes_.empty() && selected_palette < 0 && settings.palette.empty()) {
            selected_palette = 0;
            settings.palette = palettes_[0].colors;
        }

        const bool has_saved_selection = selected_palette >= 0 && selected_palette < static_cast<int>(palettes_.size());
        if (palettes_.empty()) {
            ImGui::TextDisabled("%s", text(TextId::NoPalettesSaved));
        } else {
            const char* preview = has_saved_selection ? palettes_[static_cast<std::size_t>(selected_palette)].name.c_str() : text(TextId::UnsavedPalette);
            if (ImGui::BeginCombo(imgui_label(TextId::Palette, "Palette").c_str(), preview)) {
                for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
                    const bool selected = selected_palette == i;
                    if (ImGui::Selectable(palettes_[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                        selected_palette = i;
                        settings.palette = palettes_[static_cast<std::size_t>(i)].colors;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::Button(imgui_label(TextId::NewPalette, "NewPalette").c_str())) {
            request_new_palette();
        }
        ImGui::SameLine();

        const bool can_save = has_saved_selection && !settings.palette.empty();
        if (!can_save) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(imgui_label(TextId::Save, "SavePalette").c_str())) {
            request_save_palette();
        }
        if (!can_save) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();

        const bool can_save_new = !settings.palette.empty();
        if (!can_save_new) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(imgui_label(TextId::SaveNew, "SaveNewPalette").c_str())) {
            request_save_palette_as();
        }
        if (!can_save_new) {
            ImGui::EndDisabled();
        }

        if (has_saved_selection) {
            if (ImGui::Button(imgui_label(TextId::DeletePalette, "DeletePalette").c_str())) {
                request_delete_selected_palette();
            }
        }

        ImGui::Spacing();
        const bool show_transparency_swatch = settings.preserve_transparency;
        const std::size_t displayed_palette_count = settings.palette.size() + (show_transparency_swatch ? 1U : 0U);
        ImGui::Text(text(TextId::PaletteCountFormat), displayed_palette_count, kMaxPaletteColors);
        if (show_transparency_swatch) {
            ImGui::SameLine();
            ImGui::TextUnformatted("(*)");
        }
        if (settings.palette.empty()) {
            ImGui::TextDisabled("%s", text(TextId::AddColorToBegin));
        }

        const float swatch = 16.0F;
        const float start_x = ImGui::GetCursorScreenPos().x;
        const float max_x = start_x + ImGui::GetContentRegionAvail().x;
        const bool can_add_palette_color = settings.palette.size() < kMaxPaletteColors;
        const auto continue_palette_swatch_row = [&](bool has_more) {
            if (has_more && ImGui::GetItemRectMax().x + swatch + ImGui::GetStyle().ItemSpacing.x < max_x) {
                ImGui::SameLine();
            }
        };
        for (std::size_t color_index = 0; color_index < settings.palette.size(); ++color_index) {
            const Color32 color = settings.palette[color_index];
            ImGui::PushID(static_cast<int>(color_index));
            if (ImGui::ColorButton("swatch", color_to_imgui(color), ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) {
                request_edit_palette_color(color_index);
            }
            if (ImGui::IsItemHovered()) {
                const std::string tooltip = textf(TextId::EditColorFormat, {{"index", std::to_string(color_index + 1U)}});
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            ImGui::PopID();
            const bool has_more = color_index + 1U < settings.palette.size() || show_transparency_swatch || can_add_palette_color;
            continue_palette_swatch_row(has_more);
        }

        if (show_transparency_swatch) {
            draw_transparency_swatch("TransparencySwatch", ImVec2(swatch, swatch));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", text(TextId::PreserveTransparency));
            }
            continue_palette_swatch_row(can_add_palette_color);
        }

        if (can_add_palette_color) {
            if (ImGui::Button("+##AddPaletteColor", ImVec2(swatch, swatch))) {
                request_add_palette_color();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", text(TextId::AddColor));
            }
        }
    } else {
        slider_int_direct(TextId::MaxColors, "MaxColors", settings.reduction_max_colors, 0, 256);
        if (settings.reduction_max_colors == 0) {
            slider_int_direct(TextId::ColorLevels, "ColorLevels", settings.color_levels, 2, 64);
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::Dithering));
    ImGui::Separator();
    if (ImGui::BeginCombo(imgui_label(TextId::Mode, "DitherMode").c_str(), text(dither_label(settings.dither_mode)))) {
        for (DitherMode mode : {
                 DitherMode::None,
                 DitherMode::Bayer,
                 DitherMode::BlueNoise,
                 DitherMode::FloydSteinberg,
                 DitherMode::FalseFloydSteinberg,
                 DitherMode::FilterLite,
                 DitherMode::ZhigangFan,
                 DitherMode::ShiauFan,
                 DitherMode::JarvisJudiceNinke,
                 DitherMode::Atkinson,
                 DitherMode::Stucki,
                 DitherMode::Burkes,
                 DitherMode::Sierra,
                 DitherMode::TwoRowSierra,
                 DitherMode::Riemersma,
                 DitherMode::ClusterDot4x4,
                 DitherMode::ClusterDot8x8,
                 DitherMode::Horizontal2x2,
                 DitherMode::Horizontal8x1,
                 DitherMode::Horizontal12x4,
                 DitherMode::Vertical2x2,
                 DitherMode::Vertical1x8,
                 DitherMode::Vertical4x12,
                 DitherMode::Diagonal5x5,
            }) {
            const bool selected = settings.dither_mode == mode;
            if (ImGui::Selectable(text(dither_label(mode)), selected)) {
                settings.dither_mode = mode;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (settings.dither_mode == DitherMode::Bayer) {
        if (ImGui::BeginCombo(imgui_label(TextId::Pattern, "BayerPattern").c_str(), bayer_pattern_label(settings.bayer_matrix_size))) {
            for (int size : {2, 4, 8, 16}) {
                const bool selected = settings.bayer_matrix_size == size;
                if (ImGui::Selectable(bayer_pattern_label(size), selected)) {
                    settings.bayer_matrix_size = size;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    float dither_percent = settings.dither_amount * 100.0F;
    float* dither_amount = &settings.dither_amount;
    slider_float_direct_value(
        TextId::Amount,
        "DitherAmount",
        dither_percent,
        0.0F,
        100.0F,
        "%.0f%%",
        [dither_amount](float value) {
            *dither_amount = value / 100.0F;
        });

    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::Adjustments));
    ImGui::Separator();
    slider_float_direct(TextId::Brightness, "Brightness", settings.adjustments.brightness, -1.0F, 1.0F, "%.2f");
    slider_float_direct(TextId::Contrast, "Contrast", settings.adjustments.contrast, -1.0F, 1.0F, "%.2f");
    slider_float_direct(TextId::Gamma, "Gamma", settings.adjustments.gamma, 0.1F, 4.0F, "%.2f");
    slider_float_direct(TextId::Saturation, "Saturation", settings.adjustments.saturation, 0.0F, 2.5F, "%.2f");
    if (slider_float_direct(TextId::InputBlack, "InputBlack", settings.adjustments.input_black, 0.0F, 0.95F, "%.2f")) {
        settings.adjustments.input_black = std::min(settings.adjustments.input_black, settings.adjustments.input_white - 0.01F);
    }
    if (slider_float_direct(TextId::InputWhite, "InputWhite", settings.adjustments.input_white, 0.05F, 1.0F, "%.2f")) {
        settings.adjustments.input_white = std::max(settings.adjustments.input_white, settings.adjustments.input_black + 0.01F);
    }
    if (slider_float_direct(TextId::OutputBlack, "OutputBlack", settings.adjustments.output_black, 0.0F, 0.95F, "%.2f")) {
        settings.adjustments.output_black = std::min(settings.adjustments.output_black, settings.adjustments.output_white - 0.01F);
    }
    if (slider_float_direct(TextId::OutputWhite, "OutputWhite", settings.adjustments.output_white, 0.05F, 1.0F, "%.2f")) {
        settings.adjustments.output_white = std::max(settings.adjustments.output_white, settings.adjustments.output_black + 0.01F);
    }

    float tint[3] = {
        settings.adjustments.tint.r / 255.0F,
        settings.adjustments.tint.g / 255.0F,
        settings.adjustments.tint.b / 255.0F,
    };
    if (ImGui::ColorEdit3(imgui_label(TextId::Tint, "Tint").c_str(), tint, ImGuiColorEditFlags_NoInputs)) {
        settings.adjustments.tint.r = static_cast<std::uint8_t>(std::lround(std::clamp(tint[0], 0.0F, 1.0F) * 255.0F));
        settings.adjustments.tint.g = static_cast<std::uint8_t>(std::lround(std::clamp(tint[1], 0.0F, 1.0F) * 255.0F));
        settings.adjustments.tint.b = static_cast<std::uint8_t>(std::lround(std::clamp(tint[2], 0.0F, 1.0F) * 255.0F));
    }
    slider_float_direct(TextId::TintStrength, "TintStrength", settings.adjustments.tint_strength, 0.0F, 1.0F, "%.2f");

    ImGui::Spacing();
    if (ImGui::Button(imgui_label(TextId::ResetAdjustments, "ResetAdjustments").c_str())) {
        settings.adjustments = {};
    }

    record_control_history(edit.before());
}

void App::render_viewports()
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x <= 0.0F || available.y <= 0.0F) {
        return;
    }

    if (viewport_mode_ == ViewportMode::Single) {
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view(TextId::Result, "Result", result_texture_, result_zoom_);
        ImGui::EndChild();
        return;
    }

    if (viewport_layout_ == ViewportLayout::SideBySide) {
        if (available.x <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_image_view(TextId::Original, "Original", original_texture_, original_zoom_);
            ImGui::EndChild();
            return;
        }

        const float splitter_width = splitter_thickness_for(available.x);
        const float usable_width = available.x - splitter_width;
        float original_width = split_size_from_ratio(viewport_split_ratio_, usable_width);
        viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);

        ImGui::BeginChild("OriginalPane", ImVec2(original_width, 0), ImGuiChildFlags_Borders);
        render_image_view(TextId::Original, "Original", original_texture_, original_zoom_);
        ImGui::EndChild();

        ImGui::SameLine(0.0F, 0.0F);
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterX", ImVec2(splitter_width, available.y), ImGuiMouseCursor_ResizeEW, delta)) {
            original_width = split_size_from_ratio(viewport_split_ratio_, usable_width) + delta;
            viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);
        }

        ImGui::SameLine(0.0F, 0.0F);
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view(TextId::Result, "Result", result_texture_, result_zoom_);
        ImGui::EndChild();
    } else {
        if (available.y <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_image_view(TextId::Result, "Result", result_texture_, result_zoom_);
            ImGui::EndChild();
            return;
        }

        const float splitter_height = splitter_thickness_for(available.y);
        const float usable_height = available.y - splitter_height;
        float result_height = split_size_from_ratio(viewport_split_ratio_, usable_height);
        viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);

        ImGui::BeginChild("ResultPane", ImVec2(0, result_height), ImGuiChildFlags_Borders);
        render_image_view(TextId::Result, "Result", result_texture_, result_zoom_);
        ImGui::EndChild();

        remove_vertical_item_spacing();
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterY", ImVec2(available.x, splitter_height), ImGuiMouseCursor_ResizeNS, delta)) {
            result_height = split_size_from_ratio(viewport_split_ratio_, usable_height) + delta;
            viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);
        }

        remove_vertical_item_spacing();
        ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view(TextId::Original, "Original", original_texture_, original_zoom_);
        ImGui::EndChild();
    }
}

void App::render_image_view(TextId label, const char* id, Texture& texture, float& zoom)
{
    const ImVec2 pane_available = ImGui::GetContentRegionAvail();

    ImGui::TextUnformatted(text(label));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    const std::string zoom_id = std::string("Zoom") + id;
    ImGui::SliderFloat(imgui_label(TextId::Zoom, zoom_id.c_str()).c_str(), &zoom, 0.05F, 32.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (ImGui::SmallButton(("1:1##OneToOne" + std::string(id)).c_str())) {
        zoom = 1.0F;
    }
    ImGui::SameLine();
    const std::string fit_id = std::string("Fit") + id;
    if (ImGui::SmallButton(imgui_label(TextId::Fit, fit_id.c_str()).c_str())) {
        zoom = fit_zoom_for_size(texture.width, texture.height, pane_available);
    }

    ImGui::Separator();

    ImGui::BeginChild((std::string(id) + "Scroll").c_str(), ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (!texture.handle) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (avail.x - 140.0F) * 0.5F), std::max(0.0F, (avail.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("%s", text(TextId::NoImageLoaded));
    } else {
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0F) {
            zoom = std::clamp(zoom * (ImGui::GetIO().MouseWheel > 0.0F ? 1.12F : 0.89F), 0.05F, 32.0F);
        }

        const ImVec2 size(static_cast<float>(texture.width) * zoom, static_cast<float>(texture.height) * zoom);
        ImGui::Image(reinterpret_cast<ImTextureID>(texture.handle), size);
    }
    ImGui::EndChild();
}

void App::render_number_edit_popup()
{
    const std::string popup_id = imgui_label(TextId::SetNumericValue, "SetNumericValue");

    if (number_edit_ && number_edit_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        number_edit_->request_open = false;
    }

    if (!number_edit_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(number_edit_->label.c_str());
        ImGui::SetNextItemWidth(220.0F);

        const bool submitted = ImGui::InputText(
            imgui_label(TextId::Value, "NumericValue").c_str(),
            number_edit_->input.data(),
            number_edit_->input.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);

        const bool apply = submitted || ImGui::Button(imgui_label(TextId::Apply, "ApplyNumericValue").c_str());
        ImGui::SameLine();
        const bool cancel = ImGui::Button(imgui_label(TextId::Cancel, "CancelNumericValue").c_str());

        if (apply) {
            double parsed = 0.0;
            if (!parse_number_edit_input(number_edit_->input.data(), number_edit_->integer, parsed)) {
                set_status(textf(TextId::InvalidValueFormat, {{"label", number_edit_->label}}));
                ImGui::EndPopup();
                return;
            }

            number_edit_->value = parsed;
            const double clamped = std::clamp(number_edit_->value, number_edit_->minimum, number_edit_->maximum);
            number_edit_->apply(clamped);
            normalize_settings();
            mark_dirty();
            commit_history_change(number_edit_->before);
            set_status(textf(TextId::SetValueFormat, {{"label", number_edit_->label}}));
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        } else if (cancel || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        number_edit_.reset();
    }
}

void App::render_drop_confirm_popup()
{
    const std::string popup_id = imgui_label(TextId::OpenDroppedImageTitle, "OpenDroppedImage");

    if (open_drop_confirm_) {
        ImGui::OpenPopup(popup_id.c_str());
        open_drop_confirm_ = false;
    }

    if (!pending_dropped_image_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(TextId::ReplaceDroppedImage));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", pending_dropped_image_->filename().string().c_str());

        if (ImGui::Button(imgui_label(TextId::Open, "OpenDroppedImageConfirm").c_str())) {
            const auto path = *pending_dropped_image_;
            pending_dropped_image_.reset();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
            load_image_from_path(path);
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelDroppedImage").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_dropped_image_.reset();
    }
}

void App::render_delete_palette_popup()
{
    const std::string popup_id = imgui_label(TextId::DeletePaletteTitle, "DeletePalettePopup");

    if (open_delete_palette_confirm_) {
        ImGui::OpenPopup(popup_id.c_str());
        open_delete_palette_confirm_ = false;
    }

    if (!pending_delete_palette_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(TextId::DeleteSavedPalette));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", pending_delete_palette_->name.c_str());

        if (ImGui::Button(imgui_label(TextId::Delete, "DeletePaletteConfirm").c_str())) {
            delete_pending_palette();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelDeletePalette").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_delete_palette_.reset();
    }
}

void App::render_palette_import_conflict_popup()
{
    const std::string popup_id = imgui_label(TextId::PaletteAlreadyExistsTitle, "PaletteAlreadyExists");

    const bool popup_active = ImGui::IsPopupOpen(popup_id.c_str());
    if (pending_palette_import_ && pending_palette_import_->request_conflict_open) {
        ImGui::OpenPopup(popup_id.c_str());
        pending_palette_import_->request_conflict_open = false;
    }

    if (!pending_palette_import_
        || (!pending_palette_import_->request_conflict_open && !popup_active && !ImGui::IsPopupOpen(popup_id.c_str()))) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string message = textf(TextId::PaletteAlreadyExistsFormat, {{"name", pending_palette_import_->parsed.name}});
        ImGui::TextWrapped("%s", message.c_str());

        if (ImGui::Button(imgui_label(TextId::Overwrite, "OverwritePalette").c_str())) {
            if (import_pending_palette(PaletteImportMode::Overwrite)) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::KeepBoth, "KeepBothPalettes").c_str())) {
            const std::string name = suggest_import_palette_copy_name(pending_palette_import_->source);
            std::snprintf(pending_palette_import_->name.data(), pending_palette_import_->name.size(), "%s", name.c_str());
            pending_palette_import_->request_name_open = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelPaletteConflict").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str()) && !pending_palette_import_->request_name_open) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_palette_import_.reset();
    }
}

void App::render_palette_import_name_popup()
{
    const std::string popup_id = imgui_label(TextId::NameImportedPaletteTitle, "NameImportedPalette");

    const bool popup_active = ImGui::IsPopupOpen(popup_id.c_str());
    if (pending_palette_import_ && pending_palette_import_->request_name_open) {
        ImGui::OpenPopup(popup_id.c_str());
        pending_palette_import_->request_name_open = false;
    }

    if (!pending_palette_import_
        || (!pending_palette_import_->request_name_open && !popup_active && !ImGui::IsPopupOpen(popup_id.c_str()))) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(TextId::PaletteName));
        ImGui::SetNextItemWidth(260.0F);
        const bool submitted = ImGui::InputText(
            "##ImportedPaletteName",
            pending_palette_import_->name.data(),
            pending_palette_import_->name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool save_clicked = ImGui::Button(imgui_label(TextId::Save, "SaveImportedPaletteName").c_str());
        if (submitted || save_clicked) {
            if (save_pending_palette_import_as_name(pending_palette_import_->name.data())) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelImportedPaletteName").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_palette_import_.reset();
    }
}

void App::render_palette_color_popup()
{
    const std::string popup_id = imgui_label(TextId::PaletteColorTitle, "PaletteColor");

    if (palette_color_edit_ && palette_color_edit_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        palette_color_edit_->request_open = false;
    }

    if (!palette_color_edit_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(palette_color_edit_->adding ? TextId::AddPaletteColor : TextId::EditPaletteColor));
        ImGui::ColorPicker3(
            "##PaletteColorPicker",
            palette_color_edit_->color.data(),
            ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB);

        const bool adding = palette_color_edit_->adding;
        if (ImGui::Button(imgui_label(adding ? TextId::Add : TextId::Apply, "ApplyPaletteColor").c_str())) {
            if (adding) {
                if (edit_session_.settings_for_edit().palette.size() >= kMaxPaletteColors) {
                    set_status(text(TextId::StatusPaletteFull));
                } else {
                    edit_session_.settings_for_edit().palette.push_back(color_from_rgb_floats(palette_color_edit_->color));
                    mark_dirty();
                    commit_history_change(palette_color_edit_->before);
                    set_status(text(TextId::StatusAddedColor));
                    ImGui::CloseCurrentPopup();
                    reset_popup = true;
                }
            } else {
                const int index = palette_color_edit_->index;
                if (index < 0 || index >= static_cast<int>(edit_session_.settings_for_edit().palette.size())) {
                    set_status(text(TextId::StatusPaletteColorMissing));
                } else {
                    edit_session_.settings_for_edit().palette[static_cast<std::size_t>(index)] = color_from_rgb_floats(palette_color_edit_->color);
                    mark_dirty();
                    commit_history_change(palette_color_edit_->before);
                    set_status(text(TextId::StatusUpdatedColor));
                    ImGui::CloseCurrentPopup();
                    reset_popup = true;
                }
            }
        }

        ImGui::SameLine();
        const bool can_delete = !palette_color_edit_->adding
            && palette_color_edit_->index >= 0
            && palette_color_edit_->index < static_cast<int>(edit_session_.settings_for_edit().palette.size());
        if (!can_delete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(imgui_label(TextId::DeleteColor, "DeletePaletteColor").c_str())) {
            edit_session_.settings_for_edit().palette.erase(edit_session_.settings_for_edit().palette.begin() + palette_color_edit_->index);
            mark_dirty();
            commit_history_change(palette_color_edit_->before);
            set_status(text(TextId::StatusDeletedColor));
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }
        if (!can_delete) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelPaletteColor").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        palette_color_edit_.reset();
    }
}

void App::render_save_palette_popup()
{
    const std::string popup_id = imgui_label(TextId::SavePaletteAsTitle, "SavePaletteAs");

    if (palette_save_as_ && palette_save_as_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        palette_save_as_->request_open = false;
    }

    if (!palette_save_as_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(TextId::PaletteName));
        ImGui::SetNextItemWidth(260.0F);
        const bool submitted = ImGui::InputText(
            "##PaletteName",
            palette_save_as_->name.data(),
            palette_save_as_->name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool save_clicked = ImGui::Button(imgui_label(TextId::Save, "SavePaletteAsName").c_str());
        if (submitted || save_clicked) {
            if (save_palette_as_name(palette_save_as_->name.data())) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelSavePaletteAs").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        palette_save_as_.reset();
    }
}

void App::handle_shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (file_commands_.dialog_open() || number_edit_ || palette_color_edit_ || palette_save_as_ || pending_palette_import_
        || io.WantTextInput || !io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        return;
    }

    if (io.KeyShift) {
        redo();
    } else {
        undo();
    }
}

void App::update_preview_if_needed()
{
    if (!edit_session_.preview_dirty() || original_.empty()) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    result_ = process_image(original_, edit_session_.settings());
    rebuild_texture(result_texture_, result_, true);
    edit_session_.clear_preview_dirty();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    status_ = textf(TextId::StatusPreviewUpdatedFormat, {{"ms", std::to_string(elapsed)}});
}

void App::rebuild_texture(Texture& texture, const Image& image, bool nearest)
{
    destroy_texture(texture);
    if (image.empty()) {
        return;
    }

    texture.handle = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
    texture.width = image.width;
    texture.height = image.height;

    if (!texture.handle) {
        set_status(textf(TextId::StatusSdlCreateTextureFailedFormat, {{"error", SDL_GetError()}}));
        return;
    }

    SDL_SetTextureScaleMode(texture.handle, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_UpdateTexture(texture.handle, nullptr, image.rgba.data(), image.width * 4);
}

void App::configure_fonts()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());

    for (const LanguageDefinition& definition : language_definitions()) {
        builder.AddText(definition.native_name);
        for (std::size_t id = 0; id < kTextCount; ++id) {
            builder.AddText(translate(definition.language, static_cast<TextId>(id)));
        }
    }

    static ImVector<ImWchar> ranges;
    ranges.clear();
    builder.BuildRanges(&ranges);

    io.Fonts->Clear();
    constexpr float kFontSize = 16.0F;
    const std::vector<std::filesystem::path> base_candidates = {
        R"(C:\Windows\Fonts\segoeui.ttf)",
        R"(C:\Windows\Fonts\arial.ttf)",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    const bool loaded_base = add_font_from_candidates(base_candidates, kFontSize, nullptr, ranges.Data);
    if (!loaded_base) {
        io.Fonts->AddFontDefault();
    }

    ImFontConfig merge_config;
    merge_config.MergeMode = true;
    merge_config.PixelSnapH = true;

    const std::vector<std::filesystem::path> fallback_candidates = {
        R"(C:\Windows\Fonts\seguisym.ttf)",
        R"(C:\Windows\Fonts\msyh.ttc)",
        R"(C:\Windows\Fonts\msyh.ttf)",
        R"(C:\Windows\Fonts\msjh.ttc)",
        R"(C:\Windows\Fonts\YuGothM.ttc)",
        R"(C:\Windows\Fonts\YuGothR.ttc)",
        R"(C:\Windows\Fonts\meiryo.ttc)",
        R"(C:\Windows\Fonts\malgun.ttf)",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKkr-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/truetype/unifont/unifont.ttf",
    };

    for (const std::filesystem::path& candidate : fallback_candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) {
            continue;
        }
        io.Fonts->AddFontFromFileTTF(candidate.string().c_str(), kFontSize, &merge_config, ranges.Data);
    }
}

void App::destroy_texture(Texture& texture)
{
    if (texture.handle) {
        SDL_DestroyTexture(texture.handle);
        texture.handle = nullptr;
    }
    texture.width = 0;
    texture.height = 0;
}

void App::request_open_image()
{
    (void)file_commands_.request_open_image_dialog(window_, file_dialog_labels());
}

void App::request_import_palette()
{
    (void)file_commands_.request_import_palette_dialog(window_, file_dialog_labels());
}

void App::request_export_png()
{
    (void)file_commands_.request_export_png_dialog(window_, file_dialog_labels());
}

void App::drain_file_commands()
{
    for (const FileCommand& command : file_commands_.drain_commands()) {
        handle_file_command(command);
    }
}

void App::handle_file_command(const FileCommand& command)
{
    switch (command.kind) {
    case FileCommandKind::OpenImage:
        load_image_from_path(command.path);
        break;
    case FileCommandKind::ImportPalette:
        import_palette_from_path(command.path);
        break;
    case FileCommandKind::ExportPng:
        export_result_to_path(command.path);
        break;
    case FileCommandKind::ConfirmOpenImage:
        pending_dropped_image_ = command.path;
        open_drop_confirm_ = true;
        break;
    case FileCommandKind::DialogFailed: {
        const std::string error = command.error.empty() ? "unknown error" : command.error;
        set_status(textf(TextId::StatusFileDialogFailedFormat, {{"error", error}}));
        break;
    }
    }
}

void App::load_image_from_path(const std::filesystem::path& path)
{
    ImageLoadResult loaded = load_image_rgba(path.string());
    if (!loaded.error.empty()) {
        set_status(textf(TextId::StatusImageLoadFailedFormat, {{"error", loaded.error}}));
        return;
    }

    original_ = std::move(loaded.image);
    current_image_path_ = path;
    rebuild_texture(original_texture_, original_, false);
    original_zoom_ = 1.0F;
    result_zoom_ = 1.0F;
    mark_dirty();
    set_status(textf(TextId::StatusLoadedFormat, {{"name", path.filename().string()}}));
}

void App::import_palette_from_path(const std::filesystem::path& path)
{
    Palette parsed;
    std::string error;
    if (!validate_import_palette_file(path, parsed, error)) {
        set_status(textf(TextId::StatusPaletteImportFailedFormat, {{"error", error}}));
        return;
    }

    if (auto existing = find_import_palette_conflict(path)) {
        PendingPaletteImportState state;
        state.source = path;
        state.parsed = std::move(parsed);
        state.existing = std::move(*existing);
        state.request_conflict_open = true;
        pending_palette_import_ = std::move(state);
        return;
    }

    PendingPaletteImportState state;
    state.source = path;
    state.parsed = std::move(parsed);
    pending_palette_import_ = std::move(state);
    import_pending_palette(PaletteImportMode::Create);
    pending_palette_import_.reset();
}

bool App::import_pending_palette(PaletteImportMode mode)
{
    if (!pending_palette_import_) {
        return false;
    }

    Palette imported;
    std::string error;
    if (!import_palette_file(pending_palette_import_->source, mode, imported, error)) {
        set_status(textf(TextId::StatusPaletteImportFailedFormat, {{"error", error}}));
        return false;
    }

    finish_palette_import(imported, mode == PaletteImportMode::Overwrite ? TextId::StatusOverwrotePaletteFormat : TextId::StatusImportedPaletteFormat);
    return true;
}

bool App::save_pending_palette_import_as_name(const std::string& name)
{
    if (!pending_palette_import_) {
        return false;
    }

    Palette saved;
    std::string error;
    if (!save_palette_as_new(name, pending_palette_import_->parsed.colors, saved, error)) {
        set_status(textf(TextId::StatusPaletteImportFailedFormat, {{"error", error}}));
        return false;
    }

    finish_palette_import(saved, TextId::StatusImportedPaletteFormat);
    return true;
}

void App::finish_palette_import(const Palette& palette, TextId message)
{
    const HistorySnapshot before = capture_history_snapshot();
    refresh_palettes();
    if (!select_palette_by_path(palette.path)) {
        edit_session_.selected_palette_for_edit() = -1;
        edit_session_.settings_for_edit().palette = palette.colors;
    }
    edit_session_.settings_for_edit().use_palette = true;
    mark_dirty();
    commit_history_change(before);
    set_status(textf(message, {{"name", palette.name}}));
}

void App::request_new_palette()
{
    edit_session_.selected_palette_for_edit() = -1;
    edit_session_.settings_for_edit().use_palette = true;
    edit_session_.settings_for_edit().palette = {
        Color32{8, 10, 14, 255},
        Color32{238, 142, 45, 255},
    };
    mark_dirty();
    set_status(text(TextId::StatusStartedNewPalette));
}

void App::request_add_palette_color()
{
    if (edit_session_.settings_for_edit().palette.size() >= kMaxPaletteColors) {
        set_status(text(TextId::StatusPaletteFull));
        return;
    }

    const Color32 seed = edit_session_.settings_for_edit().palette.empty() ? Color32{255, 255, 255, 255} : edit_session_.settings_for_edit().palette.back();
    edit_session_.cancel_live_edit();
    palette_color_edit_ = PaletteColorEditState{
        static_cast<int>(edit_session_.settings_for_edit().palette.size()),
        true,
        true,
        color_to_rgb_floats(seed),
        capture_history_snapshot(),
    };
}

void App::request_edit_palette_color(std::size_t index)
{
    if (index >= edit_session_.settings_for_edit().palette.size()) {
        set_status(text(TextId::StatusPaletteColorMissing));
        return;
    }

    edit_session_.cancel_live_edit();
    palette_color_edit_ = PaletteColorEditState{
        static_cast<int>(index),
        false,
        true,
        color_to_rgb_floats(edit_session_.settings_for_edit().palette[index]),
        capture_history_snapshot(),
    };
}

void App::request_save_palette()
{
    if (edit_session_.selected_palette_for_edit() < 0 || edit_session_.selected_palette_for_edit() >= static_cast<int>(palettes_.size())) {
        set_status(text(TextId::StatusUseSaveNew));
        return;
    }

    const Palette selected = palettes_[static_cast<std::size_t>(edit_session_.selected_palette_for_edit())];
    std::string error;
    if (!overwrite_palette_file(selected, edit_session_.settings_for_edit().palette, error)) {
        set_status(textf(TextId::StatusPaletteSaveFailedFormat, {{"error", error}}));
        return;
    }

    refresh_palettes();
    select_palette_by_path(selected.path);
    set_status(textf(TextId::StatusSavedPaletteFormat, {{"name", selected.name}}));
}

void App::request_save_palette_as()
{
    if (edit_session_.settings_for_edit().palette.empty()) {
        set_status(text(TextId::StatusAddColorBeforeSaving));
        return;
    }

    std::string name = "custom-palette";
    if (edit_session_.selected_palette_for_edit() >= 0 && edit_session_.selected_palette_for_edit() < static_cast<int>(palettes_.size())) {
        name = palettes_[static_cast<std::size_t>(edit_session_.selected_palette_for_edit())].name + "-copy";
    }

    PaletteSaveAsState state;
    std::snprintf(state.name.data(), state.name.size(), "%s", name.c_str());
    state.request_open = true;
    palette_save_as_ = state;
}

void App::request_delete_selected_palette()
{
    if (edit_session_.selected_palette_for_edit() < 0 || edit_session_.selected_palette_for_edit() >= static_cast<int>(palettes_.size())) {
        set_status(text(TextId::StatusNoPaletteSelected));
        return;
    }

    pending_delete_palette_ = palettes_[static_cast<std::size_t>(edit_session_.selected_palette_for_edit())];
    open_delete_palette_confirm_ = true;
}

bool App::save_palette_as_name(const std::string& name)
{
    Palette saved;
    std::string error;
    if (!save_palette_as_new(name, edit_session_.settings_for_edit().palette, saved, error)) {
        set_status(textf(TextId::StatusPaletteSaveFailedFormat, {{"error", error}}));
        return false;
    }

    refresh_palettes();
    select_palette_by_path(saved.path);
    edit_session_.settings_for_edit().use_palette = true;
    set_status(textf(TextId::StatusSavedPaletteFormat, {{"name", saved.name}}));
    return true;
}

bool App::select_palette_by_path(const std::filesystem::path& path)
{
    for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
        if (palettes_[static_cast<std::size_t>(i)].path == path) {
            edit_session_.selected_palette_for_edit() = i;
            edit_session_.settings_for_edit().palette = palettes_[static_cast<std::size_t>(i)].colors;
            return true;
        }
    }

    edit_session_.selected_palette_for_edit() = -1;
    return false;
}

void App::delete_pending_palette()
{
    if (!pending_delete_palette_) {
        return;
    }

    const std::string deleted_name = pending_delete_palette_->name;
    std::string error;
    if (!delete_palette_file(*pending_delete_palette_, error)) {
        set_status(textf(TextId::StatusPaletteDeleteFailedFormat, {{"error", error}}));
        return;
    }

    refresh_palettes();
    mark_dirty();
    set_status(textf(TextId::StatusDeletedPaletteFormat, {{"name", deleted_name}}));
}

void App::export_result_to_path(const std::filesystem::path& path)
{
    if (result_.empty()) {
        set_status(text(TextId::StatusExportSkipped));
        return;
    }

    std::string error;
    const std::string destination = ensure_png_extension(path);
    if (!save_png_rgba(destination, result_, error)) {
        set_status(textf(TextId::StatusExportFailedFormat, {{"error", error}}));
        return;
    }

    last_export_path_ = destination;
    set_status(textf(TextId::StatusExportedFormat, {{"name", std::filesystem::path(destination).filename().string()}}));
}

void App::refresh_palettes()
{
    palettes_ = load_saved_palettes();
    if (!palettes_.empty()) {
        edit_session_.selected_palette_for_edit() = std::clamp(edit_session_.selected_palette_for_edit(), 0, static_cast<int>(palettes_.size()) - 1);
        edit_session_.settings_for_edit().palette = palettes_[static_cast<std::size_t>(edit_session_.selected_palette_for_edit())].colors;
    } else {
        edit_session_.selected_palette_for_edit() = -1;
        edit_session_.settings_for_edit().palette.clear();
    }
}

void App::mark_dirty()
{
    edit_session_.mark_dirty();
}

void App::set_status(std::string message)
{
    status_ = std::move(message);
}

const char* App::text(TextId id) const
{
    return translate(language_, id);
}

std::string App::textf(
    TextId id,
    std::initializer_list<std::pair<std::string_view, std::string_view>> values) const
{
    return format_translation(language_, id, values);
}

std::string App::imgui_label(TextId label, const char* id) const
{
    std::string value = text(label);
    value += "###";
    value += id;
    return value;
}

FileDialogLabels App::file_dialog_labels() const
{
    return {
        text(TextId::ImagesFilter),
        text(TextId::AllFilesFilter),
        text(TextId::LospecPalettesFilter),
        text(TextId::PngImageFilter),
    };
}

void App::normalize_settings()
{
    edit_session_.normalize();
}

App::HistorySnapshot App::capture_history_snapshot() const
{
    return edit_session_.capture_snapshot();
}

void App::record_control_history(const HistorySnapshot& before)
{
    edit_session_.finish_live_edit(before, ImGui::IsAnyItemActive());
}

void App::commit_history_change(const HistorySnapshot& before)
{
    edit_session_.commit_edit(before);
}

void App::undo()
{
    if (!edit_session_.undo(palettes_.size())) {
        set_status(text(TextId::StatusNothingToUndo));
        return;
    }

    set_status(text(TextId::StatusUndid));
}

void App::redo()
{
    if (!edit_session_.redo(palettes_.size())) {
        set_status(text(TextId::StatusNothingToRedo));
        return;
    }

    set_status(text(TextId::StatusRedid));
}

bool App::can_undo() const noexcept
{
    return edit_session_.can_undo();
}

bool App::can_redo() const noexcept
{
    return edit_session_.can_redo();
}

bool App::slider_int_direct(TextId label, const char* id, int& value, int minimum, int maximum)
{
    const std::string widget_label = imgui_label(label, id);
    const bool changed = ImGui::SliderInt(widget_label.c_str(), &value, minimum, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        int* target = &value;
        open_number_edit(text(label), value, minimum, maximum, true, "%d", [target, minimum, maximum](double edited) {
            *target = std::clamp(static_cast<int>(std::lround(edited)), minimum, maximum);
        });
    }
    return changed;
}

bool App::slider_float_direct(TextId label, const char* id, float& value, float minimum, float maximum, const char* format)
{
    const std::string widget_label = imgui_label(label, id);
    const bool changed = ImGui::SliderFloat(widget_label.c_str(), &value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        float* target = &value;
        open_number_edit(text(label), value, minimum, maximum, false, format, [target, minimum, maximum](double edited) {
            *target = std::clamp(static_cast<float>(edited), minimum, maximum);
        });
    }
    return changed;
}

bool App::slider_float_direct_value(TextId label, const char* id, float value, float minimum, float maximum, const char* format, std::function<void(float)> apply)
{
    float editable = value;
    const std::string widget_label = imgui_label(label, id);
    const bool changed = ImGui::SliderFloat(widget_label.c_str(), &editable, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (changed) {
        apply(std::clamp(editable, minimum, maximum));
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        open_number_edit(text(label), value, minimum, maximum, false, format, [apply = std::move(apply), minimum, maximum](double edited) {
            apply(std::clamp(static_cast<float>(edited), minimum, maximum));
        });
    }

    return changed;
}

void App::open_number_edit(std::string label, double value, double minimum, double maximum, bool integer, std::string format, std::function<void(double)> apply)
{
    edit_session_.cancel_live_edit();

    NumberEditState state;
    state.label = std::move(label);
    state.format = std::move(format);
    state.value = value;
    state.minimum = minimum;
    state.maximum = maximum;
    state.integer = integer;
    state.request_open = true;
    state.before = capture_history_snapshot();
    state.apply = std::move(apply);

    if (state.integer) {
        std::snprintf(state.input.data(), state.input.size(), "%d", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(state.input.data(), state.input.size(), state.format.c_str(), value);
    }

    number_edit_ = std::move(state);
}

} // namespace pixelizer
