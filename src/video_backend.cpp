#include "pixatto/video_backend.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace pixatto {
namespace {

struct ProcessOutput {
    std::vector<unsigned char> bytes;
    int exit_code = -1;
    std::string error;
};

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string lowercase_extension(const std::filesystem::path& path)
{
    return lowercase(path.extension().string());
}

bool has_token(const std::vector<std::string>& values, std::string_view token)
{
    return std::find(values.begin(), values.end(), token) != values.end();
}

bool executable_exists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::vector<std::filesystem::path> path_entries()
{
    std::vector<std::filesystem::path> result;
    const char* path_env = std::getenv("PATH");
    if (!path_env || *path_env == '\0') {
        return result;
    }

#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    std::string_view value(path_env);
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(separator, begin);
        std::string_view entry = value.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        if (!entry.empty()) {
            result.emplace_back(entry);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

std::filesystem::path find_executable(std::string_view name)
{
#if defined(_WIN32)
    const std::string executable_name = std::string(name) + ".exe";
    if (const char* base_path = SDL_GetBasePath(); base_path && *base_path != '\0') {
        std::filesystem::path candidate = std::filesystem::path(base_path) / executable_name;
        if (executable_exists(candidate)) {
            return candidate;
        }
    }
#else
    const std::string executable_name(name);
#endif

    for (const std::filesystem::path& entry : path_entries()) {
        std::filesystem::path candidate = entry / executable_name;
        if (executable_exists(candidate)) {
            return candidate;
        }
    }

    return {};
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

ProcessOutput run_process_capture(const std::vector<std::string>& args, bool stderr_to_stdout)
{
    ProcessOutput result;
    if (args.empty()) {
        result.error = "No process was specified.";
        return result;
    }

    std::vector<const char*> c_args = c_args_for(args);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, c_args.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    if (stderr_to_stdout) {
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    }

    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        result.error = SDL_GetError();
        if (result.error.empty()) {
            result.error = "Unable to start process.";
        }
        return result;
    }

    size_t data_size = 0;
    int exit_code = -1;
    void* data = SDL_ReadProcess(process, &data_size, &exit_code);
    if (data && data_size > 0U) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        result.bytes.assign(bytes, bytes + data_size);
    }
    SDL_free(data);
    result.exit_code = exit_code;
    SDL_DestroyProcess(process);

    if (exit_code != 0) {
        result.error.assign(result.bytes.begin(), result.bytes.end());
        if (result.error.empty()) {
            result.error = "Process failed with exit code " + std::to_string(exit_code) + ".";
        }
    }
    return result;
}

std::string bytes_to_string(const std::vector<unsigned char>& bytes)
{
    return {bytes.begin(), bytes.end()};
}

std::unordered_map<std::string, std::string> parse_key_values(std::string_view text)
{
    std::unordered_map<std::string, std::string> result;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        std::string line(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos) {
            result[line.substr(0, equals)] = line.substr(equals + 1U);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

double parse_double_or(std::string_view value, double fallback)
{
    if (value.empty() || value == "N/A") {
        return fallback;
    }
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(std::string(value), &consumed);
        if (consumed > 0U && std::isfinite(parsed)) {
            return parsed;
        }
    } catch (...) {
    }
    return fallback;
}

int parse_int_or(std::string_view value, int fallback)
{
    if (value.empty() || value == "N/A") {
        return fallback;
    }
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(std::string(value), &consumed);
        if (consumed > 0U) {
            return parsed;
        }
    } catch (...) {
    }
    return fallback;
}

long long parse_long_long_or(std::string_view value, long long fallback)
{
    if (value.empty() || value == "N/A") {
        return fallback;
    }
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(std::string(value), &consumed);
        if (consumed > 0U) {
            return parsed;
        }
    } catch (...) {
    }
    return fallback;
}

double parse_rational_or(std::string_view value, double fallback)
{
    if (value.empty() || value == "N/A" || value == "0/0") {
        return fallback;
    }
    const std::size_t slash = value.find('/');
    if (slash == std::string_view::npos) {
        return parse_double_or(value, fallback);
    }

    const double numerator = parse_double_or(value.substr(0, slash), 0.0);
    const double denominator = parse_double_or(value.substr(slash + 1U), 0.0);
    if (denominator <= 0.0) {
        return fallback;
    }
    return numerator / denominator;
}

std::string value_for(const std::unordered_map<std::string, std::string>& values, const char* key)
{
    const auto found = values.find(key);
    return found == values.end() ? std::string{} : found->second;
}

std::string format_ffmpeg_number(double value)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << value;
    std::string text = output.str();
    while (text.size() > 1U && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text.empty() ? "0" : text;
}

std::string ensure_extension(std::filesystem::path path, std::string_view extension)
{
    if (lowercase_extension(path) != extension) {
        path.replace_extension(extension);
    }
    return path.string();
}

std::string json_escape(std::string_view value)
{
    std::string output;
    output.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output += ch;
            break;
        }
    }
    return output;
}

std::string json_string(std::string_view value)
{
    return "\"" + json_escape(value) + "\"";
}

std::string json_bool(bool value)
{
    return value ? "true" : "false";
}

std::string json_string_array(const std::vector<std::string>& values)
{
    std::string output = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0U) {
            output += ',';
        }
        output += json_string(values[index]);
    }
    output += ']';
    return output;
}

std::filesystem::path video_export_diagnostic_log_path(const VideoExportSettings& settings)
{
    return std::filesystem::path(ensure_extension(settings.destination_path, settings.profile.extension) + ".pixatto-export.jsonl");
}

const char* video_backend_label(VideoExportBackend backend)
{
    switch (backend) {
    case VideoExportBackend::Software:
        return "software";
    case VideoExportBackend::NvidiaNvenc:
        return "nvenc";
    case VideoExportBackend::AmdAmf:
        return "amf";
    case VideoExportBackend::IntelQsv:
        return "qsv";
    }
    return "unknown";
}

struct VideoFrameBufferPool {
    std::mutex mutex;
    std::vector<std::vector<unsigned char>> buffers;
    std::size_t frame_size = 0;
    std::size_t max_cached = 0;
};

struct VideoExportDiagnostics {
    std::mutex mutex;
    std::ofstream output;
    std::filesystem::path path;
    std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_sample = started_at;
    std::atomic<int> decoded_frames = 0;
    std::atomic<int> queued_frames = 0;
    std::atomic<int> processing_started = 0;
    std::atomic<int> processed_frames = 0;
    std::atomic<int> written_frames = 0;
    std::atomic<int> read_calls = 0;
    std::atomic<int> write_calls = 0;
    std::atomic<int> source_buffers_reused = 0;
    std::atomic<int> source_buffers_allocated = 0;
    std::atomic<int> source_buffers_returned = 0;
    std::atomic<int> source_buffers_dropped = 0;
    std::atomic<int> decode_not_ready = 0;
    std::atomic<int> write_not_ready = 0;
    std::atomic<int> enqueue_waits = 0;
    std::atomic<int> completed_insert_waits = 0;
    std::atomic<int> writer_order_waits = 0;
    std::atomic<long long> decoded_bytes = 0;
    std::atomic<long long> written_bytes = 0;
    std::atomic<long long> read_ns = 0;
    std::atomic<long long> decode_wait_ns = 0;
    std::atomic<long long> enqueue_wait_ns = 0;
    std::atomic<long long> process_ns = 0;
    std::atomic<long long> completed_insert_wait_ns = 0;
    std::atomic<long long> writer_order_wait_ns = 0;
    std::atomic<long long> write_ns = 0;
    std::atomic<long long> write_wait_ns = 0;
};

