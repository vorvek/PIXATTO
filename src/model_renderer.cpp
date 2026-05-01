#include "pixelizer/model_renderer.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace pixelizer {
namespace {

using GlActiveTexture = void(APIENTRY*)(GLenum);
using GlAttachShader = void(APIENTRY*)(GLuint, GLuint);
using GlBindAttribLocation = void(APIENTRY*)(GLuint, GLuint, const GLchar*);
using GlBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBindRenderbuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBindVertexArray = void(APIENTRY*)(GLuint);
using GlBufferData = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
using GlCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using GlCompileShader = void(APIENTRY*)(GLuint);
using GlCreateProgram = GLuint(APIENTRY*)();
using GlCreateShader = GLuint(APIENTRY*)(GLenum);
using GlDeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlDeleteProgram = void(APIENTRY*)(GLuint);
using GlDeleteRenderbuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlDeleteShader = void(APIENTRY*)(GLuint);
using GlDeleteVertexArrays = void(APIENTRY*)(GLsizei, const GLuint*);
using GlEnableVertexAttribArray = void(APIENTRY*)(GLuint);
using GlDisableVertexAttribArray = void(APIENTRY*)(GLuint);
using GlFramebufferRenderbuffer = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using GlFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlGenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlGenRenderbuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlGenVertexArrays = void(APIENTRY*)(GLsizei, GLuint*);
using GlGetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlGetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlGetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetUniformLocation = GLint(APIENTRY*)(GLuint, const GLchar*);
using GlLinkProgram = void(APIENTRY*)(GLuint);
using GlRenderbufferStorage = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using GlShaderSource = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GlUniform1i = void(APIENTRY*)(GLint, GLint);
using GlUniformMatrix4fv = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using GlUseProgram = void(APIENTRY*)(GLuint);
using GlVertexAttribPointer = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);

GlActiveTexture px_glActiveTexture = nullptr;
GlAttachShader px_glAttachShader = nullptr;
GlBindAttribLocation px_glBindAttribLocation = nullptr;
GlBindBuffer px_glBindBuffer = nullptr;
GlBindFramebuffer px_glBindFramebuffer = nullptr;
GlBindRenderbuffer px_glBindRenderbuffer = nullptr;
GlBindVertexArray px_glBindVertexArray = nullptr;
GlBufferData px_glBufferData = nullptr;
GlCheckFramebufferStatus px_glCheckFramebufferStatus = nullptr;
GlCompileShader px_glCompileShader = nullptr;
GlCreateProgram px_glCreateProgram = nullptr;
GlCreateShader px_glCreateShader = nullptr;
GlDeleteBuffers px_glDeleteBuffers = nullptr;
GlDeleteFramebuffers px_glDeleteFramebuffers = nullptr;
GlDeleteProgram px_glDeleteProgram = nullptr;
GlDeleteRenderbuffers px_glDeleteRenderbuffers = nullptr;
GlDeleteShader px_glDeleteShader = nullptr;
GlDeleteVertexArrays px_glDeleteVertexArrays = nullptr;
GlDisableVertexAttribArray px_glDisableVertexAttribArray = nullptr;
GlEnableVertexAttribArray px_glEnableVertexAttribArray = nullptr;
GlFramebufferRenderbuffer px_glFramebufferRenderbuffer = nullptr;
GlFramebufferTexture2D px_glFramebufferTexture2D = nullptr;
GlGenBuffers px_glGenBuffers = nullptr;
GlGenFramebuffers px_glGenFramebuffers = nullptr;
GlGenRenderbuffers px_glGenRenderbuffers = nullptr;
GlGenVertexArrays px_glGenVertexArrays = nullptr;
GlGetProgramInfoLog px_glGetProgramInfoLog = nullptr;
GlGetProgramiv px_glGetProgramiv = nullptr;
GlGetShaderInfoLog px_glGetShaderInfoLog = nullptr;
GlGetShaderiv px_glGetShaderiv = nullptr;
GlGetUniformLocation px_glGetUniformLocation = nullptr;
GlLinkProgram px_glLinkProgram = nullptr;
GlRenderbufferStorage px_glRenderbufferStorage = nullptr;
GlShaderSource px_glShaderSource = nullptr;
GlUniform1i px_glUniform1i = nullptr;
GlUniformMatrix4fv px_glUniformMatrix4fv = nullptr;
GlUseProgram px_glUseProgram = nullptr;
GlVertexAttribPointer px_glVertexAttribPointer = nullptr;

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
        && load_gl_proc(px_glBindBuffer, "glBindBuffer", error)
        && load_gl_proc(px_glBindFramebuffer, "glBindFramebuffer", error)
        && load_gl_proc(px_glBindRenderbuffer, "glBindRenderbuffer", error)
        && load_gl_proc(px_glBindVertexArray, "glBindVertexArray", error)
        && load_gl_proc(px_glBufferData, "glBufferData", error)
        && load_gl_proc(px_glCheckFramebufferStatus, "glCheckFramebufferStatus", error)
        && load_gl_proc(px_glCompileShader, "glCompileShader", error)
        && load_gl_proc(px_glCreateProgram, "glCreateProgram", error)
        && load_gl_proc(px_glCreateShader, "glCreateShader", error)
        && load_gl_proc(px_glDeleteBuffers, "glDeleteBuffers", error)
        && load_gl_proc(px_glDeleteFramebuffers, "glDeleteFramebuffers", error)
        && load_gl_proc(px_glDeleteProgram, "glDeleteProgram", error)
        && load_gl_proc(px_glDeleteRenderbuffers, "glDeleteRenderbuffers", error)
        && load_gl_proc(px_glDeleteShader, "glDeleteShader", error)
        && load_gl_proc(px_glDeleteVertexArrays, "glDeleteVertexArrays", error)
        && load_gl_proc(px_glDisableVertexAttribArray, "glDisableVertexAttribArray", error)
        && load_gl_proc(px_glEnableVertexAttribArray, "glEnableVertexAttribArray", error)
        && load_gl_proc(px_glFramebufferRenderbuffer, "glFramebufferRenderbuffer", error)
        && load_gl_proc(px_glFramebufferTexture2D, "glFramebufferTexture2D", error)
        && load_gl_proc(px_glGenBuffers, "glGenBuffers", error)
        && load_gl_proc(px_glGenFramebuffers, "glGenFramebuffers", error)
        && load_gl_proc(px_glGenRenderbuffers, "glGenRenderbuffers", error)
        && load_gl_proc(px_glGenVertexArrays, "glGenVertexArrays", error)
        && load_gl_proc(px_glGetProgramInfoLog, "glGetProgramInfoLog", error)
        && load_gl_proc(px_glGetProgramiv, "glGetProgramiv", error)
        && load_gl_proc(px_glGetShaderInfoLog, "glGetShaderInfoLog", error)
        && load_gl_proc(px_glGetShaderiv, "glGetShaderiv", error)
        && load_gl_proc(px_glGetUniformLocation, "glGetUniformLocation", error)
        && load_gl_proc(px_glLinkProgram, "glLinkProgram", error)
        && load_gl_proc(px_glRenderbufferStorage, "glRenderbufferStorage", error)
        && load_gl_proc(px_glShaderSource, "glShaderSource", error)
        && load_gl_proc(px_glUniform1i, "glUniform1i", error)
        && load_gl_proc(px_glUniformMatrix4fv, "glUniformMatrix4fv", error)
        && load_gl_proc(px_glUseProgram, "glUseProgram", error)
        && load_gl_proc(px_glVertexAttribPointer, "glVertexAttribPointer", error);
}

