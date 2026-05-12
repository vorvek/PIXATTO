#include "pixatto/video_backend.hpp"
#include "pixatto/video_playback_decoder.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kSkip = 77;

struct SdlScope {
    bool initialized = false;

    SdlScope()
        : initialized(SDL_Init(0))
    {
    }

    ~SdlScope()
    {
        if (initialized) {
            SDL_Quit();
        }
    }
};

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<const char*> c_args_for(const std::vector<std::string>& args)
{
    std::vector<const char*> c_args;
    c_args.reserve(args.size() + 1U);
    for (const std::string& arg : args) {
        c_args.push_back(arg.c_str());
    }
    c_args.push_back(nullptr);
    return c_args;
}

bool run_process(const std::vector<std::string>& args)
{
    std::vector<const char*> c_args = c_args_for(args);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, c_args.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        return false;
    }

    int exit_code = -1;
    SDL_WaitProcess(process, true, &exit_code);
    SDL_DestroyProcess(process);
    return exit_code == 0;
}

std::filesystem::path make_temp_dir()
{
    std::error_code ec;
    std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec || base.empty()) {
        base = std::filesystem::current_path(ec);
    }
    const auto suffix = std::to_string(SDL_GetTicksNS());
    std::filesystem::path path = base / ("pixatto-video-integration-" + suffix);
    std::filesystem::create_directories(path, ec);
    require(!ec, "failed to create temp directory");
    return path;
}

const pixatto::VideoExportProfile* choose_profile(const std::vector<pixatto::VideoExportProfile>& profiles)
{
    auto by_id = [&](const std::string& id) -> const pixatto::VideoExportProfile* {
        const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const pixatto::VideoExportProfile& profile) {
            return profile.id == id;
        });
        return found == profiles.end() ? nullptr : &*found;
    };

    if (const pixatto::VideoExportProfile* profile = by_id("h264")) {
        return profile;
    }
    const auto software = std::find_if(profiles.begin(), profiles.end(), [](const pixatto::VideoExportProfile& profile) {
        return profile.backend == pixatto::VideoExportBackend::Software;
    });
    return software == profiles.end() ? nullptr : &*software;
}