long long elapsed_ms(const VideoExportDiagnostics& diagnostics, std::chrono::steady_clock::time_point now)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - diagnostics.started_at).count();
}

std::string diagnostic_counter_fields(const VideoExportDiagnostics& diagnostics)
{
    std::ostringstream fields;
    fields << ",\"decoded_frames\":" << diagnostics.decoded_frames.load()
           << ",\"queued_frames\":" << diagnostics.queued_frames.load()
           << ",\"processing_started\":" << diagnostics.processing_started.load()
           << ",\"processed_frames\":" << diagnostics.processed_frames.load()
           << ",\"written_frames\":" << diagnostics.written_frames.load()
           << ",\"decoded_bytes\":" << diagnostics.decoded_bytes.load()
           << ",\"written_bytes\":" << diagnostics.written_bytes.load()
           << ",\"read_calls\":" << diagnostics.read_calls.load()
           << ",\"write_calls\":" << diagnostics.write_calls.load()
           << ",\"source_buffers_reused\":" << diagnostics.source_buffers_reused.load()
           << ",\"source_buffers_allocated\":" << diagnostics.source_buffers_allocated.load()
           << ",\"source_buffers_returned\":" << diagnostics.source_buffers_returned.load()
           << ",\"source_buffers_dropped\":" << diagnostics.source_buffers_dropped.load()
           << ",\"decode_not_ready\":" << diagnostics.decode_not_ready.load()
           << ",\"write_not_ready\":" << diagnostics.write_not_ready.load()
           << ",\"enqueue_waits\":" << diagnostics.enqueue_waits.load()
           << ",\"completed_insert_waits\":" << diagnostics.completed_insert_waits.load()
           << ",\"writer_order_waits\":" << diagnostics.writer_order_waits.load()
           << ",\"read_ms\":" << diagnostics.read_ns.load() / 1'000'000.0
           << ",\"decode_wait_ms\":" << diagnostics.decode_wait_ns.load() / 1'000'000.0
           << ",\"enqueue_wait_ms\":" << diagnostics.enqueue_wait_ns.load() / 1'000'000.0
           << ",\"process_ms\":" << diagnostics.process_ns.load() / 1'000'000.0
           << ",\"completed_insert_wait_ms\":" << diagnostics.completed_insert_wait_ns.load() / 1'000'000.0
           << ",\"writer_order_wait_ms\":" << diagnostics.writer_order_wait_ns.load() / 1'000'000.0
           << ",\"write_ms\":" << diagnostics.write_ns.load() / 1'000'000.0
           << ",\"write_wait_ms\":" << diagnostics.write_wait_ns.load() / 1'000'000.0;
    return fields.str();
}

std::shared_ptr<VideoExportDiagnostics> open_video_export_diagnostics(const std::filesystem::path& path)
{
    auto diagnostics = std::make_shared<VideoExportDiagnostics>();
    diagnostics->path = path;
    diagnostics->output.open(path, std::ios::trunc);
    if (!diagnostics->output) {
        return {};
    }
    return diagnostics;
}

std::vector<unsigned char> acquire_frame_buffer(
    const std::shared_ptr<VideoFrameBufferPool>& pool,
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics)
{
    if (pool) {
        std::lock_guard lock(pool->mutex);
        if (!pool->buffers.empty()) {
            std::vector<unsigned char> buffer = std::move(pool->buffers.back());
            pool->buffers.pop_back();
            buffer.resize(pool->frame_size);
            if (diagnostics) {
                diagnostics->source_buffers_reused.fetch_add(1);
            }
            return buffer;
        }
    }

    if (diagnostics) {
        diagnostics->source_buffers_allocated.fetch_add(1);
    }
    return pool ? std::vector<unsigned char>(pool->frame_size) : std::vector<unsigned char>{};
}

void release_frame_buffer(
    const std::shared_ptr<VideoFrameBufferPool>& pool,
    std::vector<unsigned char> buffer,
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics)
{
    if (!pool || buffer.capacity() < pool->frame_size) {
        if (diagnostics) {
            diagnostics->source_buffers_dropped.fetch_add(1);
        }
        return;
    }

    buffer.clear();
    std::lock_guard lock(pool->mutex);
    if (pool->buffers.size() >= pool->max_cached) {
        if (diagnostics) {
            diagnostics->source_buffers_dropped.fetch_add(1);
        }
        return;
    }
    pool->buffers.push_back(std::move(buffer));
    if (diagnostics) {
        diagnostics->source_buffers_returned.fetch_add(1);
    }
}

void write_diagnostic_record(
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics,
    std::string_view event,
    std::string_view extra_fields = {},
    bool include_counters = true)
{
    if (!diagnostics || !diagnostics->output) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(diagnostics->mutex);
    diagnostics->output << "{\"event\":" << json_string(event)
                        << ",\"elapsed_ms\":" << elapsed_ms(*diagnostics, now);
    if (include_counters) {
        diagnostics->output << diagnostic_counter_fields(*diagnostics);
    }
    diagnostics->output << extra_fields << "}\n";
    diagnostics->output.flush();
}

void maybe_write_diagnostic_sample(const std::shared_ptr<VideoExportDiagnostics>& diagnostics)
{
    if (!diagnostics || !diagnostics->output) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(diagnostics->mutex);
    if (now - diagnostics->last_sample < std::chrono::seconds(1)) {
        return;
    }
    diagnostics->last_sample = now;
    diagnostics->output << "{\"event\":\"sample\",\"elapsed_ms\":" << elapsed_ms(*diagnostics, now)
                        << diagnostic_counter_fields(*diagnostics) << "}\n";
    diagnostics->output.flush();
}

std::string nvenc_preset(VideoHardwareSpeed speed)
{
    switch (speed) {
    case VideoHardwareSpeed::Balanced:
        return "p4";
    case VideoHardwareSpeed::Fast:
        return "p2";
    case VideoHardwareSpeed::VeryFast:
        return "p1";
    }
    return "p4";
}

std::string amf_quality(VideoHardwareSpeed speed)
{
    switch (speed) {
    case VideoHardwareSpeed::Balanced:
        return "balanced";
    case VideoHardwareSpeed::Fast:
    case VideoHardwareSpeed::VeryFast:
        return "speed";
    }
    return "balanced";
}

std::string qsv_preset(VideoHardwareSpeed speed)
{
    switch (speed) {
    case VideoHardwareSpeed::Balanced:
        return "medium";
    case VideoHardwareSpeed::Fast:
        return "fast";
    case VideoHardwareSpeed::VeryFast:
        return "veryfast";
    }
    return "medium";
}

void append_bitrate_args(std::vector<std::string>& args, int bitrate_mbps)
{
    const int bitrate = std::clamp(bitrate_mbps, 1, 500);
    args.push_back("-b:v");
    args.push_back(std::to_string(bitrate) + "M");
    args.push_back("-maxrate");
    args.push_back(std::to_string(std::max(1, bitrate * 2)) + "M");
    args.push_back("-bufsize");
    args.push_back(std::to_string(std::max(1, bitrate * 4)) + "M");
}

