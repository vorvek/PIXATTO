#include "pixelizer/palette.hpp"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <fstream>
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

bool parse_hex_color(std::string token, Color32& color)
{
    token = trim(std::move(token));
    if (token.empty()) {
        return false;
    }
    if (token.front() == '#') {
        token.erase(token.begin());
    }

    if (token.size() != 6 && token.size() != 8) {
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

} // namespace

bool load_palette_file(const std::filesystem::path& path, Palette& palette, std::string& error)
{
    std::ifstream input(path);
    if (!input) {
        error = "Unable to open palette file.";
        return false;
    }

    palette = {};
    palette.name = path.stem().string();
    palette.path = path;

    std::string line;
    while (std::getline(input, line)) {
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
            if (parse_hex_color(token, color)) {
                palette.colors.push_back(color);
            }
        }
    }

    if (palette.colors.empty()) {
        error = "No hex colors were found in the palette.";
        return false;
    }

    return true;
}

bool import_palette_file(const std::filesystem::path& source, Palette& imported, std::string& error)
{
    Palette parsed;
    if (!load_palette_file(source, parsed, error)) {
        return false;
    }

    const auto dir = palette_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    std::filesystem::path normalized_name = source.filename();
    if (lowercase(normalized_name.extension().string()) != ".hex") {
        normalized_name.replace_extension(".hex");
    }

    auto destination = dir / normalized_name;
    int suffix = 1;
    while (std::filesystem::exists(destination)) {
        destination = dir / (normalized_name.stem().string() + "-" + std::to_string(suffix) + ".hex");
        ++suffix;
    }

    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    return load_palette_file(destination, imported, error);
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
