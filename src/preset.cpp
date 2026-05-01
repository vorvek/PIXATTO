#include "pixatto/preset.hpp"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace pixatto {
namespace {

constexpr std::string_view kPresetExtension = ".pxpreset";
constexpr std::string_view kPresetVersionKey = "pixatto_preset";
constexpr std::string_view kLegacyPresetVersionKey = "pixelizer_preset";
constexpr std::string_view kPresetVersion = "1";
constexpr std::size_t kMaxPresetPaletteColors = 256;

constexpr const char* kPreferenceOrganization = "Codex";
constexpr const char* kPreferenceAppName = "Pixatto";
constexpr const char* kLegacyPreferenceAppName = "Pixelizer";

std::optional<std::filesystem::path> preference_base_dir(const char* app_name)
{
    char* pref = SDL_GetPrefPath(kPreferenceOrganization, app_name);
    if (!pref) {
        return std::nullopt;
    }

    std::filesystem::path base(pref);
    SDL_free(pref);
    return base;
}

void copy_legacy_dir_if_needed(const std::filesystem::path& target, const std::filesystem::path& legacy)
{
    std::error_code ec;
    if (std::filesystem::exists(target, ec) || ec) {
        return;
    }

    ec.clear();
    if (!std::filesystem::exists(legacy, ec) || ec) {
        return;
    }

    ec.clear();
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        return;
    }

