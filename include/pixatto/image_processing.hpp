#pragma once

#include "pixatto/image.hpp"

#include <cstdint>
#include <vector>

namespace pixatto {

enum class DitherMode {
    None,
    Bayer,
    BlueNoise,
    FloydSteinberg,
    FalseFloydSteinberg,
    FilterLite,
    ZhigangFan,
    ShiauFan,
    JarvisJudiceNinke,
    Atkinson,
    Stucki,
    Burkes,
    Sierra,
    TwoRowSierra,
    Riemersma,
    ClusterDot4x4,
    ClusterDot8x8,
    Horizontal2x2,
    Horizontal8x1,
    Horizontal12x4,
    Vertical2x2,
    Vertical1x8,
    Vertical4x12,
    Diagonal5x5,
};

enum class BlockColorMode {
    Average,
    WeightedAverage,
};

struct Adjustments {
    float brightness = 0.0F;
    float contrast = 0.0F;
    float gamma = 1.0F;
    float input_black = 0.0F;
    float input_white = 1.0F;
    float output_black = 0.0F;
    float output_white = 1.0F;
    float saturation = 1.0F;
    Color32 tint = {255, 255, 255, 255};
    float tint_strength = 0.0F;

    bool operator==(const Adjustments&) const = default;
};

struct ProcessSettings {
    int pixel_size = 2;
    BlockColorMode block_color_mode = BlockColorMode::WeightedAverage;
    bool use_palette = false;
    bool preserve_transparency = false;
    std::vector<Color32> palette;
    int color_levels = 8;
    int reduction_max_colors = 0;
    DitherMode dither_mode = DitherMode::None;
    int bayer_matrix_size = 8;
    // Stored as 0..1, shown as 0..100%. Each dither mode maps this percentage
    // to its own strength control in image_processing.cpp.
    float dither_amount = 0.0F;
    std::uint32_t blue_noise_seed = 1337;
    Adjustments adjustments;

    bool operator==(const ProcessSettings&) const = default;
};

Image process_image(const Image& source, const ProcessSettings& settings);
Image collapse_pixel_blocks(const Image& image, int pixel_size);

} // namespace pixatto
