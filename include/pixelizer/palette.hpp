#pragma once

#include "pixelizer/image.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace pixelizer {

struct Palette {
    std::string name;
    std::filesystem::path path;
    std::vector<Color32> colors;
};

std::vector<Palette> load_saved_palettes();
bool import_palette_file(const std::filesystem::path& source, Palette& imported, std::string& error);
bool load_palette_file(const std::filesystem::path& path, Palette& palette, std::string& error);

} // namespace pixelizer