struct Mat4 {
    std::array<float, 16> m = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

Mat4 multiply(Mat4 lhs, Mat4 rhs)
{
    Mat4 result;
    result.m.fill(0.0F);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[static_cast<std::size_t>(column * 4 + row)] +=
                    lhs.m[static_cast<std::size_t>(k * 4 + row)] * rhs.m[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Vec3 subtract(Vec3 lhs, Vec3 rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {value.x / length, value.y / length, value.z / length};
}

Vec3 cross(Vec3 lhs, Vec3 rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

float dot(Vec3 lhs, Vec3 rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up)
{
    const Vec3 forward = normalize(subtract(center, eye));
    const Vec3 side = normalize(cross(forward, up));
    const Vec3 real_up = cross(side, forward);

    Mat4 result;
    result.m = {
        side.x, real_up.x, -forward.x, 0.0F,
        side.y, real_up.y, -forward.y, 0.0F,
        side.z, real_up.z, -forward.z, 0.0F,
        -dot(side, eye), -dot(real_up, eye), dot(forward, eye), 1.0F,
    };
    return result;
}

Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane)
{
    const float f = 1.0F / std::tan(fov_y * 0.5F);
    Mat4 result;
    result.m.fill(0.0F);
    result.m[0] = f / std::max(aspect, 0.001F);
    result.m[5] = f;
    result.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    result.m[11] = -1.0F;
    result.m[14] = (2.0F * far_plane * near_plane) / (near_plane - far_plane);
    return result;
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

GLuint build_program(const std::string& glsl_version, GLint& mvp_location, GLint& texture_location, std::string& error)
{
    const std::string vertex_source = glsl_version + R"(
in vec3 a_position;
in vec2 a_uv;
out vec2 v_uv;
uniform mat4 u_mvp;
void main()
{
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

    const std::string fragment_source = glsl_version + R"(
in vec2 v_uv;
out vec4 frag_color;
uniform sampler2D u_texture;
void main()
{
    frag_color = texture(u_texture, v_uv);
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
    px_glBindAttribLocation(program, 1, "a_uv");
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

    mvp_location = px_glGetUniformLocation(program, "u_mvp");
    texture_location = px_glGetUniformLocation(program, "u_texture");
    return program;
}

GLuint create_fallback_texture()
{
    static constexpr std::array<unsigned char, 4U> pixels = {170, 172, 176, 255};

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return texture;
}

void upload_texture(GLuint& texture, const Image& image, bool nearest)
{
    if (image.empty()) {
        return;
    }

    if (texture == 0U) {
        glGenTextures(1, &texture);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        image.width,
        image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.rgba.data());
}

} // namespace

struct ModelRenderer::Impl {
    struct Primitive {
        GLuint vbo = 0;
        GLsizei vertex_count = 0;
        int texture_index = -1;
    };

    GLuint program = 0;
    GLint mvp_location = -1;
    GLint texture_location = -1;
    GLuint vao = 0;
    GLuint framebuffer = 0;
    GLuint color_texture = 0;
    GLuint depth_renderbuffer = 0;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    GLuint fallback_texture = 0;
    std::vector<Primitive> primitives;
    std::vector<GLuint> processed_textures;
};

ModelRenderer::~ModelRenderer()
{
    shutdown();
}

bool ModelRenderer::initialize(std::string glsl_version, std::string& error)
{
    if (impl_) {
        return true;
    }

    if (!load_gl_functions(error)) {
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->program = build_program(glsl_version, impl->mvp_location, impl->texture_location, error);
    if (impl->program == 0U) {
        return false;
    }
    px_glGenVertexArrays(1, &impl->vao);
    impl->fallback_texture = create_fallback_texture();

    impl_ = impl.release();
    return true;
}

void ModelRenderer::shutdown()
{
    if (!impl_) {
        return;
    }

    for (Impl::Primitive& primitive : impl_->primitives) {
        if (primitive.vbo != 0U) {
            px_glDeleteBuffers(1, &primitive.vbo);
        }
    }
    for (GLuint texture : impl_->processed_textures) {
        if (texture != 0U) {
            glDeleteTextures(1, &texture);
        }
    }
    if (impl_->fallback_texture != 0U) {
        glDeleteTextures(1, &impl_->fallback_texture);
    }
    if (impl_->color_texture != 0U) {
        glDeleteTextures(1, &impl_->color_texture);
    }
    if (impl_->depth_renderbuffer != 0U) {
        px_glDeleteRenderbuffers(1, &impl_->depth_renderbuffer);
    }
    if (impl_->framebuffer != 0U) {
        px_glDeleteFramebuffers(1, &impl_->framebuffer);
    }
    if (impl_->vao != 0U) {
        px_glDeleteVertexArrays(1, &impl_->vao);
    }
    if (impl_->program != 0U) {
        px_glDeleteProgram(impl_->program);
    }

    delete impl_;
    impl_ = nullptr;
}

bool ModelRenderer::upload_model(
    const ModelDocument& model,
    const std::vector<Image>& processed_textures,
    std::string& error)
{
    if (!impl_) {
        error = "OpenGL model renderer is not initialized.";
        return false;
    }

    for (Impl::Primitive& primitive : impl_->primitives) {
        if (primitive.vbo != 0U) {
            px_glDeleteBuffers(1, &primitive.vbo);
        }
    }
    impl_->primitives.clear();

    for (const ModelPrimitive& source : model.primitives) {
        if (source.vertices.empty()) {
            continue;
        }

        std::vector<float> packed;
        packed.reserve(source.vertices.size() * 5U);
        for (const ModelVertex& vertex : source.vertices) {
            packed.push_back(vertex.x);
            packed.push_back(vertex.y);
            packed.push_back(vertex.z);
            packed.push_back(vertex.u);
            packed.push_back(vertex.v);
        }

        Impl::Primitive primitive;
        primitive.vertex_count = static_cast<GLsizei>(source.vertices.size());
        primitive.texture_index = source.texture_index;
        px_glGenBuffers(1, &primitive.vbo);
        px_glBindBuffer(GL_ARRAY_BUFFER, primitive.vbo);
        px_glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(packed.size() * sizeof(float)),
            packed.data(),
            GL_STATIC_DRAW);
        impl_->primitives.push_back(primitive);
    }
    px_glBindBuffer(GL_ARRAY_BUFFER, 0);

    update_processed_textures(processed_textures);
    return true;
}

void ModelRenderer::update_processed_textures(const std::vector<Image>& processed_textures)
{
    if (!impl_) {
        return;
    }

    while (impl_->processed_textures.size() > processed_textures.size()) {
        GLuint texture = impl_->processed_textures.back();
        if (texture != 0U) {
            glDeleteTextures(1, &texture);
        }
        impl_->processed_textures.pop_back();
    }
    impl_->processed_textures.resize(processed_textures.size(), 0U);

    for (std::size_t index = 0; index < processed_textures.size(); ++index) {
        upload_texture(impl_->processed_textures[index], processed_textures[index], true);
    }
}

std::uintptr_t ModelRenderer::render_preview(
    const ModelDocument& model,
    int width,
    int height,
    float yaw,
    float pitch,
    float distance,
    float target_offset_x,
    float target_offset_y,
    float target_offset_z,
    std::string& error)
{
    if (!impl_) {
        error = "OpenGL model renderer is not initialized.";
        return 0U;
    }
    if (width <= 0 || height <= 0 || impl_->primitives.empty()) {
        return 0U;
    }

    GLint previous_framebuffer = 0;
    GLint previous_renderbuffer = 0;
    GLint previous_texture = 0;
    GLint previous_viewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_renderbuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);

    if (impl_->framebuffer == 0U || impl_->framebuffer_width != width || impl_->framebuffer_height != height) {
        if (impl_->color_texture != 0U) {
            glDeleteTextures(1, &impl_->color_texture);
            impl_->color_texture = 0U;
        }
        if (impl_->depth_renderbuffer != 0U) {
            px_glDeleteRenderbuffers(1, &impl_->depth_renderbuffer);
            impl_->depth_renderbuffer = 0U;
        }
        if (impl_->framebuffer == 0U) {
            px_glGenFramebuffers(1, &impl_->framebuffer);
        }

        glGenTextures(1, &impl_->color_texture);
        glBindTexture(GL_TEXTURE_2D, impl_->color_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        px_glGenRenderbuffers(1, &impl_->depth_renderbuffer);
        px_glBindRenderbuffer(GL_RENDERBUFFER, impl_->depth_renderbuffer);
        px_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

        px_glBindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
        px_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl_->color_texture, 0);
        px_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, impl_->depth_renderbuffer);
        if (px_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = "OpenGL framebuffer is incomplete.";
            px_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
            px_glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_renderbuffer));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
            return 0U;
        }

        impl_->framebuffer_width = width;
        impl_->framebuffer_height = height;
    }

    px_glBindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.08F, 0.09F, 0.11F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Vec3 center = {
        model.center[0] + target_offset_x,
        model.center[1] + target_offset_y,
        model.center[2] + target_offset_z,
    };
    const float radius = std::max(model.radius, 0.1F);
    const float camera_distance = std::max(distance, 0.8F) * radius;
    const float clamped_pitch = std::clamp(pitch, -1.45F, 1.45F);
    const Vec3 eye = {
        center.x + std::sin(yaw) * std::cos(clamped_pitch) * camera_distance,
        center.y + std::sin(clamped_pitch) * camera_distance,
        center.z + std::cos(yaw) * std::cos(clamped_pitch) * camera_distance,
    };
    const Mat4 view = look_at(eye, center, {0.0F, 1.0F, 0.0F});
    const Mat4 projection = perspective(45.0F * 3.1415926535F / 180.0F, static_cast<float>(width) / static_cast<float>(height), radius * 0.01F, radius * 100.0F + camera_distance);
    const Mat4 mvp = multiply(projection, view);

    px_glUseProgram(impl_->program);
    px_glUniformMatrix4fv(impl_->mvp_location, 1, GL_FALSE, mvp.m.data());
    px_glUniform1i(impl_->texture_location, 0);
    px_glActiveTexture(GL_TEXTURE0);
    px_glBindVertexArray(impl_->vao);

    for (const Impl::Primitive& primitive : impl_->primitives) {
        GLuint texture = impl_->fallback_texture;
        if (primitive.texture_index >= 0 && primitive.texture_index < static_cast<int>(impl_->processed_textures.size())
            && impl_->processed_textures[static_cast<std::size_t>(primitive.texture_index)] != 0U) {
            texture = impl_->processed_textures[static_cast<std::size_t>(primitive.texture_index)];
        }

        glBindTexture(GL_TEXTURE_2D, texture);
        px_glBindBuffer(GL_ARRAY_BUFFER, primitive.vbo);
        px_glEnableVertexAttribArray(0);
        px_glEnableVertexAttribArray(1);
        px_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(float) * 5U), nullptr);
        px_glVertexAttribPointer(
            1,
            2,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(float) * 5U),
            reinterpret_cast<const void*>(sizeof(float) * 3U));
        glDrawArrays(GL_TRIANGLES, 0, primitive.vertex_count);
    }

    px_glDisableVertexAttribArray(0);
    px_glDisableVertexAttribArray(1);
    px_glBindBuffer(GL_ARRAY_BUFFER, 0);
    px_glBindVertexArray(0);
    px_glUseProgram(0);

    px_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    px_glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_renderbuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    return static_cast<std::uintptr_t>(impl_->color_texture);
}

} // namespace pixelizer
