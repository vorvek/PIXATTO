#include "pixatto/gpu_image_processor.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

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
uniform int u_source_width;
uniform int u_source_height;
uniform int u_pixel_size;
uniform int u_color_levels;
uniform int u_preserve_transparency;
uniform int u_dither_mode;
uniform int u_bayer_size;
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

int normalized_bayer_size(int size)
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

float bayer_threshold(int x, int y)
{
    int size = normalized_bayer_size(u_bayer_size);
    int wrapped_x = x - (x / size) * size;
    int wrapped_y = y - (y / size) * size;
    int index = 0;
    int scale = 1;
    for (int step = size; step > 1; step = step / 2) {
        int half_step = step / 2;
        int qx = int(mod(float(wrapped_x / half_step), 2.0));
        int qy = int(mod(float(wrapped_y / half_step), 2.0));
        int cell = qy == 0 ? (qx == 0 ? 0 : 2) : (qx == 0 ? 3 : 1);
        index += scale * cell;
        scale *= 4;
    }
    return (float(index) + 0.5) / float(size * size) - 0.5;
}

vec3 apply_dither(vec3 color, int block_x, int block_y)
{
    float amount = u_dither_amount > 1.0 ? u_dither_amount / 100.0 : u_dither_amount;
    amount = clamp01(amount);
    if (u_dither_mode != 1 || amount <= 0.0) {
        return color;
    }
    float offset = bayer_threshold(block_x, block_y) * amount * 0.35;
    return clamp(color + vec3(offset), 0.0, 1.0);
}

vec3 reduce_color(vec3 color)
{
    float levels = float(clamp(u_color_levels, 2, 64));
    float max_level = levels - 1.0;
    return round(clamp(color, 0.0, 1.0) * max_level) / max_level;
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
    frag_color = vec4(reduce_color(color), 1.0);
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

int dither_mode_uniform(const ProcessSettings& settings)
{
    const float amount = settings.dither_amount > 1.0F ? settings.dither_amount / 100.0F : settings.dither_amount;
    if (amount <= 0.0F || settings.dither_mode == DitherMode::None) {
        return 0;
    }
    return settings.dither_mode == DitherMode::Bayer ? 1 : 0;
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
    GLint texture0_binding = 0;
    GLint framebuffer = 0;
    GLint program = 0;
    GLint vertex_array = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint unpack_alignment = 4;
    GLint pack_alignment = 4;
    GLboolean blend_enabled = GL_FALSE;
    GLboolean depth_test_enabled = GL_FALSE;
    GLboolean dither_enabled = GL_FALSE;

    GlStateGuard()
    {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        if (px_glActiveTexture) {
            px_glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0_binding);
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
        blend_enabled = glIsEnabled(GL_BLEND);
        depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        dither_enabled = glIsEnabled(GL_DITHER);
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
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment);
        if (px_glActiveTexture) {
            px_glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0_binding));
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
    if (impl->vao == 0U || impl->framebuffer == 0U || impl->source_texture == 0U || impl->output_texture == 0U) {
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

    const GlStateGuard state_guard;

    glBindTexture(GL_TEXTURE_2D, impl_->source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source.width, source.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, source.rgba.data());

    if (impl_->output_width != output_width || impl_->output_height != output_height) {
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
    set_uniform(impl_->program, "u_source", 0);
    set_uniform(impl_->program, "u_source_width", source.width);
    set_uniform(impl_->program, "u_source_height", source.height);
    set_uniform(impl_->program, "u_pixel_size", pixel_size);
    set_uniform(impl_->program, "u_color_levels", settings.color_levels);
    set_uniform(impl_->program, "u_preserve_transparency", settings.preserve_transparency ? 1 : 0);
    set_uniform(impl_->program, "u_dither_mode", dither_mode_uniform(settings));
    set_uniform(impl_->program, "u_bayer_size", settings.bayer_matrix_size);
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
    if (!supports_low_quality_process(settings) || settings.use_palette || settings.reduction_max_colors > 0) {
        return false;
    }
    const float dither_amount = settings.dither_amount > 1.0F ? settings.dither_amount / 100.0F : settings.dither_amount;
    return dither_amount <= 0.0F
        || settings.dither_mode == DitherMode::None
        || settings.dither_mode == DitherMode::Bayer;
}

} // namespace pixatto
