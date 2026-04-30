#include "pixelizer/image_processing.hpp"

#include <stdexcept>
#include <vector>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("image processing test failed");
    }
}

pixelizer::Image single_pixel(pixelizer::Color32 color)
{
    return {1, 1, {color.r, color.g, color.b, color.a}};
}

void require_same_image(const pixelizer::Image& lhs, const pixelizer::Image& rhs)
{
    require(lhs.width == rhs.width);
    require(lhs.height == rhs.height);
    require(lhs.rgba == rhs.rgba);
}

void one_pixel_fast_path_matches_one_pixel_block_path()
{
    const std::vector<pixelizer::Color32> samples = {
        {255, 0, 0, 255},
        {34, 128, 220, 127},
        {12, 20, 32, 0},
    };

    std::vector<pixelizer::ProcessSettings> settings_cases;

    pixelizer::ProcessSettings adjusted;
    adjusted.color_levels = 5;
    adjusted.adjustments.brightness = 0.1F;
    adjusted.adjustments.contrast = -0.2F;
    adjusted.adjustments.gamma = 1.4F;
    adjusted.adjustments.tint = {240, 220, 180, 255};
    adjusted.adjustments.tint_strength = 0.35F;
    settings_cases.push_back(adjusted);

    pixelizer::ProcessSettings saturated = adjusted;
    saturated.adjustments.saturation = 0.45F;
    saturated.preserve_transparency = true;
    settings_cases.push_back(saturated);

    pixelizer::ProcessSettings paletted;
    paletted.use_palette = true;
    paletted.palette = {
        {0, 0, 0, 255},
        {255, 255, 255, 255},
        {64, 128, 224, 255},
    };
    settings_cases.push_back(paletted);

    pixelizer::ProcessSettings ordered;
    ordered.dither_mode = pixelizer::DitherMode::Bayer;
    ordered.dither_amount = 0.7F;
    ordered.bayer_matrix_size = 16;
    settings_cases.push_back(ordered);

    for (const pixelizer::Color32 sample : samples) {
        const pixelizer::Image source = single_pixel(sample);
        for (pixelizer::ProcessSettings settings : settings_cases) {
            settings.pixel_size = 1;
            const pixelizer::Image fast = pixelizer::process_image(source, settings);

            settings.pixel_size = 2;
            const pixelizer::Image block = pixelizer::process_image(source, settings);

            require_same_image(fast, block);
        }
    }
}

void pixel_size_one_writes_each_source_pixel()
{
    const pixelizer::Image source = {
        3,
        2,
        {
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            130, 130, 130, 255,
            12, 34, 56, 0,
            255, 255, 255, 127,
        },
    };

    pixelizer::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.color_levels = 2;
    settings.preserve_transparency = true;

    const pixelizer::Image result = pixelizer::process_image(source, settings);
    const std::vector<unsigned char> expected = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };

    require(result.width == source.width);
    require(result.height == source.height);
    require(result.rgba == expected);
}

void paletted_riemersma_outputs_palette_colors()
{
    const pixelizer::Image source = {
        4,
        3,
        {
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            255, 255, 255, 255,
            40, 80, 120, 255,
            90, 130, 170, 255,
            140, 180, 220, 255,
            12, 20, 32, 255,
            220, 120, 20, 255,
            180, 40, 160, 255,
            64, 64, 64, 255,
            8, 220, 140, 255,
        },
    };

    pixelizer::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.use_palette = true;
    settings.palette = {
        {0, 0, 0, 255},
        {255, 255, 255, 255},
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
    };
    settings.dither_mode = pixelizer::DitherMode::Riemersma;
    settings.dither_amount = 0.5F;

    const pixelizer::Image result = pixelizer::process_image(source, settings);
    require(result.width == source.width);
    require(result.height == source.height);

    for (std::size_t index = 0; index < result.rgba.size(); index += 4U) {
        const pixelizer::Color32 color = {
            result.rgba[index],
            result.rgba[index + 1U],
            result.rgba[index + 2U],
            result.rgba[index + 3U],
        };

        bool found = false;
        for (const pixelizer::Color32 palette_color : settings.palette) {
            found = found || color == palette_color;
        }
        require(found);
    }
}

void added_dither_modes_create_two_tone_patterns()
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(12 * 12 * 4), 255);
    for (std::size_t index = 0; index < pixels.size(); index += 4U) {
        pixels[index] = 128;
        pixels[index + 1U] = 128;
        pixels[index + 2U] = 128;
        pixels[index + 3U] = 255;
    }

    const pixelizer::Image source = {12, 12, pixels};
    const std::vector<pixelizer::DitherMode> modes = {
        pixelizer::DitherMode::FalseFloydSteinberg,
        pixelizer::DitherMode::FilterLite,
        pixelizer::DitherMode::ZhigangFan,
        pixelizer::DitherMode::ShiauFan,
        pixelizer::DitherMode::Stucki,
        pixelizer::DitherMode::Burkes,
        pixelizer::DitherMode::Sierra,
        pixelizer::DitherMode::TwoRowSierra,
        pixelizer::DitherMode::ClusterDot4x4,
        pixelizer::DitherMode::ClusterDot8x8,
        pixelizer::DitherMode::Horizontal2x2,
        pixelizer::DitherMode::Horizontal8x1,
        pixelizer::DitherMode::Horizontal12x4,
        pixelizer::DitherMode::Vertical2x2,
        pixelizer::DitherMode::Vertical1x8,
        pixelizer::DitherMode::Vertical4x12,
        pixelizer::DitherMode::Diagonal5x5,
    };

    pixelizer::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.color_levels = 2;
    settings.dither_amount = 1.0F;

    for (const pixelizer::DitherMode mode : modes) {
        settings.dither_mode = mode;
        const pixelizer::Image result = pixelizer::process_image(source, settings);
        require(result.width == source.width);
        require(result.height == source.height);

        bool has_black = false;
        bool has_white = false;
        for (std::size_t index = 0; index < result.rgba.size(); index += 4U) {
            const bool black = result.rgba[index] == 0 && result.rgba[index + 1U] == 0 && result.rgba[index + 2U] == 0;
            const bool white = result.rgba[index] == 255 && result.rgba[index + 1U] == 255 && result.rgba[index + 2U] == 255;
            has_black = has_black || black;
            has_white = has_white || white;
        }

        require(has_black);
        require(has_white);
    }
}

} // namespace

int main()
{
    one_pixel_fast_path_matches_one_pixel_block_path();
    pixel_size_one_writes_each_source_pixel();
    paletted_riemersma_outputs_palette_colors();
    added_dither_modes_create_two_tone_patterns();
    return 0;
}
