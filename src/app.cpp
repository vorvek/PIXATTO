#include "pixatto/app.hpp"

#include "pixatto/image_formats.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace pixatto {
namespace {

constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;
constexpr const char* kGlslVersion =
#if defined(__APPLE__)
    "#version 150\n";
#else
    "#version 130\n";
#endif
constexpr float kViewportSplitterThickness = 8.0F;
constexpr float kViewportMinimumPaneSize = 180.0F;
constexpr const char* kModelTextureDragPayload = "PIXATTO_MODEL_TEXTURE_INDEX";
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

std::mutex& runtime_log_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::filesystem::path runtime_log_path()
{
    std::error_code ec;
    std::filesystem::path directory = std::filesystem::temp_directory_path(ec);
    if (ec || directory.empty()) {
        directory = std::filesystem::current_path(ec);
    }
    if (ec || directory.empty()) {
        directory = ".";
    }
    return directory / "pixatto_debug.log";
}

std::string runtime_log_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(ms);
}

void append_runtime_log(std::string_view message)
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    std::ofstream output(runtime_log_path(), std::ios::app);
    if (!output) {
        return;
    }

    output << runtime_log_timestamp()
           << " [thread " << std::this_thread::get_id() << "] "
           << message << '\n';
    output.flush();
}

void reset_runtime_log()
{
    std::lock_guard<std::mutex> lock(runtime_log_mutex());
    std::ofstream output(runtime_log_path(), std::ios::trunc);
    if (!output) {
        return;
    }

    output << runtime_log_timestamp()
           << " [thread " << std::this_thread::get_id() << "] "
           << "PIXATTO runtime log started. path=" << runtime_log_path().string() << '\n';
    output.flush();
}

std::string quote_path_for_log(const std::filesystem::path& path)
{
    return "\"" + path.string() + "\"";
}

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

std::string app_lowercase_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

std::string ensure_extension(std::filesystem::path path, const char* expected_extension)
{
    if (app_lowercase_extension(path) != expected_extension) {
        path.replace_extension(expected_extension);
    }
    return path.string();
}

const char* video_container_label(VideoContainer container)
{
    switch (container) {
    case VideoContainer::Mp4:
        return "MP4";
    case VideoContainer::Webm:
        return "WebM";
    case VideoContainer::Mkv:
        return "MKV";
    }
    return "MP4";
}

TextId video_audio_mode_label(VideoAudioMode mode)
{
    switch (mode) {
    case VideoAudioMode::None:
        return TextId::VideoNoAudio;
    case VideoAudioMode::Copy:
        return TextId::VideoAudioCopy;
    case VideoAudioMode::Aac:
        return TextId::VideoAudioAac;
    case VideoAudioMode::Vorbis:
        return TextId::VideoAudioVorbis;
    }
    return TextId::VideoNoAudio;
}

TextId video_hardware_speed_text(VideoHardwareSpeed speed)
{
    switch (speed) {
    case VideoHardwareSpeed::Balanced:
        return TextId::VideoHardwareSpeedBalanced;
    case VideoHardwareSpeed::Fast:
        return TextId::VideoHardwareSpeedFast;
    case VideoHardwareSpeed::VeryFast:
        return TextId::VideoHardwareSpeedVeryFast;
    }
    return TextId::VideoHardwareSpeedBalanced;
}

bool video_low_quality_process_allowed(const ProcessSettings& settings)
{
    return can_process_sampled_collapsed_on_gpu(settings);
}

std::vector<VideoContainer> video_container_options(
    const VideoExportProfile& profile,
    const VideoCapabilities& capabilities)
{
    std::vector<VideoContainer> result;
    const std::array containers = profile.codec == VideoCodec::Vp9
        ? std::array{VideoContainer::Webm, VideoContainer::Mkv, VideoContainer::Mp4}
        : std::array{VideoContainer::Mp4, VideoContainer::Mkv, VideoContainer::Webm};
    for (const VideoContainer container : containers) {
        if (video_profile_supports_container(profile, container)
            && capabilities.has_muxer(video_container_muxer(container))) {
            result.push_back(container);
        }
    }
    return result;
}

struct VideoAudioOption {
    VideoAudioMode mode = VideoAudioMode::None;
    std::string encoder;
};

std::vector<VideoAudioOption> video_audio_options(
    const VideoMetadata& metadata,
    const VideoCapabilities& capabilities,
    VideoContainer container)
{
    std::vector<VideoAudioOption> result;
    result.push_back({VideoAudioMode::None, {}});
    if (!metadata.has_audio) {
        return result;
    }
    if (can_copy_audio_to_container(metadata.audio_codec, container, metadata.container_format)) {
        result.push_back({VideoAudioMode::Copy, {}});
    }
    if (can_encode_audio_to_container(VideoAudioMode::Aac, container) && capabilities.has_audio_encoder("aac")) {
        result.push_back({VideoAudioMode::Aac, "aac"});
    }
    if (can_encode_audio_to_container(VideoAudioMode::Vorbis, container)) {
        if (capabilities.has_audio_encoder("libvorbis")) {
            result.push_back({VideoAudioMode::Vorbis, "libvorbis"});
        } else if (capabilities.has_audio_encoder("vorbis")) {
            result.push_back({VideoAudioMode::Vorbis, "vorbis"});
        }
    }
    return result;
}

VideoAudioMode preferred_video_audio_mode(const std::vector<VideoAudioOption>& options)
{
    for (const VideoAudioMode preferred : {VideoAudioMode::Copy, VideoAudioMode::Aac, VideoAudioMode::Vorbis, VideoAudioMode::None}) {
        const auto found = std::find_if(options.begin(), options.end(), [preferred](const VideoAudioOption& option) {
            return option.mode == preferred;
        });
        if (found != options.end()) {
            return found->mode;
        }
    }
    return VideoAudioMode::None;
}

std::string sanitize_filename_suffix(std::string suffix)
{
    for (char& ch : suffix) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32U || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|'
            || ch == '?' || ch == '*') {
            ch = '_';
        }
    }
    return suffix;
}

