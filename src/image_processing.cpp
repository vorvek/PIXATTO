#include "pixelizer/image_processing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace pixelizer {
namespace {

constexpr float kEpsilon = 0.000001F;
constexpr float kTransparencyThreshold = 0.5F;
constexpr int kBlueNoiseSize = 32;
constexpr int kBlueNoiseCellCount = kBlueNoiseSize * kBlueNoiseSize;
constexpr std::size_t kChannelValueCount = 256;

struct Vec3 {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

struct LabColor {
    float l = 0.0F;
    float a = 0.0F;
    float b = 0.0F;
};

struct PaletteEntry {
    Color32 color;
    LabColor lab;
};

struct BlockColor {
    Vec3 srgb;
    float alpha = 1.0F;
    int area = 0;
};

struct AdjustmentContext {
    std::array<float, kChannelValueCount> pre_adjusted = {};
    std::array<float, kChannelValueCount> linear_r = {};
    std::array<float, kChannelValueCount> linear_g = {};
    std::array<float, kChannelValueCount> linear_b = {};
    Vec3 tint = {1.0F, 1.0F, 1.0F};
    float tint_strength = 0.0F;
    float output_black = 0.0F;
    float output_white = 1.0F;
    float saturation = 1.0F;
    bool channel_independent = true;
};

struct WeightKernel {
    int width = 0;
    int height = 0;
    std::vector<float> weights;
    double weight_sum = 0.0;
};

struct QuantPoint {
    std::uint32_t key = 0;
    double weight = 0.0;
    double linear_r_sum = 0.0;
    double linear_g_sum = 0.0;
    double linear_b_sum = 0.0;
    Color32 representative;
    LabColor lab;
    bool needs_rebuild = false;
};

struct QuantBox {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct DiffusionStep {
    int dx = 0;
    int dy = 0;
    float weight = 0.0F;
};

float clamp01(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

std::uint8_t to_byte(float value)
{
    return static_cast<std::uint8_t>(std::lround(clamp01(value) * 255.0F));
}

float from_byte(std::uint8_t value)
{
    return static_cast<float>(value) / 255.0F;
}

bool has_valid_rgba_size(const Image& image)
{
    if (image.width <= 0 || image.height <= 0) {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        return false;
    }

    const std::size_t pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        return false;
    }

    return image.rgba.size() == pixels * 4U;
}

float srgb_to_linear(float value)
{
    value = clamp01(value);
    if (value <= 0.04045F) {
        return value / 12.92F;
    }
    return std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(float value)
{
    value = clamp01(value);
    if (value <= 0.0031308F) {
        return value * 12.92F;
    }
    return 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

Vec3 to_linear(Vec3 srgb)
{
    return {
        srgb_to_linear(srgb.r),
        srgb_to_linear(srgb.g),
        srgb_to_linear(srgb.b),
    };
}

Vec3 to_srgb(Vec3 linear)
{
    return {
        linear_to_srgb(linear.r),
        linear_to_srgb(linear.g),
        linear_to_srgb(linear.b),
    };
}

Color32 to_color32(Vec3 color, float alpha)
{
    return {
        to_byte(color.r),
        to_byte(color.g),
        to_byte(color.b),
        to_byte(alpha),
    };
}

Color32 transparent_color()
{
    return {0, 0, 0, 0};
}

Vec3 from_color32(Color32 color)
{
    return {
        from_byte(color.r),
        from_byte(color.g),
        from_byte(color.b),
    };
}

bool should_write_transparent(float alpha, const ProcessSettings& settings)
{
    return settings.preserve_transparency && alpha < kTransparencyThreshold;
}

float output_alpha(float alpha, const ProcessSettings& settings)
{
    return settings.preserve_transparency ? 1.0F : alpha;
}

float apply_levels_channel(float value, float input_black, float input_white, float output_black, float output_white)
{
    const float in_black = clamp01(input_black);
    const float in_white = clamp01(input_white);
    const float out_black = clamp01(output_black);
    const float out_white = clamp01(output_white);
    const float normalized = clamp01((value - in_black) / std::max(kEpsilon, in_white - in_black));
    return out_black + normalized * (out_white - out_black);
}

float apply_pre_adjustments_channel(float value, const Adjustments& adjustments)
{
    value = apply_levels_channel(value, adjustments.input_black, adjustments.input_white, 0.0F, 1.0F);

    const float brightness = std::clamp(adjustments.brightness, -1.0F, 1.0F);
    value = clamp01(value + brightness);

    const float contrast = std::clamp(adjustments.contrast, -1.0F, 1.0F);
    const float contrast_factor = contrast >= 0.0F ? 1.0F + contrast * 2.0F : 1.0F + contrast;
    value = clamp01((value - 0.5F) * contrast_factor + 0.5F);

    const float gamma = std::max(0.05F, adjustments.gamma);
    return std::pow(value, 1.0F / gamma);
}

float apply_tint_and_output_channel(float value, float tint, const AdjustmentContext& context)
{
    value = clamp01(value * (1.0F - context.tint_strength) + (value * tint) * context.tint_strength);
    return apply_levels_channel(value, 0.0F, 1.0F, context.output_black, context.output_white);
}

AdjustmentContext build_adjustment_context(const Adjustments& adjustments)
{
    AdjustmentContext context;
    context.tint = from_color32(adjustments.tint);
    context.tint_strength = clamp01(adjustments.tint_strength);
    context.output_black = clamp01(adjustments.output_black);
    context.output_white = clamp01(adjustments.output_white);
    context.saturation = std::max(0.0F, adjustments.saturation);
    context.channel_independent = context.saturation == 1.0F;

    for (std::size_t index = 0; index < kChannelValueCount; ++index) {
        const float pre_adjusted = apply_pre_adjustments_channel(from_byte(static_cast<std::uint8_t>(index)), adjustments);
        context.pre_adjusted[index] = pre_adjusted;

        context.linear_r[index] = srgb_to_linear(apply_tint_and_output_channel(pre_adjusted, context.tint.r, context));
        context.linear_g[index] = srgb_to_linear(apply_tint_and_output_channel(pre_adjusted, context.tint.g, context));
        context.linear_b[index] = srgb_to_linear(apply_tint_and_output_channel(pre_adjusted, context.tint.b, context));
    }

    return context;
}

Vec3 adjusted_linear(std::uint8_t r, std::uint8_t g, std::uint8_t b, const AdjustmentContext& context)
{
    if (context.channel_independent) {
        return {
            context.linear_r[r],
            context.linear_g[g],
            context.linear_b[b],
        };
    }

    Vec3 color = {
        context.pre_adjusted[r],
        context.pre_adjusted[g],
        context.pre_adjusted[b],
    };

    const float luma = color.r * 0.2126F + color.g * 0.7152F + color.b * 0.0722F;
    color.r = clamp01(luma + (color.r - luma) * context.saturation);
    color.g = clamp01(luma + (color.g - luma) * context.saturation);
    color.b = clamp01(luma + (color.b - luma) * context.saturation);

    color.r = apply_tint_and_output_channel(color.r, context.tint.r, context);
    color.g = apply_tint_and_output_channel(color.g, context.tint.g, context);
    color.b = apply_tint_and_output_channel(color.b, context.tint.b, context);

    return to_linear(color);
}

WeightKernel build_weight_kernel(int width, int height, BlockColorMode mode)
{
    WeightKernel kernel;
    kernel.width = width;
    kernel.height = height;
    kernel.weights.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    if (mode == BlockColorMode::Average) {
        kernel.weights.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 1.0F);
        kernel.weight_sum = static_cast<double>(width) * static_cast<double>(height);
        return kernel;
    }

    const float block_width = static_cast<float>(width);
    const float block_height = static_cast<float>(height);
    const float center_x = (block_width - 1.0F) * 0.5F;
    const float center_y = (block_height - 1.0F) * 0.5F;
    const float radius_x = std::max(1.0F, block_width * 0.5F);
    const float radius_y = std::max(1.0F, block_height * 0.5F);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dx = (static_cast<float>(x) - center_x) / radius_x;
            const float dy = (static_cast<float>(y) - center_y) / radius_y;
            const float weight = std::exp(-(dx * dx + dy * dy) * 1.15F);
            kernel.weights.push_back(weight);
            kernel.weight_sum += weight;
        }
    }