void append_profile_video_args(std::vector<std::string>& args, const VideoExportSettings& settings)
{
    const VideoExportProfile& profile = settings.profile;
    args.push_back("-c:v");
    args.push_back(profile.encoder);

    if (profile.backend == VideoExportBackend::NvidiaNvenc) {
        args.push_back("-preset");
        args.push_back(nvenc_preset(settings.hardware_speed));
        append_bitrate_args(args, settings.bitrate_mbps);
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.backend == VideoExportBackend::AmdAmf) {
        args.push_back("-quality");
        args.push_back(amf_quality(settings.hardware_speed));
        append_bitrate_args(args, settings.bitrate_mbps);
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.backend == VideoExportBackend::IntelQsv) {
        args.push_back("-preset");
        args.push_back(qsv_preset(settings.hardware_speed));
        append_bitrate_args(args, settings.bitrate_mbps);
        args.push_back("-pix_fmt");
        args.push_back("nv12");
    } else if (profile.id == "mp4_h264") {
        args.push_back("-preset");
        args.push_back("medium");
        args.push_back("-crf");
        args.push_back(std::to_string(std::clamp(settings.crf, 0, 51)));
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.id == "mp4_h265") {
        args.push_back("-preset");
        args.push_back("medium");
        args.push_back("-crf");
        args.push_back(std::to_string(std::clamp(settings.crf, 0, 51)));
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
        args.push_back("-tag:v");
        args.push_back("hvc1");
    } else if (profile.id == "webm_vp9") {
        args.push_back("-crf");
        args.push_back(std::to_string(std::clamp(settings.crf, 0, 63)));
        args.push_back("-b:v");
        args.push_back("0");
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.id == "mkv_av1_svt") {
        args.push_back("-preset");
        args.push_back("6");
        args.push_back("-crf");
        args.push_back(std::to_string(std::clamp(settings.crf, 0, 63)));
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.id == "mkv_av1_aom") {
        args.push_back("-cpu-used");
        args.push_back("4");
        args.push_back("-crf");
        args.push_back(std::to_string(std::clamp(settings.crf, 0, 63)));
        args.push_back("-b:v");
        args.push_back("0");
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
    } else if (profile.id == "mkv_ffv1") {
        args.push_back("-level");
        args.push_back("3");
        args.push_back("-g");
        args.push_back("1");
        args.push_back("-pix_fmt");
        args.push_back("bgra");
    }
}

void append_movflags_if_needed(std::vector<std::string>& args, const VideoExportProfile& profile)
{
    if (profile.container == VideoContainer::Mp4) {
        args.push_back("-movflags");
        args.push_back("+faststart");
    }
}

std::vector<std::string> split_names(std::string value)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t comma = value.find(',', begin);
        std::string item = value.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin);
        if (!item.empty()) {
            result.push_back(std::move(item));
        }
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1U;
    }
    return result;
}

void parse_ffmpeg_list(std::string_view text, bool encoders, std::vector<std::string>& output)
{
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        std::string line(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
        std::istringstream input(line);
        std::string flags;
        std::string name;
        input >> flags >> name;
        if (!flags.empty() && !name.empty()) {
            const bool matches = encoders
                ? flags.find('V') != std::string::npos
                : flags.find('E') != std::string::npos;
            if (matches) {
                for (std::string split : split_names(name)) {
                    if (!has_token(output, split)) {
                        output.push_back(std::move(split));
                    }
                }
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
}

std::vector<std::string> known_hardware_encoders()
{
    return {
        "h264_nvenc",
        "hevc_nvenc",
        "av1_nvenc",
        "h264_amf",
        "hevc_amf",
        "av1_amf",
        "h264_qsv",
        "hevc_qsv",
        "av1_qsv",
    };
}

std::vector<std::string> hardware_encoder_probe_args(const VideoToolchain& tools, std::string_view encoder)
{
    std::vector<std::string> args = {
        tools.ffmpeg_path.string(),
        "-y",
        "-hide_banner",
        "-v",
        "error",
        "-f",
        "lavfi",
        "-i",
        "color=c=black:size=64x64:rate=1:duration=1",
        "-frames:v",
        "1",
        "-an",
        "-vf",
        "format=nv12",
        "-c:v",
        std::string(encoder),
    };

    if (encoder.ends_with("_nvenc")) {
        args.push_back("-preset");
        args.push_back("p1");
    } else if (encoder.ends_with("_amf")) {
        args.push_back("-quality");
        args.push_back("speed");
    } else if (encoder.ends_with("_qsv")) {
        args.push_back("-preset");
        args.push_back("veryfast");
    }

    args.push_back("-f");
    args.push_back("null");
    args.push_back("-");
    return args;
}

std::vector<std::string> probe_hardware_encoders_impl(const VideoToolchain& tools, const VideoCapabilities& capabilities)
{
    std::vector<std::string> available;
    for (const std::string& encoder : known_hardware_encoders()) {
        if (!capabilities.has_encoder(encoder)) {
            continue;
        }
        ProcessOutput probe = run_process_capture(hardware_encoder_probe_args(tools, encoder), true);
        if (probe.exit_code == 0) {
            available.push_back(encoder);
        }
    }
    std::sort(available.begin(), available.end());
    return available;
}

Image process_frame_for_export(
    std::vector<unsigned char>& frame,
    const VideoMetadata& metadata,
    const ProcessSettings& settings)
{
    Image image;
    image.width = metadata.width;
    image.height = metadata.height;
    image.rgba = std::move(frame);

    Image processed = process_image_collapsed(image, settings);
    frame = std::move(image.rgba);
    return processed;
}

struct VideoFrameJob {
    int index = 0;
    std::vector<unsigned char> rgba;
};

struct VideoFramePipeline {
    std::mutex mutex;
    std::condition_variable input_cv;
    std::condition_variable output_cv;
    std::deque<VideoFrameJob> pending;
    std::map<int, Image> completed;
    std::string error;
    std::size_t max_pending = 4;
    std::size_t max_completed = 4;
    std::size_t active_workers = 0;
    int next_write_frame = 0;
    bool input_closed = false;
    bool stop = false;
};

struct VideoFrameWriterResult {
    int frames_written = 0;
    std::string error;
};

unsigned int video_export_worker_count()
{
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads == 0U) {
        return 4U;
    }
    return std::clamp(hardware_threads > 2U ? hardware_threads - 2U : hardware_threads, 2U, 32U);
}

std::size_t video_export_queue_limit(std::size_t frame_bytes, unsigned int workers)
{
    constexpr std::size_t budget_bytes = 512ULL * 1024ULL * 1024ULL;
    const std::size_t desired = std::max<std::size_t>(4U, static_cast<std::size_t>(workers) * 2U);
    const std::size_t memory_limited = frame_bytes == 0U
        ? desired
        : std::max<std::size_t>(2U, budget_bytes / frame_bytes);
    return std::max<std::size_t>(2U, std::min(desired, memory_limited));
}

std::size_t video_export_source_pool_limit(std::size_t frame_bytes, unsigned int workers)
{
    constexpr std::size_t budget_bytes = 256ULL * 1024ULL * 1024ULL;
    if (frame_bytes == 0U) {
        return 0U;
    }
    const std::size_t desired = std::max<std::size_t>(4U, static_cast<std::size_t>(workers) + 2U);
    const std::size_t memory_limited = std::max<std::size_t>(2U, budget_bytes / frame_bytes);
    return std::max<std::size_t>(2U, std::min(desired, memory_limited));
}

void set_pipeline_error(VideoFramePipeline& pipeline, std::string error)
{
    std::lock_guard lock(pipeline.mutex);
    if (pipeline.error.empty()) {
        pipeline.error = std::move(error);
    }
    pipeline.stop = true;
    pipeline.input_cv.notify_all();
    pipeline.output_cv.notify_all();
}

bool pipeline_error(VideoFramePipeline& pipeline, std::string& error)
{
    std::lock_guard lock(pipeline.mutex);
    if (pipeline.error.empty()) {
        return false;
    }
    error = pipeline.error;
    return true;
}

bool write_all(
    SDL_IOStream* stream,
    const std::vector<unsigned char>& bytes,
    const std::shared_ptr<VideoExportProgress>& progress,
    std::string& error,
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics = {});

bool enqueue_video_frame(
    VideoFramePipeline& pipeline,
    VideoFrameJob job,
    const std::shared_ptr<VideoExportProgress>& progress,
    std::string& error,
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics)
{
    using namespace std::chrono_literals;
    std::unique_lock lock(pipeline.mutex);
    while (!pipeline.stop && pipeline.pending.size() >= pipeline.max_pending && !(progress && progress->cancel_requested)) {
        const auto wait_started = std::chrono::steady_clock::now();
        pipeline.input_cv.wait_for(lock, 10ms);
        if (diagnostics) {
            diagnostics->enqueue_waits.fetch_add(1);
            diagnostics->enqueue_wait_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_started).count());
        }
    }
    if (progress && progress->cancel_requested) {
        if (pipeline.error.empty()) {
            pipeline.error = "Video export canceled.";
        }
        pipeline.stop = true;
        error = pipeline.error;
        pipeline.input_cv.notify_all();
        pipeline.output_cv.notify_all();
        return false;
    }
    if (pipeline.stop || !pipeline.error.empty()) {
        error = pipeline.error.empty() ? "Video export canceled." : pipeline.error;
        return false;
    }

    pipeline.pending.push_back(std::move(job));
    if (diagnostics) {
        diagnostics->queued_frames.fetch_add(1);
    }
    pipeline.input_cv.notify_one();
    return true;
}

