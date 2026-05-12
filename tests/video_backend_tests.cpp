#include "pixatto/video_backend.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("video backend test failed");
    }
}

bool has_arg(const std::vector<std::string>& args, const std::string& value)
{
    return std::find(args.begin(), args.end(), value) != args.end();
}

const pixatto::VideoExportProfile& require_profile(
    const std::vector<pixatto::VideoExportProfile>& profiles,
    const std::string& id)
{
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const pixatto::VideoExportProfile& profile) {
        return profile.id == id;
    });
    require(found != profiles.end());
    return *found;
}

void video_extension_routing_is_case_insensitive()
{
    require(pixatto::is_importable_video_path("clip.MP4"));
    require(pixatto::is_importable_video_path("clip.mov"));
    require(pixatto::is_importable_video_path("clip.ogv"));
    require(!pixatto::is_importable_video_path("clip.png"));
}

void capabilities_drive_profile_availability()
{
    const char* encoders =
        " V..... libx264            H.264\n"
        " V..... libx265            H.265\n"
        " V..... libvpx-vp9         VP9\n"
        " V..... libsvtav1          AV1\n"
        " V..... ffv1               FFV1\n"
        " V..... h264_nvenc         NVIDIA H.264\n"
        " V..... hevc_nvenc         NVIDIA H.265\n"
        " V..... av1_amf            AMD AV1\n"
        " V..... h264_qsv           Intel H.264\n";
    const char* muxers =
        " E mp4             MP4\n"
        " E webm            WebM\n"
        " E matroska        Matroska\n";

    pixatto::VideoCapabilities capabilities = pixatto::parse_ffmpeg_capabilities(encoders, muxers);
    require(capabilities.has_encoder("libx264"));
    require(capabilities.has_encoder("h264_nvenc"));
    require(capabilities.has_muxer("matroska"));
    require(!capabilities.has_hardware_encoder("h264_nvenc"));

    capabilities.hardware_encoders = {"av1_amf", "h264_nvenc", "h264_qsv"};
    require(capabilities.has_hardware_encoder("h264_nvenc"));

    const std::vector<pixatto::VideoExportProfile> profiles = pixatto::build_video_export_profiles(capabilities);
    require(require_profile(profiles, "mp4_h264").backend == pixatto::VideoExportBackend::Software);
    require(require_profile(profiles, "mp4_h265").encoder == "libx265");
    require(require_profile(profiles, "webm_vp9").container == pixatto::VideoContainer::Webm);
    require(require_profile(profiles, "mkv_av1_svt").encoder == "libsvtav1");
    require(require_profile(profiles, "mkv_ffv1").lossless);
    require(require_profile(profiles, "mp4_h264_nvenc").backend == pixatto::VideoExportBackend::NvidiaNvenc);
    require(require_profile(profiles, "mkv_av1_amf").backend == pixatto::VideoExportBackend::AmdAmf);
    require(require_profile(profiles, "mp4_h264_qsv").backend == pixatto::VideoExportBackend::IntelQsv);
}

void metadata_parser_handles_fps_frames_and_audio()
{
    const pixatto::VideoMetadata metadata = pixatto::parse_video_metadata(
        "codec_name=h264\n"
        "width=1920\n"
        "height=1080\n"
        "avg_frame_rate=30000/1001\n"
        "r_frame_rate=60/1\n"
        "duration=2.5\n"
        "nb_frames=N/A\n",
        "codec_name=aac\n");

    require(metadata.width == 1920);
    require(metadata.height == 1080);
    require(metadata.video_codec == "h264");
    require(metadata.has_audio);
    require(metadata.audio_codec == "aac");
    require(metadata.frame_count == 75);
    require(metadata.variable_frame_rate);
}

