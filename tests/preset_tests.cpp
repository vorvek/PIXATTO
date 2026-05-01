#include "pixelizer/preset.hpp"

#include <filesystem>
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
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pixelizer-preset-tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

pixelizer::ProcessSettings sample_settings()
{
    pixelizer::ProcessSettings settings;
    settings.pixel_size = 13;
    settings.block_color_mode = pixelizer::BlockColorMode::Average;
    settings.use_palette = true;
    settings.preserve_transparency = true;
    settings.palette = {
        {12, 34, 56, 78},
        {240, 128, 64, 255},
    };
    settings.color_levels = 5;
    settings.reduction_max_colors = 17;
    settings.dither_mode = pixelizer::DitherMode::Atkinson;
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
    const pixelizer::ProcessSettings settings = sample_settings();

    pixelizer::Preset saved;
    std::string error;
    require(pixelizer::save_preset_to_dir(dir, "Crunchy #4", settings, pixelizer::PresetSaveMode::Create, saved, error));
    require(saved.name == "Crunchy #4");
    require(saved.settings == settings);

    pixelizer::Preset loaded;
    require(pixelizer::load_preset_file(saved.path, loaded, error));
    require(loaded.name == "Crunchy #4");
    require(loaded.settings == settings);
}

void create_refuses_existing_preset_until_overwrite()
{
    const std::filesystem::path dir = test_dir();
    pixelizer::ProcessSettings first = sample_settings();
    pixelizer::ProcessSettings second = first;
    second.pixel_size = 21;
    second.dither_mode = pixelizer::DitherMode::BlueNoise;

    pixelizer::Preset saved;
    std::string error;
    require(pixelizer::save_preset_to_dir(dir, "Shared Name", first, pixelizer::PresetSaveMode::Create, saved, error));
    require(pixelizer::find_preset_conflict_in_dir(dir, "Shared Name").has_value());
    require(!pixelizer::save_preset_to_dir(dir, "Shared Name", second, pixelizer::PresetSaveMode::Create, saved, error));
    require(pixelizer::save_preset_to_dir(dir, "Shared Name", second, pixelizer::PresetSaveMode::Overwrite, saved, error));
    require(saved.settings == second);
}

void saved_presets_are_sorted_by_name()
{
    const std::filesystem::path dir = test_dir();
    const pixelizer::ProcessSettings settings = sample_settings();
    pixelizer::Preset saved;
    std::string error;

    require(pixelizer::save_preset_to_dir(dir, "zeta", settings, pixelizer::PresetSaveMode::Create, saved, error));
    require(pixelizer::save_preset_to_dir(dir, "Alpha", settings, pixelizer::PresetSaveMode::Create, saved, error));

    const std::vector<pixelizer::Preset> presets = pixelizer::load_presets_from_dir(dir);
    require(presets.size() == 2U);
    require(presets[0].name == "Alpha");
    require(presets[1].name == "zeta");
}

void preset_files_can_be_deleted()
{
    const std::filesystem::path dir = test_dir();
    pixelizer::Preset saved;
    std::string error;
    require(pixelizer::save_preset_to_dir(dir, "Temporary", sample_settings(), pixelizer::PresetSaveMode::Create, saved, error));
    require(std::filesystem::exists(saved.path));
    require(pixelizer::delete_preset_file(saved, error));
    require(!std::filesystem::exists(saved.path));
    require(pixelizer::load_presets_from_dir(dir).empty());
}

} // namespace

int main()
{
    preset_round_trips_settings();
    create_refuses_existing_preset_until_overwrite();
    saved_presets_are_sorted_by_name();
    preset_files_can_be_deleted();
    return 0;
}
