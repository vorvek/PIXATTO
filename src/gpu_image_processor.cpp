#include "pixatto/gpu_image_processor.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace pixatto {
namespace {

constexpr const char* kGlslVersion =
#if defined(__APPLE__)
    "#version 150\n";
#else
    "#version 130\n";
#endif

using GlActiveTexture = void(APIENTRY*)(GLenum);
using GlAttachShader = void(APIENTRY*)(GLuint, GLuint);
using GlBindAttribLocation = void(APIENTRY*)(GLuint, GLuint, const GLchar*);
using GlBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBindVertexArray = void(APIENTRY*)(GLuint);
using GlCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using GlCompileShader = void(APIENTRY*)(GLuint);
using GlCreateProgram = GLuint(APIENTRY*)();
using GlCreateShader = GLuint(APIENTRY*)(GLenum);
using GlDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlDeleteProgram = void(APIENTRY*)(GLuint);
using GlDeleteShader = void(APIENTRY*)(GLuint);
using GlDeleteVertexArrays = void(APIENTRY*)(GLsizei, const GLuint*);
using GlFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlGenVertexArrays = void(APIENTRY*)(GLsizei, GLuint*);
using GlGetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlGetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlGetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetUniformLocation = GLint(APIENTRY*)(GLuint, const GLchar*);
using GlLinkProgram = void(APIENTRY*)(GLuint);
using GlShaderSource = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GlUniform1f = void(APIENTRY*)(GLint, GLfloat);
using GlUniform1i = void(APIENTRY*)(GLint, GLint);
using GlUniform3f = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
using GlUseProgram = void(APIENTRY*)(GLuint);

GlActiveTexture px_glActiveTexture = nullptr;
GlAttachShader px_glAttachShader = nullptr;
GlBindAttribLocation px_glBindAttribLocation = nullptr;
GlBindFramebuffer px_glBindFramebuffer = nullptr;
GlBindVertexArray px_glBindVertexArray = nullptr;
GlCheckFramebufferStatus px_glCheckFramebufferStatus = nullptr;
GlCompileShader px_glCompileShader = nullptr;
GlCreateProgram px_glCreateProgram = nullptr;
GlCreateShader px_glCreateShader = nullptr;
GlDeleteFramebuffers px_glDeleteFramebuffers = nullptr;
GlDeleteProgram px_glDeleteProgram = nullptr;
GlDeleteShader px_glDeleteShader = nullptr;
GlDeleteVertexArrays px_glDeleteVertexArrays = nullptr;
GlFramebufferTexture2D px_glFramebufferTexture2D = nullptr;
GlGenFramebuffers px_glGenFramebuffers = nullptr;
GlGenVertexArrays px_glGenVertexArrays = nullptr;
GlGetProgramInfoLog px_glGetProgramInfoLog = nullptr;
GlGetProgramiv px_glGetProgramiv = nullptr;
GlGetShaderInfoLog px_glGetShaderInfoLog = nullptr;
GlGetShaderiv px_glGetShaderiv = nullptr;
GlGetUniformLocation px_glGetUniformLocation = nullptr;
GlLinkProgram px_glLinkProgram = nullptr;
GlShaderSource px_glShaderSource = nullptr;
GlUniform1f px_glUniform1f = nullptr;
GlUniform1i px_glUniform1i = nullptr;
GlUniform3f px_glUniform3f = nullptr;
GlUseProgram px_glUseProgram = nullptr;

template <typename T>
bool load_gl_proc(T& proc, const char* name, std::string& error)
{
    proc = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!proc) {
        error = std::string("OpenGL function missing: ") + name;
        return false;
    }
    return true;
}