void close_pipeline_input(VideoFramePipeline& pipeline)
{
    std::lock_guard lock(pipeline.mutex);
    pipeline.input_closed = true;
    pipeline.input_cv.notify_all();
    pipeline.output_cv.notify_all();
}

struct VideoFrameWorkerGuard {
    VideoFramePipeline& pipeline;

    ~VideoFrameWorkerGuard()
    {
        std::lock_guard lock(pipeline.mutex);
        if (pipeline.active_workers > 0U) {
            --pipeline.active_workers;
        }
        pipeline.input_cv.notify_all();
        pipeline.output_cv.notify_all();
    }
};

void video_frame_worker(
    VideoFramePipeline& pipeline,
    VideoMetadata metadata,
    ProcessSettings settings,
    int output_width,
    int output_height,
    std::size_t output_frame_bytes,
    std::shared_ptr<VideoFrameBufferPool> source_pool,
    std::shared_ptr<VideoExportDiagnostics> diagnostics)
{
    VideoFrameWorkerGuard worker_guard{pipeline};

    while (true) {
        VideoFrameJob job;
        {
            std::unique_lock lock(pipeline.mutex);
            pipeline.input_cv.wait(lock, [&pipeline]() {
                return pipeline.stop || !pipeline.pending.empty() || pipeline.input_closed;
            });
            if (pipeline.stop || (pipeline.pending.empty() && pipeline.input_closed)) {
                return;
            }
            job = std::move(pipeline.pending.front());
            pipeline.pending.pop_front();
            pipeline.input_cv.notify_all();
        }

        if (diagnostics) {
            diagnostics->processing_started.fetch_add(1);
        }
        const auto process_started = std::chrono::steady_clock::now();
        Image processed = process_frame_for_export(job.rgba, metadata, settings);
        release_frame_buffer(source_pool, std::move(job.rgba), diagnostics);
        if (diagnostics) {
            diagnostics->process_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - process_started).count());
        }
        if (processed.width != output_width || processed.height != output_height
            || processed.rgba.size() != output_frame_bytes) {
            set_pipeline_error(pipeline, "Processed video frame dimensions did not match the export stream.");
            return;
        }

        {
            std::unique_lock lock(pipeline.mutex);
            while (!pipeline.stop
                   && pipeline.completed.size() >= pipeline.max_completed
                   && job.index != pipeline.next_write_frame) {
                const auto wait_started = std::chrono::steady_clock::now();
                pipeline.output_cv.wait_for(lock, std::chrono::milliseconds(10));
                if (diagnostics) {
                    diagnostics->completed_insert_waits.fetch_add(1);
                    diagnostics->completed_insert_wait_ns.fetch_add(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_started).count());
                }
            }
            if (pipeline.stop) {
                return;
            }
            pipeline.completed.emplace(job.index, std::move(processed));
            if (diagnostics) {
                diagnostics->processed_frames.fetch_add(1);
            }
            pipeline.output_cv.notify_all();
        }
    }
}

void video_frame_writer(
    VideoFramePipeline& pipeline,
    SDL_IOStream* encoder_input,
    const std::shared_ptr<VideoExportProgress>& progress,
    int total_frames,
    VideoFrameWriterResult& result,
    std::shared_ptr<VideoExportDiagnostics> diagnostics)
{
    using namespace std::chrono_literals;

    while (true) {
        Image frame;
        {
            std::unique_lock lock(pipeline.mutex);
            while (true) {
                if (progress && progress->cancel_requested) {
                    if (pipeline.error.empty()) {
                        pipeline.error = "Video export canceled.";
                    }
                    pipeline.stop = true;
                    result.error = pipeline.error;
                    pipeline.input_cv.notify_all();
                    pipeline.output_cv.notify_all();
                    return;
                }
                if (!pipeline.error.empty()) {
                    pipeline.stop = true;
                    result.error = pipeline.error;
                    pipeline.input_cv.notify_all();
                    pipeline.output_cv.notify_all();
                    return;
                }

                auto found = pipeline.completed.find(pipeline.next_write_frame);
                if (found != pipeline.completed.end()) {
                    frame = std::move(found->second);
                    pipeline.completed.erase(found);
                    ++pipeline.next_write_frame;
                    pipeline.output_cv.notify_all();
                    break;
                }

                if (pipeline.input_closed && pipeline.active_workers == 0U) {
                    if (!pipeline.completed.empty()) {
                        pipeline.error = "Processed video frames finished out of order.";
                        pipeline.stop = true;
                        result.error = pipeline.error;
                    }
                    pipeline.input_cv.notify_all();
                    pipeline.output_cv.notify_all();
                    return;
                }

                const auto wait_started = std::chrono::steady_clock::now();
                pipeline.output_cv.wait_for(lock, 10ms);
                if (diagnostics) {
                    diagnostics->writer_order_waits.fetch_add(1);
                    diagnostics->writer_order_wait_ns.fetch_add(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_started).count());
                }
            }
        }

        std::string write_error;
        if (!write_all(encoder_input, frame.rgba, progress, write_error, diagnostics)) {
            set_pipeline_error(pipeline, write_error);
            result.error = write_error;
            return;
        }

        ++result.frames_written;
        if (diagnostics) {
            diagnostics->written_frames.store(result.frames_written);
            maybe_write_diagnostic_sample(diagnostics);
        }
        if (progress) {
            progress->frames_done = result.frames_written;
            if (total_frames > 0) {
                progress->percent = std::clamp((result.frames_written * 95) / total_frames, 0, 95);
            }
        }
    }
}

void join_video_threads(std::vector<std::thread>& workers, std::thread& writer)
{
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    if (writer.joinable()) {
        writer.join();
    }
}

bool write_all(
    SDL_IOStream* stream,
    const std::vector<unsigned char>& bytes,
    const std::shared_ptr<VideoExportProgress>& progress,
    std::string& error,
    const std::shared_ptr<VideoExportDiagnostics>& diagnostics)
{
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (progress && progress->cancel_requested) {
            error = "Video export canceled.";
            return false;
        }

        const auto write_started = std::chrono::steady_clock::now();
        const size_t written = SDL_WriteIO(stream, bytes.data() + offset, bytes.size() - offset);
        if (diagnostics) {
            diagnostics->write_calls.fetch_add(1);
            diagnostics->write_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - write_started).count());
        }
        if (written > 0U) {
            offset += written;
            if (diagnostics) {
                diagnostics->written_bytes.fetch_add(static_cast<long long>(written));
            }
            continue;
        }

        const SDL_IOStatus status = SDL_GetIOStatus(stream);
        if (status == SDL_IO_STATUS_NOT_READY) {
            if (diagnostics) {
                diagnostics->write_not_ready.fetch_add(1);
            }
            const auto wait_started = std::chrono::steady_clock::now();
            SDL_Delay(2);
            if (diagnostics) {
                diagnostics->write_wait_ns.fetch_add(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_started).count());
            }
            continue;
        }

        error = SDL_GetError();
        if (error.empty()) {
            error = "Unable to write processed frame to FFmpeg.";
        }
        return false;
    }
    return true;
}

