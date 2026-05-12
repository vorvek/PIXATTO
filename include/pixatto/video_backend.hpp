#pragma once

#include "pixatto/image.hpp"
#include "pixatto/image_processing.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pixatto {

inline constexpr std::string_view kImportableVideoDialogPattern =
    "mp4;mov;mkv;webm;avi;m4v;mpg;mpeg;ogv";

enum class VideoContainer {
    Mp4,
    Webm,
    Mkv,
};

enum class VideoCodec {
    H264,
    H265,
    Av1,
    Vp9,
};

enum class VideoExportBackend {
    Software,
    NvidiaNvenc,
    AmdAmf,
    IntelQsv,
};

enum class VideoHardwareSpeed {
    Balanced,
    Fast,
    VeryFast,
};

enum class VideoAudioMode {
    None,
    Copy,
    Aac,
    Vorbis,
};

enum class VideoGpuProcessResult {
    Success,
    Fallback,
    Canceled,
};

struct VideoToolchain {
    std::filesystem::path ffmpeg_path;
    std::filesystem::path ffprobe_path;
    std::string version;
    std::string error;

    [[nodiscard]] bool available() const noexcept
    {
        return !ffmpeg_path.empty() && !ffprobe_path.empty() && error.empty();
    }
};

struct VideoCapabilities {
    std::vector<std::string> encoders;
    std::vector<std::string> audio_encoders;
    std::vector<std::string> hardware_encoders;
    std::vector<std::string> muxers;

    [[nodiscard]] bool has_encoder(std::string_view encoder) const;
    [[nodiscard]] bool has_audio_encoder(std::string_view encoder) const;
    [[nodiscard]] bool has_hardware_encoder(std::string_view encoder) const;
    [[nodiscard]] bool has_muxer(std::string_view muxer) const;
};

struct VideoMetadata {
    int width = 0;
    int height = 0;
    double duration_seconds = 0.0;
    double fps = 0.0;
    long long frame_count = 0;
    std::string video_codec;
    std::string audio_codec;
    std::string container_format;
    bool has_audio = false;
    bool variable_frame_rate = false;
};

struct VideoExportProfile {
    std::string id;
    std::string label;
    std::string encoder;
    VideoCodec codec = VideoCodec::H264;
    VideoExportBackend backend = VideoExportBackend::Software;
    int crf_default = 18;
    int qp_default = 16;
    bool needs_even_dimensions = true;
};

struct VideoExportSettings {
    VideoExportProfile profile;
    VideoMetadata metadata;
    VideoCapabilities capabilities;
    std::filesystem::path source_path;
    std::filesystem::path destination_path;
    ProcessSettings process_settings;
    VideoContainer container = VideoContainer::Mp4;
    VideoAudioMode audio_mode = VideoAudioMode::None;
    std::string audio_encoder;
    int crf = 18;
    int qp = 16;
    VideoHardwareSpeed hardware_speed = VideoHardwareSpeed::Balanced;
    bool gpu_processing = false;
    std::function<VideoGpuProcessResult(const Image&, const ProcessSettings&, Image&, std::string&)> gpu_process;
};

struct VideoExportProgress {
    std::atomic<bool> cancel_requested = false;
    std::atomic<int> frames_done = 0;
    std::atomic<int> frames_total = 0;
    std::atomic<int> percent = 0;
    std::atomic<bool> encoding = false;
};

struct VideoExportResult {
    bool success = false;
    bool audio_copied = false;
    std::filesystem::path diagnostic_log_path;
    std::string warning;
    std::string error;
};

struct VideoDimensions {
    int width = 0;
    int height = 0;
    bool padded = false;
};

[[nodiscard]] bool is_importable_video_path(const std::filesystem::path& path);
[[nodiscard]] VideoToolchain find_video_toolchain();
[[nodiscard]] VideoCapabilities parse_ffmpeg_capabilities(std::string_view encoders, std::string_view muxers);
[[nodiscard]] VideoCapabilities probe_video_capabilities(const VideoToolchain& tools, std::string& error);
[[nodiscard]] std::vector<std::string> probe_video_hardware_encoders(
    const VideoToolchain& tools,
    const VideoCapabilities& capabilities);
[[nodiscard]] std::vector<VideoExportProfile> build_video_export_profiles(const VideoCapabilities& capabilities);
[[nodiscard]] VideoMetadata parse_video_metadata(std::string_view video_output, std::string_view audio_output);
[[nodiscard]] VideoMetadata probe_video_metadata(
    const VideoToolchain& tools,
    const std::filesystem::path& path,
    std::string& error);
[[nodiscard]] Image decode_video_frame_rgba(
    const VideoToolchain& tools,
    const std::filesystem::path& path,
    const VideoMetadata& metadata,
    double seconds,
    std::string& error);
[[nodiscard]] VideoDimensions video_export_dimensions(const VideoMetadata& metadata, int pixel_size, const VideoExportProfile& profile);
[[nodiscard]] bool video_profile_supports_container(const VideoExportProfile& profile, VideoContainer container);
[[nodiscard]] bool can_copy_audio_to_container(
    std::string_view audio_codec,
    VideoContainer container,
    std::string_view source_container_format = {});
[[nodiscard]] bool can_encode_audio_to_container(VideoAudioMode audio_mode, VideoContainer container);
[[nodiscard]] std::string video_container_extension(VideoContainer container);
[[nodiscard]] std::string video_container_muxer(VideoContainer container);
[[nodiscard]] int video_profile_crf_max(const VideoExportProfile& profile);
[[nodiscard]] std::vector<std::string> build_video_encode_command(
    const VideoToolchain& tools,
    const VideoExportSettings& settings,
    int input_width,
    int input_height);
[[nodiscard]] VideoExportResult export_video_exact(
    const VideoToolchain& tools,
    const VideoExportSettings& settings,
    std::shared_ptr<VideoExportProgress> progress);
[[nodiscard]] std::string video_container_extension_filter(VideoContainer container);
[[nodiscard]] std::string format_video_time(double seconds);
[[nodiscard]] std::string format_video_fps(double fps);

#ifdef PIXATTO_VIDEO_BACKEND_TEST_HOOKS
struct VideoFrameProcessTestResult {
    bool ok = false;
    Image processed;
    std::string error;
};

[[nodiscard]] VideoFrameProcessTestResult process_video_frame_for_export_for_test(
    const Image& source,
    const ProcessSettings& settings,
    bool gpu_processing,
    const std::function<VideoGpuProcessResult(const Image&, const ProcessSettings&, Image&, std::string&)>& gpu_process,
    const std::shared_ptr<std::atomic_bool>& gpu_fallback_mode);
#endif

} // namespace pixatto
