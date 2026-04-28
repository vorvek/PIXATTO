#pragma once

#include "pixelizer/image.hpp"

#include <cstdint>
#include <vector>

namespace pixelizer {

enum class DitherMode {
    None,
    Bayer,
    BlueNoise,
    FloydSteinberg,
    JarvisJudiceNinke,
    Atkinson,
    Riemersma,
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
    int pixel_size = 8;
    BlockColorMode block_color_mode = BlockColorMode::WeightedAverage;
    bool use_palette = false;
    std::vector<Color32> palette;
    int color_levels = 8;
    int reduction_max_colors = 0;
    DitherMode dither_mode = DitherMode::None;
    int bayer_matrix_size = 8;
    float dither_amount = 0.0F;
    std::uint32_t blue_noise_seed = 1337;
    Adjustments adjustments;

    bool operator==(const ProcessSettings&) const = default;
};

Image process_image(const Image& source, const ProcessSettings& settings);

} // namespace pixelizer