SDL_Process* start_process_with_pipes(
    const std::vector<std::string>& args,
    SDL_ProcessIO stdin_option,
    SDL_ProcessIO stdout_option,
    SDL_ProcessIO stderr_option,
    bool stderr_to_stdout,
    std::string& error)
{
    std::vector<const char*> c_args = c_args_for(args);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, c_args.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, stdin_option);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, stdout_option);
    if (stderr_to_stdout) {
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, stderr_option);
    }

    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        error = SDL_GetError();
        if (error.empty()) {
            error = "Unable to start process.";
        }
    }
    return process;
}

} // namespace

bool VideoCapabilities::has_encoder(std::string_view encoder) const
{
    return has_token(encoders, encoder);
}

bool VideoCapabilities::has_hardware_encoder(std::string_view encoder) const
{
    return has_token(hardware_encoders, encoder);
}

bool VideoCapabilities::has_muxer(std::string_view muxer) const
{
    return has_token(muxers, muxer);
}

bool is_importable_video_path(const std::filesystem::path& path)
{
    const std::string extension = lowercase_extension(path);
    static constexpr std::array<std::string_view, 9> extensions = {
        ".mp4",
        ".mov",
        ".mkv",
        ".webm",
        ".avi",
        ".m4v",
        ".mpg",
        ".mpeg",
        ".ogv",
    };
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

VideoToolchain find_video_toolchain()
{
    VideoToolchain result;
    result.ffmpeg_path = find_executable("ffmpeg");
    result.ffprobe_path = find_executable("ffprobe");
    if (result.ffmpeg_path.empty() || result.ffprobe_path.empty()) {
#if defined(_WIN32)
        result.error = "FFmpeg was not found. Put ffmpeg.exe and ffprobe.exe next to pixatto.exe, or add them to PATH.";
#elif defined(__APPLE__)
        result.error = "FFmpeg was not found. Install ffmpeg and ffprobe, for example with Homebrew, and make sure they are on PATH.";
#else
        result.error = "FFmpeg was not found. Install your distribution's ffmpeg package and make sure ffmpeg and ffprobe are on PATH.";
#endif
        return result;
    }

    ProcessOutput version = run_process_capture({result.ffmpeg_path.string(), "-version"}, true);
    if (version.exit_code != 0) {
        result.error = version.error.empty() ? "Unable to run ffmpeg." : version.error;
        return result;
    }

    const std::string version_text = bytes_to_string(version.bytes);
    const std::size_t end = version_text.find('\n');
    result.version = version_text.substr(0, end == std::string::npos ? version_text.size() : end);
    return result;
}

VideoCapabilities parse_ffmpeg_capabilities(std::string_view encoders, std::string_view muxers)
{
    VideoCapabilities result;
    parse_ffmpeg_list(encoders, true, result.encoders);
    parse_ffmpeg_list(muxers, false, result.muxers);
    std::sort(result.encoders.begin(), result.encoders.end());
    std::sort(result.muxers.begin(), result.muxers.end());
    return result;
}

VideoCapabilities probe_video_capabilities(const VideoToolchain& tools, std::string& error)
{
    error.clear();
    if (!tools.available()) {
        error = tools.error.empty() ? "FFmpeg is not available." : tools.error;
        return {};
    }

    ProcessOutput encoders = run_process_capture({tools.ffmpeg_path.string(), "-hide_banner", "-encoders"}, true);
    if (encoders.exit_code != 0) {
        error = encoders.error.empty() ? "Unable to list FFmpeg encoders." : encoders.error;
        return {};
    }

    ProcessOutput muxers = run_process_capture({tools.ffmpeg_path.string(), "-hide_banner", "-muxers"}, true);
    if (muxers.exit_code != 0) {
        error = muxers.error.empty() ? "Unable to list FFmpeg muxers." : muxers.error;
        return {};
    }

    return parse_ffmpeg_capabilities(bytes_to_string(encoders.bytes), bytes_to_string(muxers.bytes));
}

std::vector<std::string> probe_video_hardware_encoders(
    const VideoToolchain& tools,
    const VideoCapabilities& capabilities)
{
    if (!tools.available()) {
        return {};
    }
    return probe_hardware_encoders_impl(tools, capabilities);
}

std::vector<VideoExportProfile> build_video_export_profiles(const VideoCapabilities& capabilities)
{
    std::vector<VideoExportProfile> profiles;
    auto add_software = [&](VideoExportProfile profile) {
        if (capabilities.has_encoder(profile.encoder) && capabilities.has_muxer(profile.muxer)) {
            profiles.push_back(std::move(profile));
        }
    };
    auto add_hardware = [&](VideoExportProfile profile) {
        if (capabilities.has_hardware_encoder(profile.encoder) && capabilities.has_muxer(profile.muxer)) {
            profiles.push_back(std::move(profile));
        }
    };

    add_software({"mp4_h264", "MP4 / H.264", "libx264", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::Software, 16, 24, false, true});
    add_software({"mp4_h265", "MP4 / H.265", "libx265", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::Software, 18, 24, false, true});
    add_software({"webm_vp9", "WebM / VP9", "libvpx-vp9", "webm", ".webm", VideoContainer::Webm, VideoExportBackend::Software, 20, 18, false, true});
    if (capabilities.has_encoder("libsvtav1") && capabilities.has_muxer("matroska")) {
        profiles.push_back({"mkv_av1_svt", "MKV / AV1", "libsvtav1", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::Software, 24, 18, false, true});
    } else {
        add_software({"mkv_av1_aom", "MKV / AV1", "libaom-av1", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::Software, 24, 18, false, true});
    }
    add_software({"mkv_ffv1", "MKV / FFV1 Lossless", "ffv1", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::Software, 0, 0, true, false});

    add_hardware({"mp4_h264_nvenc", "MP4 / H.264 NVENC", "h264_nvenc", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::NvidiaNvenc, 0, 28, false, true});
    add_hardware({"mp4_h265_nvenc", "MP4 / H.265 NVENC", "hevc_nvenc", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::NvidiaNvenc, 0, 28, false, true});
    add_hardware({"mkv_av1_nvenc", "MKV / AV1 NVENC", "av1_nvenc", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::NvidiaNvenc, 0, 24, false, true});

    add_hardware({"mp4_h264_amf", "MP4 / H.264 AMF", "h264_amf", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::AmdAmf, 0, 28, false, true});
    add_hardware({"mp4_h265_amf", "MP4 / H.265 AMF", "hevc_amf", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::AmdAmf, 0, 28, false, true});
    add_hardware({"mkv_av1_amf", "MKV / AV1 AMF", "av1_amf", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::AmdAmf, 0, 24, false, true});

    add_hardware({"mp4_h264_qsv", "MP4 / H.264 Intel QSV", "h264_qsv", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::IntelQsv, 0, 28, false, true});
    add_hardware({"mp4_h265_qsv", "MP4 / H.265 Intel QSV", "hevc_qsv", "mp4", ".mp4", VideoContainer::Mp4, VideoExportBackend::IntelQsv, 0, 28, false, true});
    add_hardware({"mkv_av1_qsv", "MKV / AV1 Intel QSV", "av1_qsv", "matroska", ".mkv", VideoContainer::Mkv, VideoExportBackend::IntelQsv, 0, 24, false, true});

    return profiles;
}

VideoMetadata parse_video_metadata(std::string_view video_output, std::string_view audio_output)
{
    const auto video = parse_key_values(video_output);
    const auto audio = parse_key_values(audio_output);

    VideoMetadata metadata;
    metadata.width = parse_int_or(value_for(video, "width"), 0);
    metadata.height = parse_int_or(value_for(video, "height"), 0);
    metadata.duration_seconds = parse_double_or(value_for(video, "duration"), 0.0);
    metadata.fps = parse_rational_or(value_for(video, "avg_frame_rate"), 0.0);
    const double nominal_fps = parse_rational_or(value_for(video, "r_frame_rate"), metadata.fps);
    metadata.variable_frame_rate = metadata.fps > 0.0 && nominal_fps > 0.0 && std::abs(metadata.fps - nominal_fps) > 0.01;
    metadata.frame_count = parse_long_long_or(value_for(video, "nb_frames"), 0);
    if (metadata.frame_count <= 0 && metadata.duration_seconds > 0.0 && metadata.fps > 0.0) {
        metadata.frame_count = static_cast<long long>(std::ceil(metadata.duration_seconds * metadata.fps));
    }
    metadata.video_codec = value_for(video, "codec_name");
    metadata.audio_codec = value_for(audio, "codec_name");
    metadata.has_audio = !metadata.audio_codec.empty() && metadata.audio_codec != "N/A";
    return metadata;
}

VideoMetadata probe_video_metadata(const VideoToolchain& tools, const std::filesystem::path& path, std::string& error)
{
    error.clear();
    if (!tools.available()) {
        error = tools.error.empty() ? "FFmpeg is not available." : tools.error;
        return {};
    }

    const std::vector<std::string> video_args = {
        tools.ffprobe_path.string(),
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=codec_name,width,height,avg_frame_rate,r_frame_rate,duration,nb_frames",
        "-show_entries",
        "format=duration",
        "-of",
        "default=noprint_wrappers=1:nokey=0",
        path.string(),
    };
    ProcessOutput video = run_process_capture(video_args, true);
    if (video.exit_code != 0) {
        error = video.error.empty() ? "Unable to read video metadata." : video.error;
        return {};
    }

    const std::vector<std::string> audio_args = {
        tools.ffprobe_path.string(),
        "-v",
        "error",
        "-select_streams",
        "a:0",
        "-show_entries",
        "stream=codec_name",
        "-of",
        "default=noprint_wrappers=1:nokey=0",
        path.string(),
    };
    ProcessOutput audio = run_process_capture(audio_args, true);
    VideoMetadata metadata = parse_video_metadata(bytes_to_string(video.bytes), audio.exit_code == 0 ? bytes_to_string(audio.bytes) : std::string{});
    if (metadata.width <= 0 || metadata.height <= 0 || metadata.fps <= 0.0) {
        error = "The selected file does not contain a readable video stream.";
    }
    return metadata;
}

Image decode_video_frame_rgba(
    const VideoToolchain& tools,
    const std::filesystem::path& path,
    const VideoMetadata& metadata,
    double seconds,
    std::string& error)
{
    error.clear();
    if (!tools.available()) {
        error = tools.error.empty() ? "FFmpeg is not available." : tools.error;
        return {};
    }
    if (metadata.width <= 0 || metadata.height <= 0) {
        error = "Video dimensions are unavailable.";
        return {};
    }

    seconds = std::clamp(seconds, 0.0, std::max(0.0, metadata.duration_seconds));
    const std::vector<std::string> args = {
        tools.ffmpeg_path.string(),
        "-v",
        "error",
        "-ss",
        format_ffmpeg_number(seconds),
        "-i",
        path.string(),
        "-map",
        "0:v:0",
        "-frames:v",
        "1",
        "-an",
        "-sn",
        "-dn",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgba",
        "pipe:1",
    };

    ProcessOutput output = run_process_capture(args, false);
    if (output.exit_code != 0) {
        error = output.error.empty() ? "Unable to decode video frame." : output.error;
        return {};
    }

    const std::size_t expected = static_cast<std::size_t>(metadata.width) * static_cast<std::size_t>(metadata.height) * 4U;
    if (output.bytes.size() < expected) {
        error = "Decoded frame was shorter than expected.";
        return {};
    }
    output.bytes.resize(expected);
    return {metadata.width, metadata.height, std::move(output.bytes)};
}

VideoDimensions video_export_dimensions(const VideoMetadata& metadata, int pixel_size, const VideoExportProfile& profile)
{
    const int block = std::clamp(pixel_size, 1, 256);
    VideoDimensions dimensions;
    dimensions.width = metadata.width > 0 ? (metadata.width - 1) / block + 1 : 0;
    dimensions.height = metadata.height > 0 ? (metadata.height - 1) / block + 1 : 0;
    if (profile.needs_even_dimensions) {
        const int padded_width = dimensions.width + (dimensions.width & 1);
        const int padded_height = dimensions.height + (dimensions.height & 1);
        dimensions.padded = padded_width != dimensions.width || padded_height != dimensions.height;
        dimensions.width = padded_width;
        dimensions.height = padded_height;
    }
    return dimensions;
}

bool can_copy_audio_to_container(std::string_view audio_codec, VideoContainer container)
{
    if (audio_codec.empty()) {
        return false;
    }
    const std::string codec = lowercase(std::string(audio_codec));
    if (container == VideoContainer::Mkv) {
        return true;
    }
    if (container == VideoContainer::Webm) {
        return codec == "opus" || codec == "vorbis";
    }
    return codec == "aac" || codec == "mp3" || codec == "alac" || codec == "ac3" || codec == "eac3";
}

std::vector<std::string> build_video_encode_command(
    const VideoToolchain& tools,
    const VideoExportSettings& settings,
    int input_width,
    int input_height,
    bool copy_audio)
{
    std::vector<std::string> args = {
        tools.ffmpeg_path.string(),
        "-y",
        "-v",
        "error",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgba",
        "-s",
        std::to_string(input_width) + "x" + std::to_string(input_height),
        "-framerate",
        format_ffmpeg_number(settings.metadata.fps),
        "-i",
        "pipe:0",
    };

    const bool audio_copy_enabled = copy_audio && settings.metadata.has_audio
        && can_copy_audio_to_container(settings.metadata.audio_codec, settings.profile.container);
    if (audio_copy_enabled) {
        args.push_back("-i");
        args.push_back(settings.source_path.string());
    }

    args.push_back("-map");
    args.push_back("0:v:0");
    if (audio_copy_enabled) {
        args.push_back("-map");
        args.push_back("1:a:0");
    }

    append_profile_video_args(args, settings);
    if (settings.profile.needs_even_dimensions) {
        args.push_back("-vf");
        args.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
    }

    if (audio_copy_enabled) {
        args.push_back("-c:a");
        args.push_back("copy");
        args.push_back("-shortest");
    } else {
        args.push_back("-an");
    }

    append_movflags_if_needed(args, settings.profile);
    args.push_back(ensure_extension(settings.destination_path, settings.profile.extension));
    return args;
}

VideoExportResult export_video_exact(
    const VideoToolchain& tools,
    const VideoExportSettings& settings,
    std::shared_ptr<VideoExportProgress> progress)
{
    VideoExportResult result;
    if (!progress) {
        progress = std::make_shared<VideoExportProgress>();
    }
    if (!tools.available()) {
        result.error = tools.error.empty() ? "FFmpeg is not available." : tools.error;
        return result;
    }
    if (settings.metadata.width <= 0 || settings.metadata.height <= 0 || settings.metadata.fps <= 0.0) {
        result.error = "Video metadata is incomplete.";
        return result;
    }

    const int block = std::clamp(settings.process_settings.pixel_size, 1, 256);
    const int output_width = (settings.metadata.width - 1) / block + 1;
    const int output_height = (settings.metadata.height - 1) / block + 1;
    const std::size_t source_frame_bytes = static_cast<std::size_t>(settings.metadata.width)
        * static_cast<std::size_t>(settings.metadata.height) * 4U;
    const std::size_t output_frame_bytes = static_cast<std::size_t>(output_width)
        * static_cast<std::size_t>(output_height) * 4U;
    if (source_frame_bytes == 0U || output_frame_bytes == 0U
        || source_frame_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) * 128U
        || output_frame_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) * 128U) {
        result.error = "Video dimensions are too large.";
        return result;
    }

    const int total_frames = settings.metadata.frame_count > 0
        ? static_cast<int>(std::min<long long>(settings.metadata.frame_count, std::numeric_limits<int>::max()))
        : 0;
    progress->frames_total = total_frames;
    progress->frames_done = 0;
    progress->percent = 0;
    progress->encoding = false;

    result.diagnostic_log_path = video_export_diagnostic_log_path(settings);
    const auto diagnostics = open_video_export_diagnostics(result.diagnostic_log_path);
    const unsigned int worker_count = video_export_worker_count();
    const std::size_t pending_queue_limit = video_export_queue_limit(source_frame_bytes, worker_count);
    const std::size_t completed_queue_limit = video_export_queue_limit(output_frame_bytes, worker_count);
    const std::size_t source_pool_limit = video_export_source_pool_limit(source_frame_bytes, worker_count);
    auto source_pool = std::make_shared<VideoFrameBufferPool>();
    source_pool->frame_size = source_frame_bytes;
    source_pool->max_cached = source_pool_limit;

    const bool copy_audio = settings.copy_audio && settings.metadata.has_audio
        && can_copy_audio_to_container(settings.metadata.audio_codec, settings.profile.container);
    std::vector<std::string> encode_args = build_video_encode_command(tools, settings, output_width, output_height, copy_audio);
    std::string process_error;
    SDL_Process* encoder = start_process_with_pipes(
        encode_args,
        SDL_PROCESS_STDIO_APP,
        SDL_PROCESS_STDIO_NULL,
        SDL_PROCESS_STDIO_NULL,
        false,
        process_error);
    if (!encoder) {
        result.error = process_error.empty() ? "Unable to start FFmpeg video encode." : process_error;
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }

    auto stop_process = [](SDL_Process* process) {
        if (process) {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, nullptr);
            SDL_DestroyProcess(process);
        }
    };

    SDL_IOStream* encoder_input = SDL_GetProcessInput(encoder);
    if (!encoder_input) {
        result.error = SDL_GetError();
        if (result.error.empty()) {
            result.error = "Unable to write FFmpeg video input.";
        }
        stop_process(encoder);
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }

    const std::string decode_filter = settings.metadata.variable_frame_rate
        ? "fps=" + format_ffmpeg_number(settings.metadata.fps) + ",format=rgba"
        : "format=rgba";
    const std::vector<std::string> decode_args = {
        tools.ffmpeg_path.string(),
        "-v",
        "error",
        "-threads",
        "0",
        "-i",
        settings.source_path.string(),
        "-map",
        "0:v:0",
        "-vf",
        decode_filter,
        "-an",
        "-sn",
        "-dn",
        "-f",
        "rawvideo",
        "pipe:1",
    };

    std::ostringstream start_fields;
    start_fields << ",\"log_path\":" << json_string(result.diagnostic_log_path.string())
                 << ",\"source_path\":" << json_string(settings.source_path.string())
                 << ",\"destination_path\":" << json_string(ensure_extension(settings.destination_path, settings.profile.extension))
                 << ",\"profile_id\":" << json_string(settings.profile.id)
                 << ",\"profile_label\":" << json_string(settings.profile.label)
                 << ",\"encoder\":" << json_string(settings.profile.encoder)
                 << ",\"backend\":" << json_string(video_backend_label(settings.profile.backend))
                 << ",\"source_width\":" << settings.metadata.width
                 << ",\"source_height\":" << settings.metadata.height
                 << ",\"output_width\":" << output_width
                 << ",\"output_height\":" << output_height
                 << ",\"fps\":" << settings.metadata.fps
                 << ",\"duration_seconds\":" << settings.metadata.duration_seconds
                 << ",\"metadata_frame_count\":" << settings.metadata.frame_count
                 << ",\"variable_frame_rate\":" << json_bool(settings.metadata.variable_frame_rate)
                 << ",\"pixel_size\":" << block
                 << ",\"source_frame_bytes\":" << source_frame_bytes
                 << ",\"output_frame_bytes\":" << output_frame_bytes
                 << ",\"worker_count\":" << worker_count
                 << ",\"pending_queue_limit\":" << pending_queue_limit
                 << ",\"completed_queue_limit\":" << completed_queue_limit
                 << ",\"source_frame_pool_limit\":" << source_pool_limit
                 << ",\"copy_audio\":" << json_bool(copy_audio)
                 << ",\"decode_filter\":" << json_string(decode_filter)
                 << ",\"decode_args\":" << json_string_array(decode_args)
                 << ",\"encode_args\":" << json_string_array(encode_args);
    write_diagnostic_record(diagnostics, "start", start_fields.str(), false);

    SDL_Process* decoder = start_process_with_pipes(
        decode_args,
        SDL_PROCESS_STDIO_NULL,
        SDL_PROCESS_STDIO_APP,
        SDL_PROCESS_STDIO_NULL,
        false,
        process_error);
    if (!decoder) {
        stop_process(encoder);
        result.error = process_error.empty() ? "Unable to start FFmpeg frame decode." : process_error;
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }

    SDL_IOStream* output = SDL_GetProcessOutput(decoder);
    if (!output) {
        stop_process(decoder);
        stop_process(encoder);
        result.error = SDL_GetError();
        if (result.error.empty()) {
            result.error = "Unable to read FFmpeg frame output.";
        }
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }

    VideoFramePipeline pipeline;
    pipeline.max_pending = pending_queue_limit;
    pipeline.max_completed = completed_queue_limit;
    pipeline.active_workers = worker_count;

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (unsigned int index = 0; index < worker_count; ++index) {
        workers.emplace_back([&pipeline,
                              metadata = settings.metadata,
                              process_settings = settings.process_settings,
                              output_width,
                              output_height,
                              output_frame_bytes,
                              source_pool,
                              diagnostics]() {
            video_frame_worker(pipeline, metadata, process_settings, output_width, output_height, output_frame_bytes, source_pool, diagnostics);
        });
    }

    VideoFrameWriterResult writer_result;
    std::thread writer([&pipeline, encoder_input, progress, total_frames, &writer_result, diagnostics]() {
        video_frame_writer(pipeline, encoder_input, progress, total_frames, writer_result, diagnostics);
    });

    auto abort_pipeline = [&](std::string error) {
        if (!error.empty()) {
            set_pipeline_error(pipeline, std::move(error));
        }
        SDL_KillProcess(encoder, true);
        join_video_threads(workers, writer);
    };

    std::vector<unsigned char> frame = acquire_frame_buffer(source_pool, diagnostics);
    std::size_t frame_offset = 0;
    int frame_index = 0;
    int decode_not_ready_streak = 0;
    std::string frame_error;
    bool decode_failed = false;
    while (true) {
        if (progress->cancel_requested) {
            SDL_KillProcess(decoder, true);
            decode_failed = true;
            frame_error = "Video export canceled.";
            break;
        }
        if (pipeline_error(pipeline, frame_error)) {
            SDL_KillProcess(decoder, true);
            decode_failed = true;
            break;
        }

        const std::size_t remaining_frame = frame.size() - frame_offset;
        const auto read_started = std::chrono::steady_clock::now();
        const size_t read = SDL_ReadIO(output, frame.data() + frame_offset, remaining_frame);
        if (diagnostics) {
            diagnostics->read_calls.fetch_add(1);
            diagnostics->read_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - read_started).count());
        }
        if (read > 0U) {
            decode_not_ready_streak = 0;
            frame_offset += read;
            if (diagnostics) {
                diagnostics->decoded_bytes.fetch_add(static_cast<long long>(read));
            }
            if (frame_offset < frame.size()) {
                continue;
            }

            VideoFrameJob job;
            job.index = frame_index;
            job.rgba = std::move(frame);
            if (!enqueue_video_frame(pipeline, std::move(job), progress, frame_error, diagnostics)) {
                SDL_KillProcess(decoder, true);
                decode_failed = true;
                break;
            }
            ++frame_index;
            if (diagnostics) {
                diagnostics->decoded_frames.store(frame_index);
                maybe_write_diagnostic_sample(diagnostics);
            }
            frame = acquire_frame_buffer(source_pool, diagnostics);
            frame_offset = 0;
            continue;
        }

        const SDL_IOStatus status = SDL_GetIOStatus(output);
        if (status == SDL_IO_STATUS_EOF) {
            break;
        }
        if (status == SDL_IO_STATUS_NOT_READY) {
            if (diagnostics) {
                diagnostics->decode_not_ready.fetch_add(1);
            }
            ++decode_not_ready_streak;
            const auto wait_started = std::chrono::steady_clock::now();
            if (decode_not_ready_streak < 128) {
                std::this_thread::yield();
            } else {
                SDL_Delay(1);
            }
            if (diagnostics) {
                diagnostics->decode_wait_ns.fetch_add(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_started).count());
            }
            continue;
        }

        decode_failed = true;
        frame_error = SDL_GetError();
        if (frame_error.empty()) {
            frame_error = "FFmpeg frame decode failed.";
        }
        break;
    }

    int decode_exit = -1;
    SDL_WaitProcess(decoder, true, &decode_exit);
    SDL_DestroyProcess(decoder);
    release_frame_buffer(source_pool, std::move(frame), diagnostics);
    if (decode_failed) {
        abort_pipeline(frame_error);
        stop_process(encoder);
        result.error = frame_error;
        if (settings.profile.backend != VideoExportBackend::Software && result.error.find("canceled") == std::string::npos) {
            result.error += " Hardware encoder initialization can fail when the matching driver or device is unavailable. Try a software export profile.";
        }
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }
    if (decode_exit != 0) {
        abort_pipeline("FFmpeg frame decode failed with exit code " + std::to_string(decode_exit) + ".");
        stop_process(encoder);
        result.error = "FFmpeg frame decode failed with exit code " + std::to_string(decode_exit) + ".";
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error) + ",\"decode_exit\":" + std::to_string(decode_exit));
        return result;
    }
    if (frame_offset != 0U || frame_index == 0) {
        abort_pipeline("Decoded video ended with an incomplete frame.");
        stop_process(encoder);
        result.error = "Decoded video ended with an incomplete frame.";
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }

    close_pipeline_input(pipeline);
    join_video_threads(workers, writer);
    if (!writer_result.error.empty()) {
        stop_process(encoder);
        result.error = writer_result.error;
        if (settings.profile.backend != VideoExportBackend::Software && result.error.find("canceled") == std::string::npos) {
            result.error += " Hardware encoder initialization can fail when the matching driver or device is unavailable. Try a software export profile.";
        }
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }
    if (writer_result.frames_written != frame_index) {
        stop_process(encoder);
        result.error = "Video export wrote fewer frames than were decoded.";
        write_diagnostic_record(
            diagnostics,
            "error",
            ",\"error\":" + json_string(result.error)
                + ",\"decoded_frame_count\":" + std::to_string(frame_index)
                + ",\"written_frame_count\":" + std::to_string(writer_result.frames_written));
        return result;
    }

    progress->frames_total = frame_index;
    progress->encoding = true;
    progress->percent = std::max(progress->percent.load(), 96);
    if (!SDL_CloseIO(encoder_input)) {
        stop_process(encoder);
        progress->encoding = false;
        result.error = SDL_GetError();
        if (result.error.empty()) {
            result.error = "Unable to finish FFmpeg video input.";
        }
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
        return result;
    }
    encoder_input = nullptr;

    int encode_exit = -1;
    while (!SDL_WaitProcess(encoder, false, &encode_exit)) {
        if (progress->cancel_requested) {
            SDL_KillProcess(encoder, true);
            SDL_WaitProcess(encoder, true, &encode_exit);
            SDL_DestroyProcess(encoder);
            progress->encoding = false;
            result.error = "Video export canceled.";
            write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error));
            return result;
        }
        SDL_Delay(10);
    }
    SDL_DestroyProcess(encoder);
    progress->encoding = false;
    progress->percent = encode_exit == 0 ? 100 : progress->percent.load();
    if (encode_exit != 0) {
        result.error = "FFmpeg video encode failed with exit code " + std::to_string(encode_exit) + ".";
        if (settings.profile.backend != VideoExportBackend::Software) {
            result.error += " Hardware encoder initialization can fail when the matching driver or device is unavailable. Try a software export profile.";
        }
        write_diagnostic_record(diagnostics, "error", ",\"error\":" + json_string(result.error) + ",\"encode_exit\":" + std::to_string(encode_exit));
        return result;
    }

    result.success = true;
    result.audio_copied = copy_audio;
    if (settings.copy_audio && settings.metadata.has_audio && !copy_audio) {
        result.warning = "Audio was not copied because the source audio codec is not compatible with the selected container.";
    }
    write_diagnostic_record(
        diagnostics,
        "end",
        ",\"success\":true,\"decode_exit\":" + std::to_string(decode_exit)
            + ",\"encode_exit\":" + std::to_string(encode_exit)
            + ",\"decoded_frame_count\":" + std::to_string(frame_index)
            + ",\"written_frame_count\":" + std::to_string(writer_result.frames_written));
    return result;
}