void output_dimensions_and_audio_rules_are_container_aware()
{
    pixatto::VideoMetadata metadata;
    metadata.width = 1919;
    metadata.height = 1079;

    pixatto::VideoExportProfile h264;
    h264.needs_even_dimensions = true;
    const pixatto::VideoDimensions h264_dimensions = pixatto::video_export_dimensions(metadata, 4, h264);
    require(h264_dimensions.width == 480);
    require(h264_dimensions.height == 270);
    require(!h264_dimensions.padded);

    pixatto::VideoExportProfile ffv1;
    ffv1.needs_even_dimensions = false;
    const pixatto::VideoDimensions ffv1_dimensions = pixatto::video_export_dimensions(metadata, 4, ffv1);
    require(ffv1_dimensions.width == 480);
    require(ffv1_dimensions.height == 270);
    require(!ffv1_dimensions.padded);

    metadata.width = 1921;
    metadata.height = 1081;
    const pixatto::VideoDimensions odd_dimensions = pixatto::video_export_dimensions(metadata, 4, h264);
    require(odd_dimensions.width == 482);
    require(odd_dimensions.height == 272);
    require(odd_dimensions.padded);

    require(pixatto::can_copy_audio_to_container("aac", pixatto::VideoContainer::Mp4));
    require(!pixatto::can_copy_audio_to_container("flac", pixatto::VideoContainer::Mp4));
    require(pixatto::can_copy_audio_to_container("opus", pixatto::VideoContainer::Webm));
    require(pixatto::can_copy_audio_to_container("flac", pixatto::VideoContainer::Mkv));
}

void encode_command_uses_raw_frame_pipe_and_profile_settings()
{
    pixatto::VideoCapabilities capabilities;
    capabilities.encoders = {"h264_amf", "h264_nvenc", "h264_qsv", "libx264"};
    capabilities.hardware_encoders = {"h264_amf", "h264_nvenc", "h264_qsv"};
    capabilities.muxers = {"mp4"};
    std::vector<pixatto::VideoExportProfile> profiles = pixatto::build_video_export_profiles(capabilities);

    pixatto::VideoToolchain tools;
    tools.ffmpeg_path = "ffmpeg";
    tools.ffprobe_path = "ffprobe";

    pixatto::VideoExportSettings settings;
    settings.profile = require_profile(profiles, "mp4_h264");
    settings.metadata.width = 640;
    settings.metadata.height = 480;
    settings.metadata.fps = 24.0;
    settings.metadata.has_audio = true;
    settings.metadata.audio_codec = "aac";
    settings.source_path = "input.mov";
    settings.destination_path = "out";
    settings.crf = 16;

    std::vector<std::string> args = pixatto::build_video_encode_command(tools, settings, 320, 240, true);
    require(has_arg(args, "-f"));
    require(has_arg(args, "rawvideo"));
    require(has_arg(args, "-s"));
    require(has_arg(args, "320x240"));
    require(has_arg(args, "pipe:0"));
    require(has_arg(args, "1:a:0"));
    require(has_arg(args, "libx264"));
    require(has_arg(args, "+faststart"));
    require(args.back() == "out.mp4");

    settings.profile = require_profile(profiles, "mp4_h264_nvenc");
    settings.hardware_speed = pixatto::VideoHardwareSpeed::VeryFast;
    settings.bitrate_mbps = 32;
    args = pixatto::build_video_encode_command(tools, settings, 320, 240, false);
    require(has_arg(args, "h264_nvenc"));
    require(has_arg(args, "p1"));
    require(has_arg(args, "32M"));
    require(has_arg(args, "-an"));

    settings.profile = require_profile(profiles, "mp4_h264_amf");
    settings.hardware_speed = pixatto::VideoHardwareSpeed::Fast;
    args = pixatto::build_video_encode_command(tools, settings, 320, 240, false);
    require(has_arg(args, "h264_amf"));
    require(has_arg(args, "speed"));

    settings.profile = require_profile(profiles, "mp4_h264_qsv");
    settings.hardware_speed = pixatto::VideoHardwareSpeed::Balanced;
    args = pixatto::build_video_encode_command(tools, settings, 320, 240, false);
    require(has_arg(args, "h264_qsv"));
    require(has_arg(args, "medium"));
    require(has_arg(args, "nv12"));
}

} // namespace

int main()
{
    video_extension_routing_is_case_insensitive();
    capabilities_drive_profile_availability();
    metadata_parser_handles_fps_frames_and_audio();
    output_dimensions_and_audio_rules_are_container_aware();
    encode_command_uses_raw_frame_pipe_and_profile_settings();
    return 0;
}
