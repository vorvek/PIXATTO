#pragma once

#include "pixelizer/image.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace pixelizer {

inline constexpr std::size_t kMaxPaletteColors = 256;

struct Palette {
    std::string name;
    std::filesystem::path path;
    std::vector<Color32> colors;
};

std::vector<Palette> load_saved_palettes();
bool import_palette_file(const std::filesystem::path& source, Palette& imported, std::string& error);
bool load_palette_file(const std::filesystem::path& path, Palette& palette, std::string& error);
bool delete_palette_file(const Palette& palette, std::string& error);
bool overwrite_palette_file(const Palette& palette, const std::vector<Color32>& colors, std::string& error);
bool save_palette_as_new(const std::string& name, const std::vector<Color32>& colors, Palette& saved, std::string& error);

} // namespace pixelizer