bool load_gl_functions(std::string& error)
{
    return load_gl_proc(px_glActiveTexture, "glActiveTexture", error)
        && load_gl_proc(px_glAttachShader, "glAttachShader", error)
        && load_gl_proc(px_glBindAttribLocation, "glBindAttribLocation", error)
        && load_gl_proc(px_glBindFramebuffer, "glBindFramebuffer", error)
        && load_gl_proc(px_glBindVertexArray, "glBindVertexArray", error)
        && load_gl_proc(px_glCheckFramebufferStatus, "glCheckFramebufferStatus", error)
        && load_gl_proc(px_glCompileShader, "glCompileShader", error)
        && load_gl_proc(px_glCreateProgram, "glCreateProgram", error)
        && load_gl_proc(px_glCreateShader, "glCreateShader", error)
        && load_gl_proc(px_glDeleteFramebuffers, "glDeleteFramebuffers", error)
        && load_gl_proc(px_glDeleteProgram, "glDeleteProgram", error)
        && load_gl_proc(px_glDeleteShader, "glDeleteShader", error)
        && load_gl_proc(px_glDeleteVertexArrays, "glDeleteVertexArrays", error)
        && load_gl_proc(px_glFramebufferTexture2D, "glFramebufferTexture2D", error)
        && load_gl_proc(px_glGenFramebuffers, "glGenFramebuffers", error)
        && load_gl_proc(px_glGenVertexArrays, "glGenVertexArrays", error)
        && load_gl_proc(px_glGetProgramInfoLog, "glGetProgramInfoLog", error)
        && load_gl_proc(px_glGetProgramiv, "glGetProgramiv", error)
        && load_gl_proc(px_glGetShaderInfoLog, "glGetShaderInfoLog", error)
        && load_gl_proc(px_glGetShaderiv, "glGetShaderiv", error)
        && load_gl_proc(px_glGetUniformLocation, "glGetUniformLocation", error)
        && load_gl_proc(px_glLinkProgram, "glLinkProgram", error)
        && load_gl_proc(px_glShaderSource, "glShaderSource", error)
        && load_gl_proc(px_glUniform1f, "glUniform1f", error)
        && load_gl_proc(px_glUniform1i, "glUniform1i", error)
        && load_gl_proc(px_glUniform3f, "glUniform3f", error)
        && load_gl_proc(px_glUseProgram, "glUseProgram", error);
}

std::string shader_log(GLuint shader)
{
    GLint length = 0;
    px_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    px_glGetShaderInfoLog(shader, length, nullptr, log.data());
    return log;
}

std::string program_log(GLuint program)
{
    GLint length = 0;
    px_glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    px_glGetProgramInfoLog(program, length, nullptr, log.data());
    return log;
}

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

struct OrderedDitherMap {
    int width = 0;
    int height = 0;
    int levels = 0;
    const int* cells = nullptr;
};

struct ThresholdTextureData {
    int width = 1;
    int height = 1;
    bool enabled = false;
    std::vector<float> values = {0.0F};
};

struct PaletteTextureData {
    int size = 0;
    std::vector<std::uint8_t> colors;
    std::vector<float> labs;
};

constexpr float kEpsilon = 0.000001F;
constexpr int kBlueNoiseSize = 32;
constexpr int kBlueNoiseCellCount = kBlueNoiseSize * kBlueNoiseSize;