int run()
{
    SdlScope sdl;
    if (!sdl.initialized) {
        std::cout << "SDL_Init failed: " << SDL_GetError() << '\n';
        return kSkip;
    }

    pixatto::VideoToolchain tools = pixatto::find_video_toolchain();
    if (!tools.available()) {
        std::cout << tools.error << '\n';
        return kSkip;
    }

    std::string error;
    pixatto::VideoCapabilities capabilities = pixatto::probe_video_capabilities(tools, error);
    if (!error.empty()) {
        std::cout << error << '\n';
        return kSkip;
    }
    const std::vector<pixatto::VideoExportProfile> profiles = pixatto::build_video_export_profiles(capabilities);
    const pixatto::VideoExportProfile* profile = choose_profile(profiles);
    if (!profile) {
        std::cout << "No usable FFmpeg export profile is available.\n";
        return kSkip;
    }

    const std::filesystem::path temp_dir = make_temp_dir();
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temp_dir, ignored);
    };

    const std::filesystem::path input = temp_dir / "source.mp4";
    std::vector<std::string> generate_args = {
        tools.ffmpeg_path.string(),
        "-y",
        "-v",
        "error",
        "-f",
        "lavfi",
        "-i",
        "testsrc2=size=64x48:rate=5:duration=1",
    };
    if (capabilities.has_encoder("libx264")) {
        generate_args.insert(generate_args.end(), {
            "-c:v",
            "libx264",
            "-preset",
            "ultrafast",
            "-crf",
            "23",
            "-pix_fmt",
            "yuv420p",
        });
    } else {
        generate_args.insert(generate_args.end(), {
            "-c:v",
            "mpeg4",
            "-q:v",
            "4",
        });
    }
    generate_args.push_back(input.string());

    const bool generated = run_process(generate_args);
    if (!generated) {
        cleanup();
        std::cout << "Unable to generate test video with this FFmpeg build.\n";
        return kSkip;
    }

    pixatto::VideoMetadata metadata = pixatto::probe_video_metadata(tools, input, error);
    require(error.empty(), "failed to probe generated video");
    require(metadata.width == 64, "generated video width mismatch");
    require(metadata.height == 48, "generated video height mismatch");
    require(metadata.fps > 0.0, "generated video fps missing");

    pixatto::Image frame = pixatto::decode_video_frame_rgba(tools, input, metadata, 0.2, error);
    require(error.empty(), "failed to decode exact video frame");
    require(frame.width == 64 && frame.height == 48, "decoded frame dimensions mismatch");
    require(frame.rgba.size() == 64U * 48U * 4U, "decoded frame byte size mismatch");

    pixatto::VideoPlaybackDecoder playback_decoder;
    error.clear();
    if (playback_decoder.open(input, error)) {
        require(playback_decoder.seek(0.2, error), "native playback decoder seek failed");
        pixatto::Image playback_frame;
        double playback_time = 0.0;
        require(playback_decoder.read_next_frame(playback_frame, playback_time, error), "native playback decoder frame read failed");
        require(playback_frame.width == 64 && playback_frame.height == 48, "native playback frame dimensions mismatch");
        require(playback_frame.rgba.size() == 64U * 48U * 4U, "native playback frame byte size mismatch");
    } else {
        std::cout << "Native playback decoder unavailable for generated video: " << error << '\n';
    }

    pixatto::VideoExportSettings settings;
    settings.profile = *profile;
    settings.capabilities = capabilities;
    settings.container = pixatto::video_profile_supports_container(*profile, pixatto::VideoContainer::Mp4)
        && capabilities.has_muxer(pixatto::video_container_muxer(pixatto::VideoContainer::Mp4))
        ? pixatto::VideoContainer::Mp4
        : pixatto::VideoContainer::Mkv;
    settings.metadata = metadata;
    settings.source_path = input;
    settings.destination_path = temp_dir / ("converted" + pixatto::video_container_extension(settings.container));
    settings.process_settings.pixel_size = 4;
    settings.crf = profile->crf_default;
    settings.qp = profile->qp_default;
    settings.audio_mode = pixatto::VideoAudioMode::None;

    std::shared_ptr<pixatto::VideoExportProgress> progress = std::make_shared<pixatto::VideoExportProgress>();
    pixatto::VideoExportResult exported = pixatto::export_video_exact(tools, settings, progress);
    require(exported.success, exported.error.empty() ? "video export failed" : exported.error.c_str());
#ifndef NDEBUG
    require(!exported.diagnostic_log_path.empty(), "video export diagnostic log path missing");
    require(std::filesystem::is_regular_file(exported.diagnostic_log_path), "video export diagnostic log missing");
    std::ifstream diagnostic_log(exported.diagnostic_log_path);
    std::ostringstream diagnostic_text;
    diagnostic_text << diagnostic_log.rdbuf();
    const std::string diagnostics = diagnostic_text.str();
    require(diagnostics.find("\"event\":\"start\"") != std::string::npos, "video export diagnostic start event missing");
    require(diagnostics.find("\"event\":\"end\"") != std::string::npos, "video export diagnostic end event missing");
    require(diagnostics.find("\"decoded_frames\"") != std::string::npos, "video export diagnostic counters missing");
    require(diagnostics.find("\"write_ms\"") != std::string::npos, "video export diagnostic timing missing");
#else
    require(exported.diagnostic_log_path.empty(), "release export should not write diagnostic log path");
#endif

    std::filesystem::path output = settings.destination_path;
    const std::string extension = pixatto::video_container_extension(settings.container);
    if (output.extension() != extension) {
        output.replace_extension(extension);
    }
    pixatto::VideoMetadata output_metadata = pixatto::probe_video_metadata(tools, output, error);
    require(error.empty(), "failed to probe exported video");
    const pixatto::VideoDimensions expected = pixatto::video_export_dimensions(metadata, settings.process_settings.pixel_size, *profile);
    require(output_metadata.width == expected.width, "exported video width mismatch");
    require(output_metadata.height == expected.height, "exported video height mismatch");
    require(output_metadata.fps > 0.0, "exported video fps missing");

    cleanup();
    return 0;
}

} // namespace

int main()
{
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
