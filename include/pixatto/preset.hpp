#pragma once

#include "pixatto/image_processing.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pixatto {

struct Preset {
    std::string name;
    std::filesystem::path path;
    ProcessSettings settings;
};

enum class PresetSaveMode {
    Create,
    Overwrite,
};

std::string normalize_preset_name(const std::string& name);
std::vector<Preset> load_saved_presets();
std::vector<Preset> load_presets_from_dir(const std::filesystem::path& dir);
bool load_preset_file(const std::filesystem::path& path, Preset& preset, std::string& error);
std::optional<Preset> find_preset_conflict(const std::string& name);
std::optional<Preset> find_preset_conflict_in_dir(const std::filesystem::path& dir, const std::string& name);
bool delete_preset_file(const Preset& preset, std::string& error);
bool save_preset_as(const std::string& name, const ProcessSettings& settings, PresetSaveMode mode, Preset& saved, std::string& error);
bool save_preset_to_dir(
    const std::filesystem::path& dir,
    const std::string& name,
    const ProcessSettings& settings,
    PresetSaveMode mode,
    Preset& saved,
    std::string& error);

} // namespace pixatto