std::string video_profile_extension_filter(const VideoExportProfile& profile)
{
    if (profile.extension.size() > 1U && profile.extension.front() == '.') {
        return profile.extension.substr(1U);
    }
    return profile.extension;
}

std::string video_hardware_speed_label(VideoHardwareSpeed speed)
{
    switch (speed) {
    case VideoHardwareSpeed::Balanced:
        return "Balanced";
    case VideoHardwareSpeed::Fast:
        return "Fast";
    case VideoHardwareSpeed::VeryFast:
        return "Very Fast";
    }
    return "Balanced";
}

std::string format_video_time(double seconds)
{
    seconds = std::max(0.0, seconds);
    const auto total = static_cast<long long>(std::floor(seconds + 0.5));
    const long long hours = total / 3600;
    const long long minutes = (total / 60) % 60;
    const long long secs = total % 60;
    std::ostringstream output;
    if (hours > 0) {
        output << hours << ':'
               << std::setw(2) << std::setfill('0') << minutes << ':'
               << std::setw(2) << std::setfill('0') << secs;
    } else {
        output << minutes << ':'
               << std::setw(2) << std::setfill('0') << secs;
    }
    return output.str();
}

std::string format_video_fps(double fps)
{
    if (fps <= 0.0 || !std::isfinite(fps)) {
        return "0";
    }
    return format_ffmpeg_number(fps);
}

} // namespace pixatto