    ec.clear();
    std::filesystem::copy(legacy, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
}

std::filesystem::path preset_dir()
{
    const auto base = preference_base_dir(kPreferenceAppName);
    if (!base) {
        return std::filesystem::current_path() / "presets";
    }

    const std::filesystem::path dir = *base / "presets";
    if (const auto legacy_base = preference_base_dir(kLegacyPreferenceAppName)) {
        copy_legacy_dir_if_needed(dir, *legacy_base / "presets");
    }
    return dir;
}

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string sanitized_preset_stem(std::string name)
{
    name = normalize_preset_name(name);

    std::string sanitized;
    sanitized.reserve(name.size());
    bool previous_dash = false;
    for (const unsigned char ch : name) {
        const bool keep = std::isalnum(ch) != 0 || ch == '_' || ch == '-';
        if (keep) {
            sanitized.push_back(static_cast<char>(std::tolower(ch)));
            previous_dash = false;
        } else if (!previous_dash) {
            sanitized.push_back('-');
            previous_dash = true;
        }
    }

    while (!sanitized.empty() && sanitized.back() == '-') {
        sanitized.pop_back();
    }
    return sanitized.empty() ? "preset" : sanitized;
}

std::filesystem::path preset_path_for_name(const std::filesystem::path& dir, const std::string& name)
{
    return dir / (sanitized_preset_stem(name) + std::string(kPresetExtension));
}

bool has_preset_extension(const std::filesystem::path& path)
{
    return lowercase(path.extension().string()) == kPresetExtension;
}

std::string bool_text(bool value)
{
    return value ? "true" : "false";
}

bool parse_bool(std::string value, bool& parsed)
{
    value = lowercase(trim(std::move(value)));
    if (value == "true" || value == "1") {
        parsed = true;
        return true;
    }
    if (value == "false" || value == "0") {
        parsed = false;
        return true;
    }
    return false;
}

bool parse_int(std::string value, int minimum, int maximum, int& parsed)
{
    value = trim(std::move(value));
    if (value.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long number = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || number < minimum || number > maximum) {
        return false;
    }
    parsed = static_cast<int>(number);
    return true;
}

bool parse_uint32(std::string value, std::uint32_t& parsed)
{
    value = trim(std::move(value));
    if (value.empty() || value.front() == '-') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long number = std::strtoull(value.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0' || number > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    parsed = static_cast<std::uint32_t>(number);
    return true;
}

bool parse_float(std::string value, float& parsed)
{
    value = trim(std::move(value));
    if (value.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const float number = std::strtof(value.c_str(), &end);
    if (errno != 0 || !end || *end != '\0' || !std::isfinite(number)) {
        return false;
    }
    parsed = number;
    return true;
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool parse_hex_color(std::string value, Color32& color)
{
    value = trim(std::move(value));
    if (!value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }

    if (value.size() != 6U && value.size() != 8U) {
        return false;
    }

    for (char ch : value) {
        if (hex_value(ch) < 0) {
            return false;
        }
    }

    auto byte_at = [&](std::size_t offset) {
        return static_cast<std::uint8_t>((hex_value(value[offset]) << 4) | hex_value(value[offset + 1U]));
    };

    color.r = byte_at(0);
    color.g = byte_at(2);
    color.b = byte_at(4);
    color.a = value.size() == 8U ? byte_at(6) : 255;
    return true;
}

std::string color_hex(Color32 color)
{
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << static_cast<int>(color.r)
           << std::setw(2) << static_cast<int>(color.g)
           << std::setw(2) << static_cast<int>(color.b)
           << std::setw(2) << static_cast<int>(color.a);
    return output.str();
}

std::string block_color_mode_key(BlockColorMode mode)
{
    switch (mode) {
    case BlockColorMode::Average:
        return "average";
    case BlockColorMode::WeightedAverage:
        return "weighted_average";
    }
    return "weighted_average";
}

bool parse_block_color_mode(std::string value, BlockColorMode& mode)
{
    value = lowercase(trim(std::move(value)));
    if (value == "average") {
        mode = BlockColorMode::Average;
        return true;
    }
    if (value == "weighted_average" || value == "weighted") {
        mode = BlockColorMode::WeightedAverage;
        return true;
    }
    return false;
}

constexpr std::array<std::pair<std::string_view, DitherMode>, 24> kDitherModes = {{
    {"none", DitherMode::None},
    {"bayer", DitherMode::Bayer},
    {"blue_noise", DitherMode::BlueNoise},
    {"floyd_steinberg", DitherMode::FloydSteinberg},
    {"false_floyd_steinberg", DitherMode::FalseFloydSteinberg},
    {"filter_lite", DitherMode::FilterLite},
    {"zhigang_fan", DitherMode::ZhigangFan},
    {"shiau_fan", DitherMode::ShiauFan},
    {"jarvis_judice_ninke", DitherMode::JarvisJudiceNinke},
    {"atkinson", DitherMode::Atkinson},
    {"stucki", DitherMode::Stucki},
    {"burkes", DitherMode::Burkes},
    {"sierra", DitherMode::Sierra},
    {"two_row_sierra", DitherMode::TwoRowSierra},
    {"riemersma", DitherMode::Riemersma},
    {"cluster_dot_4x4", DitherMode::ClusterDot4x4},
    {"cluster_dot_8x8", DitherMode::ClusterDot8x8},
    {"horizontal_2x2", DitherMode::Horizontal2x2},
    {"horizontal_8x1", DitherMode::Horizontal8x1},
    {"horizontal_12x4", DitherMode::Horizontal12x4},
    {"vertical_2x2", DitherMode::Vertical2x2},
    {"vertical_1x8", DitherMode::Vertical1x8},
    {"vertical_4x12", DitherMode::Vertical4x12},
    {"diagonal_5x5", DitherMode::Diagonal5x5},
}};

std::string dither_mode_key(DitherMode mode)
{
    for (const auto& [key, candidate] : kDitherModes) {
        if (candidate == mode) {
            return std::string(key);
        }
    }
    return "none";
}

bool parse_dither_mode(std::string value, DitherMode& mode)
{
    value = lowercase(trim(std::move(value)));
    for (const auto& [key, candidate] : kDitherModes) {
        if (value == key) {
            mode = candidate;
            return true;
        }
    }
    return false;
}

std::string palette_text(const std::vector<Color32>& palette)
{
    std::string text;
    for (std::size_t index = 0; index < palette.size(); ++index) {
        if (index > 0) {
            text.push_back(' ');
        }
        text += color_hex(palette[index]);
    }
    return text;
}

bool parse_palette(std::string value, std::vector<Color32>& palette)
{
    std::istringstream input(value);
    std::string token;
    std::vector<Color32> parsed;
    while (input >> token) {
        if (parsed.size() >= kMaxPresetPaletteColors) {
            return false;
        }

        Color32 color;
        if (!parse_hex_color(token, color)) {
            return false;
        }
        parsed.push_back(color);
    }

    palette = std::move(parsed);
    return true;
}

void write_preset_value(std::ostream& output, std::string_view key, const std::string& value)
{
    output << key << '=' << value << '\n';
}

void write_preset_value(std::ostream& output, std::string_view key, int value)
{
    output << key << '=' << value << '\n';
}

void write_preset_value(std::ostream& output, std::string_view key, std::uint32_t value)
{
    output << key << '=' << value << '\n';
}

void write_preset_value(std::ostream& output, std::string_view key, float value)
{
    output << key << '=' << std::setprecision(9) << value << '\n';
}

bool write_preset_file(
    const std::filesystem::path& path,
    const std::string& name,
    const ProcessSettings& settings,
    std::string& error)
{
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "Unable to write preset file.";
        return false;
    }

    write_preset_value(output, kPresetVersionKey, std::string(kPresetVersion));
    write_preset_value(output, "name", normalize_preset_name(name));
    write_preset_value(output, "pixel_size", settings.pixel_size);
    write_preset_value(output, "block_color_mode", block_color_mode_key(settings.block_color_mode));
    write_preset_value(output, "use_palette", bool_text(settings.use_palette));
    write_preset_value(output, "preserve_transparency", bool_text(settings.preserve_transparency));
    write_preset_value(output, "palette", palette_text(settings.palette));
    write_preset_value(output, "color_levels", settings.color_levels);
    write_preset_value(output, "reduction_max_colors", settings.reduction_max_colors);
    write_preset_value(output, "dither_mode", dither_mode_key(settings.dither_mode));
    write_preset_value(output, "bayer_matrix_size", settings.bayer_matrix_size);
    write_preset_value(output, "dither_amount", settings.dither_amount);
    write_preset_value(output, "blue_noise_seed", settings.blue_noise_seed);
    write_preset_value(output, "brightness", settings.adjustments.brightness);
    write_preset_value(output, "contrast", settings.adjustments.contrast);
    write_preset_value(output, "gamma", settings.adjustments.gamma);
    write_preset_value(output, "input_black", settings.adjustments.input_black);
    write_preset_value(output, "input_white", settings.adjustments.input_white);
    write_preset_value(output, "output_black", settings.adjustments.output_black);
    write_preset_value(output, "output_white", settings.adjustments.output_white);
    write_preset_value(output, "saturation", settings.adjustments.saturation);
    write_preset_value(output, "tint", color_hex(settings.adjustments.tint));
    write_preset_value(output, "tint_strength", settings.adjustments.tint_strength);

    if (!output) {
        error = "Preset file write failed.";
        return false;
    }
    return true;
}

struct PresetEntry {
    std::string key;
    std::string value;
};

bool read_preset_entries(const std::filesystem::path& path, std::vector<PresetEntry>& entries, std::string& error)
{
    std::ifstream input(path);
    if (!input) {
        error = "Unable to open preset file.";
        return false;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) {
            line.erase(0, 3);
        }

        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            error = "Invalid preset line " + std::to_string(line_number) + ".";
            return false;
        }

        std::string key = lowercase(trim(line.substr(0, separator)));
        std::string value = trim(line.substr(separator + 1U));
        if (key.empty()) {
            error = "Invalid preset key on line " + std::to_string(line_number) + ".";
            return false;
        }
        entries.push_back({std::move(key), std::move(value)});
    }

    return true;
}

std::optional<std::string> find_entry(const std::vector<PresetEntry>& entries, std::string_view key)
{
    for (const PresetEntry& entry : entries) {
        if (entry.key == key) {
            return entry.value;
        }
    }
    return std::nullopt;
}

bool is_preset_version_key(std::string_view key)
{
    return key == kPresetVersionKey || key == kLegacyPresetVersionKey;
}

bool apply_entry(ProcessSettings& settings, const PresetEntry& entry, std::string& error)
{
    if (is_preset_version_key(entry.key) || entry.key == "name") {
        return true;
    }

    auto fail = [&]() {
        error = "Invalid preset value for " + entry.key + ".";
        return false;
    };

    if (entry.key == "pixel_size") {
        return parse_int(entry.value, 1, 128, settings.pixel_size) || fail();
    }
    if (entry.key == "block_color_mode") {
        return parse_block_color_mode(entry.value, settings.block_color_mode) || fail();
    }
    if (entry.key == "use_palette") {
        return parse_bool(entry.value, settings.use_palette) || fail();
    }
    if (entry.key == "preserve_transparency") {
        return parse_bool(entry.value, settings.preserve_transparency) || fail();
    }
    if (entry.key == "palette") {
        return parse_palette(entry.value, settings.palette) || fail();
    }
    if (entry.key == "color_levels") {
        return parse_int(entry.value, 2, 64, settings.color_levels) || fail();
    }
    if (entry.key == "reduction_max_colors") {
        return parse_int(entry.value, 0, 256, settings.reduction_max_colors) || fail();
    }
    if (entry.key == "dither_mode") {
        return parse_dither_mode(entry.value, settings.dither_mode) || fail();
    }
    if (entry.key == "bayer_matrix_size") {
        return parse_int(entry.value, 2, 16, settings.bayer_matrix_size) || fail();
    }
    if (entry.key == "dither_amount") {
        return parse_float(entry.value, settings.dither_amount) || fail();
    }
    if (entry.key == "blue_noise_seed") {
        return parse_uint32(entry.value, settings.blue_noise_seed) || fail();
    }
    if (entry.key == "brightness") {
        return parse_float(entry.value, settings.adjustments.brightness) || fail();
    }
    if (entry.key == "contrast") {
        return parse_float(entry.value, settings.adjustments.contrast) || fail();
    }
    if (entry.key == "gamma") {
        return parse_float(entry.value, settings.adjustments.gamma) || fail();
    }
    if (entry.key == "input_black") {
        return parse_float(entry.value, settings.adjustments.input_black) || fail();
    }
    if (entry.key == "input_white") {
        return parse_float(entry.value, settings.adjustments.input_white) || fail();
    }
    if (entry.key == "output_black") {
        return parse_float(entry.value, settings.adjustments.output_black) || fail();
    }
    if (entry.key == "output_white") {
        return parse_float(entry.value, settings.adjustments.output_white) || fail();
    }
    if (entry.key == "saturation") {
        return parse_float(entry.value, settings.adjustments.saturation) || fail();
    }
    if (entry.key == "tint") {
        return parse_hex_color(entry.value, settings.adjustments.tint) || fail();
    }
    if (entry.key == "tint_strength") {
        return parse_float(entry.value, settings.adjustments.tint_strength) || fail();
    }

    return true;
}

} // namespace