    return kernel;
}

BlockColor choose_block_color(
    const Image& source,
    int start_x,
    int start_y,
    const WeightKernel& kernel,
    const AdjustmentContext& adjustments)
{
    double linear_r_sum = 0.0;
    double linear_g_sum = 0.0;
    double linear_b_sum = 0.0;
    double weighted_alpha = 0.0;

    std::size_t weight_index = 0;
    for (int local_y = 0; local_y < kernel.height; ++local_y) {
        const std::uint8_t* pixel = source.rgba.data()
            + (static_cast<std::size_t>(start_y + local_y) * static_cast<std::size_t>(source.width)
                + static_cast<std::size_t>(start_x))
                * 4U;

        for (int local_x = 0; local_x < kernel.width; ++local_x) {
            const float alpha = from_byte(pixel[3U]);
            const float weight = kernel.weights[weight_index++];
            const double color_weight = static_cast<double>(weight) * static_cast<double>(alpha);
            const Vec3 linear = adjusted_linear(pixel[0U], pixel[1U], pixel[2U], adjustments);

            linear_r_sum += linear.r * color_weight;
            linear_g_sum += linear.g * color_weight;
            linear_b_sum += linear.b * color_weight;
            weighted_alpha += static_cast<double>(alpha) * static_cast<double>(weight);
            pixel += 4U;
        }
    }

    const float output_alpha = kernel.weight_sum > 0.0 ? static_cast<float>(weighted_alpha / kernel.weight_sum) : 0.0F;
    if (weighted_alpha <= kEpsilon) {
        return {{0.0F, 0.0F, 0.0F}, output_alpha, kernel.width * kernel.height};
    }

    const Vec3 averaged = to_srgb({
        static_cast<float>(linear_r_sum / weighted_alpha),
        static_cast<float>(linear_g_sum / weighted_alpha),
        static_cast<float>(linear_b_sum / weighted_alpha),
    });
    return {averaged, output_alpha, kernel.width * kernel.height};
}

LabColor to_oklab(Vec3 srgb)
{
    const Vec3 linear = to_linear(srgb);

    const float l = 0.4122214708F * linear.r + 0.5363325363F * linear.g + 0.0514459929F * linear.b;
    const float m = 0.2119034982F * linear.r + 0.6806995451F * linear.g + 0.1073969566F * linear.b;
    const float s = 0.0883024619F * linear.r + 0.2817188376F * linear.g + 0.6299787005F * linear.b;

    const float l_root = std::cbrt(std::max(0.0F, l));
    const float m_root = std::cbrt(std::max(0.0F, m));
    const float s_root = std::cbrt(std::max(0.0F, s));

    return {
        0.2104542553F * l_root + 0.7936177850F * m_root - 0.0040720468F * s_root,
        1.9779984951F * l_root - 2.4285922050F * m_root + 0.4505937099F * s_root,
        0.0259040371F * l_root + 0.7827717662F * m_root - 0.8086757660F * s_root,
    };
}

float distance_squared(LabColor lhs, LabColor rhs)
{
    const float dl = lhs.l - rhs.l;
    const float da = lhs.a - rhs.a;
    const float db = lhs.b - rhs.b;
    return dl * dl + da * da + db * db;
}

std::vector<PaletteEntry> build_palette_entries(const std::vector<Color32>& palette)
{
    std::vector<PaletteEntry> entries;
    entries.reserve(palette.size());
    for (const Color32 color : palette) {
        entries.push_back({color, to_oklab(from_color32(color))});
    }
    return entries;
}

Color32 nearest_palette_color(Vec3 color, const std::vector<PaletteEntry>& palette, float alpha)
{
    if (palette.empty()) {
        return to_color32(color, alpha);
    }

    const LabColor target = to_oklab(color);
    float best_distance = std::numeric_limits<float>::max();
    Color32 best = palette.front().color;

    for (const PaletteEntry& candidate : palette) {
        const float distance = distance_squared(target, candidate.lab);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate.color;
        }
    }

