#include "pixatto/image_processing.hpp"
#include "pixatto/gpu_image_processor.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("image processing test failed");
    }
}

pixatto::Image single_pixel(pixatto::Color32 color)
{
    return {1, 1, {color.r, color.g, color.b, color.a}};
}

void require_same_image(const pixatto::Image& lhs, const pixatto::Image& rhs)
{
    require(lhs.width == rhs.width);
    require(lhs.height == rhs.height);
    require(lhs.rgba == rhs.rgba);
}

void one_pixel_fast_path_matches_one_pixel_block_path()
{
    const std::vector<pixatto::Color32> samples = {
        {255, 0, 0, 255},
        {34, 128, 220, 127},
        {12, 20, 32, 0},
    };

    std::vector<pixatto::ProcessSettings> settings_cases;

    pixatto::ProcessSettings adjusted;
    adjusted.color_levels = 5;
    adjusted.adjustments.brightness = 0.1F;
    adjusted.adjustments.contrast = -0.2F;
    adjusted.adjustments.gamma = 1.4F;
    adjusted.adjustments.tint = {240, 220, 180, 255};
    adjusted.adjustments.tint_strength = 0.35F;
    settings_cases.push_back(adjusted);

    pixatto::ProcessSettings saturated = adjusted;
    saturated.adjustments.saturation = 0.45F;
    saturated.preserve_transparency = true;
    settings_cases.push_back(saturated);

    pixatto::ProcessSettings paletted;
    paletted.use_palette = true;
    paletted.palette = {
        {0, 0, 0, 255},
        {255, 255, 255, 255},
        {64, 128, 224, 255},
    };
    settings_cases.push_back(paletted);

    pixatto::ProcessSettings ordered;
    ordered.dither_mode = pixatto::DitherMode::Bayer;
    ordered.dither_amount = 0.7F;
    ordered.bayer_matrix_size = 16;
    settings_cases.push_back(ordered);

    for (const pixatto::Color32 sample : samples) {
        const pixatto::Image source = single_pixel(sample);
        for (pixatto::ProcessSettings settings : settings_cases) {
            settings.pixel_size = 1;
            const pixatto::Image fast = pixatto::process_image(source, settings);

            settings.pixel_size = 2;
            const pixatto::Image block = pixatto::process_image(source, settings);

            require_same_image(fast, block);
        }
    }
}