float clamp01(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

float from_byte(std::uint8_t value)
{
    return static_cast<float>(value) / 255.0F;
}

Vec3 from_color32(Color32 color)
{
    return {
        from_byte(color.r),
        from_byte(color.g),
        from_byte(color.b),
    };
}

float srgb_to_linear(float value)
{
    value = clamp01(value);
    if (value <= 0.04045F) {
        return value / 12.92F;
    }
    return std::pow((value + 0.055F) / 1.055F, 2.4F);
}

Vec3 to_linear(Vec3 srgb)
{
    return {
        srgb_to_linear(srgb.r),
        srgb_to_linear(srgb.g),
        srgb_to_linear(srgb.b),
    };
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

int wrapped_index(int value, int size)
{
    const int wrapped = value % size;
    return wrapped < 0 ? wrapped + size : wrapped;
}

const OrderedDitherMap* ordered_dither_map(DitherMode mode)
{
    static constexpr std::array<int, 16> cluster_dot_4x4 = {
        12, 5, 6, 13,
        4, 0, 1, 7,
        11, 3, 2, 8,
        15, 10, 9, 14,
    };
    static constexpr std::array<int, 64> cluster_dot_8x8 = {
        3, 9, 17, 27, 25, 15, 7, 1,
        11, 29, 38, 46, 44, 36, 23, 5,
        19, 40, 52, 58, 56, 50, 34, 13,
        31, 48, 60, 63, 62, 54, 42, 21,
        30, 47, 59, 63, 61, 53, 41, 20,
        18, 39, 51, 57, 55, 49, 33, 12,
        10, 28, 37, 45, 43, 35, 22, 4,
        2, 8, 16, 26, 24, 14, 6, 0,
    };
    static constexpr std::array<int, 4> horizontal_2x2 = {
        0, 0,
        1, 1,
    };
    static constexpr std::array<int, 8> horizontal_8x1 = {
        0, 1, 2, 3, 4, 5, 6, 7,
    };
    static constexpr std::array<int, 48> horizontal_12x4 = {
        6, 7, 7, 7, 7, 6, 5, 5, 4, 4, 4, 5,
        1, 0, 0, 0, 0, 1, 2, 3, 3, 3, 2, 2,
        5, 5, 4, 4, 4, 5, 6, 7, 7, 7, 7, 6,
        2, 3, 3, 3, 2, 2, 1, 0, 0, 0, 0, 1,
    };
    static constexpr std::array<int, 4> vertical_2x2 = {
        0, 1,
        0, 1,
    };
    static constexpr std::array<int, 8> vertical_1x8 = {
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
    };
    static constexpr std::array<int, 48> vertical_4x12 = {
        6, 1, 5, 2,
        7, 0, 5, 3,
        7, 0, 4, 3,
        7, 0, 4, 3,
        7, 0, 4, 2,
        6, 1, 5, 2,
        5, 2, 6, 1,
        5, 3, 7, 0,
        4, 3, 7, 0,
        4, 3, 7, 0,
        4, 2, 7, 0,
        5, 2, 6, 1,
    };
    static constexpr std::array<int, 25> diagonal_5x5 = {
        3, 1, 0, 2, 4,
        1, 0, 2, 4, 3,
        0, 2, 4, 3, 1,
        2, 4, 3, 1, 0,
        4, 3, 1, 0, 2,
    };

    static const OrderedDitherMap cluster_dot_4x4_map{4, 4, 16, cluster_dot_4x4.data()};
    static const OrderedDitherMap cluster_dot_8x8_map{8, 8, 64, cluster_dot_8x8.data()};
    static const OrderedDitherMap horizontal_2x2_map{2, 2, 2, horizontal_2x2.data()};
    static const OrderedDitherMap horizontal_8x1_map{8, 1, 8, horizontal_8x1.data()};
    static const OrderedDitherMap horizontal_12x4_map{12, 4, 8, horizontal_12x4.data()};
    static const OrderedDitherMap vertical_2x2_map{2, 2, 2, vertical_2x2.data()};
    static const OrderedDitherMap vertical_1x8_map{1, 8, 8, vertical_1x8.data()};
    static const OrderedDitherMap vertical_4x12_map{4, 12, 8, vertical_4x12.data()};
    static const OrderedDitherMap diagonal_5x5_map{5, 5, 5, diagonal_5x5.data()};

    switch (mode) {
    case DitherMode::ClusterDot4x4:
        return &cluster_dot_4x4_map;
    case DitherMode::ClusterDot8x8:
        return &cluster_dot_8x8_map;
    case DitherMode::Horizontal2x2:
        return &horizontal_2x2_map;
    case DitherMode::Horizontal8x1:
        return &horizontal_8x1_map;
    case DitherMode::Horizontal12x4:
        return &horizontal_12x4_map;
    case DitherMode::Vertical2x2:
        return &vertical_2x2_map;
    case DitherMode::Vertical1x8:
        return &vertical_1x8_map;
    case DitherMode::Vertical4x12:
        return &vertical_4x12_map;
    case DitherMode::Diagonal5x5:
        return &diagonal_5x5_map;
    case DitherMode::None:
    case DitherMode::Bayer:
    case DitherMode::BlueNoise:
    case DitherMode::FloydSteinberg:
    case DitherMode::FalseFloydSteinberg:
    case DitherMode::FilterLite:
    case DitherMode::ZhigangFan:
    case DitherMode::ShiauFan:
    case DitherMode::JarvisJudiceNinke:
    case DitherMode::Atkinson:
    case DitherMode::Stucki:
    case DitherMode::Burkes:
    case DitherMode::Sierra:
    case DitherMode::TwoRowSierra:
    case DitherMode::Riemersma:
        return nullptr;
    }
    return nullptr;
}

float ordered_map_threshold(int x, int y, const OrderedDitherMap& map)
{
    if (!map.cells || map.width <= 0 || map.height <= 0 || map.levels <= 0) {
        return 0.0F;
    }

    const int wrapped_x = wrapped_index(x, map.width);
    const int wrapped_y = wrapped_index(y, map.height);
    const int cell = map.cells[static_cast<std::size_t>(wrapped_y * map.width + wrapped_x)];
    return (static_cast<float>(cell) + 0.5F) / static_cast<float>(map.levels) - 0.5F;
}

float normalized_dither_amount(float amount)
{
    if (amount > 1.0F) {
        amount /= 100.0F;
    }
    return clamp01(amount);
}

bool supports_gpu_dither_mode(DitherMode mode)
{
    switch (mode) {
    case DitherMode::None:
    case DitherMode::Bayer:
    case DitherMode::BlueNoise:
    case DitherMode::ClusterDot4x4:
    case DitherMode::ClusterDot8x8:
    case DitherMode::Horizontal2x2:
    case DitherMode::Horizontal8x1:
    case DitherMode::Horizontal12x4:
    case DitherMode::Vertical2x2:
    case DitherMode::Vertical1x8:
    case DitherMode::Vertical4x12:
    case DitherMode::Diagonal5x5:
        return true;
    case DitherMode::FloydSteinberg:
    case DitherMode::FalseFloydSteinberg:
    case DitherMode::FilterLite:
    case DitherMode::ZhigangFan:
    case DitherMode::ShiauFan:
    case DitherMode::JarvisJudiceNinke:
    case DitherMode::Atkinson:
    case DitherMode::Stucki:
    case DitherMode::Burkes:
    case DitherMode::Sierra:
    case DitherMode::TwoRowSierra:
    case DitherMode::Riemersma:
        return false;
    }
    return false;
}

ThresholdTextureData build_threshold_texture_data(const ProcessSettings& settings)
{
    const float amount = normalized_dither_amount(settings.dither_amount);
    if (amount <= 0.0F || settings.dither_mode == DitherMode::None) {
        return {};
    }

    ThresholdTextureData data;
    data.enabled = true;
    if (settings.dither_mode == DitherMode::Bayer) {
        data.width = normalized_bayer_matrix_size(settings.bayer_matrix_size);
        data.height = data.width;
        data.values.resize(static_cast<std::size_t>(data.width * data.height));
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.values[static_cast<std::size_t>(y * data.width + x)] =
                    bayer_threshold(x, y, settings.bayer_matrix_size);
            }
        }
        return data;
    }

    if (settings.dither_mode == DitherMode::BlueNoise) {
        data.width = kBlueNoiseSize;
        data.height = kBlueNoiseSize;
        data.values.resize(static_cast<std::size_t>(kBlueNoiseCellCount));
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.values[static_cast<std::size_t>(y * data.width + x)] =
                    blue_noise_threshold(x, y, settings.blue_noise_seed);
            }
        }
        return data;
    }

    if (const OrderedDitherMap* map = ordered_dither_map(settings.dither_mode)) {
        data.width = map->width;
        data.height = map->height;
        data.values.resize(static_cast<std::size_t>(data.width * data.height));
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                data.values[static_cast<std::size_t>(y * data.width + x)] =
                    ordered_map_threshold(x, y, *map);
            }
        }
        return data;
    }

    return {};
}