std::string normalize_preset_name(const std::string& name)
{
    std::string normalized = trim(name);
    return normalized.empty() ? "preset" : normalized;
}

bool load_preset_file(const std::filesystem::path& path, Preset& preset, std::string& error)
{
    std::vector<PresetEntry> entries;
    if (!read_preset_entries(path, entries, error)) {
        return false;
    }

    std::optional<std::string> version = find_entry(entries, kPresetVersionKey);
    if (!version) {
        version = find_entry(entries, kLegacyPresetVersionKey);
    }
    if (!version || *version != kPresetVersion) {
        error = "Unsupported preset version.";
        return false;
    }

    Preset loaded;
    loaded.path = path;
    loaded.name = path.stem().string();
    loaded.settings = {};
    if (const std::optional<std::string> name = find_entry(entries, "name")) {
        loaded.name = normalize_preset_name(*name);
    }

    for (const PresetEntry& entry : entries) {
        if (!apply_entry(loaded.settings, entry, error)) {
            return false;
        }
    }

    preset = std::move(loaded);
    return true;
}

std::vector<Preset> load_presets_from_dir(const std::filesystem::path& dir)
{
    std::vector<Preset> presets;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return presets;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file() || !has_preset_extension(entry.path())) {
            continue;
        }

        Preset preset;
        std::string error;
        if (load_preset_file(entry.path(), preset, error)) {
            presets.push_back(std::move(preset));
        }
    }

    std::sort(presets.begin(), presets.end(), [](const Preset& lhs, const Preset& rhs) {
        return lowercase(lhs.name) < lowercase(rhs.name);
    });
    return presets;
}

