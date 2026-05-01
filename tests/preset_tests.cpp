#include "pixatto/preset.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("preset test failed");
    }
}

std::filesystem::path test_dir()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pixatto-preset-tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

pixatto::ProcessSettings sample_settings()
{
    pixatto::ProcessSettings settings;
    settings.pixel_size = 13;
    settings.block_color_mode = pixatto::BlockColorMode::Average;
    settings.use_palette = true;
    settings.preserve_transparency = true;
    settings.palette = {
        {12, 34, 56, 78},
        {240, 128, 64, 255},
    };
    settings.color_levels = 5;
    settings.reduction_max_colors = 17;
    settings.dither_mode = pixatto::DitherMode::Atkinson;
    settings.bayer_matrix_size = 16;
    settings.dither_amount = 0.35F;
    settings.blue_noise_seed = 42;
    settings.adjustments.brightness = 0.2F;
    settings.adjustments.contrast = -0.15F;
    settings.adjustments.gamma = 1.7F;
    settings.adjustments.input_black = 0.1F;
    settings.adjustments.input_white = 0.92F;
    settings.adjustments.output_black = 0.08F;
    settings.adjustments.output_white = 0.95F;
    settings.adjustments.saturation = 1.4F;
    settings.adjustments.tint = {200, 210, 220, 255};
    settings.adjustments.tint_strength = 0.45F;
    return settings;
}

void preset_round_trips_settings()
{
    const std::filesystem::path dir = test_dir();
    const pixatto::ProcessSettings settings = sample_settings();

    pixatto::Preset saved;
    std::string error;
    require(pixatto::save_preset_to_dir(dir, "Crunchy #4", settings, pixatto::PresetSaveMode::Create, saved, error));
    require(saved.name == "Crunchy #4");
    require(saved.settings == settings);

    pixatto::Preset loaded;
    require(pixatto::load_preset_file(saved.path, loaded, error));
    require(loaded.name == "Crunchy #4");
    require(loaded.settings == settings);
}

void create_refuses_existing_preset_until_overwrite()
{
    const std::filesystem::path dir = test_dir();
    pixatto::ProcessSettings first = sample_settings();
    pixatto::ProcessSettings second = first;
    second.pixel_size = 21;
    second.dither_mode = pixatto::DitherMode::BlueNoise;

    pixatto::Preset saved;
    std::string error;
    require(pixatto::save_preset_to_dir(dir, "Shared Name", first, pixatto::PresetSaveMode::Create, saved, error));
    require(pixatto::find_preset_conflict_in_dir(dir, "Shared Name").has_value());
    require(!pixatto::save_preset_to_dir(dir, "Shared Name", second, pixatto::PresetSaveMode::Create, saved, error));
    require(pixatto::save_preset_to_dir(dir, "Shared Name", second, pixatto::PresetSaveMode::Overwrite, saved, error));
    require(saved.settings == second);
}

void saved_presets_are_sorted_by_name()
{
    const std::filesystem::path dir = test_dir();
    const pixatto::ProcessSettings settings = sample_settings();
    pixatto::Preset saved;
    std::string error;

    require(pixatto::save_preset_to_dir(dir, "zeta", settings, pixatto::PresetSaveMode::Create, saved, error));
    require(pixatto::save_preset_to_dir(dir, "Alpha", settings, pixatto::PresetSaveMode::Create, saved, error));

    const std::vector<pixatto::Preset> presets = pixatto::load_presets_from_dir(dir);
    require(presets.size() == 2U);
    require(presets[0].name == "Alpha");
    require(presets[1].name == "zeta");
}

void preset_files_can_be_deleted()
{
    const std::filesystem::path dir = test_dir();
    pixatto::Preset saved;
    std::string error;
    require(pixatto::save_preset_to_dir(dir, "Temporary", sample_settings(), pixatto::PresetSaveMode::Create, saved, error));
    require(std::filesystem::exists(saved.path));
    require(pixatto::delete_preset_file(saved, error));
    require(!std::filesystem::exists(saved.path));
    require(pixatto::load_presets_from_dir(dir).empty());
}

void legacy_pixelizer_preset_version_key_still_loads()
{
    const std::filesystem::path dir = test_dir();
    const std::filesystem::path path = dir / "legacy.pxpreset";
    {
        std::ofstream output(path);
        output << "pixelizer_preset=1\n";
        output << "name=Legacy Look\n";
        output << "pixel_size=7\n";
    }

    pixatto::Preset loaded;
    std::string error;
    require(pixatto::load_preset_file(path, loaded, error));
    require(loaded.name == "Legacy Look");
    require(loaded.settings.pixel_size == 7);
}

} // namespace

int main()
{
    preset_round_trips_settings();
    create_refuses_existing_preset_until_overwrite();
    saved_presets_are_sorted_by_name();
    preset_files_can_be_deleted();
    legacy_pixelizer_preset_version_key_still_loads();
    return 0;
}