    best.a = to_byte(alpha);
    return best;
}

std::uint32_t hash_u32(std::uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

int next_open_cell(const std::array<int, kBlueNoiseCellCount>& ranks, int start, std::uint32_t step)
{
    step |= 1U;
    int candidate = start & (kBlueNoiseCellCount - 1);
    while (ranks[static_cast<std::size_t>(candidate)] >= 0) {
        candidate = (candidate + static_cast<int>(step)) & (kBlueNoiseCellCount - 1);
    }
    return candidate;
}

float toroidal_distance_squared(int lhs, int rhs)
{
    const int lhs_x = lhs & (kBlueNoiseSize - 1);
    const int lhs_y = lhs / kBlueNoiseSize;
    const int rhs_x = rhs & (kBlueNoiseSize - 1);
    const int rhs_y = rhs / kBlueNoiseSize;

    int dx = std::abs(lhs_x - rhs_x);
    int dy = std::abs(lhs_y - rhs_y);
    dx = std::min(dx, kBlueNoiseSize - dx);
    dy = std::min(dy, kBlueNoiseSize - dy);
    return static_cast<float>(dx * dx + dy * dy);
}

float nearest_point_distance_squared(const std::vector<int>& points, int candidate)
{
    float best = std::numeric_limits<float>::max();
    for (const int point : points) {
        best = std::min(best, toroidal_distance_squared(candidate, point));
    }
    return best;
}

std::array<float, kBlueNoiseCellCount> build_blue_noise_thresholds()
{
    std::array<int, kBlueNoiseCellCount> ranks;
    ranks.fill(-1);

    std::vector<int> points;
    points.reserve(kBlueNoiseCellCount);

    for (int rank = 0; rank < kBlueNoiseCellCount; ++rank) {
        int best_cell = 0;
        float best_distance = -1.0F;
        std::uint32_t best_hash = 0;

        const int candidate_count = rank == 0 ? 1 : 48;
        for (int candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
            const std::uint32_t candidate_hash = hash_u32(0x6d2b79f5U
                ^ (static_cast<std::uint32_t>(rank) * 0x9e3779b9U)
                ^ (static_cast<std::uint32_t>(candidate_index) * 0x85ebca6bU));
            const int candidate = next_open_cell(ranks, static_cast<int>(candidate_hash), candidate_hash >> 10U);
            const float distance = points.empty() ? std::numeric_limits<float>::max() : nearest_point_distance_squared(points, candidate);

            if (distance > best_distance || (distance == best_distance && candidate_hash > best_hash)) {
                best_cell = candidate;
                best_distance = distance;
                best_hash = candidate_hash;
            }
        }

        ranks[static_cast<std::size_t>(best_cell)] = rank;
        points.push_back(best_cell);
    }

    std::array<float, kBlueNoiseCellCount> thresholds;
    for (std::size_t index = 0; index < thresholds.size(); ++index) {
        thresholds[index] = (static_cast<float>(ranks[index]) + 0.5F) / static_cast<float>(kBlueNoiseCellCount) - 0.5F;
    }
    return thresholds;
}

float blue_noise_threshold(int x, int y, std::uint32_t seed)
{
    static const std::array<float, kBlueNoiseCellCount> thresholds = build_blue_noise_thresholds();

    const std::uint32_t offset = hash_u32(seed);
    const int offset_x = static_cast<int>(offset) & (kBlueNoiseSize - 1);
    const int offset_y = static_cast<int>(offset >> 8U) & (kBlueNoiseSize - 1);
    const int wrapped_x = (x + offset_x) & (kBlueNoiseSize - 1);
    const int wrapped_y = (y + offset_y) & (kBlueNoiseSize - 1);
    return thresholds[static_cast<std::size_t>(wrapped_y * kBlueNoiseSize + wrapped_x)];
}

int normalized_bayer_matrix_size(int size)
{
    if (size <= 2) {
        return 2;
    }
    if (size <= 4) {
        return 4;
    }
    if (size <= 8) {
        return 8;
    }
    return 16;
}

float bayer_threshold(int x, int y, int requested_size)
{
    static constexpr std::array<std::array<int, 2>, 2> matrix2 = {{
        {{0, 2}},
        {{3, 1}},
    }};

    const int size = normalized_bayer_matrix_size(requested_size);
    const int wrapped_x = x & (size - 1);
    const int wrapped_y = y & (size - 1);

    int index = 0;
    int scale = 1;
    for (int step = size; step > 1; step /= 2) {
        const int half = step / 2;
        const int quadrant_x = (wrapped_x / half) & 1;
        const int quadrant_y = (wrapped_y / half) & 1;
        index += scale * matrix2[static_cast<std::size_t>(quadrant_y)][static_cast<std::size_t>(quadrant_x)];
        scale *= 4;
    }

    const float denominator = static_cast<float>(size * size);
    return (static_cast<float>(index) + 0.5F) / denominator - 0.5F;
}

float normalized_dither_amount(float amount)
{
    if (amount > 1.0F) {
        amount /= 100.0F;
    }
    return clamp01(amount);
}

Vec3 apply_dither(Vec3 color, int block_x, int block_y, const ProcessSettings& settings)
{
    const float amount = normalized_dither_amount(settings.dither_amount);
    if (settings.dither_mode == DitherMode::None
        || settings.dither_mode == DitherMode::FloydSteinberg
        || settings.dither_mode == DitherMode::JarvisJudiceNinke
        || settings.dither_mode == DitherMode::Atkinson
        || settings.dither_mode == DitherMode::Riemersma
        || amount <= 0.0F) {
        return color;
    }

    float threshold = 0.0F;
    if (settings.dither_mode == DitherMode::Bayer) {
        threshold = bayer_threshold(block_x, block_y, settings.bayer_matrix_size);
    } else if (settings.dither_mode == DitherMode::BlueNoise) {
        threshold = blue_noise_threshold(block_x, block_y, settings.blue_noise_seed);
    }

    // Ordered dithers use the percentage as threshold amplitude: 100% applies
    // the full Bayer or blue-noise cell offset before palette/color reduction,
    // while smaller values fade that ordered pattern toward no offset.
    constexpr float kDitherRange = 0.35F;
    const float offset = threshold * amount * kDitherRange;
    return {
        clamp01(color.r + offset),
        clamp01(color.g + offset),
        clamp01(color.b + offset),
    };
}

Color32 reduce_color(Vec3 color, int levels, float alpha)
{
    levels = std::clamp(levels, 2, 64);
    const float max_level = static_cast<float>(levels - 1);
    color.r = std::round(clamp01(color.r) * max_level) / max_level;
    color.g = std::round(clamp01(color.g) * max_level) / max_level;
    color.b = std::round(clamp01(color.b) * max_level) / max_level;
    return to_color32(color, alpha);
}

Color32 quantize_color(Vec3 color, float alpha, const std::vector<PaletteEntry>& palette_entries, const ProcessSettings& settings)
{
    if (should_write_transparent(alpha, settings)) {
        return transparent_color();
    }

    color.r = clamp01(color.r);
    color.g = clamp01(color.g);
    color.b = clamp01(color.b);

    return !palette_entries.empty()
        ? nearest_palette_color(color, palette_entries, output_alpha(alpha, settings))
        : reduce_color(color, settings.color_levels, output_alpha(alpha, settings));
}

std::uint32_t quant_key(Color32 color)
{
    const std::uint32_t r = color.r >> 3U;
    const std::uint32_t g = color.g >> 3U;
    const std::uint32_t b = color.b >> 3U;
    return (r << 10U) | (g << 5U) | b;
}

std::vector<QuantPoint> build_quant_points(const std::vector<BlockColor>& blocks, const ProcessSettings& settings)
{
    std::vector<QuantPoint> points;
    points.reserve(blocks.size());

    for (const BlockColor& block : blocks) {
        if (block.area <= 0 || block.alpha <= 0.0F || should_write_transparent(block.alpha, settings)) {
            continue;
        }

        const Color32 representative = to_color32(block.srgb, 1.0F);
        const Vec3 linear = to_linear(block.srgb);
        const double weight = static_cast<double>(block.area) * static_cast<double>(std::max(block.alpha, 1.0F / 255.0F));

        QuantPoint point;
        point.key = quant_key(representative);
        point.weight = weight;
        point.linear_r_sum = linear.r * weight;
        point.linear_g_sum = linear.g * weight;
        point.linear_b_sum = linear.b * weight;
        point.representative = representative;
        point.lab = to_oklab(block.srgb);
        points.push_back(point);
    }

    std::stable_sort(points.begin(), points.end(), [](const QuantPoint& lhs, const QuantPoint& rhs) {
        return lhs.key < rhs.key;
    });

    std::vector<QuantPoint> aggregated;
    aggregated.reserve(points.size());
    for (const QuantPoint& point : points) {
        if (!aggregated.empty() && aggregated.back().key == point.key) {
            QuantPoint& merged = aggregated.back();
            merged.weight += point.weight;
            merged.linear_r_sum += point.linear_r_sum;
            merged.linear_g_sum += point.linear_g_sum;
            merged.linear_b_sum += point.linear_b_sum;
            merged.needs_rebuild = true;
        } else {
            aggregated.push_back(point);
        }
    }

    for (QuantPoint& point : aggregated) {
        if (!point.needs_rebuild) {
            continue;
        }

        const Vec3 merged_srgb = to_srgb({
            static_cast<float>(point.linear_r_sum / point.weight),
            static_cast<float>(point.linear_g_sum / point.weight),
            static_cast<float>(point.linear_b_sum / point.weight),
        });
        point.representative = to_color32(merged_srgb, 1.0F);
        point.lab = to_oklab(merged_srgb);
    }

    return aggregated;
}

double box_weight(const std::vector<QuantPoint>& points, QuantBox box)
{
    double weight = 0.0;
    for (std::size_t index = box.begin; index < box.end; ++index) {
        weight += points[index].weight;
    }
    return weight;
}

std::array<float, 3> box_ranges(const std::vector<QuantPoint>& points, QuantBox box)
{
    float min_l = std::numeric_limits<float>::max();
    float min_a = std::numeric_limits<float>::max();
    float min_b = std::numeric_limits<float>::max();
    float max_l = std::numeric_limits<float>::lowest();
    float max_a = std::numeric_limits<float>::lowest();
    float max_b = std::numeric_limits<float>::lowest();

    for (std::size_t index = box.begin; index < box.end; ++index) {
        const LabColor lab = points[index].lab;
        min_l = std::min(min_l, lab.l);
        min_a = std::min(min_a, lab.a);
        min_b = std::min(min_b, lab.b);
        max_l = std::max(max_l, lab.l);
        max_a = std::max(max_a, lab.a);
        max_b = std::max(max_b, lab.b);
    }

    return {max_l - min_l, max_a - min_a, max_b - min_b};
}

double split_score(const std::vector<QuantPoint>& points, QuantBox box)
{
    if (box.end <= box.begin + 1U) {
        return -1.0;
    }

    const auto ranges = box_ranges(points, box);
    return static_cast<double>(*std::max_element(ranges.begin(), ranges.end())) * box_weight(points, box);
}

int dominant_axis(const std::vector<QuantPoint>& points, QuantBox box)
{
    const auto ranges = box_ranges(points, box);
    return static_cast<int>(std::distance(ranges.begin(), std::max_element(ranges.begin(), ranges.end())));
}

float axis_value(const QuantPoint& point, int axis)
{
    if (axis == 0) {
        return point.lab.l;
    }
    if (axis == 1) {
        return point.lab.a;
    }
    return point.lab.b;
}

bool split_box(std::vector<QuantPoint>& points, QuantBox box, QuantBox& left, QuantBox& right)
{
    if (box.end <= box.begin + 1U) {
        return false;
    }

    const int axis = dominant_axis(points, box);
    std::stable_sort(points.begin() + static_cast<std::ptrdiff_t>(box.begin),
                     points.begin() + static_cast<std::ptrdiff_t>(box.end),
                     [axis](const QuantPoint& lhs, const QuantPoint& rhs) {
                         const float lhs_value = axis_value(lhs, axis);
                         const float rhs_value = axis_value(rhs, axis);
                         if (lhs_value == rhs_value) {
                             return lhs.key < rhs.key;
                         }
                         return lhs_value < rhs_value;
                     });

    const double half_weight = box_weight(points, box) * 0.5;
    double running_weight = 0.0;
    std::size_t split = box.begin + 1U;
    for (std::size_t index = box.begin; index < box.end; ++index) {
        running_weight += points[index].weight;
        if (running_weight >= half_weight) {
            split = std::min(index + 1U, box.end - 1U);
            break;
        }
    }

    left = {box.begin, split};
    right = {split, box.end};
    return left.begin < left.end && right.begin < right.end;
}

Color32 representative_for_box(const std::vector<QuantPoint>& points, QuantBox box)
{
    double weight = 0.0;
    double linear_r_sum = 0.0;
    double linear_g_sum = 0.0;
    double linear_b_sum = 0.0;

    for (std::size_t index = box.begin; index < box.end; ++index) {
        weight += points[index].weight;
        linear_r_sum += points[index].linear_r_sum;
        linear_g_sum += points[index].linear_g_sum;
        linear_b_sum += points[index].linear_b_sum;
    }

    if (weight <= 0.0) {
        return {0, 0, 0, 255};
    }

    return to_color32(to_srgb({
        static_cast<float>(linear_r_sum / weight),
        static_cast<float>(linear_g_sum / weight),
        static_cast<float>(linear_b_sum / weight),
    }), 1.0F);
}

std::vector<Color32> generate_reduced_palette(const std::vector<BlockColor>& blocks, int max_colors, const ProcessSettings& settings)
{
    if (max_colors <= 0) {
        return {};
    }

    max_colors = std::clamp(max_colors, 1, 1024);
    std::vector<QuantPoint> points = build_quant_points(blocks, settings);
    if (points.empty()) {
        return {};
    }

    if (points.size() <= static_cast<std::size_t>(max_colors)) {
        std::vector<Color32> palette;
        palette.reserve(points.size());
        for (const QuantPoint& point : points) {
            palette.push_back(point.representative);
        }
        return palette;
    }

    std::vector<QuantBox> boxes = {{0, points.size()}};
    while (boxes.size() < static_cast<std::size_t>(max_colors)) {
        std::size_t best_index = boxes.size();
        double best_score = -1.0;
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            const double score = split_score(points, boxes[index]);
            if (score > best_score) {
                best_score = score;
                best_index = index;
            }
        }

        if (best_index >= boxes.size()) {
            break;
        }

        QuantBox left;
        QuantBox right;
        if (!split_box(points, boxes[best_index], left, right)) {
            break;
        }

        boxes[best_index] = left;
        boxes.push_back(right);
    }

    std::vector<Color32> palette;
    palette.reserve(boxes.size());
    for (const QuantBox box : boxes) {
        palette.push_back(representative_for_box(points, box));
    }
    return palette;
}

void write_block(Image& result, int start_x, int start_y, int end_x, int end_y, Color32 color)
{
    for (int y = start_y; y < end_y; ++y) {
        std::uint8_t* pixel = result.rgba.data()
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width) + static_cast<std::size_t>(start_x)) * 4U;
        for (int x = start_x; x < end_x; ++x) {
            pixel[0U] = color.r;
            pixel[1U] = color.g;
            pixel[2U] = color.b;
            pixel[3U] = color.a;
            pixel += 4U;
        }
    }
}

