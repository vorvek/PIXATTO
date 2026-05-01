#pragma once

#include "pixatto/image.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pixatto {

inline constexpr std::size_t kMaxPaletteColors = 256;

struct Palette {
    std::string name;
    std::filesystem::path path;
    std::vector<Color32> colors;
};

enum class PaletteImportMode {
    Create,
    Overwrite,
};

std::vector<Palette> load_saved_palettes();
bool validate_import_palette_file(const std::filesystem::path& source, Palette& palette, std::string& error);
std::optional<Palette> find_import_palette_conflict(const std::filesystem::path& source);
std::string suggest_import_palette_copy_name(const std::filesystem::path& source);
bool import_palette_file(const std::filesystem::path& source, PaletteImportMode mode, Palette& imported, std::string& error);
bool load_palette_file(const std::filesystem::path& path, Palette& palette, std::string& error);
bool delete_palette_file(const Palette& palette, std::string& error);
bool overwrite_palette_file(const Palette& palette, const std::vector<Color32>& colors, std::string& error);
bool save_palette_as_new(const std::string& name, const std::vector<Color32>& colors, Palette& saved, std::string& error);

} // namespace pixatto