PaletteTextureData build_palette_texture_data(const std::vector<Color32>& palette)
{
    PaletteTextureData data;
    data.size = static_cast<int>(palette.size());
    data.colors.reserve(palette.size() * 4U);
    data.labs.reserve(palette.size() * 3U);
    for (const Color32 color : palette) {
        data.colors.push_back(color.r);
        data.colors.push_back(color.g);
        data.colors.push_back(color.b);
        data.colors.push_back(255);

        const LabColor lab = to_oklab(from_color32(color));
        data.labs.push_back(lab.l);
        data.labs.push_back(lab.a);
        data.labs.push_back(lab.b);
    }
    return data;
}

GLuint compile_shader(GLenum type, const std::string& source, std::string& error)
{
    const GLuint shader = px_glCreateShader(type);
    const char* source_ptr = source.c_str();
    px_glShaderSource(shader, 1, &source_ptr, nullptr);
    px_glCompileShader(shader);

    GLint ok = GL_FALSE;
    px_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        error = shader_log(shader);
        if (error.empty()) {
            error = "OpenGL shader compilation failed.";
        }
        px_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint build_program(std::string& error)
{
    const std::string vertex_source = std::string(kGlslVersion) + R"(
out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 position = positions[gl_VertexID];
    v_uv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

    const std::string fragment_source = std::string(kGlslVersion) + R"(
uniform sampler2D u_source;
uniform sampler2D u_thresholds;
uniform sampler2D u_palette_colors;
uniform sampler2D u_palette_labs;
uniform int u_source_width;
uniform int u_source_height;
uniform int u_pixel_size;
uniform int u_color_levels;
uniform int u_preserve_transparency;
uniform int u_threshold_width;
uniform int u_threshold_height;
uniform int u_threshold_enabled;
uniform int u_use_palette;
uniform int u_palette_size;
uniform float u_dither_amount;
uniform float u_brightness;
uniform float u_contrast;
uniform float u_gamma;
uniform float u_input_black;
uniform float u_input_white;
uniform float u_output_black;
uniform float u_output_white;
uniform float u_saturation;
uniform vec3 u_tint;
uniform float u_tint_strength;
out vec4 frag_color;

float clamp01(float value)
{
    return clamp(value, 0.0, 1.0);
}

float apply_levels(float value, float input_black, float input_white, float output_black, float output_white)
{
    float normalized = clamp01((value - clamp01(input_black)) / max(0.000001, clamp01(input_white) - clamp01(input_black)));
    return clamp01(output_black) + normalized * (clamp01(output_white) - clamp01(output_black));
}

float pre_adjust_channel(float value)
{
    value = apply_levels(value, u_input_black, u_input_white, 0.0, 1.0);
    value = clamp01(value + clamp(u_brightness, -1.0, 1.0));
    float contrast = clamp(u_contrast, -1.0, 1.0);
    float contrast_factor = contrast >= 0.0 ? 1.0 + contrast * 2.0 : 1.0 + contrast;
    value = clamp01((value - 0.5) * contrast_factor + 0.5);
    return pow(value, 1.0 / max(0.05, u_gamma));
}

vec3 adjusted_srgb(vec3 color)
{
    color = vec3(pre_adjust_channel(color.r), pre_adjust_channel(color.g), pre_adjust_channel(color.b));
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = clamp(mix(vec3(luma), color, max(0.0, u_saturation)), 0.0, 1.0);
    color = clamp(color * (1.0 - clamp01(u_tint_strength)) + (color * u_tint) * clamp01(u_tint_strength), 0.0, 1.0);
    return vec3(
        apply_levels(color.r, 0.0, 1.0, u_output_black, u_output_white),
        apply_levels(color.g, 0.0, 1.0, u_output_black, u_output_white),
        apply_levels(color.b, 0.0, 1.0, u_output_black, u_output_white)
    );
}

float srgb_to_linear(float value)
{
    value = clamp01(value);
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return pow((value + 0.055) / 1.055, 2.4);
}

vec3 to_oklab(vec3 srgb)
{
    vec3 linear = vec3(
        srgb_to_linear(srgb.r),
        srgb_to_linear(srgb.g),
        srgb_to_linear(srgb.b)
    );
    float l = 0.4122214708 * linear.r + 0.5363325363 * linear.g + 0.0514459929 * linear.b;
    float m = 0.2119034982 * linear.r + 0.6806995451 * linear.g + 0.1073969566 * linear.b;
    float s = 0.0883024619 * linear.r + 0.2817188376 * linear.g + 0.6299787005 * linear.b;
    float l_root = pow(max(0.0, l), 1.0 / 3.0);
    float m_root = pow(max(0.0, m), 1.0 / 3.0);
    float s_root = pow(max(0.0, s), 1.0 / 3.0);
    return vec3(
        0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
        1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
        0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root
    );
}

vec3 apply_dither(vec3 color, int block_x, int block_y)
{
    float amount = u_dither_amount > 1.0 ? u_dither_amount / 100.0 : u_dither_amount;
    amount = clamp01(amount);
    if (u_threshold_enabled == 0 || amount <= 0.0) {
        return color;
    }
    int wrapped_x = block_x - (block_x / u_threshold_width) * u_threshold_width;
    int wrapped_y = block_y - (block_y / u_threshold_height) * u_threshold_height;
    float threshold = texelFetch(u_thresholds, ivec2(wrapped_x, wrapped_y), 0).r;
    float offset = threshold * amount * 0.35;
    return clamp(color + vec3(offset), 0.0, 1.0);
}

vec3 reduce_color(vec3 color)
{
    float levels = float(clamp(u_color_levels, 2, 64));
    float max_level = levels - 1.0;
    return round(clamp(color, 0.0, 1.0) * max_level) / max_level;
}

vec3 nearest_palette_color(vec3 color)
{
    vec3 target = to_oklab(color);
    float best_distance = 1000000.0;
    vec3 best_color = texelFetch(u_palette_colors, ivec2(0, 0), 0).rgb;

    for (int index = 0; index < 256; ++index) {
        if (index >= u_palette_size) {
            break;
        }
        vec3 candidate_lab = texelFetch(u_palette_labs, ivec2(index, 0), 0).rgb;
        vec3 delta = target - candidate_lab;
        float distance = dot(delta, delta);
        if (distance < best_distance) {
            best_distance = distance;
            best_color = texelFetch(u_palette_colors, ivec2(index, 0), 0).rgb;
        }
    }

    return best_color;
}

void main()
{
    ivec2 block = ivec2(int(gl_FragCoord.x), int(gl_FragCoord.y));
    int start_x = block.x * u_pixel_size;
    int start_y = block.y * u_pixel_size;
    int end_x = min(start_x + u_pixel_size, u_source_width);
    int end_y = min(start_y + u_pixel_size, u_source_height);
    int sample_x = start_x + max(0, end_x - start_x - 1) / 2;
    int sample_y = start_y + max(0, end_y - start_y - 1) / 2;

    vec4 source = texelFetch(u_source, ivec2(sample_x, sample_y), 0);
    if (u_preserve_transparency != 0 && source.a < 0.5) {
        frag_color = vec4(0.0);
        return;
    }
    if (source.a <= 0.000001) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 color = adjusted_srgb(source.rgb);
    color = apply_dither(color, block.x, block.y);
    color = u_use_palette != 0 && u_palette_size > 0 ? nearest_palette_color(color) : reduce_color(color);
    frag_color = vec4(color, 1.0);
}
)";

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, error);
    if (vertex_shader == 0U) {
        return 0;
    }
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, error);
    if (fragment_shader == 0U) {
        px_glDeleteShader(vertex_shader);
        return 0;
    }

    const GLuint program = px_glCreateProgram();
    px_glAttachShader(program, vertex_shader);
    px_glAttachShader(program, fragment_shader);
    px_glBindAttribLocation(program, 0, "a_position");
    px_glLinkProgram(program);
    px_glDeleteShader(vertex_shader);
    px_glDeleteShader(fragment_shader);

    GLint ok = GL_FALSE;
    px_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        error = program_log(program);
        if (error.empty()) {
            error = "OpenGL shader link failed.";
        }
        px_glDeleteProgram(program);
        return 0;
    }
    return program;
}