std::vector<Preset> load_saved_presets()
{
    const auto dir = preset_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return {};
    }
    return load_presets_from_dir(dir);
}

std::optional<Preset> find_preset_conflict_in_dir(const std::filesystem::path& dir, const std::string& name)
{
    const auto path = preset_path_for_name(dir, name);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::nullopt;
    }

    Preset preset;
    std::string error;
    if (load_preset_file(path, preset, error)) {
        return preset;
    }

    preset.name = normalize_preset_name(name);
    preset.path = path;
    preset.settings = {};
    return preset;
}

std::optional<Preset> find_preset_conflict(const std::string& name)
{
    return find_preset_conflict_in_dir(preset_dir(), name);
}

bool delete_preset_file(const Preset& preset, std::string& error)
{
    if (preset.path.empty()) {
        error = "No preset file is selected.";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(preset.path, ec)) {
        error = ec ? ec.message() : "Preset file does not exist.";
        return false;
    }

    if (!has_preset_extension(preset.path)) {
        error = "Only saved .pxpreset presets can be deleted.";
        return false;
    }

    if (!std::filesystem::remove(preset.path, ec)) {
        error = ec ? ec.message() : "Preset file could not be removed.";
        return false;
    }

    if (ec) {
        error = ec.message();
        return false;
    }

    return true;
}

bool save_preset_to_dir(
    const std::filesystem::path& dir,
    const std::string& name,
    const ProcessSettings& settings,
    PresetSaveMode mode,
    Preset& saved,
    std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const std::string normalized_name = normalize_preset_name(name);
    const auto path = preset_path_for_name(dir, normalized_name);
    if (mode == PresetSaveMode::Create && std::filesystem::exists(path, ec)) {
        error = normalized_name + " already exists.";
        return false;
    }
    if (ec) {
        error = ec.message();
        return false;
    }

    if (!write_preset_file(path, normalized_name, settings, error)) {
        return false;
    }
    return load_preset_file(path, saved, error);
}

bool save_preset_as(
    const std::string& name,
    const ProcessSettings& settings,
    PresetSaveMode mode,
    Preset& saved,
    std::string& error)
{
    return save_preset_to_dir(preset_dir(), name, settings, mode, saved, error);
}

} // namespace pixatto
