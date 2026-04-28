#include "pixelizer/palette.hpp"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace pixelizer {
namespace {

std::filesystem::path palette_dir()
{
    char* pref = SDL_GetPrefPath("Codex", "Pixelizer");
    if (!pref) {
        return std::filesystem::current_path() / "palettes";
    }

    std::filesystem::path base(pref);
    SDL_free(pref);
    return base / "palettes";
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

std::string sanitized_palette_stem(std::string name)
{
    name = trim(std::move(name));
    if (name.empty()) {
        return "palette";
    }

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
    return sanitized.empty() ? "palette" : sanitized;
}

bool has_hex_extension(const std::filesystem::path& path)
{
    return lowercase(path.extension().string()) == ".hex";
}

std::filesystem::path import_palette_destination(const std::filesystem::path& source)
{
    std::filesystem::path normalized_name = source.filename();
    normalized_name.replace_extension(".hex");
    return palette_dir() / normalized_name;
}

bool validate_palette_colors(const std::vector<Color32>& colors, std::string& error)
{
    if (colors.empty()) {
        error = "Palette must contain at least one color.";
        return false;
    }
    if (colors.size() > kMaxPaletteColors) {
        error = "Palettes are limited to 256 colors.";
        return false;
    }
    return true;
}

bool write_palette_file(const std::filesystem::path& path, const std::vector<Color32>& colors, std::string& error)
{
    if (!validate_palette_colors(colors, error)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "Unable to write palette file.";
        return false;
    }

    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < colors.size(); ++index) {
        const Color32 color = colors[index];
        output << std::setw(2) << static_cast<int>(color.r)
               << std::setw(2) << static_cast<int>(color.g)
               << std::setw(2) << static_cast<int>(color.b);
        if ((index + 1U) % 8U == 0U || index + 1U == colors.size()) {
            output << '\n';
        } else {
            output << ' ';
        }
    }

    if (!output) {
        error = "Palette file write failed.";
        return false;
    }
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

bool parse_hex_color(std::string token, Color32& color, bool allow_alpha)
{
    token = trim(std::move(token));
    if (token.empty()) {
        return false;
    }
    if (token.front() == '#') {
        token.erase(token.begin());
    }

    if (token.size() != 6 && (!allow_alpha || token.size() != 8)) {
        return false;
    }

    for (char ch : token) {
        if (hex_value(ch) < 0) {
            return false;
        }
    }

    auto byte_at = [&](std::size_t offset) {
        return static_cast<std::uint8_t>((hex_value(token[offset]) << 4) | hex_value(token[offset + 1U]));
    };

    color.r = byte_at(0);
    color.g = byte_at(2);
    color.b = byte_at(4);
    color.a = token.size() == 8 ? byte_at(6) : 255;
    return true;
}

bool load_palette_file_impl(
    const std::filesystem::path& path,
    Palette& palette,
    std::string& error,
    bool require_hex_extension,
    bool allow_alpha)
{
    if (require_hex_extension && !has_hex_extension(path)) {
        error = "Only .hex palette files can be imported.";
        return false;
    }

    std::ifstream input(path);
    if (!input) {
        error = "Unable to open palette file.";
        return false;
    }

    palette = {};
    palette.name = path.stem().string();
    palette.path = path;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) {
            line.erase(0, 3);
        }

        const auto semicolon_comment = line.find(';');
        const auto slash_comment = line.find("//");
        const auto comment = std::min(
            semicolon_comment == std::string::npos ? line.size() : semicolon_comment,
            slash_comment == std::string::npos ? line.size() : slash_comment);
        if (comment < line.size()) {
            line = line.substr(0, comment);
        }

        std::istringstream parts(line);
        std::string token;
        while (parts >> token) {
            Color32 color;
            if (!parse_hex_color(token, color, allow_alpha)) {
                error = "Invalid hex color \"" + token + "\" on line " + std::to_string(line_number) + ".";
                return false;
            }
            if (palette.colors.size() >= kMaxPaletteColors) {
                error = "Palettes are limited to 256 colors.";
                return false;
            }
            palette.colors.push_back(color);
        }
    }

    if (palette.colors.empty()) {
        error = "No hex colors were found in the palette.";
        return false;
    }