void set_uniform(GLuint program, const char* name, int value)
{
    const GLint location = px_glGetUniformLocation(program, name);
    if (location >= 0) {
        px_glUniform1i(location, value);
    }
}

void set_uniform(GLuint program, const char* name, float value)
{
    const GLint location = px_glGetUniformLocation(program, name);
    if (location >= 0) {
        px_glUniform1f(location, value);
    }
}

void set_uniform(GLuint program, const char* name, Color32 color)
{
    const GLint location = px_glGetUniformLocation(program, name);
    if (location >= 0) {
        px_glUniform3f(
            location,
            static_cast<float>(color.r) / 255.0F,
            static_cast<float>(color.g) / 255.0F,
            static_cast<float>(color.b) / 255.0F);
    }
}

void restore_enabled(GLenum capability, GLboolean enabled)
{
    if (enabled == GL_TRUE) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

struct GlStateGuard {
    GLint active_texture = GL_TEXTURE0;
    std::array<GLint, 4> texture_bindings = {};
    GLint framebuffer = 0;
    GLint program = 0;
    GLint vertex_array = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint unpack_alignment = 4;
    GLint pack_alignment = 4;
    GLboolean color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean blend_enabled = GL_FALSE;
    GLboolean depth_test_enabled = GL_FALSE;
    GLboolean dither_enabled = GL_FALSE;
    GLboolean scissor_test_enabled = GL_FALSE;
    GLboolean stencil_test_enabled = GL_FALSE;
    GLboolean cull_face_enabled = GL_FALSE;
    GLboolean rasterizer_discard_enabled = GL_FALSE;

    GlStateGuard()
    {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        if (px_glActiveTexture) {
            for (std::size_t index = 0; index < texture_bindings.size(); ++index) {
                px_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_bindings[index]);
            }
            px_glActiveTexture(static_cast<GLenum>(active_texture));
        }
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        if (px_glBindVertexArray) {
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);
        }
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        blend_enabled = glIsEnabled(GL_BLEND);
        depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        dither_enabled = glIsEnabled(GL_DITHER);
        scissor_test_enabled = glIsEnabled(GL_SCISSOR_TEST);
        stencil_test_enabled = glIsEnabled(GL_STENCIL_TEST);
        cull_face_enabled = glIsEnabled(GL_CULL_FACE);
        rasterizer_discard_enabled = glIsEnabled(GL_RASTERIZER_DISCARD);
    }

    ~GlStateGuard()
    {
        if (px_glBindVertexArray) {
            px_glBindVertexArray(static_cast<GLuint>(vertex_array));
        }
        if (px_glUseProgram) {
            px_glUseProgram(static_cast<GLuint>(program));
        }
        if (px_glBindFramebuffer) {
            px_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        }
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        restore_enabled(GL_BLEND, blend_enabled);
        restore_enabled(GL_DEPTH_TEST, depth_test_enabled);
        restore_enabled(GL_DITHER, dither_enabled);
        restore_enabled(GL_SCISSOR_TEST, scissor_test_enabled);
        restore_enabled(GL_STENCIL_TEST, stencil_test_enabled);
        restore_enabled(GL_CULL_FACE, cull_face_enabled);
        restore_enabled(GL_RASTERIZER_DISCARD, rasterizer_discard_enabled);
        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
        if (px_glActiveTexture) {
            for (std::size_t index = 0; index < texture_bindings.size(); ++index) {
                px_glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_bindings[index]));
            }
            px_glActiveTexture(static_cast<GLenum>(active_texture));
        }
    }
};

} // namespace