void diffuse_error(std::vector<Vec3>& working, int blocks_x, int blocks_y, int x, int y, Vec3 error, float scale)
{
    if (x < 0 || y < 0 || x >= blocks_x || y >= blocks_y) {
        return;
    }

    Vec3& color = working[static_cast<std::size_t>(y) * static_cast<std::size_t>(blocks_x) + static_cast<std::size_t>(x)];
    color.r = clamp01(color.r + error.r * scale);
    color.g = clamp01(color.g + error.g * scale);
    color.b = clamp01(color.b + error.b * scale);
}

const std::vector<DiffusionStep>& diffusion_steps(DitherMode mode)
{
    static const std::vector<DiffusionStep> floyd_steinberg = {
        {1, 0, 7.0F / 16.0F},
        {-1, 1, 3.0F / 16.0F},
        {0, 1, 5.0F / 16.0F},
        {1, 1, 1.0F / 16.0F},
    };
    static const std::vector<DiffusionStep> jarvis_judice_ninke = {
        {1, 0, 7.0F / 48.0F},
        {2, 0, 5.0F / 48.0F},
        {-2, 1, 3.0F / 48.0F},
        {-1, 1, 5.0F / 48.0F},
        {0, 1, 7.0F / 48.0F},
        {1, 1, 5.0F / 48.0F},
        {2, 1, 3.0F / 48.0F},
        {-2, 2, 1.0F / 48.0F},
        {-1, 2, 3.0F / 48.0F},
        {0, 2, 5.0F / 48.0F},
        {1, 2, 3.0F / 48.0F},
        {2, 2, 1.0F / 48.0F},
    };
    static const std::vector<DiffusionStep> atkinson = {
        {1, 0, 1.0F / 8.0F},
        {2, 0, 1.0F / 8.0F},
        {-1, 1, 1.0F / 8.0F},
        {0, 1, 1.0F / 8.0F},
        {1, 1, 1.0F / 8.0F},
        {0, 2, 1.0F / 8.0F},
    };

    if (mode == DitherMode::JarvisJudiceNinke) {
        return jarvis_judice_ninke;
    }
    if (mode == DitherMode::Atkinson) {
        return atkinson;
    }
    return floyd_steinberg;
}