    return true;
}

} // namespace

bool load_palette_file(const std::filesystem::path& path, Palette& palette, std::string& error)
{
    return load_palette_file_impl(path, palette, error, false, true);
}

bool validate_import_palette_file(const std::filesystem::path& source, Palette& palette, std::string& error)
{
    return load_palette_file_impl(source, palette, error, true, false);
}

std::optional<Palette> find_import_palette_conflict(const std::filesystem::path& source)
{
    const auto destination = import_palette_destination(source);
    std::error_code ec;
    if (!std::filesystem::exists(destination, ec)) {
        return std::nullopt;
    }

    Palette existing;
    std::string error;
    if (!load_palette_file(destination, existing, error)) {
        existing.name = destination.stem().string();
        existing.path = destination;
        existing.colors.clear();
    }
    return existing;
}

std::string suggest_import_palette_copy_name(const std::filesystem::path& source)
{
    const auto destination = import_palette_destination(source);
    const std::string stem = destination.stem().string();
    const auto parent = destination.parent_path();

    std::filesystem::path candidate = destination;
    int suffix = 1;
    while (std::filesystem::exists(candidate)) {
        candidate = parent / (stem + "-" + std::to_string(suffix) + ".hex");
        ++suffix;
    }
    return candidate.stem().string();
}

bool import_palette_file(const std::filesystem::path& source, PaletteImportMode mode, Palette& imported, std::string& error)
{
    Palette parsed;
    if (!validate_import_palette_file(source, parsed, error)) {
        return false;
    }

    const auto destination = import_palette_destination(source);
    std::error_code ec;
    if (mode == PaletteImportMode::Create && std::filesystem::exists(destination, ec)) {
        error = parsed.name + " already exists.";
        return false;
    }
    if (ec) {
        error = ec.message();
        return false;
    }

    if (!write_palette_file(destination, parsed.colors, error)) {
        return false;
    }
    return load_palette_file(destination, imported, error);
}

bool delete_palette_file(const Palette& palette, std::string& error)
{
    if (palette.path.empty()) {
        error = "No palette file is selected.";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(palette.path, ec)) {
        error = ec ? ec.message() : "Palette file does not exist.";
        return false;
    }

    if (lowercase(palette.path.extension().string()) != ".hex") {
        error = "Only saved .hex palettes can be deleted.";
        return false;
    }

    if (!std::filesystem::remove(palette.path, ec)) {
        error = ec ? ec.message() : "Palette file could not be removed.";
        return false;
    }

    if (ec) {
        error = ec.message();
        return false;
    }

    return true;
}

bool overwrite_palette_file(const Palette& palette, const std::vector<Color32>& colors, std::string& error)
{
    if (palette.path.empty()) {
        error = "No saved palette is selected.";
        return false;
    }
    if (lowercase(palette.path.extension().string()) != ".hex") {
        error = "Only saved .hex palettes can be overwritten.";
        return false;
    }
    return write_palette_file(palette.path, colors, error);
}

bool save_palette_as_new(const std::string& name, const std::vector<Color32>& colors, Palette& saved, std::string& error)
{
    if (!validate_palette_colors(colors, error)) {
        return false;
    }

    const auto dir = palette_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const std::string stem = sanitized_palette_stem(name);
    auto destination = dir / (stem + ".hex");
    int suffix = 1;
    while (std::filesystem::exists(destination)) {
        destination = dir / (stem + "-" + std::to_string(suffix) + ".hex");
        ++suffix;
    }

    if (!write_palette_file(destination, colors, error)) {
        return false;
    }
    return load_palette_file(destination, saved, error);
}

std::vector<Palette> load_saved_palettes()
{
    std::vector<Palette> palettes;
    const auto dir = palette_dir();

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return palettes;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        const auto extension = lowercase(entry.path().extension().string());
        if (extension != ".hex") {
            continue;
        }

        Palette palette;
        std::string error;
        if (load_palette_file(entry.path(), palette, error)) {
            palettes.push_back(std::move(palette));
        }
    }

    std::sort(palettes.begin(), palettes.end(), [](const Palette& lhs, const Palette& rhs) {
        return lhs.name < rhs.name;
    });

    return palettes;
}

} // namespace pixelizer