void pixel_size_one_writes_each_source_pixel()
{
    const pixatto::Image source = {
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

    pixatto::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.color_levels = 2;
    settings.preserve_transparency = true;

    const pixatto::Image result = pixatto::process_image(source, settings);
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

void collapsed_pixel_blocks_use_one_pixel_per_block()
{
    const pixatto::Image image = {
        5,
        3,
        {
            255, 0, 0, 255,
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            255, 0, 0, 255,
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 255, 0, 255,
            0, 0, 255, 255,
            255, 255, 0, 255,
            255, 255, 0, 255,
            255, 0, 255, 255,
            255, 0, 255, 255,
            0, 255, 255, 255,
        },
    };

    const pixatto::Image result = pixatto::collapse_pixel_blocks(image, 2);
    const std::vector<unsigned char> expected = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 0, 255,
        255, 0, 255, 255,
        0, 255, 255, 255,
    };

    require(result.width == 3);
    require(result.height == 2);
    require(result.rgba == expected);
}

void paletted_riemersma_outputs_palette_colors()
{
    const pixatto::Image source = {
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

    pixatto::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.use_palette = true;
    settings.palette = {
        {0, 0, 0, 255},
        {255, 255, 255, 255},
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
    };
    settings.dither_mode = pixatto::DitherMode::Riemersma;
    settings.dither_amount = 0.5F;

    const pixatto::Image result = pixatto::process_image(source, settings);
    require(result.width == source.width);
    require(result.height == source.height);

    for (std::size_t index = 0; index < result.rgba.size(); index += 4U) {
        const pixatto::Color32 color = {
            result.rgba[index],
            result.rgba[index + 1U],
            result.rgba[index + 2U],
            result.rgba[index + 3U],
        };

        bool found = false;
        for (const pixatto::Color32 palette_color : settings.palette) {
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

    const pixatto::Image source = {12, 12, pixels};
    const std::vector<pixatto::DitherMode> modes = {
        pixatto::DitherMode::FalseFloydSteinberg,
        pixatto::DitherMode::FilterLite,
        pixatto::DitherMode::ZhigangFan,
        pixatto::DitherMode::ShiauFan,
        pixatto::DitherMode::Stucki,
        pixatto::DitherMode::Burkes,
        pixatto::DitherMode::Sierra,
        pixatto::DitherMode::TwoRowSierra,
        pixatto::DitherMode::ClusterDot4x4,
        pixatto::DitherMode::ClusterDot8x8,
        pixatto::DitherMode::Horizontal2x2,
        pixatto::DitherMode::Horizontal8x1,
        pixatto::DitherMode::Horizontal12x4,
        pixatto::DitherMode::Vertical2x2,
        pixatto::DitherMode::Vertical1x8,
        pixatto::DitherMode::Vertical4x12,
        pixatto::DitherMode::Diagonal5x5,
    };

    pixatto::ProcessSettings settings;
    settings.pixel_size = 1;
    settings.color_levels = 2;
    settings.dither_amount = 1.0F;

    for (const pixatto::DitherMode mode : modes) {
        settings.dither_mode = mode;
        const pixatto::Image result = pixatto::process_image(source, settings);
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

void collapsed_processing_matches_full_processing_sampled()
{
    const pixatto::Image source = {
        5,
        4,
        {
            255, 0, 0, 255,      0, 255, 0, 255,      0, 0, 255, 255,      255, 255, 0, 255,   255, 0, 255, 255,
            12, 34, 56, 255,     78, 90, 123, 255,    200, 170, 40, 255,   80, 30, 220, 255,   20, 240, 160, 255,
            5, 10, 15, 127,      40, 80, 120, 255,    90, 130, 170, 255,   140, 180, 220, 255, 255, 255, 255, 255,
            220, 120, 20, 255,   180, 40, 160, 255,   64, 64, 64, 255,     8, 220, 140, 255,   0, 0, 0, 0,
        },
    };

    std::vector<pixatto::ProcessSettings> settings_cases;

    pixatto::ProcessSettings adjusted;
    adjusted.pixel_size = 2;
    adjusted.color_levels = 4;
    adjusted.adjustments.brightness = 0.05F;
    adjusted.adjustments.gamma = 1.2F;
    settings_cases.push_back(adjusted);

    pixatto::ProcessSettings paletted = adjusted;
    paletted.use_palette = true;
    paletted.palette = {
        {0, 0, 0, 255},
        {255, 255, 255, 255},
        {255, 0, 0, 255},
        {0, 0, 255, 255},
    };
    paletted.dither_mode = pixatto::DitherMode::Bayer;
    paletted.dither_amount = 0.6F;
    settings_cases.push_back(paletted);

    pixatto::ProcessSettings reduced = adjusted;
    reduced.pixel_size = 3;
    reduced.reduction_max_colors = 3;
    settings_cases.push_back(reduced);

    pixatto::ProcessSettings diffusion = paletted;
    diffusion.dither_mode = pixatto::DitherMode::Atkinson;
    diffusion.dither_amount = 0.7F;
    settings_cases.push_back(diffusion);

    pixatto::ProcessSettings riemersma = paletted;
    riemersma.dither_mode = pixatto::DitherMode::Riemersma;
    riemersma.dither_amount = 0.5F;
    settings_cases.push_back(riemersma);

    for (const pixatto::ProcessSettings& settings : settings_cases) {
        const pixatto::Image full = pixatto::process_image(source, settings);
        const pixatto::Image sampled = pixatto::collapse_pixel_blocks(full, settings.pixel_size);
        const pixatto::Image direct = pixatto::process_image_collapsed(source, settings);
        require_same_image(sampled, direct);
    }
}

void sampled_collapsed_processing_is_the_low_quality_path()
{
    pixatto::Image source;
    source.width = 2;
    source.height = 2;
    source.rgba = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
        255, 255, 255, 255,
    };

    pixatto::ProcessSettings settings;
    settings.pixel_size = 2;
    require(pixatto::supports_low_quality_process(settings));

    const pixatto::Image exact = pixatto::process_image_collapsed(source, settings);
    const pixatto::Image sampled = pixatto::process_image_sampled_collapsed(source, settings);
    require(exact.width == 1 && exact.height == 1);
    require(sampled.width == 1 && sampled.height == 1);
    require(exact.rgba != sampled.rgba);

    settings.dither_mode = pixatto::DitherMode::Atkinson;
    settings.dither_amount = 1.0F;
    require(!pixatto::supports_low_quality_process(settings));
    require_same_image(pixatto::process_image_collapsed(source, settings), pixatto::process_image_sampled_collapsed(source, settings));
}

void gpu_sampled_processor_capability_matches_shader_scope()
{
    const std::vector<pixatto::DitherMode> stateless_modes = {
        pixatto::DitherMode::None,
        pixatto::DitherMode::Bayer,
        pixatto::DitherMode::BlueNoise,
        pixatto::DitherMode::ClusterDot4x4,
        pixatto::DitherMode::ClusterDot8x8,
        pixatto::DitherMode::Horizontal2x2,
        pixatto::DitherMode::Horizontal8x1,
        pixatto::DitherMode::Horizontal12x4,
        pixatto::DitherMode::Vertical2x2,
        pixatto::DitherMode::Vertical1x8,
        pixatto::DitherMode::Vertical4x12,
        pixatto::DitherMode::Diagonal5x5,
    };

    pixatto::ProcessSettings settings;
    settings.dither_amount = 0.5F;
    for (const pixatto::DitherMode mode : stateless_modes) {
        settings.dither_mode = mode;
        require(pixatto::supports_low_quality_process(settings));
        require(pixatto::can_process_sampled_collapsed_on_gpu(settings));
    }

    for (const pixatto::DitherMode mode : {
             pixatto::DitherMode::FloydSteinberg,
             pixatto::DitherMode::FalseFloydSteinberg,
             pixatto::DitherMode::FilterLite,
             pixatto::DitherMode::ZhigangFan,
             pixatto::DitherMode::ShiauFan,
             pixatto::DitherMode::JarvisJudiceNinke,
             pixatto::DitherMode::Atkinson,
             pixatto::DitherMode::Stucki,
             pixatto::DitherMode::Burkes,
             pixatto::DitherMode::Sierra,
             pixatto::DitherMode::TwoRowSierra,
             pixatto::DitherMode::Riemersma,
         }) {
        settings.dither_mode = mode;
        require(!pixatto::supports_low_quality_process(settings));
        require(!pixatto::can_process_sampled_collapsed_on_gpu(settings));
    }

    settings.dither_mode = pixatto::DitherMode::Bayer;
    settings.use_palette = true;
    settings.palette = {{0, 0, 0, 255}, {255, 255, 255, 255}};
    require(pixatto::can_process_sampled_collapsed_on_gpu(settings));

    settings.palette.clear();
    require(!pixatto::can_process_sampled_collapsed_on_gpu(settings));

    settings.palette.assign(257U, {0, 0, 0, 255});
    require(!pixatto::can_process_sampled_collapsed_on_gpu(settings));

    settings.use_palette = false;
    settings.palette.clear();
    settings.reduction_max_colors = 4;
    require(!pixatto::can_process_sampled_collapsed_on_gpu(settings));
}

void gpu_sampled_processor_matches_cpu_when_context_is_available()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("gpu-test", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    if (!SDL_GL_MakeCurrent(window, context)) {
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    auto require_gpu_matches_cpu = [](pixatto::GpuImageProcessor& processor, const pixatto::Image& source, const pixatto::ProcessSettings& settings) {
        pixatto::Image gpu_result;
        std::string error;
        if (!processor.process_sampled_collapsed(source, settings, gpu_result, error)) {
            std::cerr << "gpu error: " << error << "\n";
            require(false);
        }
        const pixatto::Image cpu_result = pixatto::process_image_sampled_collapsed(source, settings);
        if (cpu_result.rgba != gpu_result.rgba) {
            std::cerr << "cpu:";
            for (unsigned char value : cpu_result.rgba) {
                std::cerr << " " << static_cast<int>(value);
            }
            std::cerr << "\ngpu:";
            for (unsigned char value : gpu_result.rgba) {
                std::cerr << " " << static_cast<int>(value);
            }
            std::cerr << "\n";
        }
        require_same_image(cpu_result, gpu_result);
    };

    {
        pixatto::GpuImageProcessor processor;

        {
            pixatto::Image source;
            source.width = 4;
            source.height = 4;
            source.rgba = {
                0, 0, 0, 0,
                0, 0, 0, 255,
                255, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 255, 0, 255,
                0, 0, 0, 255,
                0, 0, 255, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
                0, 0, 0, 255,
            };

            pixatto::ProcessSettings settings;
            settings.pixel_size = 2;
            settings.color_levels = 8;

            require_gpu_matches_cpu(processor, source, settings);
        }

        const pixatto::Image ordered_source = {
            5,
            3,
            {
                12, 36, 72, 255,
                36, 72, 108, 255,
                72, 108, 144, 255,
                108, 144, 180, 255,
                144, 180, 216, 255,
                24, 48, 96, 255,
                48, 96, 128, 255,
                96, 128, 160, 255,
                128, 160, 192, 255,
                160, 192, 224, 255,
                0, 0, 0, 0,
                60, 90, 120, 255,
                90, 120, 150, 255,
                120, 150, 180, 255,
                150, 180, 210, 255,
            },
        };

        for (const pixatto::DitherMode mode : {
                 pixatto::DitherMode::Bayer,
                 pixatto::DitherMode::ClusterDot4x4,
                 pixatto::DitherMode::ClusterDot8x8,
                 pixatto::DitherMode::Horizontal2x2,
                 pixatto::DitherMode::Horizontal8x1,
                 pixatto::DitherMode::Horizontal12x4,
                 pixatto::DitherMode::Vertical2x2,
                 pixatto::DitherMode::Vertical1x8,
                 pixatto::DitherMode::Vertical4x12,
                 pixatto::DitherMode::Diagonal5x5,
             }) {
            pixatto::ProcessSettings settings;
            settings.pixel_size = 2;
            settings.color_levels = 4;
            settings.dither_mode = mode;
            settings.dither_amount = 0.5F;
            settings.bayer_matrix_size = 4;

            require_gpu_matches_cpu(processor, ordered_source, settings);
        }

        {
            pixatto::ProcessSettings settings;
            settings.pixel_size = 2;
            settings.color_levels = 4;
            settings.dither_mode = pixatto::DitherMode::BlueNoise;
            settings.dither_amount = 0.5F;
            settings.blue_noise_seed = 4242;

            require_gpu_matches_cpu(processor, ordered_source, settings);
        }

        {
            pixatto::ProcessSettings settings;
            settings.pixel_size = 2;
            settings.color_levels = 8;
            settings.preserve_transparency = true;

            require_gpu_matches_cpu(processor, ordered_source, settings);
        }

        {
            pixatto::ProcessSettings settings;
            settings.pixel_size = 2;
            settings.color_levels = 4;
            settings.dither_mode = pixatto::DitherMode::Bayer;
            settings.dither_amount = 0.5F;

            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, 1, 1);
            glEnable(GL_CULL_FACE);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            require_gpu_matches_cpu(processor, ordered_source, settings);
            require(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
            require(glIsEnabled(GL_CULL_FACE) == GL_TRUE);
            GLboolean color_mask[4] = {};
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
            require(color_mask[0] == GL_FALSE);
            require(color_mask[1] == GL_FALSE);
            require(color_mask[2] == GL_FALSE);
            require(color_mask[3] == GL_FALSE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }

        const pixatto::Image palette_source = {
            7,
            5,
            {
                0, 0, 0, 255,       255, 255, 255, 255, 250, 10, 10, 255,    10, 245, 10, 255,    10, 10, 245, 255,    240, 240, 20, 255,  20, 240, 240, 255,
                4, 4, 4, 255,       245, 245, 245, 255, 230, 30, 20, 255,    30, 220, 25, 255,    24, 24, 230, 255,    224, 210, 30, 255,  26, 220, 220, 255,
                0, 0, 0, 0,         250, 250, 250, 255, 220, 20, 40, 255,    20, 235, 40, 255,    40, 20, 220, 255,    235, 235, 35, 255,  35, 235, 235, 255,
                8, 8, 8, 255,       240, 240, 240, 255, 235, 50, 40, 255,    40, 235, 50, 255,    50, 40, 235, 255,    245, 220, 45, 255,  45, 245, 220, 255,
                2, 2, 2, 255,       255, 255, 255, 255, 255, 20, 20, 255,    20, 255, 20, 255,    20, 20, 255, 255,    250, 250, 30, 255,  30, 250, 250, 255,
            },
        };

        for (const pixatto::DitherMode mode : {
                 pixatto::DitherMode::None,
                 pixatto::DitherMode::Bayer,
                 pixatto::DitherMode::BlueNoise,
                 pixatto::DitherMode::ClusterDot4x4,
                 pixatto::DitherMode::ClusterDot8x8,
                 pixatto::DitherMode::Horizontal2x2,
                 pixatto::DitherMode::Horizontal8x1,
                 pixatto::DitherMode::Horizontal12x4,
                 pixatto::DitherMode::Vertical2x2,
                 pixatto::DitherMode::Vertical1x8,
                 pixatto::DitherMode::Vertical4x12,
                 pixatto::DitherMode::Diagonal5x5,
             }) {
            pixatto::ProcessSettings settings;
            settings.pixel_size = 3;
            settings.use_palette = true;
            settings.palette = {
                {0, 0, 0, 255},
                {255, 255, 255, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {0, 0, 255, 255},
                {255, 255, 0, 255},
                {0, 255, 255, 255},
            };
            settings.dither_mode = mode;
            settings.dither_amount = mode == pixatto::DitherMode::None ? 0.0F : 0.35F;
            settings.bayer_matrix_size = 8;
            settings.blue_noise_seed = 777;

            require_gpu_matches_cpu(processor, palette_source, settings);
        }
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

} // namespace

int main()
{
    one_pixel_fast_path_matches_one_pixel_block_path();
    pixel_size_one_writes_each_source_pixel();
    collapsed_pixel_blocks_use_one_pixel_per_block();
    paletted_riemersma_outputs_palette_colors();
    added_dither_modes_create_two_tone_patterns();
    collapsed_processing_matches_full_processing_sampled();
    sampled_collapsed_processing_is_the_low_quality_path();
    gpu_sampled_processor_capability_matches_shader_scope();
    gpu_sampled_processor_matches_cpu_when_context_is_available();
    return 0;
}