void write_error_diffusion(
    Image& result,
    const std::vector<BlockColor>& blocks,
    int blocks_x,
    int blocks_y,
    int pixel_size,
    const std::vector<PaletteEntry>& palette_entries,
    const ProcessSettings& settings)
{
    std::vector<Vec3> working;
    working.reserve(blocks.size());
    for (const BlockColor& block : blocks) {
        working.push_back(block.srgb);
    }

    const float amount = normalized_dither_amount(settings.dither_amount);
    const auto& steps = diffusion_steps(settings.dither_mode);
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            const std::size_t index = static_cast<std::size_t>(by) * static_cast<std::size_t>(blocks_x) + static_cast<std::size_t>(bx);
            const BlockColor& block = blocks[index];
            if (should_write_transparent(block.alpha, settings)) {
                const int start_x = bx * pixel_size;
                const int start_y = by * pixel_size;
                const int end_x = std::min(start_x + pixel_size, result.width);
                const int end_y = std::min(start_y + pixel_size, result.height);
                write_block(result, start_x, start_y, end_x, end_y, transparent_color());
                continue;
            }

            Vec3 color = working[index];
            color.r = clamp01(color.r);
            color.g = clamp01(color.g);
            color.b = clamp01(color.b);

            const Color32 final_color = quantize_color(color, block.alpha, palette_entries, settings);
            const Vec3 quantized = from_color32(final_color);
            const Vec3 error = {
                color.r - quantized.r,
                color.g - quantized.g,
                color.b - quantized.b,
            };

            // Diffusion dithers use the percentage as error feedback gain:
            // Floyd-Steinberg and Jarvis-Judice-Ninke reach their full kernels
            // at 100%, while Atkinson still keeps its classic partial-error
            // look because the kernel weights intentionally sum below 1.
            for (const DiffusionStep step : steps) {
                diffuse_error(working, blocks_x, blocks_y, bx + step.dx, by + step.dy, error, amount * step.weight);
            }

            const int start_x = bx * pixel_size;
            const int start_y = by * pixel_size;
            const int end_x = std::min(start_x + pixel_size, result.width);
            const int end_y = std::min(start_y + pixel_size, result.height);
            write_block(result, start_x, start_y, end_x, end_y, final_color);
        }
    }
}

