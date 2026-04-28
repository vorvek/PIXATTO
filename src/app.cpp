#include "pixelizer/app.hpp"

#include <SDL3/SDL_dialog.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace pixelizer {
namespace {

constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;
constexpr std::size_t kMaxHistoryEntries = 100;
constexpr float kViewportSplitterThickness = 8.0F;
constexpr float kViewportMinimumPaneSize = 180.0F;

constexpr std::array<SDL_DialogFileFilter, 2> kImageFilters = {{
    {"Images", "png;jpg;jpeg;bmp"},
    {"All files", "*"},
}};

constexpr std::array<SDL_DialogFileFilter, 2> kPaletteFilters = {{
    {"Lospec palettes", "hex"},
    {"All files", "*"},
}};

constexpr std::array<SDL_DialogFileFilter, 1> kPngFilters = {{
    {"PNG image", "png"},
}};

const char* dither_label(DitherMode mode)
{
    switch (mode) {
    case DitherMode::None:
        return "None";
    case DitherMode::Bayer:
        return "Bayer";
    case DitherMode::BlueNoise:
        return "Blue Noise";
    case DitherMode::FloydSteinberg:
        return "Floyd-Steinberg";
    case DitherMode::JarvisJudiceNinke:
        return "Jarvis-Judice-Ninke";
    case DitherMode::Atkinson:
        return "Atkinson";
    case DitherMode::Riemersma:
        return "Riemersma";
    }
    return "None";
}

const char* block_mode_label(BlockColorMode mode)
{
    switch (mode) {
    case BlockColorMode::Average:
        return "Average";
    case BlockColorMode::WeightedAverage:
        return "Weighted";
    }
    return "Weighted";
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

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool has_extension(const std::filesystem::path& path, const char* extension)
{
    return lowercase(path.extension().string()) == extension;
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

struct App::DialogState {
    std::mutex mutex;
    std::vector<PendingDialog> pending_dialogs;
};

struct App::DialogPayload {
    std::weak_ptr<DialogState> state;
    DialogKind kind;
};

App::App()
    : dialog_state_(std::make_shared<DialogState>())
{
}

App::~App()
{
    shutdown();
}

bool App::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        set_status(std::string("SDL_Init failed: ") + SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow("Pixelizer", kInitialWidth, kInitialHeight, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        set_status(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        return false;
    }
    set_window_icon(window_);

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        set_status(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
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
        handle_pending_dialogs();
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
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            handle_dropped_file(event.drop.data);
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
    ImGui::Begin("Pixelizer Workspace", nullptr, flags);

    const float control_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.24F, 300.0F, 390.0F);
    ImGui::BeginChild("Controls", ImVec2(control_width, 0), ImGuiChildFlags_Borders);
    render_controls();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Views", ImVec2(0, 0), ImGuiChildFlags_None);
    render_viewports();
    ImGui::EndChild();

    ImGui::End();

    render_number_edit_popup();
    render_drop_confirm_popup();
    render_delete_palette_popup();
    render_palette_import_conflict_popup();
    render_palette_import_name_popup();
    render_palette_color_popup();
    render_save_palette_popup();

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

    if (ImGui::Button("Open Image")) {
        request_open_image();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Palette")) {
        request_import_palette();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export PNG")) {
        request_export_png();
    }
    ImGui::SameLine();
    const bool single_viewport = viewport_mode_ == ViewportMode::Single;
    if (ImGui::Button(single_viewport ? "Two Views" : "One View")) {
        viewport_mode_ = single_viewport ? ViewportMode::Split : ViewportMode::Single;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(single_viewport ? "Show original and result viewports." : "Show only the result viewport.");
    }
    if (viewport_mode_ == ViewportMode::Split) {
        ImGui::SameLine();
        const bool side_by_side = viewport_layout_ == ViewportLayout::SideBySide;
        if (ImGui::Button(side_by_side ? "Stack Views" : "Side by Side")) {
            viewport_layout_ = viewport_layout_ == ViewportLayout::SideBySide ? ViewportLayout::Stacked : ViewportLayout::SideBySide;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(side_by_side ? "Show result on top and original on bottom." : "Show original left and result right.");
        }
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    ImGui::TextUnformatted(status_.c_str());

    ImGui::EndMainMenuBar();
}

void App::render_controls()
{
    const HistorySnapshot before = capture_history_snapshot();

    ImGui::TextUnformatted("Pixelize");
    ImGui::Separator();

    if (slider_int_direct("Pixel size", settings_.pixel_size, 1, 128)) {
        mark_dirty();
    }

    if (ImGui::BeginCombo("Block sample", block_mode_label(settings_.block_color_mode))) {
        for (BlockColorMode mode : {BlockColorMode::WeightedAverage, BlockColorMode::Average}) {
            const bool selected = settings_.block_color_mode == mode;
            if (ImGui::Selectable(block_mode_label(mode), selected)) {
                settings_.block_color_mode = mode;
                mark_dirty();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Checkbox("Use palette", &settings_.use_palette)) {
        mark_dirty();
    }

    if (settings_.use_palette) {
        if (selected_palette_ >= static_cast<int>(palettes_.size())) {
            selected_palette_ = -1;
        }
        if (!palettes_.empty() && selected_palette_ < 0 && settings_.palette.empty()) {
            selected_palette_ = 0;
            settings_.palette = palettes_[0].colors;
            mark_dirty();
        }

        const bool has_saved_selection = selected_palette_ >= 0 && selected_palette_ < static_cast<int>(palettes_.size());
        if (palettes_.empty()) {
            ImGui::TextDisabled("No palettes saved.");
        } else {
            const char* preview = has_saved_selection ? palettes_[static_cast<std::size_t>(selected_palette_)].name.c_str() : "Unsaved palette";
            if (ImGui::BeginCombo("Palette", preview)) {
                for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
                    const bool selected = selected_palette_ == i;
                    if (ImGui::Selectable(palettes_[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                        selected_palette_ = i;
                        settings_.palette = palettes_[static_cast<std::size_t>(i)].colors;
                        mark_dirty();
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::Button("New Palette")) {
            request_new_palette();
        }
        ImGui::SameLine();

        const bool can_save = has_saved_selection && !settings_.palette.empty();
        if (!can_save) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save")) {
            request_save_palette();
        }
        if (!can_save) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();

        const bool can_save_new = !settings_.palette.empty();
        if (!can_save_new) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save New")) {
            request_save_palette_as();
        }
        if (!can_save_new) {
            ImGui::EndDisabled();
        }

        if (has_saved_selection) {
            if (ImGui::Button("Delete Palette")) {
                request_delete_selected_palette();
            }
        }

        ImGui::Spacing();
        ImGui::Text("%zu / %zu colors", settings_.palette.size(), kMaxPaletteColors);
        if (settings_.palette.empty()) {
            ImGui::TextDisabled("Add a color to begin.");
        }

        const float swatch = 16.0F;
        const float start_x = ImGui::GetCursorScreenPos().x;
        const float max_x = start_x + ImGui::GetContentRegionAvail().x;
        for (std::size_t color_index = 0; color_index < settings_.palette.size(); ++color_index) {
            const Color32 color = settings_.palette[color_index];
            ImGui::PushID(static_cast<int>(color_index));
            if (ImGui::ColorButton("swatch", color_to_imgui(color), ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) {
                request_edit_palette_color(color_index);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edit color %zu", color_index + 1U);
            }
            ImGui::PopID();
            if (ImGui::GetItemRectMax().x + swatch + ImGui::GetStyle().ItemSpacing.x < max_x) {
                ImGui::SameLine();
            }
        }

        if (settings_.palette.size() < kMaxPaletteColors) {
            if (ImGui::Button("+##AddPaletteColor", ImVec2(swatch, swatch))) {
                request_add_palette_color();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Add color");
            }
        }
    } else {
        if (slider_int_direct("Max colors", settings_.reduction_max_colors, 0, 256)) {
            mark_dirty();
        }
        if (settings_.reduction_max_colors == 0 && slider_int_direct("Color levels", settings_.color_levels, 2, 64)) {
            mark_dirty();
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Dithering");
    ImGui::Separator();
    if (ImGui::BeginCombo("Mode", dither_label(settings_.dither_mode))) {
        for (DitherMode mode : {
                 DitherMode::None,
                 DitherMode::Bayer,
                 DitherMode::BlueNoise,
                 DitherMode::FloydSteinberg,
                 DitherMode::JarvisJudiceNinke,
                 DitherMode::Atkinson,
                 DitherMode::Riemersma,
             }) {
            const bool selected = settings_.dither_mode == mode;
            if (ImGui::Selectable(dither_label(mode), selected)) {
                settings_.dither_mode = mode;
                mark_dirty();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (settings_.dither_mode == DitherMode::Bayer) {
        if (ImGui::BeginCombo("Pattern", bayer_pattern_label(settings_.bayer_matrix_size))) {
            for (int size : {2, 4, 8, 16}) {
                const bool selected = settings_.bayer_matrix_size == size;
                if (ImGui::Selectable(bayer_pattern_label(size), selected)) {
                    settings_.bayer_matrix_size = size;
                    mark_dirty();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    float dither_percent = settings_.dither_amount * 100.0F;
    if (slider_float_direct_value("Amount", dither_percent, 0.0F, 100.0F, "%.0f%%", [this](float value) {
            settings_.dither_amount = value / 100.0F;
        })) {
        mark_dirty();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Adjustments");
    ImGui::Separator();
    if (slider_float_direct("Brightness", settings_.adjustments.brightness, -1.0F, 1.0F, "%.2f")) {
        mark_dirty();
    }
    if (slider_float_direct("Contrast", settings_.adjustments.contrast, -1.0F, 1.0F, "%.2f")) {
        mark_dirty();
    }
    if (slider_float_direct("Gamma", settings_.adjustments.gamma, 0.1F, 4.0F, "%.2f")) {
        mark_dirty();
    }
    if (slider_float_direct("Saturation", settings_.adjustments.saturation, 0.0F, 2.5F, "%.2f")) {
        mark_dirty();
    }
    if (slider_float_direct("Input black", settings_.adjustments.input_black, 0.0F, 0.95F, "%.2f")) {
        settings_.adjustments.input_black = std::min(settings_.adjustments.input_black, settings_.adjustments.input_white - 0.01F);
        mark_dirty();
    }
    if (slider_float_direct("Input white", settings_.adjustments.input_white, 0.05F, 1.0F, "%.2f")) {
        settings_.adjustments.input_white = std::max(settings_.adjustments.input_white, settings_.adjustments.input_black + 0.01F);
        mark_dirty();
    }
    if (slider_float_direct("Output black", settings_.adjustments.output_black, 0.0F, 0.95F, "%.2f")) {
        settings_.adjustments.output_black = std::min(settings_.adjustments.output_black, settings_.adjustments.output_white - 0.01F);
        mark_dirty();
    }
    if (slider_float_direct("Output white", settings_.adjustments.output_white, 0.05F, 1.0F, "%.2f")) {
        settings_.adjustments.output_white = std::max(settings_.adjustments.output_white, settings_.adjustments.output_black + 0.01F);
        mark_dirty();
    }

    float tint[3] = {
        settings_.adjustments.tint.r / 255.0F,
        settings_.adjustments.tint.g / 255.0F,
        settings_.adjustments.tint.b / 255.0F,
    };
    if (ImGui::ColorEdit3("Tint", tint, ImGuiColorEditFlags_NoInputs)) {
        settings_.adjustments.tint.r = static_cast<std::uint8_t>(std::lround(std::clamp(tint[0], 0.0F, 1.0F) * 255.0F));
        settings_.adjustments.tint.g = static_cast<std::uint8_t>(std::lround(std::clamp(tint[1], 0.0F, 1.0F) * 255.0F));
        settings_.adjustments.tint.b = static_cast<std::uint8_t>(std::lround(std::clamp(tint[2], 0.0F, 1.0F) * 255.0F));
        mark_dirty();
    }
    if (slider_float_direct("Tint strength", settings_.adjustments.tint_strength, 0.0F, 1.0F, "%.2f")) {
        mark_dirty();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Adjustments")) {
        settings_.adjustments = {};
        mark_dirty();
    }

    record_control_history(before);
}

void App::render_viewports()
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x <= 0.0F || available.y <= 0.0F) {
        return;
    }

    if (viewport_mode_ == ViewportMode::Single) {
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view("Result", result_texture_, result_zoom_);
        ImGui::EndChild();
        return;
    }

    if (viewport_layout_ == ViewportLayout::SideBySide) {
        if (available.x <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_image_view("Original", original_texture_, original_zoom_);
            ImGui::EndChild();
            return;
        }

        const float splitter_width = splitter_thickness_for(available.x);
        const float usable_width = available.x - splitter_width;
        float original_width = split_size_from_ratio(viewport_split_ratio_, usable_width);
        viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);

        ImGui::BeginChild("OriginalPane", ImVec2(original_width, 0), ImGuiChildFlags_Borders);
        render_image_view("Original", original_texture_, original_zoom_);
        ImGui::EndChild();

        ImGui::SameLine(0.0F, 0.0F);
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterX", ImVec2(splitter_width, available.y), ImGuiMouseCursor_ResizeEW, delta)) {
            original_width = split_size_from_ratio(viewport_split_ratio_, usable_width) + delta;
            viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);
        }

        ImGui::SameLine(0.0F, 0.0F);
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view("Result", result_texture_, result_zoom_);
        ImGui::EndChild();
    } else {
        if (available.y <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_image_view("Result", result_texture_, result_zoom_);
            ImGui::EndChild();
            return;
        }

        const float splitter_height = splitter_thickness_for(available.y);
        const float usable_height = available.y - splitter_height;
        float result_height = split_size_from_ratio(viewport_split_ratio_, usable_height);
        viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);

        ImGui::BeginChild("ResultPane", ImVec2(0, result_height), ImGuiChildFlags_Borders);
        render_image_view("Result", result_texture_, result_zoom_);
        ImGui::EndChild();

        remove_vertical_item_spacing();
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterY", ImVec2(available.x, splitter_height), ImGuiMouseCursor_ResizeNS, delta)) {
            result_height = split_size_from_ratio(viewport_split_ratio_, usable_height) + delta;
            viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);
        }

        remove_vertical_item_spacing();
        ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_image_view("Original", original_texture_, original_zoom_);
        ImGui::EndChild();
    }
}

void App::render_image_view(const char* label, Texture& texture, float& zoom)
{
    const ImVec2 pane_available = ImGui::GetContentRegionAvail();

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::SliderFloat(("Zoom##" + std::string(label)).c_str(), &zoom, 0.05F, 32.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (ImGui::SmallButton(("1:1##" + std::string(label)).c_str())) {
        zoom = 1.0F;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(("Fit##" + std::string(label)).c_str())) {
        zoom = fit_zoom_for_size(texture.width, texture.height, pane_available);
    }

    ImGui::Separator();

    ImGui::BeginChild((std::string(label) + "Scroll").c_str(), ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (!texture.handle) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (avail.x - 140.0F) * 0.5F), std::max(0.0F, (avail.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("No image loaded");
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
    static constexpr const char* kPopupId = "Set numeric value";

    if (number_edit_ && number_edit_->request_open) {
        ImGui::OpenPopup(kPopupId);
        number_edit_->request_open = false;
    }

    if (!number_edit_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(number_edit_->label.c_str());
        ImGui::SetNextItemWidth(220.0F);

        bool submitted = false;
        if (number_edit_->integer) {
            int value = static_cast<int>(std::lround(number_edit_->value));
            submitted = ImGui::InputInt("Value", &value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
            number_edit_->value = static_cast<double>(value);
        } else {
            float value = static_cast<float>(number_edit_->value);
            submitted = ImGui::InputFloat("Value", &value, 0.0F, 0.0F, number_edit_->format.c_str(), ImGuiInputTextFlags_EnterReturnsTrue);
            number_edit_->value = static_cast<double>(value);
        }

        const bool apply = submitted || ImGui::Button("Apply");
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel");

        if (apply) {
            const double clamped = std::clamp(number_edit_->value, number_edit_->minimum, number_edit_->maximum);
            number_edit_->apply(clamped);
            normalize_settings();
            mark_dirty();
            commit_history_change(number_edit_->before);
            set_status("Set " + number_edit_->label + ".");
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        } else if (cancel || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        number_edit_.reset();
    }
}

void App::render_drop_confirm_popup()
{
    static constexpr const char* kPopupId = "Open dropped image?";

    if (open_drop_confirm_) {
        ImGui::OpenPopup(kPopupId);
        open_drop_confirm_ = false;
    }

    if (!pending_dropped_image_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Replace the current image with the dropped image?");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", pending_dropped_image_->filename().string().c_str());

        if (ImGui::Button("Open")) {
            const auto path = *pending_dropped_image_;
            pending_dropped_image_.reset();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
            load_image_from_path(path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_dropped_image_.reset();
    }
}

void App::render_delete_palette_popup()
{
    static constexpr const char* kPopupId = "Delete palette?";

    if (open_delete_palette_confirm_) {
        ImGui::OpenPopup(kPopupId);
        open_delete_palette_confirm_ = false;
    }

    if (!pending_delete_palette_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this saved palette?");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", pending_delete_palette_->name.c_str());

        if (ImGui::Button("Delete")) {
            delete_pending_palette();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_delete_palette_.reset();
    }
}

void App::render_palette_import_conflict_popup()
{
    static constexpr const char* kPopupId = "Palette already exists";

    const bool popup_active = ImGui::IsPopupOpen(kPopupId);
    if (pending_palette_import_ && pending_palette_import_->request_conflict_open) {
        ImGui::OpenPopup(kPopupId);
        pending_palette_import_->request_conflict_open = false;
    }

    if (!pending_palette_import_
        || (!pending_palette_import_->request_conflict_open && !popup_active && !ImGui::IsPopupOpen(kPopupId))) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s already exists.", pending_palette_import_->parsed.name.c_str());

        if (ImGui::Button("Overwrite")) {
            if (import_pending_palette(PaletteImportMode::Overwrite)) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep both")) {
            const std::string name = suggest_import_palette_copy_name(pending_palette_import_->source);
            std::snprintf(pending_palette_import_->name.data(), pending_palette_import_->name.size(), "%s", name.c_str());
            pending_palette_import_->request_name_open = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId) && !pending_palette_import_->request_name_open) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_palette_import_.reset();
    }
}

void App::render_palette_import_name_popup()
{
    static constexpr const char* kPopupId = "Name imported palette";

    const bool popup_active = ImGui::IsPopupOpen(kPopupId);
    if (pending_palette_import_ && pending_palette_import_->request_name_open) {
        ImGui::OpenPopup(kPopupId);
        pending_palette_import_->request_name_open = false;
    }

    if (!pending_palette_import_
        || (!pending_palette_import_->request_name_open && !popup_active && !ImGui::IsPopupOpen(kPopupId))) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Palette name");
        ImGui::SetNextItemWidth(260.0F);
        const bool submitted = ImGui::InputText(
            "##ImportedPaletteName",
            pending_palette_import_->name.data(),
            pending_palette_import_->name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool save_clicked = ImGui::Button("Save");
        if (submitted || save_clicked) {
            if (save_pending_palette_import_as_name(pending_palette_import_->name.data())) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_palette_import_.reset();
    }
}

void App::render_palette_color_popup()
{
    static constexpr const char* kPopupId = "Palette color";

    if (palette_color_edit_ && palette_color_edit_->request_open) {
        ImGui::OpenPopup(kPopupId);
        palette_color_edit_->request_open = false;
    }

    if (!palette_color_edit_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(palette_color_edit_->adding ? "Add palette color" : "Edit palette color");
        ImGui::ColorPicker3(
            "##PaletteColorPicker",
            palette_color_edit_->color.data(),
            ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB);

        const bool adding = palette_color_edit_->adding;
        if (ImGui::Button(adding ? "Add" : "Apply")) {
            if (adding) {
                if (settings_.palette.size() >= kMaxPaletteColors) {
                    set_status("Palette already has 256 colors.");
                } else {
                    settings_.palette.push_back(color_from_rgb_floats(palette_color_edit_->color));
                    mark_dirty();
                    commit_history_change(palette_color_edit_->before);
                    set_status("Added palette color.");
                    ImGui::CloseCurrentPopup();
                    reset_popup = true;
                }
            } else {
                const int index = palette_color_edit_->index;
                if (index < 0 || index >= static_cast<int>(settings_.palette.size())) {
                    set_status("Palette color no longer exists.");
                } else {
                    settings_.palette[static_cast<std::size_t>(index)] = color_from_rgb_floats(palette_color_edit_->color);
                    mark_dirty();
                    commit_history_change(palette_color_edit_->before);
                    set_status("Updated palette color.");
                    ImGui::CloseCurrentPopup();
                    reset_popup = true;
                }
            }
        }

        ImGui::SameLine();
        const bool can_delete = !palette_color_edit_->adding
            && palette_color_edit_->index >= 0
            && palette_color_edit_->index < static_cast<int>(settings_.palette.size());
        if (!can_delete) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Delete Color")) {
            settings_.palette.erase(settings_.palette.begin() + palette_color_edit_->index);
            mark_dirty();
            commit_history_change(palette_color_edit_->before);
            set_status("Deleted palette color.");
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }
        if (!can_delete) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        palette_color_edit_.reset();
    }
}

void App::render_save_palette_popup()
{
    static constexpr const char* kPopupId = "Save palette as";

    if (palette_save_as_ && palette_save_as_->request_open) {
        ImGui::OpenPopup(kPopupId);
        palette_save_as_->request_open = false;
    }

    if (!palette_save_as_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(kPopupId, &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Palette name");
        ImGui::SetNextItemWidth(260.0F);
        const bool submitted = ImGui::InputText(
            "##PaletteName",
            palette_save_as_->name.data(),
            palette_save_as_->name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool save_clicked = ImGui::Button("Save");
        if (submitted || save_clicked) {
            if (save_palette_as_name(palette_save_as_->name.data())) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(kPopupId)) {
        reset_popup = true;
    }

    if (reset_popup) {
        palette_save_as_.reset();
    }
}

void App::handle_shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (number_edit_ || palette_color_edit_ || palette_save_as_ || pending_palette_import_
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
    if (!preview_dirty_ || original_.empty()) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    result_ = process_image(original_, settings_);
    rebuild_texture(result_texture_, result_, true);
    preview_dirty_ = false;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    status_ = "Preview updated in " + std::to_string(elapsed) + " ms.";
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
        set_status(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(texture.handle, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_UpdateTexture(texture.handle, nullptr, image.rgba.data(), image.width * 4);
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
    auto* payload = new DialogPayload{dialog_state_, DialogKind::OpenImage};
    SDL_ShowOpenFileDialog(dialog_callback, payload, window_, kImageFilters.data(), static_cast<int>(kImageFilters.size()), nullptr, false);
}

void App::request_import_palette()
{
    auto* payload = new DialogPayload{dialog_state_, DialogKind::ImportPalette};
    SDL_ShowOpenFileDialog(dialog_callback, payload, window_, kPaletteFilters.data(), static_cast<int>(kPaletteFilters.size()), nullptr, false);
}

void App::request_export_png()
{
    auto* payload = new DialogPayload{dialog_state_, DialogKind::ExportPng};
    SDL_ShowSaveFileDialog(dialog_callback, payload, window_, kPngFilters.data(), static_cast<int>(kPngFilters.size()), nullptr);
}

void App::dialog_callback(void* userdata, const char* const* filelist, int)
{
    std::unique_ptr<DialogPayload> payload(static_cast<DialogPayload*>(userdata));
    if (!payload) {
        return;
    }

    auto state = payload->state.lock();
    if (!state || !filelist || !filelist[0]) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    state->pending_dialogs.push_back({payload->kind, filelist[0]});
}

void App::handle_pending_dialogs()
{
    std::vector<PendingDialog> dialogs;
    auto state = dialog_state_;
    if (!state) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        dialogs.swap(state->pending_dialogs);
    }

    for (const PendingDialog& dialog : dialogs) {
        switch (dialog.kind) {
        case DialogKind::OpenImage:
            load_image_from_path(dialog.path);
            break;
        case DialogKind::ImportPalette:
            import_palette_from_path(dialog.path);
            break;
        case DialogKind::ExportPng:
            export_result_to_path(dialog.path);
            break;
        }
    }
}

void App::handle_dropped_file(const std::filesystem::path& path)
{
    if (path.empty()) {
        return;
    }

    if (has_extension(path, ".hex")) {
        import_palette_from_path(path);
        return;
    }

    if (original_.empty()) {
        load_image_from_path(path);
        return;
    }

    pending_dropped_image_ = path;
    open_drop_confirm_ = true;
}

void App::load_image_from_path(const std::filesystem::path& path)
{
    ImageLoadResult loaded = load_image_rgba(path.string());
    if (!loaded.error.empty()) {
        set_status("Image load failed: " + loaded.error);
        return;
    }

    original_ = std::move(loaded.image);
    current_image_path_ = path;
    rebuild_texture(original_texture_, original_, false);
    original_zoom_ = 1.0F;
    result_zoom_ = 1.0F;
    mark_dirty();
    set_status("Loaded " + path.filename().string() + ".");
}

void App::import_palette_from_path(const std::filesystem::path& path)
{
    Palette parsed;
    std::string error;
    if (!validate_import_palette_file(path, parsed, error)) {
        set_status("Palette import failed: " + error);
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
        set_status("Palette import failed: " + error);
        return false;
    }

    finish_palette_import(imported, mode == PaletteImportMode::Overwrite ? "Overwrote" : "Imported");
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
        set_status("Palette import failed: " + error);
        return false;
    }

    finish_palette_import(saved, "Imported");
    return true;
}

void App::finish_palette_import(const Palette& palette, const std::string& action)
{
    const HistorySnapshot before = capture_history_snapshot();
    refresh_palettes();
    if (!select_palette_by_path(palette.path)) {
        selected_palette_ = -1;
        settings_.palette = palette.colors;
    }
    settings_.use_palette = true;
    mark_dirty();
    commit_history_change(before);
    set_status(action + " palette " + palette.name + ".");
}

void App::request_new_palette()
{
    selected_palette_ = -1;
    settings_.use_palette = true;
    settings_.palette = {
        Color32{8, 10, 14, 255},
        Color32{238, 142, 45, 255},
    };
    mark_dirty();
    set_status("Started a new unsaved palette.");
}

void App::request_add_palette_color()
{
    if (settings_.palette.size() >= kMaxPaletteColors) {
        set_status("Palette already has 256 colors.");
        return;
    }

    const Color32 seed = settings_.palette.empty() ? Color32{255, 255, 255, 255} : settings_.palette.back();
    active_edit_snapshot_.reset();
    palette_color_edit_ = PaletteColorEditState{
        static_cast<int>(settings_.palette.size()),
        true,
        true,
        color_to_rgb_floats(seed),
        capture_history_snapshot(),
    };
}

void App::request_edit_palette_color(std::size_t index)
{
    if (index >= settings_.palette.size()) {
        set_status("Palette color no longer exists.");
        return;
    }

    active_edit_snapshot_.reset();
    palette_color_edit_ = PaletteColorEditState{
        static_cast<int>(index),
        false,
        true,
        color_to_rgb_floats(settings_.palette[index]),
        capture_history_snapshot(),
    };
}

void App::request_save_palette()
{
    if (selected_palette_ < 0 || selected_palette_ >= static_cast<int>(palettes_.size())) {
        set_status("Use Save New for unsaved palettes.");
        return;
    }

    const Palette selected = palettes_[static_cast<std::size_t>(selected_palette_)];
    std::string error;
    if (!overwrite_palette_file(selected, settings_.palette, error)) {
        set_status("Palette save failed: " + error);
        return;
    }

    refresh_palettes();
    select_palette_by_path(selected.path);
    set_status("Saved palette " + selected.name + ".");
}

void App::request_save_palette_as()
{
    if (settings_.palette.empty()) {
        set_status("Add at least one color before saving.");
        return;
    }

    std::string name = "custom-palette";
    if (selected_palette_ >= 0 && selected_palette_ < static_cast<int>(palettes_.size())) {
        name = palettes_[static_cast<std::size_t>(selected_palette_)].name + "-copy";
    }

    PaletteSaveAsState state;
    std::snprintf(state.name.data(), state.name.size(), "%s", name.c_str());
    state.request_open = true;
    palette_save_as_ = state;
}

void App::request_delete_selected_palette()
{
    if (selected_palette_ < 0 || selected_palette_ >= static_cast<int>(palettes_.size())) {
        set_status("No palette selected.");
        return;
    }

    pending_delete_palette_ = palettes_[static_cast<std::size_t>(selected_palette_)];
    open_delete_palette_confirm_ = true;
}

bool App::save_palette_as_name(const std::string& name)
{
    Palette saved;
    std::string error;
    if (!save_palette_as_new(name, settings_.palette, saved, error)) {
        set_status("Palette save failed: " + error);
        return false;
    }

    refresh_palettes();
    select_palette_by_path(saved.path);
    settings_.use_palette = true;
    set_status("Saved palette " + saved.name + ".");
    return true;
}

bool App::select_palette_by_path(const std::filesystem::path& path)
{
    for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
        if (palettes_[static_cast<std::size_t>(i)].path == path) {
            selected_palette_ = i;
            settings_.palette = palettes_[static_cast<std::size_t>(i)].colors;
            return true;
        }
    }

    selected_palette_ = -1;
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
        set_status("Palette delete failed: " + error);
        return;
    }

    refresh_palettes();
    mark_dirty();
    set_status("Deleted palette " + deleted_name + ".");
}

void App::export_result_to_path(const std::filesystem::path& path)
{
    if (result_.empty()) {
        set_status("Export skipped: no result image yet.");
        return;
    }

    std::string error;
    const std::string destination = ensure_png_extension(path);
    if (!save_png_rgba(destination, result_, error)) {
        set_status("Export failed: " + error);
        return;
    }

    last_export_path_ = destination;
    set_status("Exported " + std::filesystem::path(destination).filename().string() + ".");
}

void App::refresh_palettes()
{
    palettes_ = load_saved_palettes();
    if (!palettes_.empty()) {
        selected_palette_ = std::clamp(selected_palette_, 0, static_cast<int>(palettes_.size()) - 1);
        settings_.palette = palettes_[static_cast<std::size_t>(selected_palette_)].colors;
    } else {
        selected_palette_ = -1;
        settings_.palette.clear();
    }
}

void App::mark_dirty()
{
    preview_dirty_ = true;
}

void App::set_status(std::string message)
{
    status_ = std::move(message);
}

void App::normalize_settings()
{
    settings_.pixel_size = std::clamp(settings_.pixel_size, 1, 128);
    settings_.color_levels = std::clamp(settings_.color_levels, 2, 64);
    settings_.reduction_max_colors = std::clamp(settings_.reduction_max_colors, 0, 256);
    if (settings_.bayer_matrix_size <= 2) {
        settings_.bayer_matrix_size = 2;
    } else if (settings_.bayer_matrix_size <= 4) {
        settings_.bayer_matrix_size = 4;
    } else {
        settings_.bayer_matrix_size = 8;
    }
    settings_.dither_amount = std::clamp(settings_.dither_amount, 0.0F, 1.0F);

    settings_.adjustments.brightness = std::clamp(settings_.adjustments.brightness, -1.0F, 1.0F);
    settings_.adjustments.contrast = std::clamp(settings_.adjustments.contrast, -1.0F, 1.0F);
    settings_.adjustments.gamma = std::clamp(settings_.adjustments.gamma, 0.1F, 4.0F);
    settings_.adjustments.saturation = std::clamp(settings_.adjustments.saturation, 0.0F, 2.5F);
    settings_.adjustments.tint_strength = std::clamp(settings_.adjustments.tint_strength, 0.0F, 1.0F);

    settings_.adjustments.input_black = std::clamp(settings_.adjustments.input_black, 0.0F, 0.95F);
    settings_.adjustments.input_white = std::clamp(settings_.adjustments.input_white, 0.05F, 1.0F);
    if (settings_.adjustments.input_black >= settings_.adjustments.input_white) {
        settings_.adjustments.input_black = std::max(0.0F, settings_.adjustments.input_white - 0.01F);
    }

    settings_.adjustments.output_black = std::clamp(settings_.adjustments.output_black, 0.0F, 0.95F);
    settings_.adjustments.output_white = std::clamp(settings_.adjustments.output_white, 0.05F, 1.0F);
    if (settings_.adjustments.output_black >= settings_.adjustments.output_white) {
        settings_.adjustments.output_black = std::max(0.0F, settings_.adjustments.output_white - 0.01F);
    }
}

App::HistorySnapshot App::capture_history_snapshot() const
{
    return {settings_, selected_palette_};
}

void App::record_control_history(const HistorySnapshot& before)
{
    const HistorySnapshot after = capture_history_snapshot();
    const bool changed = !(before == after);
    const bool editing = ImGui::IsAnyItemActive();

    if (editing) {
        if (changed && !active_edit_snapshot_) {
            active_edit_snapshot_ = before;
        }
        return;
    }

    if (active_edit_snapshot_) {
        commit_history_change(*active_edit_snapshot_);
        active_edit_snapshot_.reset();
        return;
    }

    if (changed) {
        commit_history_change(before);
    }
}

void App::commit_history_change(const HistorySnapshot& before)
{
    if (before == capture_history_snapshot()) {
        return;
    }

    undo_stack_.push_back(before);
    if (undo_stack_.size() > kMaxHistoryEntries) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
}

void App::apply_history_snapshot(const HistorySnapshot& snapshot)
{
    settings_ = snapshot.settings;
    selected_palette_ = snapshot.selected_palette;
    if (selected_palette_ < 0 || selected_palette_ >= static_cast<int>(palettes_.size())) {
        selected_palette_ = -1;
    }
    active_edit_snapshot_.reset();
    mark_dirty();
}

void App::undo()
{
    if (!can_undo()) {
        set_status("Nothing to undo.");
        return;
    }

    redo_stack_.push_back(capture_history_snapshot());
    const HistorySnapshot snapshot = undo_stack_.back();
    undo_stack_.pop_back();
    apply_history_snapshot(snapshot);
    set_status("Undid last edit.");
}

void App::redo()
{
    if (!can_redo()) {
        set_status("Nothing to redo.");
        return;
    }

    undo_stack_.push_back(capture_history_snapshot());
    const HistorySnapshot snapshot = redo_stack_.back();
    redo_stack_.pop_back();
    apply_history_snapshot(snapshot);
    set_status("Redid last edit.");
}

bool App::can_undo() const noexcept
{
    return !undo_stack_.empty();
}

bool App::can_redo() const noexcept
{
    return !redo_stack_.empty();
}

bool App::slider_int_direct(const char* label, int& value, int minimum, int maximum)
{
    const bool changed = ImGui::SliderInt(label, &value, minimum, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        int* target = &value;
        open_number_edit(label, value, minimum, maximum, true, "%d", [target, minimum, maximum](double edited) {
            *target = std::clamp(static_cast<int>(std::lround(edited)), minimum, maximum);
        });
    }
    return changed;
}

bool App::slider_float_direct(const char* label, float& value, float minimum, float maximum, const char* format)
{
    const bool changed = ImGui::SliderFloat(label, &value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        float* target = &value;
        open_number_edit(label, value, minimum, maximum, false, format, [target, minimum, maximum](double edited) {
            *target = std::clamp(static_cast<float>(edited), minimum, maximum);
        });
    }
    return changed;
}

bool App::slider_float_direct_value(const char* label, float value, float minimum, float maximum, const char* format, std::function<void(float)> apply)
{
    float editable = value;
    const bool changed = ImGui::SliderFloat(label, &editable, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (changed) {
        apply(std::clamp(editable, minimum, maximum));
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        open_number_edit(label, value, minimum, maximum, false, format, [apply = std::move(apply), minimum, maximum](double edited) {
            apply(std::clamp(static_cast<float>(edited), minimum, maximum));
        });
    }

    return changed;
}

void App::open_number_edit(std::string label, double value, double minimum, double maximum, bool integer, std::string format, std::function<void(double)> apply)
{
    active_edit_snapshot_.reset();
    number_edit_ = NumberEditState{
        std::move(label),
        std::move(format),
        value,
        minimum,
        maximum,
        integer,
        true,
        capture_history_snapshot(),
        std::move(apply),
    };
}

} // namespace pixelizer