struct GpuImageProcessor::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint framebuffer = 0;
    GLuint source_texture = 0;
    GLuint output_texture = 0;
    GLuint threshold_texture = 0;
    GLuint palette_color_texture = 0;
    GLuint palette_lab_texture = 0;
    int output_width = 0;
    int output_height = 0;

    ~Impl()
    {
        if (source_texture != 0U) {
            glDeleteTextures(1, &source_texture);
        }
        if (output_texture != 0U) {
            glDeleteTextures(1, &output_texture);
        }
        if (threshold_texture != 0U) {
            glDeleteTextures(1, &threshold_texture);
        }
        if (palette_color_texture != 0U) {
            glDeleteTextures(1, &palette_color_texture);
        }
        if (palette_lab_texture != 0U) {
            glDeleteTextures(1, &palette_lab_texture);
        }
        if (framebuffer != 0U && px_glDeleteFramebuffers) {
            px_glDeleteFramebuffers(1, &framebuffer);
        }
        if (vao != 0U && px_glDeleteVertexArrays) {
            px_glDeleteVertexArrays(1, &vao);
        }
        if (program != 0U && px_glDeleteProgram) {
            px_glDeleteProgram(program);
        }
    }
};

GpuImageProcessor::GpuImageProcessor() = default;

GpuImageProcessor::~GpuImageProcessor() = default;