int next_power_of_two(int value)
{
    int result = 1;
    while (result < value) {
        result *= 2;
    }
    return result;
}

void hilbert_rotate(int size, int& x, int& y, int rx, int ry)
{
    if (ry == 0) {
        if (rx == 1) {
            x = size - 1 - x;
            y = size - 1 - y;
        }
        std::swap(x, y);
    }
}

void hilbert_index_to_xy(int size, int index, int& x, int& y)
{
    x = 0;
    y = 0;
    for (int step = 1; step < size; step *= 2) {
        const int rx = (index / 2) & 1;
        const int ry = (index ^ rx) & 1;
        hilbert_rotate(step, x, y, rx, ry);
        x += step * rx;
        y += step * ry;
        index /= 4;
    }
}

void write_riemersma(
    Image& result,
    const std::vector<BlockColor>& blocks,
    int blocks_x,
    int blocks_y,
    int pixel_size,
    const std::vector<PaletteEntry>& palette_entries,
    const ProcessSettings& settings)
{
    static constexpr int kQueueSize = 16;
    static constexpr float kDecay = 0.75F;

    std::array<Vec3, kQueueSize> error_queue = {};
    std::array<float, kQueueSize> weights = {};
    float weight_sum = 0.0F;
    float weight = 1.0F;
    for (float& entry : weights) {
        entry = weight;
        weight_sum += entry;
        weight *= kDecay;
    }
    for (float& entry : weights) {
        entry /= weight_sum;
    }

    const float amount = normalized_dither_amount(settings.dither_amount);
    const int curve_size = next_power_of_two(std::max(blocks_x, blocks_y));
    const int curve_pixels = curve_size * curve_size;

    for (int index = 0; index < curve_pixels; ++index) {
        int bx = 0;
        int by = 0;
        hilbert_index_to_xy(curve_size, index, bx, by);
        if (bx >= blocks_x || by >= blocks_y) {
            continue;
        }

        const std::size_t block_index = static_cast<std::size_t>(by) * static_cast<std::size_t>(blocks_x) + static_cast<std::size_t>(bx);
        const BlockColor& block = blocks[block_index];
        if (should_write_transparent(block.alpha, settings)) {
            error_queue.fill(Vec3{});
            const int start_x = bx * pixel_size;
            const int start_y = by * pixel_size;
            const int end_x = std::min(start_x + pixel_size, result.width);
            const int end_y = std::min(start_y + pixel_size, result.height);
            write_block(result, start_x, start_y, end_x, end_y, transparent_color());
            continue;
        }

        Vec3 color = block.srgb;
        // Riemersma uses the percentage as the strength of the queued Hilbert
        // curve error history, not as a spatial threshold offset.
        for (int error_index = 0; error_index < kQueueSize; ++error_index) {
            color.r += error_queue[static_cast<std::size_t>(error_index)].r * weights[static_cast<std::size_t>(error_index)] * amount;
            color.g += error_queue[static_cast<std::size_t>(error_index)].g * weights[static_cast<std::size_t>(error_index)] * amount;
            color.b += error_queue[static_cast<std::size_t>(error_index)].b * weights[static_cast<std::size_t>(error_index)] * amount;
        }
        color.r = clamp01(color.r);
        color.g = clamp01(color.g);
        color.b = clamp01(color.b);

        const Color32 final_color = quantize_color(color, block.alpha, palette_entries, settings);
        const Vec3 quantized = from_color32(final_color);
        for (int error_index = kQueueSize - 1; error_index > 0; --error_index) {
            error_queue[static_cast<std::size_t>(error_index)] = error_queue[static_cast<std::size_t>(error_index - 1)];
        }
        error_queue[0] = {
            color.r - quantized.r,
            color.g - quantized.g,
            color.b - quantized.b,
        };

        const int start_x = bx * pixel_size;
        const int start_y = by * pixel_size;
        const int end_x = std::min(start_x + pixel_size, result.width);
        const int end_y = std::min(start_y + pixel_size, result.height);
        write_block(result, start_x, start_y, end_x, end_y, final_color);
    }
}

} // namespace