std::filesystem::path batch_output_path(
    const std::filesystem::path& output_dir,
    const std::filesystem::path& source,
    std::string suffix,
    std::string_view extension)
{
    std::string stem = source.stem().string();
    if (stem.empty()) {
        stem = "image";
    }
    stem += sanitize_filename_suffix(std::move(suffix));
    stem += extension;
    return output_dir / stem;
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

ImTextureID imgui_texture_id(std::uintptr_t handle)
{
    return static_cast<ImTextureID>(handle);
}

struct CameraVector {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

CameraVector cross(CameraVector lhs, CameraVector rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

float dot(CameraVector lhs, CameraVector rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

CameraVector normalize(CameraVector value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {value.x / length, value.y / length, value.z / length};
}

void model_camera_basis(float yaw, float pitch, CameraVector& right, CameraVector& up, CameraVector& forward)
{
    const float clamped_pitch = std::clamp(pitch, -1.45F, 1.45F);
    const CameraVector eye_direction = {
        std::sin(yaw) * std::cos(clamped_pitch),
        std::sin(clamped_pitch),
        std::cos(yaw) * std::cos(clamped_pitch),
    };
    forward = normalize({-eye_direction.x, -eye_direction.y, -eye_direction.z});
    right = normalize(cross(forward, {0.0F, 1.0F, 0.0F}));
    up = normalize(cross(right, forward));
}

ImVec2 projected_axis(CameraVector axis, CameraVector right, CameraVector up, float length)
{
    return ImVec2(dot(axis, right) * length, -dot(axis, up) * length);
}

void draw_model_gizmo(ImDrawList* draw_list, ImVec2 image_min, ImVec2 image_max, float yaw, float pitch)
{
    if (!draw_list || image_max.x - image_min.x < 96.0F || image_max.y - image_min.y < 96.0F) {
        return;
    }

    CameraVector right;
    CameraVector up;
    CameraVector forward;
    model_camera_basis(yaw, pitch, right, up, forward);

    struct Axis {
        CameraVector direction;
        const char* label = "";
        ImU32 color = 0;
        float depth = 0.0F;
    };

    std::array<Axis, 3> axes = {{
        {{1.0F, 0.0F, 0.0F}, "X", IM_COL32(224, 73, 73, 255), dot({1.0F, 0.0F, 0.0F}, forward)},
        {{0.0F, 1.0F, 0.0F}, "Y", IM_COL32(92, 192, 112, 255), dot({0.0F, 1.0F, 0.0F}, forward)},
        {{0.0F, 0.0F, 1.0F}, "Z", IM_COL32(86, 142, 235, 255), dot({0.0F, 0.0F, 1.0F}, forward)},
    }};
    std::sort(axes.begin(), axes.end(), [](const Axis& lhs, const Axis& rhs) {
        return lhs.depth < rhs.depth;
    });

    const ImVec2 center(image_max.x - 62.0F, image_min.y + 62.0F);
    const float radius = 46.0F;
    const float length = 33.0F;
    draw_list->AddCircleFilled(center, radius, IM_COL32(10, 12, 16, 120), 32);
    draw_list->AddCircle(center, radius, IM_COL32(255, 255, 255, 34), 32, 1.0F);

    for (const Axis& axis : axes) {
        const ImVec2 projected = projected_axis(axis.direction, right, up, length);
        const ImVec2 end(center.x + projected.x, center.y + projected.y);
        draw_list->AddLine(center, end, axis.color, 3.0F);
        draw_list->AddCircleFilled(end, 6.0F, axis.color, 16);
        draw_list->AddText(ImVec2(end.x + 7.0F, end.y - 8.0F), axis.color, axis.label);
    }
    draw_list->AddCircleFilled(center, 3.0F, IM_COL32(235, 238, 244, 220), 12);
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

void draw_wastebasket_icon(ImDrawList* draw_list, ImVec2 min, ImVec2 max, ImU32 color)
{
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float scale = std::min(width, height);
    const float icon_width = scale * 0.48F;
    const float icon_height = scale * 0.54F;
    const float left = min.x + (width - icon_width) * 0.5F;
    const float top = min.y + (height - icon_height) * 0.5F + scale * 0.04F;
    const float right = left + icon_width;
    const float bottom = top + icon_height;
    const float lid_y = top + scale * 0.08F;
    const float body_top = top + scale * 0.18F;
    const float thickness = std::max(1.25F, scale * 0.075F);

    draw_list->AddLine(ImVec2(left - scale * 0.05F, lid_y), ImVec2(right + scale * 0.05F, lid_y), color, thickness);
    draw_list->AddLine(ImVec2(left + icon_width * 0.33F, top), ImVec2(right - icon_width * 0.33F, top), color, thickness);
    draw_list->AddRect(ImVec2(left, body_top), ImVec2(right, bottom), color, 1.5F, 0, thickness);
    draw_list->AddLine(ImVec2(left + icon_width * 0.35F, body_top + scale * 0.08F), ImVec2(left + icon_width * 0.35F, bottom - scale * 0.08F), color, thickness * 0.8F);
    draw_list->AddLine(ImVec2(right - icon_width * 0.35F, body_top + scale * 0.08F), ImVec2(right - icon_width * 0.35F, bottom - scale * 0.08F), color, thickness * 0.8F);
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
    reset_runtime_log();
    append_runtime_log("initialize: begin");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        append_runtime_log(std::string("initialize: SDL_Init failed: ") + SDL_GetError());
        set_status(textf(TextId::StatusSdlInitFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    append_runtime_log("initialize: SDL_Init ok");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    window_ = SDL_CreateWindow("PIXATTO", kInitialWidth, kInitialHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window_) {
        append_runtime_log(std::string("initialize: SDL_CreateWindow failed: ") + SDL_GetError());
        set_status(textf(TextId::StatusSdlCreateWindowFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    append_runtime_log("initialize: SDL_CreateWindow ok");
    set_window_icon(window_);

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        append_runtime_log(std::string("initialize: SDL_GL_CreateContext failed: ") + SDL_GetError());
        set_status(textf(TextId::StatusRendererSetupFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    append_runtime_log("initialize: SDL_GL_CreateContext ok");
    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(1);

    std::string renderer_error;
    if (!model_renderer_.initialize(kGlslVersion, renderer_error)) {
        append_runtime_log(std::string("initialize: model renderer setup failed: ") + renderer_error);
        set_status(textf(TextId::StatusRendererSetupFailedFormat, {{"error", renderer_error}}));
        return false;
    }
    append_runtime_log("initialize: model renderer setup ok");

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
    ImGui_ImplSDL3_InitForOpenGL(window_, gl_context_);
    ImGui_ImplOpenGL3_Init(kGlslVersion);
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    refresh_palettes();
    refresh_presets();
    append_runtime_log("initialize: complete");
    return true;
}

int App::run()
{
    running_ = true;
    while (running_) {
        process_events(running_);
        drain_file_commands();
        service_video_export_gpu_queue();
        update_pending_model_load();
        update_pending_video_export();
        update_pending_video_hardware_probe();
        update_pending_video_preview();
        update_video_playback();
        update_preview_if_needed();
        update_batch_processing();
        render_frame();
    }

    return 0;
}

void App::shutdown()
{
    destroy_texture(original_texture_);
    destroy_texture(result_texture_);
    clear_video_document();
    clear_model_document();
    if (window_ && gl_context_) {
        SDL_GL_MakeCurrent(window_, gl_context_);
    }
    video_export_gpu_processor_.reset();
    model_renderer_.shutdown();

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    if (gl_context_) {
        SDL_GL_DestroyContext(gl_context_);
        gl_context_ = nullptr;
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
            const std::filesystem::path dropped_path(event.drop.data);
            append_runtime_log(std::string("drop: received ") + quote_path_for_log(dropped_path));
            if (batch_ && !batch_->processing) {
                add_batch_images({dropped_path});
            } else if (document_mode_ == DocumentMode::Model && is_importable_image_path(dropped_path)) {
                append_runtime_log("drop: importing image into model texture drawer");
                import_model_texture_from_path(dropped_path);
            } else {
                append_runtime_log("drop: queueing file command");
                file_commands_.submit_drop(dropped_path, document_mode_ == DocumentMode::Model || !original_.empty());
            }
        }
    }
}

void App::render_frame()
{
    if (!ensure_gl_context_current()) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
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
    ImGui::Begin("PIXATTO Workspace", nullptr, flags);
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
    render_save_preset_popup();
    render_preset_overwrite_popup();
    render_delete_preset_popup();
    render_batch_dialog();
    render_video_export_dialog();
    render_language_picker_popup();
    render_help_dialog();
    render_about_dialog();

    ImGui::Render();

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSizeInPixels(window_, &drawable_width, &drawable_height);
    glViewport(0, 0, drawable_width, drawable_height);
    glClearColor(22.0F / 255.0F, 24.0F / 255.0F, 28.0F / 255.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
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

    if (ImGui::BeginMenu(imgui_label(TextId::Import, "Import").c_str())) {
        if (ImGui::MenuItem(imgui_label(TextId::OpenImage, "ImportImage").c_str())) {
            request_open_image();
        }
        if (ImGui::MenuItem(imgui_label(TextId::ImportVideo, "ImportVideo").c_str())) {
            request_open_video();
        }
        if (ImGui::MenuItem(imgui_label(TextId::ImportPalette, "ImportPalette").c_str())) {
            request_import_palette();
        }
        if (ImGui::MenuItem(imgui_label(TextId::ImportModel, "ImportModel").c_str())) {
            request_open_model();
        }
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu(imgui_label(TextId::Export, "Export").c_str())) {
        if (document_mode_ == DocumentMode::Video) {
            if (ImGui::MenuItem(imgui_label(TextId::ExportVideo, "ExportVideo").c_str())) {
                request_export_video();
            }
        } else {
            if (ImGui::MenuItem(imgui_label(TextId::ExportPng, "ExportPng").c_str())) {
                request_export_png();
            }
            if (ImGui::MenuItem(imgui_label(TextId::ExportRaw, "ExportRaw").c_str())) {
                request_export_raw();
            }
        }
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::Button(imgui_label(TextId::BatchButton, "Batch").c_str())) {
        request_batch();
    }
    ImGui::SameLine();
    const bool single_viewport = viewport_mode_ == ViewportMode::Single;
    if (document_mode_ == DocumentMode::Video) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(imgui_label(single_viewport ? TextId::TwoViews : TextId::OneView, "ViewportMode").c_str())) {
        viewport_mode_ = single_viewport ? ViewportMode::Split : ViewportMode::Single;
    }
    if (document_mode_ == DocumentMode::Video) {
        ImGui::EndDisabled();
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
    const float help_width = std::max(
        ImGui::GetFrameHeight(),
        ImGui::CalcTextSize(text(TextId::HelpButtonLabel)).x + style.FramePadding.x * 2.0F);
    const float about_width = std::max(
        ImGui::GetFrameHeight(),
        ImGui::CalcTextSize(text(TextId::AboutButtonLabel)).x + style.FramePadding.x * 2.0F);
    const float right_controls_width = language_width + style.ItemSpacing.x + help_width + style.ItemSpacing.x + about_width;
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
    if (ImGui::Button(imgui_label(TextId::HelpButtonLabel, "HelpButton").c_str(), ImVec2(help_width, 0.0F))) {
        open_help_dialog_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(TextId::HelpButtonTooltip));
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
    const float popup_height = std::min(410.0F, std::max(260.0F, viewport->WorkSize.y - 24.0F));
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
    const float full_list_height = static_cast<float>(rows_per_column) * kLanguageOptionRowHeight
        + static_cast<float>(rows_per_column) * style.ItemSpacing.y;
    const float close_button_area = ImGui::GetFrameHeight() + style.WindowPadding.y + style.ItemSpacing.y;
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

    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + style.ItemSpacing.y, ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight()));
    if (ImGui::Button(imgui_label(TextId::Close, "CloseLanguagePicker").c_str()) || !popup_open) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

void App::render_help_dialog()
{
    const std::string popup_id = imgui_label(TextId::HelpWindowTitle, "HelpDialog");

    if (open_help_dialog_) {
        ImGui::OpenPopup(popup_id.c_str());
        open_help_dialog_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popup_width = std::min(560.0F, std::max(340.0F, viewport->WorkSize.x - 32.0F));
    const float popup_height = std::min(500.0F, std::max(330.0F, viewport->WorkSize.y - 32.0F));
    ImGui::SetNextWindowSize(ImVec2(popup_width, popup_height), ImGuiCond_Appearing);

    bool popup_open = true;
    if (!ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float close_button_area = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    const float content_height = std::max(170.0F, ImGui::GetContentRegionAvail().y - close_button_area);
    auto wrapped_bullet = [](const char* value) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", value);
    };
    auto section = [&](TextId title) {
        ImGui::Spacing();
        ImGui::TextUnformatted(text(title));
        ImGui::Separator();
    };

    ImGui::BeginChild("##HelpContent", ImVec2(0.0F, content_height), ImGuiChildFlags_None);
    section(TextId::HelpSectionKeyboard);
    wrapped_bullet(text(TextId::HelpUndo));
    wrapped_bullet(text(TextId::HelpRedo));
    wrapped_bullet(text(TextId::HelpNumericEntry));

    section(TextId::HelpSectionMouse);
    wrapped_bullet(text(TextId::HelpImageZoom));
    wrapped_bullet(text(TextId::HelpModelOrbit));
    wrapped_bullet(text(TextId::HelpModelPan));
    wrapped_bullet(text(TextId::HelpModelZoom));
    wrapped_bullet(text(TextId::HelpModelReset));
    wrapped_bullet(text(TextId::HelpModelOrigin));

    section(TextId::HelpSectionFiles);
    wrapped_bullet(text(TextId::HelpDragDropFiles));
    wrapped_bullet(text(TextId::HelpTextureAssign));
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button(imgui_label(TextId::Close, "CloseHelpDialog").c_str()) || !popup_open) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
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
    ImGui::TextUnformatted("PIXATTO");
    ImGui::Separator();
    ImGui::TextWrapped("%s", text(TextId::AboutProjectCredit));
    ImGui::TextWrapped("%s", text(TextId::AboutProjectLicense));

    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::AboutThirdPartyTitle));
    ImGui::Separator();
    ImGui::TextUnformatted(text(TextId::AboutDependenciesTitle));
    wrapped_bullet(text(TextId::AboutDependencySdl));
    wrapped_bullet(text(TextId::AboutDependencySdlImage));
    wrapped_bullet(text(TextId::AboutDependencyImGui));
    wrapped_bullet(text(TextId::AboutDependencyStb));
    wrapped_bullet(text(TextId::AboutDependencyTinyGltf));
    wrapped_bullet(text(TextId::AboutDependencyTinyObj));
    wrapped_bullet(text(TextId::AboutDependencyUfbx));
    wrapped_bullet(text(TextId::AboutDependencyTinyXml));
    wrapped_bullet(text(TextId::AboutDependencyFfmpegExternal));

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

void App::render_model_materials()
{
    ImGui::TextUnformatted(text(TextId::ModelMaterials));
    ImGui::Separator();

    if (model_.empty()) {
        ImGui::TextDisabled("%s", text(TextId::NoModelLoaded));
        return;
    }

    const std::vector<ModelMaterialSlot> slots = model_material_slots();
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const ModelMaterialSlot& slot = slots[index];
        std::string name = slot.material_name.empty() ? slot.mesh_name : slot.material_name;
        if (name.empty()) {
            name = "Material " + std::to_string(index + 1U);
        } else if (!slot.material_name.empty() && !slot.mesh_name.empty() && slot.material_name != slot.mesh_name) {
            name = slot.mesh_name + " / " + slot.material_name;
        }

        const std::string current = slot.texture_index >= 0
            ? model_texture_display_name(static_cast<std::size_t>(slot.texture_index))
            : std::string(text(TextId::UntexturedGrey));

        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginChild("MaterialSlot", ImVec2(0.0F, 66.0F), ImGuiChildFlags_Borders);
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::TextDisabled("%s", current.c_str());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelTextureDragPayload)) {
                if (payload->DataSize == static_cast<int>(sizeof(int))) {
                    int texture_index = -1;
                    std::memcpy(&texture_index, payload->Data, sizeof(texture_index));
                    if (texture_index >= 0) {
                        assign_model_texture_to_slot(slot, static_cast<std::size_t>(texture_index));
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::GetDragDropPayload() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            ImGui::SetTooltip("%s", text(TextId::DropTextureOnMaterial));
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

void App::render_model_texture_drawer()
{
    ImGui::Spacing();
    ImGui::TextUnformatted(text(TextId::TextureDrawer));
    ImGui::Separator();

    if (model_.textures.empty()) {
        ImGui::TextWrapped("%s", text(TextId::TextureDrawerHint));
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float thumbnail = 46.0F;
    for (std::size_t index = 0; index < model_.textures.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const Texture* preview = index < model_original_textures_.size() ? &model_original_textures_[index] : nullptr;
        const bool has_preview = preview && preview->handle != 0U && preview->width > 0 && preview->height > 0;
        if (has_preview) {
            const float scale = std::min(thumbnail / static_cast<float>(preview->width), thumbnail / static_cast<float>(preview->height));
            const ImVec2 size(
                std::max(1.0F, static_cast<float>(preview->width) * scale),
                std::max(1.0F, static_cast<float>(preview->height) * scale));
            ImGui::Image(imgui_texture_id(preview->handle), size);
        } else {
            ImGui::Button("##TexturePlaceholder", ImVec2(thumbnail, thumbnail));
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const int payload_index = static_cast<int>(index);
            ImGui::SetDragDropPayload(kModelTextureDragPayload, &payload_index, sizeof(payload_index));
            const std::string name = model_texture_display_name(index);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine();
        const float text_width = std::max(60.0F, ImGui::GetContentRegionAvail().x);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_width);
        ImGui::TextUnformatted(model_texture_display_name(index).c_str());
        ImGui::PopTextWrapPos();
        if (model_.textures[index].embedded) {
            ImGui::TextDisabled("%s", "Embedded");
        }
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0.0F, style.ItemSpacing.y));
        ImGui::PopID();
    }
}

void App::render_preset_picker(ProcessSettings& settings, int& selected_palette)
{
    ImGui::TextUnformatted(text(TextId::Presets));
    ImGui::Separator();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float save_width = ImGui::CalcTextSize(text(TextId::SavePreset)).x + style.FramePadding.x * 2.0F;
    const float delete_width = ImGui::GetFrameHeight();
    const float action_width = save_width + delete_width + style.ItemSpacing.x * 2.0F;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float picker_width = std::max(80.0F, available_width - action_width);
    const int selected_index = effective_selected_preset_index(settings);
    const char* preview = text(TextId::Presets);
    if (presets_.empty()) {
        preview = text(TextId::NoPresetsSaved);
    } else if (selected_index >= 0 && selected_index < static_cast<int>(presets_.size())) {
        preview = presets_[static_cast<std::size_t>(selected_index)].name.c_str();
    }

    if (presets_.empty()) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(picker_width);
    if (ImGui::BeginCombo("##PresetPicker", preview, ImGuiComboFlags_HeightLarge)) {
        for (std::size_t index = 0; index < presets_.size(); ++index) {
            const Preset& preset = presets_[index];
            const bool selected = selected_index == static_cast<int>(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(preset.name.c_str(), selected)) {
                selected_preset_ = static_cast<int>(index);
                apply_preset_settings(preset, settings, selected_palette);
                set_status(textf(TextId::StatusAppliedPresetFormat, {{"name", preset.name}}));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (presets_.empty()) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool can_delete = selected_index >= 0 && selected_index < static_cast<int>(presets_.size());
    if (!can_delete) {
        ImGui::BeginDisabled();
    }
    const bool delete_pressed = ImGui::Button("##DeletePreset", ImVec2(delete_width, 0.0F));
    draw_wastebasket_icon(
        ImGui::GetWindowDrawList(),
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        can_delete ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if (delete_pressed) {
        selected_preset_ = selected_index;
        request_delete_selected_preset();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(TextId::DeletePreset));
    }
    if (!can_delete) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(imgui_label(TextId::SavePreset, "SavePreset").c_str(), ImVec2(save_width, 0.0F))) {
        request_save_preset();
    }
    ImGui::Spacing();
}

void App::render_controls()
{
    auto edit = edit_session_.begin_edit();
    ProcessSettings& settings = edit.settings();
    int& selected_palette = edit.selected_palette();

    if (document_mode_ == DocumentMode::Model) {
        render_model_materials();
        render_model_texture_drawer();
        ImGui::Spacing();
    }

    render_preset_picker(settings, selected_palette);

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

    if (document_mode_ == DocumentMode::Video) {
        ImGui::BeginChild("VideoPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_video_view();
        ImGui::EndChild();
        return;
    }

    if (viewport_mode_ == ViewportMode::Single) {
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_working_view();
        ImGui::EndChild();
        return;
    }

    if (viewport_layout_ == ViewportLayout::SideBySide) {
        if (available.x <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_original_view();
            ImGui::EndChild();
            return;
        }

        const float splitter_width = splitter_thickness_for(available.x);
        const float usable_width = available.x - splitter_width;
        float original_width = split_size_from_ratio(viewport_split_ratio_, usable_width);
        viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);

        ImGui::BeginChild("OriginalPane", ImVec2(original_width, 0), ImGuiChildFlags_Borders);
        render_original_view();
        ImGui::EndChild();

        ImGui::SameLine(0.0F, 0.0F);
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterX", ImVec2(splitter_width, available.y), ImGuiMouseCursor_ResizeEW, delta)) {
            original_width = split_size_from_ratio(viewport_split_ratio_, usable_width) + delta;
            viewport_split_ratio_ = ratio_from_split_size(original_width, usable_width);
        }

        ImGui::SameLine(0.0F, 0.0F);
        ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_working_view();
        ImGui::EndChild();
    } else {
        if (available.y <= kViewportSplitterThickness * 2.0F) {
            ImGui::BeginChild("ResultPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
            render_working_view();
            ImGui::EndChild();
            return;
        }

        const float splitter_height = splitter_thickness_for(available.y);
        const float usable_height = available.y - splitter_height;
        float result_height = split_size_from_ratio(viewport_split_ratio_, usable_height);
        viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);

        ImGui::BeginChild("ResultPane", ImVec2(0, result_height), ImGuiChildFlags_Borders);
        render_working_view();
        ImGui::EndChild();

        remove_vertical_item_spacing();
        float delta = 0.0F;
        if (render_splitter("##ViewportSplitterY", ImVec2(available.x, splitter_height), ImGuiMouseCursor_ResizeNS, delta)) {
            result_height = split_size_from_ratio(viewport_split_ratio_, usable_height) + delta;
            viewport_split_ratio_ = ratio_from_split_size(result_height, usable_height);
        }

        remove_vertical_item_spacing();
        ImGui::BeginChild("OriginalPane", ImVec2(0, 0), ImGuiChildFlags_Borders);
        render_original_view();
        ImGui::EndChild();
    }
}

void App::render_original_view()
{
    if (document_mode_ == DocumentMode::Model) {
        render_model_texture_gallery();
        return;
    }

    render_image_view(TextId::Original, "Original", original_texture_, original_zoom_, false);
}

void App::render_working_view()
{
    if (document_mode_ == DocumentMode::Model) {
        render_model_view();
        return;
    }

    render_image_view(TextId::Result, "Result", result_texture_, result_zoom_, true);
}

void App::render_image_view(TextId label, const char* id, Texture& texture, float& zoom, bool show_close_file)
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
    if (show_close_file) {
        render_close_file_button();
    }

    ImGui::Separator();

    ImGui::BeginChild((std::string(id) + "Scroll").c_str(), ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (texture.handle == 0U) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (avail.x - 140.0F) * 0.5F), std::max(0.0F, (avail.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("%s", text(TextId::NoImageLoaded));
    } else {
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0F) {
            zoom = std::clamp(zoom * (ImGui::GetIO().MouseWheel > 0.0F ? 1.12F : 0.89F), 0.05F, 32.0F);
        }

        const ImVec2 size(static_cast<float>(texture.width) * zoom, static_cast<float>(texture.height) * zoom);
        ImGui::Image(imgui_texture_id(texture.handle), size);
    }
    ImGui::EndChild();
}

void App::render_video_view()
{
    const ImVec2 pane_available = ImGui::GetContentRegionAvail();

    ImGui::TextUnformatted(text(TextId::VideoPreview));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::SliderFloat(imgui_label(TextId::Zoom, "ZoomVideo").c_str(), &result_zoom_, 0.05F, 32.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (ImGui::SmallButton("1:1##OneToOneVideo")) {
        result_zoom_ = 1.0F;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(imgui_label(TextId::Fit, "FitVideo").c_str())) {
        result_zoom_ = fit_zoom_for_size(result_texture_.width, result_texture_.height, pane_available);
    }
    render_close_file_button();

    ImGui::Separator();

    const bool export_progress_visible = pending_video_export_ && pending_video_export_->progress;
    const float timeline_height = ImGui::GetFrameHeightWithSpacing() * (export_progress_visible ? 5.4F : 3.2F);
    ImGui::BeginChild("VideoScroll", ImVec2(0, -timeline_height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (result_texture_.handle == 0U) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (avail.x - 160.0F) * 0.5F), std::max(0.0F, (avail.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("%s", text(TextId::NoVideoFrame));
    } else {
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0F) {
            result_zoom_ = std::clamp(result_zoom_ * (ImGui::GetIO().MouseWheel > 0.0F ? 1.12F : 0.89F), 0.05F, 32.0F);
        }

        const ImVec2 size(static_cast<float>(result_texture_.width) * result_zoom_, static_cast<float>(result_texture_.height) * result_zoom_);
        ImGui::Image(imgui_texture_id(result_texture_.handle), size);
    }
    ImGui::EndChild();

    ImGui::Separator();
    const bool has_video = document_mode_ == DocumentMode::Video && video_.metadata.duration_seconds > 0.0;
    if (!has_video) {
        ImGui::TextDisabled("%s", text(TextId::NoVideoLoaded));
        return;
    }

    const bool disabled_by_export = pending_video_export_.has_value();
    if (disabled_by_export) {
        ImGui::BeginDisabled();
    }

    if (ImGui::SmallButton(imgui_label(video_.playing ? TextId::Pause : TextId::Play, "VideoPlayPause").c_str())) {
        video_.playing = !video_.playing;
        video_.last_tick = std::chrono::steady_clock::now();
        if (video_.playing) {
            video_.playback_seek_pending = true;
        } else {
            request_video_preview(video_.current_time, true);
        }
    }
    ImGui::SameLine();
    float timeline = static_cast<float>(video_.current_time);
    ImGui::SetNextItemWidth(std::max(120.0F, ImGui::GetContentRegionAvail().x - 190.0F));
    if (ImGui::SliderFloat(
            imgui_label(TextId::Timeline, "VideoTimeline").c_str(),
            &timeline,
            0.0F,
            static_cast<float>(std::max(0.001, video_.metadata.duration_seconds)),
            "")) {
        video_.current_time = std::clamp(static_cast<double>(timeline), 0.0, video_.metadata.duration_seconds);
        video_.playing = false;
        request_video_preview(video_.current_time, true);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted((format_video_time(video_.current_time) + " / " + format_video_time(video_.metadata.duration_seconds)).c_str());

    const long long frame_index = video_.metadata.fps > 0.0
        ? static_cast<long long>(std::floor(video_.current_time * video_.metadata.fps))
        : 0;
    const long long total_frames = std::max<long long>(1, video_.metadata.frame_count);
    const long long display_frame = std::clamp(frame_index + 1, 1LL, total_frames);
    const std::string frame_text = textf(
        TextId::VideoFrameFormat,
        {{"frame", std::to_string(display_frame)},
         {"total", std::to_string(total_frames)},
         {"fps", format_video_fps(video_.metadata.fps)}});
    ImGui::TextDisabled("%s", frame_text.c_str());

    if (disabled_by_export) {
        ImGui::EndDisabled();
    }

    if (export_progress_visible) {
        const int percent = pending_video_export_->progress->percent.load();
        ImGui::ProgressBar(std::clamp(percent / 100.0F, 0.0F, 1.0F), ImVec2(-1.0F, 0.0F));
        const int done = pending_video_export_->progress->frames_done.load();
        const int total = pending_video_export_->progress->frames_total.load();
        const bool encoding = pending_video_export_->progress->encoding.load();
        const double elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pending_video_export_->started_at)
                                           .count();
        std::string eta = "--";
        if (!encoding && done > 0 && total > done) {
            eta = format_video_time(elapsed_seconds * static_cast<double>(total - done) / static_cast<double>(done));
        } else if (total > 0 && done >= total) {
            eta = format_video_time(0.0);
        }
        const std::string timing = textf(
            TextId::VideoExportTimingFormat,
            {{"elapsed", format_video_time(elapsed_seconds)}, {"eta", eta}});
        const std::string progress = encoding
            ? text(TextId::VideoEncoding)
            : textf(
                TextId::VideoExportProgressFormat,
                {{"percent", std::to_string(percent)},
                 {"done", std::to_string(done)},
                 {"total", std::to_string(std::max(done, total))}});
        ImGui::TextDisabled("%s", progress.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", timing.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(imgui_label(TextId::Stop, "CancelVideoExport").c_str())) {
            pending_video_export_->progress->cancel_requested = true;
            close_video_export_gpu_queue(
                pending_video_export_->gpu_queue,
                VideoGpuProcessResult::Canceled,
                "Video export canceled.");
        }
    }
}

void App::render_close_file_button()
{
    if (!has_current_file()) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(text(TextId::CloseFile)).x + style.FramePadding.x * 2.0F;
    const float right_x = ImGui::GetWindowContentRegionMax().x - width;
    if (right_x > ImGui::GetCursorPosX() + style.ItemSpacing.x) {
        ImGui::SameLine(right_x);
    } else {
        ImGui::SameLine();
    }

    if (ImGui::SmallButton(imgui_label(TextId::CloseFile, "CloseFile").c_str())) {
        close_current_file();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(TextId::CloseFileTooltip));
    }
}

void App::render_model_texture_gallery()
{
    ImGui::TextUnformatted(text(TextId::OriginalTextures));
    ImGui::Separator();

    ImGui::BeginChild("ModelTextureGalleryScroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (model_original_textures_.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (avail.x - 170.0F) * 0.5F), std::max(0.0F, (avail.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("%s", text(TextId::NoModelTextures));
        ImGui::EndChild();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float available_width = std::max(240.0F, ImGui::GetContentRegionAvail().x - style.ScrollbarSize);
    const float image_gap = style.ItemSpacing.x * 2.0F;
    const float thumbnail_width = std::min(260.0F, std::max(96.0F, (available_width - image_gap) * 0.5F));
    auto render_texture_preview = [&](TextId label, const Texture* texture) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(text(label));
        if (texture && texture->handle != 0U && texture->width > 0 && texture->height > 0) {
            const float scale = std::min(thumbnail_width / static_cast<float>(texture->width), 220.0F / static_cast<float>(texture->height));
            const ImVec2 size(
                std::max(1.0F, static_cast<float>(texture->width) * std::max(scale, 0.05F)),
                std::max(1.0F, static_cast<float>(texture->height) * std::max(scale, 0.05F)));
            ImGui::Image(imgui_texture_id(texture->handle), size);
        } else {
            ImGui::Dummy(ImVec2(thumbnail_width, 48.0F));
        }
        ImGui::EndGroup();
    };

    for (std::size_t index = 0; index < model_original_textures_.size(); ++index) {
        const ModelTexture& source = model_.textures[index];
        Texture& original_texture = model_original_textures_[index];
        const Texture* result_texture = index < model_result_textures_.size() ? &model_result_textures_[index] : nullptr;
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextWrapped("%s", source.name.empty() ? default_model_texture_export_name(source, index).c_str() : source.name.c_str());
        render_texture_preview(TextId::Original, &original_texture);
        ImGui::SameLine(0.0F, image_gap);
        render_texture_preview(TextId::Result, result_texture);
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::render_model_view()
{
    ImGui::TextUnformatted(text(TextId::ModelPreview));
    ImGui::SameLine();
    if (ImGui::SmallButton(imgui_label(TextId::ResetView, "ResetModelView").c_str())) {
        reset_model_camera();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(imgui_label(TextId::ResetToOrigin, "ResetModelOrigin").c_str())) {
        reset_model_camera_to_origin();
    }
    render_close_file_button();
    ImGui::Separator();

    ImGui::BeginChild("ModelPreviewScroll", ImVec2(0, 0), ImGuiChildFlags_None);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int preview_width = std::max(1, static_cast<int>(std::floor(available.x)));
    const int preview_height = std::max(1, static_cast<int>(std::floor(available.y)));

    if (model_.empty()) {
        ImGui::SetCursorPos(ImVec2(std::max(0.0F, (available.x - 140.0F) * 0.5F), std::max(0.0F, (available.y - 20.0F) * 0.5F)));
        ImGui::TextDisabled("%s", text(TextId::NoModelLoaded));
        ImGui::EndChild();
        return;
    }

    if (!ensure_gl_context_current()) {
        ImGui::EndChild();
        return;
    }

    std::string error;
    const bool log_preview_frame = model_preview_log_frames_ > 0;
    if (log_preview_frame) {
        append_runtime_log(
            std::string("model-preview: render_preview begin ")
            + std::to_string(preview_width) + "x" + std::to_string(preview_height)
            + " primitives=" + std::to_string(model_.primitives.size())
            + " textures=" + std::to_string(model_processed_textures_.size()));
    }
    const std::uintptr_t preview_texture = model_renderer_.render_preview(
        model_,
        preview_width,
        preview_height,
        model_yaw_,
        model_pitch_,
        model_distance_,
        model_target_offset_x_,
        model_target_offset_y_,
        model_target_offset_z_,
        error);
    if (log_preview_frame) {
        append_runtime_log(
            std::string("model-preview: render_preview end texture=") + std::to_string(preview_texture)
            + " error=" + (error.empty() ? std::string("<none>") : error));
        --model_preview_log_frames_;
    }
    if (!error.empty()) {
        append_runtime_log(std::string("model-preview: render error: ") + error);
        set_status(textf(TextId::StatusModelRenderFailedFormat, {{"error", error}}));
    }

    if (preview_texture != 0U) {
        ImGui::Image(
            imgui_texture_id(preview_texture),
            ImVec2(static_cast<float>(preview_width), static_cast<float>(preview_height)),
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F));
        const ImVec2 image_min = ImGui::GetItemRectMin();
        const ImVec2 image_max = ImGui::GetItemRectMax();
        draw_model_gizmo(ImGui::GetWindowDrawList(), image_min, image_max, model_yaw_, model_pitch_);
        if (ImGui::IsItemHovered()) {
            ImGuiIO& io = ImGui::GetIO();
            const bool pan_drag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
                || ImGui::IsMouseDragging(ImGuiMouseButton_Right)
                || (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
            if (pan_drag) {
                pan_model_camera(io.MouseDelta.x, io.MouseDelta.y);
            } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                model_yaw_ += io.MouseDelta.x * 0.01F;
                model_pitch_ = std::clamp(model_pitch_ + io.MouseDelta.y * 0.01F, -1.45F, 1.45F);
            }
            if (io.MouseWheel != 0.0F) {
                model_distance_ = std::clamp(model_distance_ * (io.MouseWheel > 0.0F ? 0.88F : 1.14F), 0.8F, 12.0F);
            }
        }
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
    const std::string popup_id = imgui_label(TextId::OpenDroppedFileTitle, "OpenDroppedFile");

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
        ImGui::TextUnformatted(text(TextId::ReplaceDroppedFile));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", pending_dropped_image_->filename().string().c_str());

        if (ImGui::Button(imgui_label(TextId::Open, "OpenDroppedImageConfirm").c_str())) {
            const auto path = *pending_dropped_image_;
            pending_dropped_image_.reset();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
            load_document_from_path(path);
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

void App::render_save_preset_popup()
{
    const std::string popup_id = imgui_label(TextId::SavePresetAsTitle, "SavePresetAs");

    if (preset_save_as_ && preset_save_as_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        preset_save_as_->request_open = false;
    }

    if (!preset_save_as_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(text(TextId::PresetName));
        ImGui::SetNextItemWidth(260.0F);
        const bool submitted = ImGui::InputText(
            "##PresetName",
            preset_save_as_->name.data(),
            preset_save_as_->name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        const bool save_clicked = ImGui::Button(imgui_label(TextId::Save, "SavePresetAsName").c_str());
        if (submitted || save_clicked) {
            if (save_preset_as_name(preset_save_as_->name.data())) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            } else if (pending_preset_overwrite_ && pending_preset_overwrite_->request_open) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelSavePresetAs").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        preset_save_as_.reset();
    }
}

void App::render_preset_overwrite_popup()
{
    const std::string popup_id = imgui_label(TextId::PresetAlreadyExistsTitle, "PresetAlreadyExists");

    if (pending_preset_overwrite_ && pending_preset_overwrite_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        pending_preset_overwrite_->request_open = false;
    }

    if (!pending_preset_overwrite_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string message = textf(TextId::PresetAlreadyExistsFormat, {{"name", pending_preset_overwrite_->name}});
        ImGui::TextWrapped("%s", message.c_str());

        if (ImGui::Button(imgui_label(TextId::Overwrite, "OverwritePreset").c_str())) {
            if (overwrite_pending_preset()) {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelPresetOverwrite").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_preset_overwrite_.reset();
    }
}

void App::render_delete_preset_popup()
{
    const std::string popup_id = imgui_label(TextId::DeletePresetTitle, "DeletePresetPopup");

    if (pending_delete_preset_ && pending_delete_preset_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        pending_delete_preset_->request_open = false;
    }

    if (!pending_delete_preset_) {
        return;
    }

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string message = textf(TextId::DeleteSavedPresetFormat, {{"name", pending_delete_preset_->preset.name}});
        ImGui::TextWrapped("%s", message.c_str());

        if (ImGui::Button(imgui_label(TextId::Delete, "DeletePresetConfirm").c_str())) {
            delete_pending_preset();
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelDeletePreset").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str())) {
        reset_popup = true;
    }

    if (reset_popup) {
        pending_delete_preset_.reset();
    }
}

void App::render_batch_dialog()
{
    const std::string popup_id = imgui_label(TextId::BatchWindowTitle, "BatchPixelize");

    if (batch_ && batch_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        batch_->request_open = false;
    }

    if (!batch_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popup_width = std::min(680.0F, std::max(420.0F, viewport->WorkSize.x - 48.0F));
    const float popup_height = std::min(560.0F, std::max(380.0F, viewport->WorkSize.y - 48.0F));
    ImGui::SetNextWindowSize(ImVec2(popup_width, popup_height), ImGuiCond_Appearing);

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_NoSavedSettings)) {
        if (!popup_open) {
            if (batch_->processing) {
                batch_->cancel_requested = true;
                batch_->request_open = true;
            } else {
                ImGui::CloseCurrentPopup();
                reset_popup = true;
            }
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float footer_height = ImGui::GetFrameHeight() * 2.0F + style.ItemSpacing.y * 3.0F + style.WindowPadding.y;
        const bool controls_disabled = batch_->processing || file_commands_.dialog_open();
        if (controls_disabled) {
            ImGui::BeginDisabled();
        }

        ImGui::BeginChild("BatchBody", ImVec2(0.0F, -footer_height), ImGuiChildFlags_None);
        ImGui::TextUnformatted(text(TextId::BatchImages));
        ImGui::SameLine();
        if (ImGui::SmallButton(imgui_label(TextId::BatchBrowseImages, "BrowseBatchImages").c_str())) {
            request_batch_images();
        }

        ImGui::BeginChild("BatchImageDrawer", ImVec2(0.0F, 160.0F), ImGuiChildFlags_Borders);
        if (batch_->images.empty()) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float hint_width = ImGui::CalcTextSize(text(TextId::BatchImagesDropHint)).x;
            ImGui::SetCursorPos(ImVec2(
                std::max(0.0F, (available.x - hint_width) * 0.5F),
                std::max(0.0F, (available.y - ImGui::GetTextLineHeight()) * 0.5F)));
            ImGui::TextDisabled("%s", text(TextId::BatchImagesDropHint));
        } else {
            for (std::size_t index = 0; index < batch_->images.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::SmallButton("x")) {
                    batch_->images.erase(batch_->images.begin() + static_cast<std::ptrdiff_t>(index));
                    batch_->processed = 0;
                    batch_->succeeded = 0;
                    batch_->failed = 0;
                    batch_->last_error.clear();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::TextWrapped("%s", batch_->images[index].string().c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        const std::string count = std::to_string(batch_->images.size());
        ImGui::TextDisabled("%s", textf(TextId::BatchImageCountFormat, {{"count", count}}).c_str());

        ImGui::Spacing();
        ImGui::TextUnformatted(text(TextId::BatchOutputFolder));
        ImGui::SameLine();
        if (ImGui::SmallButton(imgui_label(TextId::BatchChooseFolder, "ChooseBatchOutputFolder").c_str())) {
            request_batch_output_folder();
        }
        if (batch_->output_dir.empty()) {
            ImGui::TextDisabled("%s", text(TextId::BatchOutputFolder));
        } else {
            ImGui::TextWrapped("%s", batch_->output_dir.string().c_str());
        }

        const char* format_preview = batch_->format == BatchExportFormat::Png ? text(TextId::BatchPng) : text(TextId::BatchRaw);
        if (ImGui::BeginCombo(imgui_label(TextId::BatchFormat, "BatchFormat").c_str(), format_preview)) {
            const bool png_selected = batch_->format == BatchExportFormat::Png;
            if (ImGui::Selectable(text(TextId::BatchPng), png_selected)) {
                batch_->format = BatchExportFormat::Png;
            }
            if (png_selected) {
                ImGui::SetItemDefaultFocus();
            }
            const bool raw_selected = batch_->format == BatchExportFormat::Raw;
            if (ImGui::Selectable(text(TextId::BatchRaw), raw_selected)) {
                batch_->format = BatchExportFormat::Raw;
            }
            if (raw_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(
            imgui_label(TextId::BatchSuffix, "BatchSuffix").c_str(),
            batch_->suffix.data(),
            batch_->suffix.size());

        const bool has_saved_preset = batch_->selected_preset >= 0
            && batch_->selected_preset < static_cast<int>(presets_.size());
        const char* preset_preview = has_saved_preset
            ? presets_[static_cast<std::size_t>(batch_->selected_preset)].name.c_str()
            : text(TextId::BatchCurrentSettings);
        if (ImGui::BeginCombo(imgui_label(TextId::BatchPreset, "BatchPreset").c_str(), preset_preview)) {
            const bool current_selected = batch_->selected_preset < 0;
            if (ImGui::Selectable(text(TextId::BatchCurrentSettings), current_selected)) {
                batch_->selected_preset = -1;
            }
            if (current_selected) {
                ImGui::SetItemDefaultFocus();
            }
            for (std::size_t index = 0; index < presets_.size(); ++index) {
                const bool selected = batch_->selected_preset == static_cast<int>(index);
                if (ImGui::Selectable(presets_[index].name.c_str(), selected)) {
                    batch_->selected_preset = static_cast<int>(index);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::EndChild();

        if (controls_disabled) {
            ImGui::EndDisabled();
        }

        const float total = static_cast<float>(batch_->images.size());
        const float progress = total <= 0.0F ? 0.0F : static_cast<float>(batch_->processed) / total;
        const std::string progress_label = std::to_string(batch_->processed) + " / " + std::to_string(batch_->images.size());
        ImGui::ProgressBar(std::clamp(progress, 0.0F, 1.0F), ImVec2(-1.0F, 0.0F), progress_label.c_str());

        const bool can_start = !file_commands_.dialog_open()
            && !batch_->processing
            && !batch_->images.empty()
            && !batch_->output_dir.empty();
        if (!can_start) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(imgui_label(TextId::BatchPixelize, "StartBatchPixelize").c_str())) {
            start_batch_processing();
        }
        if (!can_start) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (!batch_->processing) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(imgui_label(TextId::Stop, "StopBatchPixelize").c_str())) {
            batch_->cancel_requested = true;
        }
        if (!batch_->processing) {
            ImGui::EndDisabled();
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str()) && !batch_->processing) {
        reset_popup = true;
    }

    if (reset_popup) {
        batch_.reset();
    }
}

void App::render_video_export_dialog()
{
    const std::string popup_id = imgui_label(TextId::VideoExportWindowTitle, "VideoExport");

    if (video_export_dialog_ && video_export_dialog_->request_open) {
        ImGui::OpenPopup(popup_id.c_str());
        video_export_dialog_->request_open = false;
    }

    if (!video_export_dialog_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float popup_width = std::min(560.0F, std::max(420.0F, viewport->WorkSize.x - 48.0F));
    ImGui::SetNextWindowSize(ImVec2(popup_width, 0.0F), ImGuiCond_Appearing);

    bool popup_open = true;
    bool reset_popup = false;
    if (ImGui::BeginPopupModal(popup_id.c_str(), &popup_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        const bool hardware_probe_running = pending_video_hardware_probe_.has_value();
        if (video_.profiles.empty()) {
            if (hardware_probe_running) {
                ImGui::TextWrapped("%s", text(TextId::VideoHardwareProbeRunning));
            } else {
                ImGui::TextWrapped("%s", text(TextId::StatusVideoNoExportProfiles));
            }
        } else {
            int selected_profile = std::clamp(
                video_export_dialog_->selected_profile,
                0,
                static_cast<int>(video_.profiles.size()) - 1);
            video_export_dialog_->selected_profile = selected_profile;
            const VideoExportProfile& profile = video_.profiles[static_cast<std::size_t>(selected_profile)];
            if (ImGui::BeginCombo(imgui_label(TextId::VideoCodec, "VideoCodec").c_str(), profile.label.c_str())) {
                for (std::size_t index = 0; index < video_.profiles.size(); ++index) {
                    const bool selected = selected_profile == static_cast<int>(index);
                    if (ImGui::Selectable(video_.profiles[index].label.c_str(), selected)) {
                        video_export_dialog_->selected_profile = static_cast<int>(index);
                        video_export_dialog_->crf = video_.profiles[index].crf_default;
                        video_export_dialog_->qp = video_.profiles[index].qp_default;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (hardware_probe_running) {
                ImGui::TextDisabled("%s", text(TextId::VideoHardwareProbeRunning));
            }

            const VideoExportProfile& active_profile = video_.profiles[static_cast<std::size_t>(video_export_dialog_->selected_profile)];
            std::vector<VideoContainer> container_options = video_container_options(active_profile, video_.capabilities);
            if (container_options.empty()) {
                ImGui::TextWrapped("%s", text(TextId::StatusVideoNoExportProfiles));
            } else {
                if (std::find(container_options.begin(), container_options.end(), video_export_dialog_->container) == container_options.end()) {
                    video_export_dialog_->container = container_options.front();
                }
                if (ImGui::BeginCombo(
                        imgui_label(TextId::VideoContainer, "VideoContainer").c_str(),
                        video_container_label(video_export_dialog_->container))) {
                    for (VideoContainer container : container_options) {
                        const bool selected = video_export_dialog_->container == container;
                        if (ImGui::Selectable(video_container_label(container), selected)) {
                            video_export_dialog_->container = container;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (active_profile.backend == VideoExportBackend::Software) {
                ImGui::SliderInt(
                    imgui_label(TextId::VideoQualityCrf, "VideoCrf").c_str(),
                    &video_export_dialog_->crf,
                    0,
                    video_profile_crf_max(active_profile));
            } else {
                ImGui::SliderInt(
                    imgui_label(TextId::VideoQualityQp, "VideoQp").c_str(),
                    &video_export_dialog_->qp,
                    0,
                    51);
                const char* speed_label = text(video_hardware_speed_text(video_export_dialog_->hardware_speed));
                if (ImGui::BeginCombo(imgui_label(TextId::VideoHardwareSpeed, "VideoHardwareSpeed").c_str(), speed_label)) {
                    for (VideoHardwareSpeed speed : {VideoHardwareSpeed::Balanced, VideoHardwareSpeed::Fast, VideoHardwareSpeed::VeryFast}) {
                        const bool selected = video_export_dialog_->hardware_speed == speed;
                        const char* label = text(video_hardware_speed_text(speed));
                        if (ImGui::Selectable(label, selected)) {
                            video_export_dialog_->hardware_speed = speed;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextWrapped("%s", text(TextId::VideoHardwareHint));
            }

            std::vector<VideoAudioOption> audio_options =
                video_audio_options(video_.metadata, video_.capabilities, video_export_dialog_->container);
            const auto selected_audio = std::find_if(audio_options.begin(), audio_options.end(), [&](const VideoAudioOption& option) {
                return option.mode == video_export_dialog_->audio_mode;
            });
            if (selected_audio == audio_options.end()) {
                video_export_dialog_->audio_mode = preferred_video_audio_mode(audio_options);
            }
            if (ImGui::BeginCombo(
                    imgui_label(TextId::VideoAudio, "VideoAudio").c_str(),
                    text(video_audio_mode_label(video_export_dialog_->audio_mode)))) {
                for (const VideoAudioOption& option : audio_options) {
                    const bool selected = option.mode == video_export_dialog_->audio_mode;
                    if (ImGui::Selectable(text(video_audio_mode_label(option.mode)), selected)) {
                        video_export_dialog_->audio_mode = option.mode;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (video_.metadata.has_audio && audio_options.size() == 1U) {
                ImGui::TextDisabled("%s", text(TextId::VideoAudioIncompatible));
            }

            const bool can_low_quality_process = video_low_quality_process_allowed(edit_session_.settings());
            if (!can_low_quality_process) {
                video_export_dialog_->high_quality_process = true;
                ImGui::BeginDisabled();
            }
            ImGui::Checkbox(
                imgui_label(TextId::VideoHighQualityProcess, "VideoHighQualityProcess").c_str(),
                &video_export_dialog_->high_quality_process);
            if (!can_low_quality_process) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", text(TextId::VideoCpuRequiredProcessHint));
                }
            } else if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", text(TextId::VideoHighQualityProcessHint));
            }

            const VideoDimensions dimensions = video_export_dimensions(video_.metadata, edit_session_.settings().pixel_size, active_profile);
            ImGui::TextDisabled(
                "%s",
                textf(
                    TextId::VideoOutputSizeFormat,
                    {{"width", std::to_string(dimensions.width)}, {"height", std::to_string(dimensions.height)}})
                    .c_str());
            if (dimensions.padded) {
                ImGui::TextWrapped("%s", text(TextId::VideoOddDimensionWarning));
            }
            if (video_.metadata.variable_frame_rate) {
                ImGui::TextWrapped("%s", text(TextId::VideoVfrWarning));
            }

            const bool can_export = !file_commands_.dialog_open() && !pending_video_export_;
            if (!can_export) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(imgui_label(TextId::ExportVideo, "ConfirmVideoExport").c_str())) {
                std::string stem = current_image_path_.stem().string();
                if (stem.empty()) {
                    stem = "video";
                }
                const std::string extension = video_container_extension(video_export_dialog_->container);
                std::filesystem::path default_path = current_image_path_.parent_path() / (stem + "-pixatto" + extension);
                const std::string filter_label =
                    std::string(video_container_label(video_export_dialog_->container)) + " (*" + extension + ")";
                (void)file_commands_.request_export_video_dialog(
                    window_,
                    filter_label,
                    video_container_extension_filter(video_export_dialog_->container),
                    default_path,
                    video_export_dialog_->selected_profile);
                ImGui::CloseCurrentPopup();
            }
            if (!can_export) {
                ImGui::EndDisabled();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(imgui_label(TextId::Cancel, "CancelVideoExport").c_str()) || !popup_open) {
            ImGui::CloseCurrentPopup();
            reset_popup = true;
        }

        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen(popup_id.c_str()) && !file_commands_.dialog_open()) {
        reset_popup = true;
    }

    if (reset_popup) {
        video_export_dialog_.reset();
    }
}

void App::handle_shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (file_commands_.dialog_open() || number_edit_ || palette_color_edit_ || palette_save_as_ || preset_save_as_
        || pending_palette_import_ || pending_preset_overwrite_ || pending_delete_preset_
        || batch_
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
    if (!edit_session_.preview_dirty()) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    if (document_mode_ == DocumentMode::Image) {
        if (original_.empty()) {
            return;
        }
        result_ = process_image(original_, edit_session_.settings());
        rebuild_texture(result_texture_, result_, true);
    } else if (document_mode_ == DocumentMode::Model) {
        model_processed_textures_.clear();
        model_processed_textures_.reserve(model_.textures.size());
        for (const ModelTexture& texture : model_.textures) {
            model_processed_textures_.push_back(process_image(texture.image, edit_session_.settings()));
        }
        while (model_result_textures_.size() > model_processed_textures_.size()) {
            destroy_texture(model_result_textures_.back());
            model_result_textures_.pop_back();
        }
        model_result_textures_.resize(model_processed_textures_.size());
        for (std::size_t index = 0; index < model_processed_textures_.size(); ++index) {
            rebuild_texture(model_result_textures_[index], model_processed_textures_[index], true);
        }
        model_renderer_.update_processed_textures(model_processed_textures_);
    } else {
        if (video_.playing) {
            edit_session_.clear_preview_dirty();
            return;
        }
        request_video_preview(video_.current_time, true);
        return;
    }
    edit_session_.clear_preview_dirty();

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    status_ = textf(TextId::StatusPreviewUpdatedFormat, {{"ms", std::to_string(elapsed)}});
}

bool App::decode_video_preview_with_playback_decoder(double requested_time, double decode_time)
{
    if (!video_.playback_decoder_available || !video_playback_decoder_.is_open()) {
        return false;
    }

    std::string error;
    if (!video_playback_decoder_.seek(decode_time, error)) {
        set_status(textf(TextId::StatusVideoPreviewFailedFormat, {{"error", error}}));
        video_.playback_decoder_available = false;
        return false;
    }

    const double frame_interval = video_.metadata.fps > 0.0 ? 1.0 / video_.metadata.fps : 1.0 / 30.0;
    Image decoded;
    bool got_frame = false;
    const int max_advance_frames = std::max(1, static_cast<int>(std::ceil(video_.metadata.fps > 0.0 ? video_.metadata.fps * 2.0 : 60.0)));
    for (int index = 0; index < max_advance_frames; ++index) {
        Image candidate;
        double candidate_time = 0.0;
        error.clear();
        if (!video_playback_decoder_.read_next_frame(candidate, candidate_time, error)) {
            break;
        }
        decoded = std::move(candidate);
        got_frame = true;
        if (candidate_time + frame_interval * 0.5 >= decode_time) {
            break;
        }
    }

    if (!got_frame) {
        if (!error.empty()) {
            set_status(textf(TextId::StatusVideoPreviewFailedFormat, {{"error", error}}));
            video_.playback_decoder_available = false;
        }
        return false;
    }

    video_decoded_frame_ = std::move(decoded);
    video_decoded_frame_time_ = decode_time;
    video_decoded_frame_valid_ = !video_decoded_frame_.empty();
    const ProcessSettings& settings = edit_session_.settings();
    result_ = collapse_pixel_blocks(process_image(video_decoded_frame_, settings), settings.pixel_size);
    rebuild_texture(result_texture_, result_, true);
    video_.current_time = requested_time;
    video_.playback_seek_pending = true;
    edit_session_.clear_preview_dirty();
    return true;
}

void App::rebuild_texture(Texture& texture, const Image& image, bool nearest)
{
    destroy_texture(texture);
    if (image.empty()) {
        return;
    }
    if (!ensure_gl_context_current()) {
        return;
    }

    GLuint handle = 0;
    glGenTextures(1, &handle);
    texture.handle = handle;
    texture.width = image.width;
    texture.height = image.height;

    if (texture.handle == 0U) {
        set_status(textf(TextId::StatusTextureCreationFailedFormat, {{"error", "OpenGL texture id was 0"}}));
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        image.width,
        image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.rgba.data());
}

void App::configure_fonts()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesThai());
    builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    builder.AddText("🗑");

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
        R"(C:\Windows\Fonts\seguiemj.ttf)",
        R"(C:\Windows\Fonts\seguisym.ttf)",
        R"(C:\Windows\Fonts\NotoSans-Regular.ttf)",
        R"(C:\Windows\Fonts\NotoSansThai-Regular.ttf)",
        R"(C:\Windows\Fonts\NotoSansLao-Regular.ttf)",
        R"(C:\Windows\Fonts\NotoSansKhmer-Regular.ttf)",
        R"(C:\Windows\Fonts\NotoSansMyanmar-Regular.ttf)",
        R"(C:\Windows\Fonts\msyh.ttc)",
        R"(C:\Windows\Fonts\msyh.ttf)",
        R"(C:\Windows\Fonts\msjh.ttc)",
        R"(C:\Windows\Fonts\YuGothM.ttc)",
        R"(C:\Windows\Fonts\YuGothR.ttc)",
        R"(C:\Windows\Fonts\meiryo.ttc)",
        R"(C:\Windows\Fonts\malgun.ttf)",
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/System/Library/Fonts/Thonburi.ttc",
        "/System/Library/Fonts/Kohinoor.ttc",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKkr-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansLao-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansKhmer-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansMyanmar-Regular.ttf",
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
    if (texture.handle != 0U) {
        const GLuint handle = texture.handle;
        glDeleteTextures(1, &handle);
        texture.handle = 0;
    }
    texture.width = 0;
    texture.height = 0;
}

void App::request_open_image()
{
    (void)file_commands_.request_open_image_dialog(window_, file_dialog_labels());
}

void App::request_open_model()
{
    (void)file_commands_.request_open_model_dialog(window_, file_dialog_labels());
}

void App::request_open_video()
{
    (void)file_commands_.request_open_video_dialog(window_, file_dialog_labels());
}

void App::request_import_palette()
{
    (void)file_commands_.request_import_palette_dialog(window_, file_dialog_labels());
}

void App::request_export_png()
{
    if (document_mode_ == DocumentMode::Model) {
        pending_model_texture_exports_.clear();
        std::vector<bool> referenced(model_processed_textures_.size(), false);
        for (const ModelPrimitive& primitive : model_.primitives) {
            if (primitive.texture_index >= 0 && primitive.texture_index < static_cast<int>(referenced.size())) {
                referenced[static_cast<std::size_t>(primitive.texture_index)] = true;
            }
        }
        for (std::size_t index = 0; index < referenced.size(); ++index) {
            if (referenced[index]) {
                pending_model_texture_exports_.push_back(index);
            }
        }
        if (pending_model_texture_exports_.empty()) {
            set_status(text(TextId::StatusModelExportSkipped));
            return;
        }
        request_next_model_texture_export();
        return;
    }

    (void)file_commands_.request_export_png_dialog(window_, file_dialog_labels());
}

void App::request_export_raw()
{
    if (document_mode_ == DocumentMode::Model) {
        set_status(text(TextId::StatusModelRawExportUnavailable));
        return;
    }

    (void)file_commands_.request_export_raw_dialog(window_, file_dialog_labels());
}

void App::request_export_video()
{
    if (document_mode_ != DocumentMode::Video || video_.source.empty()) {
        set_status(text(TextId::StatusVideoExportSkipped));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }
    if (!video_.hardware_encoders_probed && !pending_video_hardware_probe_) {
        PendingVideoHardwareProbeState pending;
        const VideoToolchain tools = video_.tools;
        const VideoCapabilities capabilities = video_.capabilities;
        pending.result = std::async(std::launch::async, [tools, capabilities]() {
            return probe_video_hardware_encoders(tools, capabilities);
        });
        pending_video_hardware_probe_ = std::move(pending);
    }
    if (video_.profiles.empty() && video_.hardware_encoders_probed) {
        set_status(text(TextId::StatusVideoNoExportProfiles));
        return;
    }

    VideoExportDialogState state;
    state.request_open = true;
    state.selected_profile = 0;
    if (!video_.profiles.empty()) {
        const VideoExportProfile& profile = video_.profiles.front();
        state.crf = profile.crf_default;
        state.qp = profile.qp_default;
        const std::vector<VideoContainer> containers = video_container_options(profile, video_.capabilities);
        if (!containers.empty()) {
            state.container = containers.front();
        }
        const std::vector<VideoAudioOption> audio_options =
            video_audio_options(video_.metadata, video_.capabilities, state.container);
        state.audio_mode = preferred_video_audio_mode(audio_options);
    }
    video_export_dialog_ = state;
}

void App::request_batch()
{
    BatchState state;
    state.request_open = true;
    state.selected_preset = selected_preset_;
    if (state.selected_preset < 0 || state.selected_preset >= static_cast<int>(presets_.size())) {
        state.selected_preset = -1;
    }

    if (last_export_path_) {
        state.output_dir = last_export_path_->parent_path();
    }
    if (state.output_dir.empty() && !current_image_path_.empty()) {
        state.output_dir = current_image_path_.parent_path();
    }
    if (state.output_dir.empty()) {
        std::error_code ec;
        state.output_dir = std::filesystem::current_path(ec);
        if (ec) {
            state.output_dir.clear();
        }
    }

    batch_ = std::move(state);
}

void App::request_batch_images()
{
    if (!batch_ || batch_->processing) {
        return;
    }
    (void)file_commands_.request_batch_images_dialog(window_, file_dialog_labels());
}

void App::request_batch_output_folder()
{
    if (!batch_ || batch_->processing) {
        return;
    }
    std::filesystem::path default_path = batch_->output_dir;
    if (default_path.empty() && !current_image_path_.empty()) {
        default_path = current_image_path_.parent_path();
    }
    (void)file_commands_.request_batch_output_folder_dialog(window_, default_path);
}

void App::request_next_model_texture_export()
{
    if (pending_model_texture_exports_.empty() || file_commands_.dialog_open()) {
        return;
    }

    const std::size_t texture_index = pending_model_texture_exports_.front();
    pending_model_texture_exports_.pop_front();
    if (texture_index >= model_.textures.size()) {
        request_next_model_texture_export();
        return;
    }

    std::filesystem::path default_path = current_image_path_.parent_path()
        / default_model_texture_export_name(model_.textures[texture_index], texture_index);
    (void)file_commands_.request_export_model_texture_png_dialog(
        window_,
        file_dialog_labels(),
        default_path,
        static_cast<int>(texture_index));
}

void App::drain_file_commands()
{
    for (const FileCommand& command : file_commands_.drain_commands()) {
        handle_file_command(command);
    }
}

void App::update_pending_model_load()
{
    if (!pending_model_load_) {
        return;
    }

    using namespace std::chrono_literals;
    if (pending_model_load_->result.wait_for(0ms) != std::future_status::ready) {
        const auto now = std::chrono::steady_clock::now();
        if (now - pending_model_load_->last_log >= 1000ms) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pending_model_load_->started_at).count();
            append_runtime_log(
                std::string("model-load: still waiting after ") + std::to_string(elapsed) + " ms for "
                + quote_path_for_log(pending_model_load_->source));
            pending_model_load_->last_log = now;
        }
        return;
    }

    append_runtime_log(std::string("model-load: future ready for ") + quote_path_for_log(pending_model_load_->source));
    PendingModelLoadState load = std::move(*pending_model_load_);
    pending_model_load_.reset();
    try {
        finish_model_load_from_path(load.source, load.result.get());
    } catch (const std::exception& error) {
        append_runtime_log(std::string("model-load: finish threw exception: ") + error.what());
        set_status(textf(TextId::StatusModelLoadFailedFormat, {{"error", error.what()}}));
    } catch (...) {
        append_runtime_log("model-load: finish threw unknown exception");
        set_status(textf(TextId::StatusModelLoadFailedFormat, {{"error", "unknown error"}}));
    }
}

void App::update_video_playback()
{
    if (document_mode_ != DocumentMode::Video || !video_.playing || pending_video_export_) {
        return;
    }
    if (pending_video_preview_) {
        return;
    }
    if (!video_.playback_decoder_available || !video_playback_decoder_.is_open()) {
        if (!video_.playback_warning_reported) {
            set_status(text(TextId::StatusVideoPlaybackUnavailable));
            video_.playback_warning_reported = true;
        }
        video_.playing = false;
        request_video_preview(video_.current_time, true);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (video_.last_tick.time_since_epoch().count() == 0) {
        video_.last_tick = now;
    }

    const double frame_interval = video_.metadata.fps > 0.0 ? 1.0 / video_.metadata.fps : 1.0 / 30.0;
    const double elapsed = std::chrono::duration<double>(now - video_.last_tick).count();
    if (!video_.playback_seek_pending && elapsed < frame_interval) {
        return;
    }

    std::string error;
    if (video_.playback_seek_pending) {
        if (!video_playback_decoder_.seek(video_.current_time, error)) {
            set_status(textf(TextId::StatusVideoPreviewFailedFormat, {{"error", error}}));
            video_.playing = false;
            video_.playback_decoder_available = false;
            request_video_preview(video_.current_time, true);
            return;
        }
        video_.playback_seek_pending = false;
    }

    Image decoded;
    double timestamp = 0.0;
    if (!video_playback_decoder_.read_next_frame(decoded, timestamp, error)) {
        video_.playing = false;
        if (!error.empty()) {
            set_status(textf(TextId::StatusVideoPreviewFailedFormat, {{"error", error}}));
            video_.playback_decoder_available = false;
        }
        request_video_preview(video_.current_time, true);
        return;
    }

    video_.last_tick = now;
    video_.current_time = std::clamp(timestamp, 0.0, std::max(0.0, video_.metadata.duration_seconds));
    video_decoded_frame_ = std::move(decoded);
    video_decoded_frame_time_ = video_.current_time;
    video_decoded_frame_valid_ = !video_decoded_frame_.empty();
    if (video_decoded_frame_valid_) {
        const ProcessSettings& settings = edit_session_.settings();
        result_ = collapse_pixel_blocks(process_image(video_decoded_frame_, settings), settings.pixel_size);
        rebuild_texture(result_texture_, result_, true);
        edit_session_.clear_preview_dirty();
    }
    if (video_.metadata.duration_seconds > 0.0 && video_.current_time >= video_.metadata.duration_seconds - frame_interval * 0.5) {
        video_.current_time = video_.metadata.duration_seconds;
        video_.playing = false;
    }
}

void App::update_pending_video_preview()
{
    if (!pending_video_preview_) {
        if (document_mode_ == DocumentMode::Video && edit_session_.preview_dirty()) {
            request_video_preview(video_.current_time, true);
        }
        return;
    }

    using namespace std::chrono_literals;
    if (pending_video_preview_->result.wait_for(0ms) != std::future_status::ready) {
        return;
    }

    PendingVideoPreviewState pending = std::move(*pending_video_preview_);
    pending_video_preview_.reset();

    VideoPreviewResult loaded;
    try {
        loaded = pending.result.get();
    } catch (const std::exception& error) {
        loaded.error = error.what();
    } catch (...) {
        loaded.error = "unknown error";
    }

    if (pending.generation != video_.preview_generation) {
        if (loaded.error.empty() && !loaded.decoded.empty()) {
            video_decoded_frame_ = std::move(loaded.decoded);
            video_decoded_frame_time_ = pending.decode_time_seconds;
            video_decoded_frame_valid_ = true;
        }
        if (edit_session_.preview_dirty()) {
            request_video_preview(video_.current_time, true);
        }
        return;
    }

    if (!loaded.error.empty()) {
        set_status(textf(TextId::StatusVideoPreviewFailedFormat, {{"error", loaded.error}}));
        return;
    }

    video_decoded_frame_ = std::move(loaded.decoded);
    video_decoded_frame_time_ = pending.decode_time_seconds;
    video_decoded_frame_valid_ = !video_decoded_frame_.empty();
    result_ = std::move(loaded.processed);
    rebuild_texture(result_texture_, result_, true);
    video_.current_time = pending.time_seconds;

    if (edit_session_.preview_dirty()) {
        request_video_preview(video_.current_time, true);
    }
}

void App::update_pending_video_hardware_probe()
{
    if (!pending_video_hardware_probe_) {
        return;
    }

    using namespace std::chrono_literals;
    if (pending_video_hardware_probe_->result.wait_for(0ms) != std::future_status::ready) {
        return;
    }

    PendingVideoHardwareProbeState pending = std::move(*pending_video_hardware_probe_);
    pending_video_hardware_probe_.reset();

    std::vector<std::string> hardware_encoders;
    try {
        hardware_encoders = pending.result.get();
    } catch (const std::exception& error) {
        append_runtime_log(std::string("video-export: hardware encoder probe failed: ") + error.what());
    } catch (...) {
        append_runtime_log("video-export: hardware encoder probe failed: unknown error");
    }

    if (document_mode_ != DocumentMode::Video || video_.source.empty()) {
        return;
    }

    video_.capabilities.hardware_encoders = std::move(hardware_encoders);
    video_.hardware_encoders_probed = true;
    video_.profiles = build_video_export_profiles(video_.capabilities);
    if (video_export_dialog_ && !video_.profiles.empty()) {
        video_export_dialog_->selected_profile = std::clamp(
            video_export_dialog_->selected_profile,
            0,
            static_cast<int>(video_.profiles.size()) - 1);
        const VideoExportProfile& profile = video_.profiles[static_cast<std::size_t>(video_export_dialog_->selected_profile)];
        const std::vector<VideoContainer> containers = video_container_options(profile, video_.capabilities);
        if (!containers.empty()
            && std::find(containers.begin(), containers.end(), video_export_dialog_->container) == containers.end()) {
            video_export_dialog_->container = containers.front();
        }
    }
}

void App::update_pending_video_export()
{
    if (!pending_video_export_) {
        return;
    }

    using namespace std::chrono_literals;
    if (pending_video_export_->result.wait_for(0ms) != std::future_status::ready) {
        return;
    }

    PendingVideoExportState pending = std::move(*pending_video_export_);
    pending_video_export_.reset();
    close_video_export_gpu_queue(
        pending.gpu_queue,
        VideoGpuProcessResult::Fallback,
        "Video export GPU processing is closed.");
    set_video_export_fast_swap(false);

    VideoExportResult result;
    try {
        result = pending.result.get();
    } catch (const std::exception& error) {
        result.error = error.what();
    } catch (...) {
        result.error = "unknown error";
    }

    if (!result.success) {
        if (!result.diagnostic_log_path.empty()) {
            append_runtime_log(std::string("video-export: diagnostics ") + quote_path_for_log(result.diagnostic_log_path));
        }
        set_status(textf(TextId::StatusVideoExportFailedFormat, {{"error", result.error.empty() ? "unknown error" : result.error}}));
        return;
    }

    std::filesystem::path exported_path = pending.settings.destination_path;
    const std::string extension = video_container_extension(pending.settings.container);
    if (app_lowercase_extension(exported_path) != extension) {
        exported_path.replace_extension(extension);
    }
    last_export_path_ = exported_path;
    std::string message = textf(TextId::StatusVideoExportedFormat, {{"name", exported_path.filename().string()}});
    if (!result.warning.empty()) {
        message += " ";
        message += result.warning;
    }
    if (!result.diagnostic_log_path.empty()) {
        append_runtime_log(std::string("video-export: diagnostics ") + quote_path_for_log(result.diagnostic_log_path));
    }
    set_status(std::move(message));
}

void App::service_video_export_gpu_queue()
{
    if (!pending_video_export_ || !pending_video_export_->gpu_queue) {
        return;
    }

    constexpr int kMaxGpuRequestsPerService = 4;
    int serviced = 0;
    const auto queue = pending_video_export_->gpu_queue;

    bool context_ready = ensure_gl_context_current();
    if (context_ready && !video_export_gpu_processor_) {
        video_export_gpu_processor_ = std::make_unique<GpuImageProcessor>();
    }

    while (serviced < kMaxGpuRequestsPerService) {
        std::shared_ptr<VideoExportGpuRequest> request;
        {
            std::unique_lock lock(queue->mutex);
            if (queue->requests.empty()) {
                return;
            }
            request = std::move(queue->requests.front());
            queue->requests.pop_front();
        }

        Image result;
        std::string error;
        const bool success = context_ready
            && video_export_gpu_processor_
            && request->source
            && video_export_gpu_processor_->process_sampled_collapsed(*request->source, request->settings, result, error);
        if (!context_ready && error.empty()) {
            error = "OpenGL export context is not available.";
        }
        const VideoGpuProcessResult status = success ? VideoGpuProcessResult::Success : VideoGpuProcessResult::Fallback;
        if (!success && error.empty()) {
            error = "OpenGL export processing failed.";
        }

        {
            std::lock_guard request_lock(request->mutex);
            request->status = status;
            request->result = std::move(result);
            request->error = error;
            request->finished = true;
        }
        request->cv.notify_one();
        ++serviced;

        if (!success) {
            close_video_export_gpu_queue(queue, VideoGpuProcessResult::Fallback, std::move(error));
            return;
        }
    }
}

void App::close_video_export_gpu_queue(
    const std::shared_ptr<VideoExportGpuQueue>& queue,
    VideoGpuProcessResult status,
    std::string error)
{
    if (!queue) {
        return;
    }
    if (error.empty()) {
        error = status == VideoGpuProcessResult::Canceled
            ? "Video export canceled."
            : "Video export GPU processing is unavailable.";
    }

    std::deque<std::shared_ptr<VideoExportGpuRequest>> requests;
    {
        std::lock_guard lock(queue->mutex);
        queue->closed = true;
        queue->closed_status = status;
        queue->close_error = error;
        requests.swap(queue->requests);
    }
    queue->cv.notify_all();

    for (const std::shared_ptr<VideoExportGpuRequest>& request : requests) {
        {
            std::lock_guard request_lock(request->mutex);
            request->status = status;
            request->error = error;
            request->finished = true;
        }
        request->cv.notify_one();
    }
}

void App::set_video_export_fast_swap(bool enabled)
{
    if (video_export_fast_swap_ == enabled || !window_ || !gl_context_) {
        return;
    }
    if (!ensure_gl_context_current()) {
        return;
    }
    if (SDL_GL_SetSwapInterval(enabled ? 0 : 1)) {
        video_export_fast_swap_ = enabled;
    }
}

void App::handle_file_command(const FileCommand& command)
{
    switch (command.kind) {
    case FileCommandKind::OpenImage:
        if (document_mode_ == DocumentMode::Model && !model_.empty()) {
            import_model_texture_from_path(command.path);
        } else {
            load_image_from_path(command.path);
        }
        break;
    case FileCommandKind::OpenModel:
        load_model_from_path(command.path);
        break;
    case FileCommandKind::OpenVideo:
        load_video_from_path(command.path);
        break;
    case FileCommandKind::ImportPalette:
        import_palette_from_path(command.path);
        break;
    case FileCommandKind::ExportPng:
        export_result_to_png_path(command.path);
        break;
    case FileCommandKind::ExportModelTexturePng:
        if (command.index >= 0) {
            export_model_texture_to_png_path(static_cast<std::size_t>(command.index), command.path);
            request_next_model_texture_export();
        }
        break;
    case FileCommandKind::ExportRaw:
        export_result_to_raw_path(command.path);
        break;
    case FileCommandKind::ExportVideo:
        start_video_export_to_path(command.path);
        break;
    case FileCommandKind::BatchImages:
        add_batch_images(command.paths.empty() ? std::vector<std::filesystem::path>{command.path} : command.paths);
        break;
    case FileCommandKind::BatchOutputFolder:
        if (batch_) {
            batch_->output_dir = command.path;
        }
        break;
    case FileCommandKind::ConfirmOpenImage:
        pending_dropped_image_ = command.path;
        open_drop_confirm_ = true;
        break;
    case FileCommandKind::DialogFailed: {
        const std::string error = command.error.empty() ? "unknown error" : command.error;
        set_status(textf(TextId::StatusFileDialogFailedFormat, {{"error", error}}));
        pending_model_texture_exports_.clear();
        break;
    }
    case FileCommandKind::DialogCanceled: {
        pending_model_texture_exports_.clear();
        break;
    }
    }
}

void App::load_document_from_path(const std::filesystem::path& path)
{
    if (is_model_path(path)) {
        load_model_from_path(path);
        return;
    }

    if (is_importable_video_path(path)) {
        load_video_from_path(path);
        return;
    }

    load_image_from_path(path);
}

void App::load_image_from_path(const std::filesystem::path& path)
{
    if (pending_model_load_) {
        set_status(text(TextId::StatusModelStillLoading));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }

    ImageLoadResult loaded = load_image_rgba(path.string());
    if (!loaded.error.empty()) {
        set_status(textf(TextId::StatusImageLoadFailedFormat, {{"error", loaded.error}}));
        return;
    }

    clear_model_document();
    clear_video_document();
    document_mode_ = DocumentMode::Image;
    original_ = std::move(loaded.image);
    current_image_path_ = path;
    rebuild_texture(original_texture_, original_, false);
    original_zoom_ = 1.0F;
    result_zoom_ = 1.0F;
    mark_dirty();
    set_status(textf(TextId::StatusLoadedFormat, {{"name", path.filename().string()}}));
}

void App::load_model_from_path(const std::filesystem::path& path)
{
    append_runtime_log(std::string("model-load: request ") + quote_path_for_log(path));
    if (pending_model_load_) {
        append_runtime_log("model-load: rejected because another load is pending");
        set_status(text(TextId::StatusModelStillLoading));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }

    PendingModelLoadState pending;
    pending.source = path;
    pending.started_at = std::chrono::steady_clock::now();
    pending.last_log = pending.started_at;
    try {
        pending.result = std::async(std::launch::async, [path]() {
            try {
                append_runtime_log(std::string("model-load-thread: begin load_model_document ") + quote_path_for_log(path));
                ModelLoadResult result = load_model_document(path);
                append_runtime_log(
                    std::string("model-load-thread: finished load_model_document error=")
                    + (result.error.empty() ? "<none>" : result.error)
                    + " primitives=" + std::to_string(result.model.primitives.size())
                    + " textures=" + std::to_string(result.model.textures.size()));
                return result;
            } catch (const std::exception& error) {
                append_runtime_log(std::string("model-load-thread: exception: ") + error.what());
                ModelLoadResult result;
                result.error = error.what();
                return result;
            } catch (...) {
                append_runtime_log("model-load-thread: unknown exception");
                ModelLoadResult result;
                result.error = "unknown error";
                return result;
            }
        });
    } catch (const std::exception& error) {
        append_runtime_log(std::string("model-load: std::async failed: ") + error.what());
        set_status(textf(TextId::StatusModelLoadFailedFormat, {{"error", error.what()}}));
        return;
    }
    pending_model_load_ = std::move(pending);
    append_runtime_log(std::string("model-load: async started ") + quote_path_for_log(path));
    set_status(textf(TextId::StatusModelLoadingFormat, {{"name", path.filename().string()}}));
}

void App::finish_model_load_from_path(const std::filesystem::path& path, ModelLoadResult loaded)
{
    append_runtime_log(
        std::string("model-load: finish begin error=") + (loaded.error.empty() ? "<none>" : loaded.error)
        + " primitives=" + std::to_string(loaded.model.primitives.size())
        + " textures=" + std::to_string(loaded.model.textures.size()));
    if (!loaded.error.empty()) {
        set_status(textf(TextId::StatusModelLoadFailedFormat, {{"error", loaded.error}}));
        append_runtime_log("model-load: finish aborting because load result has error");
        return;
    }

    append_runtime_log("model-load: clearing existing document");
    clear_model_document();
    clear_video_document();
    destroy_texture(original_texture_);
    destroy_texture(result_texture_);
    original_ = {};
    result_ = {};
    document_mode_ = DocumentMode::Model;
    model_ = std::move(loaded.model);
    current_image_path_ = path;
    reset_model_camera();
    viewport_mode_ = ViewportMode::Split;
    viewport_layout_ = ViewportLayout::Stacked;
    viewport_split_ratio_ = 0.62F;

    append_runtime_log("model-load: processing source textures begin");
    model_original_textures_.resize(model_.textures.size());
    model_result_textures_.resize(model_.textures.size());
    model_processed_textures_.clear();
    model_processed_textures_.reserve(model_.textures.size());
    for (std::size_t index = 0; index < model_.textures.size(); ++index) {
        rebuild_texture(model_original_textures_[index], model_.textures[index].image, false);
        model_processed_textures_.push_back(process_image(model_.textures[index].image, edit_session_.settings()));
        rebuild_texture(model_result_textures_[index], model_processed_textures_.back(), true);
    }
    append_runtime_log("model-load: processing source textures complete");

    if (!ensure_gl_context_current()) {
        append_runtime_log("model-load: finish aborting because GL context could not be made current");
        return;
    }

    append_runtime_log("model-load: upload_model begin");
    std::string renderer_error;
    if (!model_renderer_.upload_model(model_, model_processed_textures_, renderer_error)) {
        append_runtime_log(std::string("model-load: upload_model failed: ") + renderer_error);
        set_status(textf(TextId::StatusModelRenderFailedFormat, {{"error", renderer_error}}));
        return;
    }
    append_runtime_log("model-load: upload_model complete");
    model_preview_log_frames_ = 3;

    edit_session_.clear_preview_dirty();
    if (model_.used_fallback_texture) {
        set_status(textf(TextId::StatusModelLoadedWithFallbackFormat, {{"name", path.filename().string()}}));
    } else {
        set_status(textf(TextId::StatusModelLoadedFormat, {{"name", path.filename().string()}}));
    }
    append_runtime_log("model-load: finish complete");
}

void App::load_video_from_path(const std::filesystem::path& path)
{
    if (pending_model_load_) {
        set_status(text(TextId::StatusModelStillLoading));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }

    VideoToolchain tools = find_video_toolchain();
    if (!tools.available()) {
        set_status(textf(TextId::StatusVideoToolsMissingFormat, {{"error", tools.error}}));
        return;
    }

    std::string error;
    VideoMetadata metadata = probe_video_metadata(tools, path, error);
    if (!error.empty()) {
        set_status(textf(TextId::StatusVideoLoadFailedFormat, {{"error", error}}));
        return;
    }

    VideoCapabilities capabilities = probe_video_capabilities(tools, error);
    if (!error.empty()) {
        set_status(textf(TextId::StatusVideoLoadFailedFormat, {{"error", error}}));
        return;
    }

    clear_model_document();
    clear_video_document();
    destroy_texture(original_texture_);
    destroy_texture(result_texture_);
    original_ = {};
    result_ = {};

    document_mode_ = DocumentMode::Video;
    video_ = {};
    video_.source = path;
    video_.tools = std::move(tools);
    video_.metadata = std::move(metadata);
    video_.capabilities = std::move(capabilities);
    video_.profiles = build_video_export_profiles(video_.capabilities);
    video_.current_time = 0.0;
    video_.last_tick = std::chrono::steady_clock::now();
    std::string playback_error;
    video_.playback_decoder_available = video_playback_decoder_.open(path, playback_error);
    video_.playback_seek_pending = true;
    video_.playback_warning_reported = false;
    if (!video_.playback_decoder_available && !playback_error.empty()) {
        append_runtime_log(std::string("video-playback: native decoder unavailable: ") + playback_error);
    }
    current_image_path_ = path;
    viewport_mode_ = ViewportMode::Single;
    result_zoom_ = 1.0F;
    edit_session_.cancel_live_edit();

    request_video_preview(0.0, true);

    std::string message = textf(TextId::StatusVideoLoadedFormat, {{"name", path.filename().string()}});
    if (video_.profiles.empty()) {
        message += " ";
        message += text(TextId::StatusVideoNoExportProfiles);
    }
    set_status(std::move(message));
}

void App::request_video_preview(double time_seconds, bool force)
{
    if (document_mode_ != DocumentMode::Video || video_.source.empty()) {
        return;
    }

    video_.current_time = std::clamp(time_seconds, 0.0, std::max(0.0, video_.metadata.duration_seconds));
    double decode_time = video_.current_time;
    if (video_.metadata.duration_seconds > 0.0 && video_.metadata.fps > 0.0) {
        const double last_frame_time = std::max(0.0, video_.metadata.duration_seconds - (0.5 / video_.metadata.fps));
        decode_time = std::min(decode_time, last_frame_time);
    }
    if (pending_video_preview_) {
        if (force) {
            ++video_.preview_generation;
            edit_session_.mark_dirty();
        }
        return;
    }

    constexpr double kFrameTimeEpsilon = 0.000001;
    if (video_decoded_frame_valid_ && std::abs(video_decoded_frame_time_ - decode_time) <= kFrameTimeEpsilon) {
        result_ = collapse_pixel_blocks(process_image(video_decoded_frame_, edit_session_.settings()), edit_session_.settings().pixel_size);
        rebuild_texture(result_texture_, result_, true);
        edit_session_.clear_preview_dirty();
        return;
    }

    if (decode_video_preview_with_playback_decoder(video_.current_time, decode_time)) {
        return;
    }

    const std::uint64_t generation = ++video_.preview_generation;
    const VideoToolchain tools = video_.tools;
    const VideoMetadata metadata = video_.metadata;
    const std::filesystem::path source = video_.source;
    const ProcessSettings settings = edit_session_.settings();
    const double requested_time = video_.current_time;

    PendingVideoPreviewState pending;
    pending.time_seconds = requested_time;
    pending.decode_time_seconds = decode_time;
    pending.generation = generation;
    pending.result = std::async(std::launch::async, [tools, metadata, source, settings, decode_time]() {
        VideoPreviewResult result;
        std::string error;
        result.decoded = decode_video_frame_rgba(tools, source, metadata, decode_time, error);
        if (!error.empty()) {
            result.error = error;
            return result;
        }
        result.processed = collapse_pixel_blocks(process_image(result.decoded, settings), settings.pixel_size);
        return result;
    });
    pending_video_preview_ = std::move(pending);
    edit_session_.clear_preview_dirty();
}

void App::start_video_export_to_path(const std::filesystem::path& path)
{
    if (document_mode_ != DocumentMode::Video || video_.source.empty()) {
        set_status(text(TextId::StatusVideoExportSkipped));
        return;
    }
    if (!video_export_dialog_) {
        set_status(text(TextId::StatusVideoExportSkipped));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }

    int profile_index = video_export_dialog_->selected_profile;
    if (profile_index < 0 || profile_index >= static_cast<int>(video_.profiles.size())) {
        profile_index = 0;
    }
    if (video_.profiles.empty()) {
        set_status(text(TextId::StatusVideoNoExportProfiles));
        return;
    }

    VideoExportSettings settings;
    settings.profile = video_.profiles[static_cast<std::size_t>(profile_index)];
    settings.metadata = video_.metadata;
    settings.capabilities = video_.capabilities;
    settings.source_path = video_.source;
    settings.destination_path = path;
    settings.process_settings = edit_session_.settings();
    settings.container = video_export_dialog_->container;
    settings.audio_mode = video_export_dialog_->audio_mode;
    const std::vector<VideoAudioOption> audio_options =
        video_audio_options(video_.metadata, video_.capabilities, settings.container);
    const auto audio_option = std::find_if(audio_options.begin(), audio_options.end(), [&](const VideoAudioOption& option) {
        return option.mode == settings.audio_mode;
    });
    if (audio_option != audio_options.end()) {
        settings.audio_encoder = audio_option->encoder;
    } else {
        settings.audio_mode = VideoAudioMode::None;
    }
    settings.crf = video_export_dialog_->crf;
    settings.qp = video_export_dialog_->qp;
    settings.hardware_speed = video_export_dialog_->hardware_speed;
    settings.high_quality_process = video_export_dialog_->high_quality_process;

    PendingVideoExportState pending;
    if (!settings.high_quality_process && can_process_sampled_collapsed_on_gpu(settings.process_settings)) {
        pending.gpu_queue = std::make_shared<VideoExportGpuQueue>();
        auto gpu_disabled = std::make_shared<std::atomic_bool>(false);
        // The export worker owns the source image until this callback returns.
        settings.gpu_process = [queue = pending.gpu_queue, gpu_disabled](
                                   const Image& source,
                                   const ProcessSettings& process_settings,
                                   Image& result,
                                   std::string& error) {
            if (gpu_disabled->load()) {
                error = "Video export GPU processing is disabled.";
                return VideoGpuProcessResult::Fallback;
            }
            auto request = std::make_shared<VideoExportGpuRequest>();
            request->source = &source;
            request->settings = process_settings;

            {
                std::lock_guard lock(queue->mutex);
                if (queue->closed) {
                    error = queue->close_error.empty() ? "Video export GPU processing is closed." : queue->close_error;
                    return queue->closed_status;
                }
                queue->requests.push_back(request);
            }
            queue->cv.notify_one();

            std::unique_lock request_lock(request->mutex);
            request->cv.wait(request_lock, [&request]() {
                return request->finished;
            });
            if (request->status != VideoGpuProcessResult::Success) {
                if (request->status == VideoGpuProcessResult::Fallback) {
                    gpu_disabled->store(true);
                }
                error = std::move(request->error);
                return request->status;
            }
            result = std::move(request->result);
            return VideoGpuProcessResult::Success;
        };
    }
    pending.settings = settings;
    pending.progress = std::make_shared<VideoExportProgress>();
    pending.started_at = std::chrono::steady_clock::now();
    const VideoToolchain tools = video_.tools;
    pending.result = std::async(std::launch::async, [tools, settings, progress = pending.progress]() {
        return export_video_exact(tools, settings, progress);
    });
    video_.playing = false;
    pending_video_export_ = std::move(pending);
    set_video_export_fast_swap(pending_video_export_->gpu_queue != nullptr);
    set_status(textf(TextId::StatusVideoExportStartedFormat, {{"profile", settings.profile.label}}));
}

void App::close_current_file()
{
    if (pending_model_load_) {
        set_status(text(TextId::StatusModelStillLoading));
        return;
    }
    if (pending_video_export_) {
        set_status(text(TextId::StatusVideoExportAlreadyRunning));
        return;
    }
    if (!has_current_file()) {
        return;
    }

    const bool was_model = document_mode_ == DocumentMode::Model;
    const bool was_video = document_mode_ == DocumentMode::Video;
    append_runtime_log(was_model ? "document: closing model file" : (was_video ? "document: closing video file" : "document: closing image file"));

    clear_model_document();
    clear_video_document();
    destroy_texture(original_texture_);
    destroy_texture(result_texture_);
    original_ = {};
    result_ = {};
    current_image_path_.clear();
    document_mode_ = DocumentMode::Image;
    original_zoom_ = 1.0F;
    result_zoom_ = 1.0F;
    pending_dropped_image_.reset();
    open_drop_confirm_ = false;
    edit_session_.cancel_live_edit();
    edit_session_.clear_preview_dirty();

    if (was_model || was_video) {
        viewport_mode_ = ViewportMode::Single;
        viewport_layout_ = ViewportLayout::SideBySide;
        viewport_split_ratio_ = 0.5F;
    }

    set_status(text(TextId::StatusClosedFile));
}

void App::import_model_texture_from_path(const std::filesystem::path& path)
{
    if (document_mode_ != DocumentMode::Model || model_.empty()) {
        load_image_from_path(path);
        return;
    }

    std::error_code ec;
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, ec);
    const std::string path_key = ec ? path.lexically_normal().string() : canonical_path.string();
    for (std::size_t index = 0; index < model_.textures.size(); ++index) {
        const std::filesystem::path& source_path = model_.textures[index].source_path;
        if (source_path.empty()) {
            continue;
        }
        std::error_code existing_ec;
        const std::filesystem::path existing_canonical = std::filesystem::weakly_canonical(source_path, existing_ec);
        const std::string existing_key = existing_ec ? source_path.lexically_normal().string() : existing_canonical.string();
        if (existing_key == path_key) {
            set_status(textf(
                TextId::StatusModelTextureAlreadyImportedFormat,
                {{"name", model_texture_display_name(index)}}));
            return;
        }
    }

    ImageLoadResult loaded = load_image_rgba(path.string());
    if (!loaded.error.empty()) {
        set_status(textf(TextId::StatusImageLoadFailedFormat, {{"error", loaded.error}}));
        return;
    }

    ModelTexture texture;
    texture.name = path.filename().string();
    if (texture.name.empty()) {
        texture.name = "texture_" + std::to_string(model_.textures.size()) + ".png";
    }
    texture.source_path = path;
    texture.embedded = false;
    texture.image = std::move(loaded.image);

    const std::size_t texture_index = model_.textures.size();
    model_.textures.push_back(std::move(texture));

    model_original_textures_.emplace_back();
    rebuild_texture(model_original_textures_.back(), model_.textures.back().image, false);
    model_processed_textures_.push_back(process_image(model_.textures.back().image, edit_session_.settings()));
    model_result_textures_.emplace_back();
    rebuild_texture(model_result_textures_.back(), model_processed_textures_.back(), true);
    if (!ensure_gl_context_current()) {
        return;
    }
    model_renderer_.update_processed_textures(model_processed_textures_);

    set_status(textf(
        TextId::StatusModelTextureImportedFormat,
        {{"name", model_texture_display_name(texture_index)}}));
}

void App::assign_model_texture_to_slot(const ModelMaterialSlot& slot, std::size_t texture_index)
{
    if (texture_index >= model_.textures.size()) {
        return;
    }

    bool assigned = false;
    for (ModelPrimitive& primitive : model_.primitives) {
        const bool matches = slot.material_index >= 0
            ? primitive.material_index == slot.material_index
            : primitive.material_index < 0 && primitive.mesh_index == slot.mesh_index;
        if (!matches) {
            continue;
        }
        primitive.texture_index = static_cast<int>(texture_index);
        assigned = true;
    }

    if (!assigned) {
        return;
    }

    model_.used_fallback_texture = false;
    for (const ModelPrimitive& primitive : model_.primitives) {
        if (primitive.texture_index < 0) {
            model_.used_fallback_texture = true;
            break;
        }
    }

    if (!ensure_gl_context_current()) {
        return;
    }

    std::string renderer_error;
    if (!model_renderer_.upload_model(model_, model_processed_textures_, renderer_error)) {
        set_status(textf(TextId::StatusModelRenderFailedFormat, {{"error", renderer_error}}));
        return;
    }

    std::string material_name = slot.material_name.empty() ? slot.mesh_name : slot.material_name;
    if (material_name.empty()) {
        material_name = "material";
    } else if (!slot.material_name.empty() && !slot.mesh_name.empty() && slot.material_name != slot.mesh_name) {
        material_name = slot.mesh_name + " / " + slot.material_name;
    }
    set_status(textf(
        TextId::StatusModelTextureAssignedFormat,
        {{"texture", model_texture_display_name(texture_index)}, {"material", material_name}}));
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

void App::request_save_preset()
{
    edit_session_.cancel_live_edit();

    std::string name = "custom-preset";
    const int matching = matching_preset_index(edit_session_.settings());
    if (matching >= 0) {
        name = presets_[static_cast<std::size_t>(matching)].name;
    }

    PresetSaveAsState state;
    state.request_open = true;
    std::snprintf(state.name.data(), state.name.size(), "%s", name.c_str());
    preset_save_as_ = state;
}

bool App::save_preset_as_name(const std::string& name)
{
    const std::string normalized_name = normalize_preset_name(name);
    if (find_preset_conflict(normalized_name)) {
        pending_preset_overwrite_ = PendingPresetOverwriteState{
            normalized_name,
            edit_session_.settings(),
            true,
        };
        return false;
    }

    Preset saved;
    std::string error;
    if (!save_preset_as(normalized_name, edit_session_.settings(), PresetSaveMode::Create, saved, error)) {
        set_status(textf(TextId::StatusPresetSaveFailedFormat, {{"error", error}}));
        return false;
    }

    refresh_presets();
    select_preset_by_path(saved.path);
    set_status(textf(TextId::StatusSavedPresetFormat, {{"name", saved.name}}));
    return true;
}

bool App::overwrite_pending_preset()
{
    if (!pending_preset_overwrite_) {
        return false;
    }

    Preset saved;
    std::string error;
    if (!save_preset_as(
            pending_preset_overwrite_->name,
            pending_preset_overwrite_->settings,
            PresetSaveMode::Overwrite,
            saved,
            error)) {
        set_status(textf(TextId::StatusPresetSaveFailedFormat, {{"error", error}}));
        return false;
    }

    refresh_presets();
    select_preset_by_path(saved.path);
    set_status(textf(TextId::StatusOverwrotePresetFormat, {{"name", saved.name}}));
    return true;
}

void App::request_delete_selected_preset()
{
    const int index = effective_selected_preset_index(edit_session_.settings());
    if (index < 0 || index >= static_cast<int>(presets_.size())) {
        return;
    }

    pending_delete_preset_ = PendingPresetDeleteState{
        presets_[static_cast<std::size_t>(index)],
        true,
    };
}

void App::delete_pending_preset()
{
    if (!pending_delete_preset_) {
        return;
    }

    const std::string deleted_name = pending_delete_preset_->preset.name;
    std::string error;
    if (!delete_preset_file(pending_delete_preset_->preset, error)) {
        set_status(textf(TextId::StatusPresetDeleteFailedFormat, {{"error", error}}));
        return;
    }

    refresh_presets();
    selected_preset_ = -1;
    set_status(textf(TextId::StatusDeletedPresetFormat, {{"name", deleted_name}}));
}

void App::apply_preset_settings(const Preset& preset, ProcessSettings& settings, int& selected_palette)
{
    settings = preset.settings;
    selected_palette = matching_palette_index(settings);
    normalize_settings();
}

void App::export_result_to_png_path(const std::filesystem::path& path)
{
    if (result_.empty()) {
        set_status(text(TextId::StatusExportSkipped));
        return;
    }

    std::string error;
    const std::string destination = ensure_extension(path, ".png");
    const Image export_image = collapse_pixel_blocks(result_, edit_session_.settings().pixel_size);
    if (!save_png_rgba(destination, export_image, error)) {
        set_status(textf(TextId::StatusExportFailedFormat, {{"error", error}}));
        return;
    }

    last_export_path_ = destination;
    set_status(textf(TextId::StatusExportedFormat, {{"name", std::filesystem::path(destination).filename().string()}}));
}

void App::export_model_texture_to_png_path(std::size_t texture_index, const std::filesystem::path& path)
{
    if (texture_index >= model_processed_textures_.size()) {
        set_status(text(TextId::StatusExportSkipped));
        return;
    }

    std::string error;
    const std::string destination = ensure_extension(path, ".png");
    const Image export_image = collapse_pixel_blocks(
        model_processed_textures_[texture_index],
        edit_session_.settings().pixel_size);
    if (!save_png_rgba(destination, export_image, error)) {
        set_status(textf(TextId::StatusExportFailedFormat, {{"error", error}}));
        return;
    }

    last_export_path_ = destination;
    set_status(textf(TextId::StatusExportedFormat, {{"name", std::filesystem::path(destination).filename().string()}}));
}

void App::export_result_to_raw_path(const std::filesystem::path& path)
{
    if (result_.empty()) {
        set_status(text(TextId::StatusExportSkipped));
        return;
    }

    const ProcessSettings& settings = edit_session_.settings();
    const std::vector<Color32> empty_palette;
    const std::vector<Color32>& preferred_palette = settings.use_palette ? settings.palette : empty_palette;

    std::string palette_name;
    const int selected_palette = edit_session_.selected_palette();
    if (settings.use_palette && selected_palette >= 0 && selected_palette < static_cast<int>(palettes_.size())) {
        const Palette& palette = palettes_[static_cast<std::size_t>(selected_palette)];
        palette_name = palette.path.empty() ? palette.name : palette.path.stem().string();
    }

    std::string error;
    const std::string destination = ensure_extension(path, ".raw");
    const Image export_image = collapse_pixel_blocks(result_, settings.pixel_size);
    if (!save_raw_indexed(destination, export_image, preferred_palette, palette_name, error)) {
        set_status(textf(TextId::StatusExportFailedFormat, {{"error", error}}));
        return;
    }

    last_export_path_ = destination;
    set_status(textf(TextId::StatusExportedFormat, {{"name", std::filesystem::path(destination).filename().string()}}));
}

void App::add_batch_images(const std::vector<std::filesystem::path>& paths)
{
    if (!batch_) {
        request_batch();
    }
    if (!batch_ || batch_->processing) {
        return;
    }

    std::size_t added = 0;
    for (const std::filesystem::path& path : paths) {
        if (!is_importable_image_path(path)) {
            continue;
        }

        const auto duplicate = std::find(batch_->images.begin(), batch_->images.end(), path);
        if (duplicate != batch_->images.end()) {
            continue;
        }

        batch_->images.push_back(path);
        ++added;
    }

    if (added == 0U) {
        set_status(text(TextId::StatusBatchNoImagesAdded));
        return;
    }

    batch_->processed = 0;
    batch_->succeeded = 0;
    batch_->failed = 0;
    batch_->last_error.clear();
    set_status(textf(TextId::StatusBatchImagesAddedFormat, {{"count", std::to_string(added)}}));
}

void App::start_batch_processing()
{
    if (!batch_) {
        return;
    }

    if (batch_->images.empty()) {
        set_status(text(TextId::StatusBatchNoImages));
        return;
    }
    if (batch_->output_dir.empty()) {
        set_status(text(TextId::StatusBatchNoOutputFolder));
        return;
    }

    if (batch_->selected_preset < 0 || batch_->selected_preset >= static_cast<int>(presets_.size())) {
        batch_->selected_preset = -1;
    }

    batch_->processing = true;
    batch_->cancel_requested = false;
    batch_->processed = 0;
    batch_->succeeded = 0;
    batch_->failed = 0;
    batch_->last_error.clear();
    set_status(textf(TextId::StatusBatchStartedFormat, {{"count", std::to_string(batch_->images.size())}}));
}

void App::update_batch_processing()
{
    if (!batch_ || !batch_->processing) {
        return;
    }

    if (batch_->cancel_requested) {
        batch_->processing = false;
        set_status(textf(
            TextId::StatusBatchStoppedFormat,
            {{"processed", std::to_string(batch_->processed)}, {"total", std::to_string(batch_->images.size())}}));
        return;
    }

    if (batch_->processed < batch_->images.size()) {
        process_next_batch_image();
    }

    if (batch_ && batch_->processing && batch_->processed >= batch_->images.size()) {
        batch_->processing = false;
        set_status(textf(
            TextId::StatusBatchCompletedFormat,
            {{"succeeded", std::to_string(batch_->succeeded)}, {"failed", std::to_string(batch_->failed)}}));
    }
}

void App::process_next_batch_image()
{
    if (!batch_ || batch_->processed >= batch_->images.size()) {
        return;
    }

    const std::filesystem::path source = batch_->images[batch_->processed];
    bool exported = false;
    std::string error;

    ImageLoadResult loaded = load_image_rgba(source.string());
    if (!loaded.error.empty()) {
        error = loaded.error;
    } else {
        ProcessSettings settings = edit_session_.settings();
        std::string palette_name;
        if (batch_->selected_preset >= 0 && batch_->selected_preset < static_cast<int>(presets_.size())) {
            const Preset& preset = presets_[static_cast<std::size_t>(batch_->selected_preset)];
            settings = preset.settings;
            palette_name = preset.name;
        } else if (settings.use_palette) {
            const int selected_palette = edit_session_.selected_palette();
            if (selected_palette >= 0 && selected_palette < static_cast<int>(palettes_.size())) {
                const Palette& palette = palettes_[static_cast<std::size_t>(selected_palette)];
                palette_name = palette.path.empty() ? palette.name : palette.path.stem().string();
            }
        }

        const Image processed = process_image(loaded.image, settings);
        const Image export_image = collapse_pixel_blocks(processed, settings.pixel_size);
        const char* extension = batch_->format == BatchExportFormat::Png ? ".png" : ".raw";
        const std::filesystem::path destination = batch_output_path(
            batch_->output_dir,
            source,
            batch_->suffix.data(),
            extension);
        if (batch_->format == BatchExportFormat::Png) {
            exported = save_png_rgba(destination.string(), export_image, error);
        } else {
            const std::vector<Color32> empty_palette;
            const std::vector<Color32>& preferred_palette = settings.use_palette ? settings.palette : empty_palette;
            exported = save_raw_indexed(destination.string(), export_image, preferred_palette, palette_name, error);
        }

        if (exported) {
            last_export_path_ = destination;
        }
    }

    if (exported) {
        ++batch_->succeeded;
    } else {
        ++batch_->failed;
        batch_->last_error = source.filename().string() + ": " + error;
        set_status(textf(TextId::StatusExportFailedFormat, {{"error", batch_->last_error}}));
    }

    ++batch_->processed;
}

void App::clear_model_document()
{
    append_runtime_log("model-doc: clear begin");
    ModelDocument empty_model;
    std::vector<Image> empty_textures;
    std::string ignored_error;
    if (ensure_gl_context_current()) {
        (void)model_renderer_.upload_model(empty_model, empty_textures, ignored_error);
        if (!ignored_error.empty()) {
            append_runtime_log(std::string("model-doc: clear upload_model reported: ") + ignored_error);
        }
    }

    for (Texture& texture : model_original_textures_) {
        destroy_texture(texture);
    }
    for (Texture& texture : model_result_textures_) {
        destroy_texture(texture);
    }
    model_original_textures_.clear();
    model_result_textures_.clear();
    model_processed_textures_.clear();
    model_ = {};
    pending_model_texture_exports_.clear();
    model_preview_log_frames_ = 0;
    append_runtime_log("model-doc: clear complete");
}

void App::clear_video_document()
{
    video_.playing = false;
    video_playback_decoder_.close();
    if (pending_video_export_ && pending_video_export_->progress) {
        pending_video_export_->progress->cancel_requested = true;
        close_video_export_gpu_queue(
            pending_video_export_->gpu_queue,
            VideoGpuProcessResult::Canceled,
            "Video export canceled.");
        set_video_export_fast_swap(false);
        try {
            (void)pending_video_export_->result.get();
        } catch (...) {
        }
        pending_video_export_.reset();
    }
    if (pending_video_preview_) {
        try {
            (void)pending_video_preview_->result.get();
        } catch (...) {
        }
        pending_video_preview_.reset();
    }
    if (pending_video_hardware_probe_) {
        try {
            (void)pending_video_hardware_probe_->result.get();
        } catch (...) {
        }
        pending_video_hardware_probe_.reset();
    }
    video_export_dialog_.reset();
    video_decoded_frame_ = {};
    video_decoded_frame_time_ = 0.0;
    video_decoded_frame_valid_ = false;
    video_ = {};
}

void App::reset_model_camera()
{
    model_yaw_ = 0.65F;
    model_pitch_ = 0.35F;
    model_distance_ = 2.8F;
    model_target_offset_x_ = 0.0F;
    model_target_offset_y_ = 0.0F;
    model_target_offset_z_ = 0.0F;
}

void App::reset_model_camera_to_origin()
{
    model_yaw_ = 0.65F;
    model_pitch_ = 0.35F;
    model_distance_ = 2.8F;
    model_target_offset_x_ = -model_.center[0];
    model_target_offset_y_ = -model_.center[1];
    model_target_offset_z_ = -model_.center[2];
}

void App::pan_model_camera(float delta_x, float delta_y)
{
    if (delta_x == 0.0F && delta_y == 0.0F) {
        return;
    }

    CameraVector right;
    CameraVector up;
    CameraVector forward;
    model_camera_basis(model_yaw_, model_pitch_, right, up, forward);

    const float radius = std::max(model_.radius, 0.1F);
    const float scale = radius * std::max(model_distance_, 0.8F) * 0.0015F;
    model_target_offset_x_ += (-right.x * delta_x + up.x * delta_y) * scale;
    model_target_offset_y_ += (-right.y * delta_x + up.y * delta_y) * scale;
    model_target_offset_z_ += (-right.z * delta_x + up.z * delta_y) * scale;
}

std::vector<App::ModelMaterialSlot> App::model_material_slots() const
{
    std::vector<ModelMaterialSlot> slots;
    for (const ModelPrimitive& primitive : model_.primitives) {
        auto found = std::find_if(slots.begin(), slots.end(), [&](const ModelMaterialSlot& slot) {
            if (primitive.material_index >= 0) {
                return slot.material_index == primitive.material_index;
            }
            return slot.material_index < 0 && slot.mesh_index == primitive.mesh_index;
        });

        if (found == slots.end()) {
            ModelMaterialSlot slot;
            slot.mesh_index = primitive.mesh_index;
            slot.material_index = primitive.material_index;
            slot.texture_index = primitive.texture_index;
            slot.primitive_count = 1U;
            slot.mesh_name = primitive.mesh_name;
            slot.material_name = primitive.material_name;
            slots.push_back(std::move(slot));
            continue;
        }

        found->primitive_count += 1U;
        if (found->texture_index != primitive.texture_index) {
            found->texture_index = -1;
        }
        if (found->mesh_name.empty()) {
            found->mesh_name = primitive.mesh_name;
        }
        if (found->material_name.empty()) {
            found->material_name = primitive.material_name;
        }
    }
    return slots;
}

std::string App::model_texture_display_name(std::size_t texture_index) const
{
    if (texture_index >= model_.textures.size()) {
        return text(TextId::UntexturedGrey);
    }

    const ModelTexture& texture = model_.textures[texture_index];
    if (!texture.name.empty()) {
        return texture.name;
    }
    return default_model_texture_export_name(texture, texture_index);
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

void App::refresh_presets()
{
    presets_ = load_saved_presets();
    if (selected_preset_ < 0 || selected_preset_ >= static_cast<int>(presets_.size())) {
        selected_preset_ = -1;
    }
}

int App::matching_preset_index(const ProcessSettings& settings) const
{
    for (int index = 0; index < static_cast<int>(presets_.size()); ++index) {
        if (presets_[static_cast<std::size_t>(index)].settings == settings) {
            return index;
        }
    }
    return -1;
}

int App::effective_selected_preset_index(const ProcessSettings& settings) const
{
    if (selected_preset_ >= 0 && selected_preset_ < static_cast<int>(presets_.size())) {
        return selected_preset_;
    }
    return matching_preset_index(settings);
}

int App::matching_palette_index(const ProcessSettings& settings) const
{
    if (!settings.use_palette || settings.palette.empty()) {
        return -1;
    }

    for (int index = 0; index < static_cast<int>(palettes_.size()); ++index) {
        if (palettes_[static_cast<std::size_t>(index)].colors == settings.palette) {
            return index;
        }
    }
    return -1;
}

bool App::has_current_file() const noexcept
{
    if (document_mode_ == DocumentMode::Model) {
        return !model_.empty();
    }
    if (document_mode_ == DocumentMode::Video) {
        return !video_.source.empty();
    }
    return !original_.empty();
}

bool App::select_preset_by_path(const std::filesystem::path& path)
{
    for (int index = 0; index < static_cast<int>(presets_.size()); ++index) {
        if (presets_[static_cast<std::size_t>(index)].path == path) {
            selected_preset_ = index;
            return true;
        }
    }

    selected_preset_ = -1;
    return false;
}

void App::mark_dirty()
{
    edit_session_.mark_dirty();
}

void App::set_status(std::string message)
{
    status_ = std::move(message);
}

bool App::ensure_gl_context_current()
{
    if (!window_ || !gl_context_) {
        append_runtime_log("gl: cannot make context current because window/context is null");
        return false;
    }
    if (!SDL_GL_MakeCurrent(window_, gl_context_)) {
        append_runtime_log(std::string("gl: SDL_GL_MakeCurrent failed: ") + SDL_GetError());
        set_status(textf(TextId::StatusRendererSetupFailedFormat, {{"error", SDL_GetError()}}));
        return false;
    }
    return true;
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
        text(TextId::ModelsFilter),
        text(TextId::VideosFilter),
        text(TextId::AllFilesFilter),
        text(TextId::LospecPalettesFilter),
        text(TextId::PngImageFilter),
        text(TextId::RawImageFilter),
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

} // namespace pixatto