bool GpuImageProcessor::initialize(std::string& error)
{
    error.clear();
    if (impl_) {
        return true;
    }

    if (!SDL_GL_GetCurrentContext()) {
        error = "OpenGL export processing requires a current OpenGL context.";
        return false;
    }

    auto impl = std::make_unique<Impl>();
    if (!load_gl_functions(error)) {
        return false;
    }
    impl->program = build_program(error);
    if (impl->program == 0U) {
        return false;
    }

    px_glGenVertexArrays(1, &impl->vao);
    px_glGenFramebuffers(1, &impl->framebuffer);
    glGenTextures(1, &impl->source_texture);
    glGenTextures(1, &impl->output_texture);
    glGenTextures(1, &impl->threshold_texture);
    glGenTextures(1, &impl->palette_color_texture);
    glGenTextures(1, &impl->palette_lab_texture);
    if (impl->vao == 0U
        || impl->framebuffer == 0U
        || impl->source_texture == 0U
        || impl->output_texture == 0U
        || impl->threshold_texture == 0U
        || impl->palette_color_texture == 0U
        || impl->palette_lab_texture == 0U) {
        error = "Unable to create OpenGL export resources.";
        return false;
    }

    impl_ = std::move(impl);
    return true;
}

bool GpuImageProcessor::process_sampled_collapsed(
    const Image& source,
    const ProcessSettings& settings,
    Image& result,
    std::string& error)
{
    error.clear();
    result = {};
    if (!impl_ && !initialize(error)) {
        return false;
    }
    if (!can_process_sampled_collapsed_on_gpu(settings)
        || source.width <= 0
        || source.height <= 0
        || source.rgba.size() != static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4U) {
        error = "Image settings are not supported by the GPU sampled processor.";
        return false;
    }

    const int pixel_size = std::clamp(settings.pixel_size, 1, 256);
    const int output_width = (source.width + pixel_size - 1) / pixel_size;
    const int output_height = (source.height + pixel_size - 1) / pixel_size;
    if (output_width <= 0 || output_height <= 0) {
        error = "Invalid GPU export output dimensions.";
        return false;
    }

    const ThresholdTextureData threshold_data = build_threshold_texture_data(settings);
    const PaletteTextureData palette_data = settings.use_palette
        ? build_palette_texture_data(settings.palette)
        : PaletteTextureData{};

    const GlStateGuard state_guard;

    px_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source.width, source.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.rgba.data());

    px_glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl_->threshold_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        threshold_data.width,
        threshold_data.height,
        0,
        GL_RED,
        GL_FLOAT,
        threshold_data.values.data());

    if (settings.use_palette) {
        px_glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, impl_->palette_color_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            palette_data.size,
            1,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            palette_data.colors.data());

        px_glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, impl_->palette_lab_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB32F,
            palette_data.size,
            1,
            0,
            GL_RGB,
            GL_FLOAT,
            palette_data.labs.data());
    }

    if (impl_->output_width != output_width || impl_->output_height != output_height) {
        px_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, impl_->output_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, output_width, output_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        px_glBindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
        px_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl_->output_texture, 0);
        if (px_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            px_glBindFramebuffer(GL_FRAMEBUFFER, 0);
            error = "OpenGL export framebuffer is incomplete.";
            return false;
        }
        impl_->output_width = output_width;
        impl_->output_height = output_height;
    } else {
        px_glBindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
    }

    px_glUseProgram(impl_->program);
    px_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->source_texture);
    px_glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, impl_->threshold_texture);
    px_glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, impl_->palette_color_texture);
    px_glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, impl_->palette_lab_texture);
    px_glActiveTexture(GL_TEXTURE0);
    set_uniform(impl_->program, "u_source", 0);
    set_uniform(impl_->program, "u_thresholds", 1);
    set_uniform(impl_->program, "u_palette_colors", 2);
    set_uniform(impl_->program, "u_palette_labs", 3);
    set_uniform(impl_->program, "u_source_width", source.width);
    set_uniform(impl_->program, "u_source_height", source.height);
    set_uniform(impl_->program, "u_pixel_size", pixel_size);
    set_uniform(impl_->program, "u_color_levels", settings.color_levels);
    set_uniform(impl_->program, "u_preserve_transparency", settings.preserve_transparency ? 1 : 0);
    set_uniform(impl_->program, "u_threshold_width", threshold_data.width);
    set_uniform(impl_->program, "u_threshold_height", threshold_data.height);
    set_uniform(impl_->program, "u_threshold_enabled", threshold_data.enabled ? 1 : 0);
    set_uniform(impl_->program, "u_use_palette", settings.use_palette ? 1 : 0);
    set_uniform(impl_->program, "u_palette_size", palette_data.size);
    set_uniform(impl_->program, "u_dither_amount", settings.dither_amount);
    set_uniform(impl_->program, "u_brightness", settings.adjustments.brightness);
    set_uniform(impl_->program, "u_contrast", settings.adjustments.contrast);
    set_uniform(impl_->program, "u_gamma", settings.adjustments.gamma);
    set_uniform(impl_->program, "u_input_black", settings.adjustments.input_black);
    set_uniform(impl_->program, "u_input_white", settings.adjustments.input_white);
    set_uniform(impl_->program, "u_output_black", settings.adjustments.output_black);
    set_uniform(impl_->program, "u_output_white", settings.adjustments.output_white);
    set_uniform(impl_->program, "u_saturation", settings.adjustments.saturation);
    set_uniform(impl_->program, "u_tint", settings.adjustments.tint);
    set_uniform(impl_->program, "u_tint_strength", settings.adjustments.tint_strength);

    px_glBindVertexArray(impl_->vao);
    glViewport(0, 0, output_width, output_height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    result.width = output_width;
    result.height = output_height;
    result.rgba.resize(static_cast<std::size_t>(output_width) * static_cast<std::size_t>(output_height) * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, output_width, output_height, GL_RGBA, GL_UNSIGNED_BYTE, result.rgba.data());

    px_glBindVertexArray(0);
    px_glUseProgram(0);
    px_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        result = {};
        error = "OpenGL export processing failed with error " + std::to_string(static_cast<unsigned int>(gl_error)) + ".";
        return false;
    }
    return true;
}

bool can_process_sampled_collapsed_on_gpu(const ProcessSettings& settings)
{
    if (!supports_low_quality_process(settings) || settings.reduction_max_colors > 0) {
        return false;
    }
    if (settings.use_palette && (settings.palette.empty() || settings.palette.size() > 256U)) {
        return false;
    }
    const float dither_amount = settings.dither_amount > 1.0F ? settings.dither_amount / 100.0F : settings.dither_amount;
    return dither_amount <= 0.0F
        || supports_gpu_dither_mode(settings.dither_mode);
}

} // namespace pixatto