Image process_image(const Image& source, const ProcessSettings& settings)
{
    if (!has_valid_rgba_size(source)) {
        return {};
    }

    Image result;
    result.width = source.width;
    result.height = source.height;
    result.rgba.resize(static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4U);

    const int pixel_size = std::clamp(settings.pixel_size, 1, 256);
    const int blocks_x = (source.width + pixel_size - 1) / pixel_size;
    const int blocks_y = (source.height + pixel_size - 1) / pixel_size;
    const int edge_width = source.width - (blocks_x - 1) * pixel_size;
    const int edge_height = source.height - (blocks_y - 1) * pixel_size;

    const AdjustmentContext adjustment_context = build_adjustment_context(settings.adjustments);
    std::vector<WeightKernel> weight_kernels;
    weight_kernels.reserve(4U);

    auto kernel_for_block = [&](int bx, int by) -> const WeightKernel& {
        const int width = (bx == blocks_x - 1) ? edge_width : pixel_size;
        const int height = (by == blocks_y - 1) ? edge_height : pixel_size;
        for (const WeightKernel& kernel : weight_kernels) {
            if (kernel.width == width && kernel.height == height) {
                return kernel;
            }
        }

        weight_kernels.push_back(build_weight_kernel(width, height, settings.block_color_mode));
        return weight_kernels.back();
    };

    std::vector<BlockColor> blocks;
    blocks.reserve(static_cast<std::size_t>(blocks_x) * static_cast<std::size_t>(blocks_y));

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            const int start_x = bx * pixel_size;
            const int start_y = by * pixel_size;
            blocks.push_back(choose_block_color(source, start_x, start_y, kernel_for_block(bx, by), adjustment_context));
        }
    }

    const std::vector<Color32> generated_palette = (!settings.use_palette && settings.reduction_max_colors > 0)
        ? generate_reduced_palette(blocks, settings.reduction_max_colors, settings)
        : std::vector<Color32>{};
    const std::vector<Color32>& active_palette = settings.use_palette ? settings.palette : generated_palette;
    const std::vector<PaletteEntry> palette_entries = build_palette_entries(active_palette);

    if ((settings.dither_mode == DitherMode::FloydSteinberg
            || settings.dither_mode == DitherMode::JarvisJudiceNinke
            || settings.dither_mode == DitherMode::Atkinson)
        && normalized_dither_amount(settings.dither_amount) > 0.0F) {
        write_error_diffusion(result, blocks, blocks_x, blocks_y, pixel_size, palette_entries, settings);
        return result;
    }

    if (settings.dither_mode == DitherMode::Riemersma && normalized_dither_amount(settings.dither_amount) > 0.0F) {
        write_riemersma(result, blocks, blocks_x, blocks_y, pixel_size, palette_entries, settings);
        return result;
    }

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            const int start_x = bx * pixel_size;
            const int start_y = by * pixel_size;
            const int end_x = std::min(start_x + pixel_size, source.width);
            const int end_y = std::min(start_y + pixel_size, source.height);
            const BlockColor& block = blocks[static_cast<std::size_t>(by) * static_cast<std::size_t>(blocks_x) + static_cast<std::size_t>(bx)];
            const Vec3 dithered = apply_dither(block.srgb, bx, by, settings);

            const Color32 final_color = quantize_color(dithered, block.alpha, palette_entries, settings);
            write_block(result, start_x, start_y, end_x, end_y, final_color);
        }
    }

    return result;
}

} // namespace pixelizer
