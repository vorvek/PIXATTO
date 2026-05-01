#include "pixelizer/model.hpp"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <tinyxml2.h>
#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixelizer {
namespace {

struct Mat4 {
    std::array<float, 16> m = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct Bounds {
    std::array<float, 3> minimum = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    std::array<float, 3> maximum = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    bool has_points = false;
};

struct ColladaSource {
    std::vector<float> values;
    std::size_t stride = 1;
    std::size_t offset = 0;
};

struct ColladaInput {
    std::string semantic;
    std::string source;
    std::size_t offset = 0;
};

enum class ColladaPrimitiveKind {
    Triangles,
    Polylist,
};

struct ColladaPrimitiveData {
    ColladaPrimitiveKind kind = ColladaPrimitiveKind::Triangles;
    std::string material_symbol;
    std::vector<ColladaInput> inputs;
    std::vector<int> indices;
    std::vector<int> vertex_counts;
};

struct ColladaGeometry {
    int index = -1;
    std::string id;
    std::string name;
    std::map<std::string, ColladaSource> sources;
    std::map<std::string, std::string> vertices_positions;
    std::vector<ColladaPrimitiveData> primitives;
};

struct ColladaImage {
    std::string id;
    std::string name;
    std::string uri;
};

struct ColladaEffect {
    std::string id;
    std::string image_id;
};

struct ColladaMaterial {
    int index = -1;
    std::string id;
    std::string name;
    std::string effect_id;
};

struct ColladaDocument {
    std::map<std::string, ColladaGeometry> geometries;
    std::map<std::string, ColladaImage> images;
    std::map<std::string, ColladaEffect> effects;
    std::map<std::string, ColladaMaterial> materials;
    const tinyxml2::XMLElement* visual_scene = nullptr;
};

std::string lower_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

void add_warning(std::string& warnings, std::string_view warning)
{
    if (warning.empty()) {
        return;
    }
    if (!warnings.empty()) {
        warnings += " ";
    }
    warnings += warning;
}

std::string file_name_or(std::filesystem::path path, std::string fallback)
{
    const std::string name = path.filename().string();
    return name.empty() ? std::move(fallback) : name;
}

std::string ufbx_string_to_std(ufbx_string value)
{
    if (!value.data || value.length == 0U) {
        return {};
    }
    return {value.data, value.length};
}

std::string ufbx_error_description(const ufbx_error& error)
{
    return ufbx_string_to_std(error.description);
}

std::string strip_url_id(std::string value)
{
    const std::size_t fragment = value.find('#');
    if (fragment != std::string::npos) {
        value.erase(0, fragment + 1U);
    }
    return value;
}

std::string xml_local_name(const char* name)
{
    if (!name) {
        return {};
    }
    std::string value(name);
    if (const std::size_t separator = value.find(':'); separator != std::string::npos) {
        value.erase(0, separator + 1U);
    }
    return value;
}

bool xml_name_is(const tinyxml2::XMLElement* element, std::string_view name)
{
    return element && xml_local_name(element->Name()) == name;
}

const tinyxml2::XMLElement* first_child_named(const tinyxml2::XMLElement* parent, std::string_view name)
{
    if (!parent) {
        return nullptr;
    }
    for (const tinyxml2::XMLElement* child = parent->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (xml_name_is(child, name)) {
            return child;
        }
    }
    return nullptr;
}

const tinyxml2::XMLElement* next_sibling_named(const tinyxml2::XMLElement* element, std::string_view name)
{
    for (const tinyxml2::XMLElement* sibling = element ? element->NextSiblingElement() : nullptr; sibling;
         sibling = sibling->NextSiblingElement()) {
        if (xml_name_is(sibling, name)) {
            return sibling;
        }
    }
    return nullptr;
}

std::string xml_attr(const tinyxml2::XMLElement* element, const char* name)
{
    if (!element) {
        return {};
    }
    if (const char* value = element->Attribute(name)) {
        return value;
    }
    return {};
}

std::string xml_text(const tinyxml2::XMLElement* element)
{
    if (!element) {
        return {};
    }
    if (const char* value = element->GetText()) {
        return value;
    }
    return {};
}

std::vector<double> parse_double_list(const std::string& text)
{
    std::vector<double> values;
    std::istringstream stream(text);
    double value = 0.0;
    while (stream >> value) {
        values.push_back(value);
    }
    return values;
}

std::vector<int> parse_int_list(const std::string& text)
{
    std::vector<int> values;
    std::istringstream stream(text);
    int value = 0;
    while (stream >> value) {
        values.push_back(value);
    }
    return values;
}

std::string with_png_extension(std::string name)
{
    std::filesystem::path path(name);
    if (path.extension().empty()) {
        path.replace_extension(".png");
        return path.string();
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".png") {
        path.replace_extension(".png");
    }
    return path.string();
}

Image rgba_from_components(int width, int height, int component_count, const std::vector<unsigned char>& bytes)
{
    if (width <= 0 || height <= 0 || component_count <= 0 || bytes.empty()) {
        return {};
    }

    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (bytes.size() < pixel_count * static_cast<std::size_t>(component_count)) {
        return {};
    }

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.reserve(pixel_count * 4U);

    const unsigned char* source = bytes.data();
    for (std::size_t index = 0; index < pixel_count; ++index) {
        if (component_count == 1) {
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[0]);
            image.rgba.push_back(255U);
        } else if (component_count == 2) {
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[1]);
        } else if (component_count == 3) {
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[1]);
            image.rgba.push_back(source[2]);
            image.rgba.push_back(255U);
        } else {
            image.rgba.push_back(source[0]);
            image.rgba.push_back(source[1]);
            image.rgba.push_back(source[2]);
            image.rgba.push_back(source[3]);
        }
        source += component_count;
    }

    return image;
}

std::filesystem::path external_texture_path(const std::filesystem::path& base_dir, std::string filename)
{
    for (char& ch : filename) {
        if (ch == '\\') {
            ch = std::filesystem::path::preferred_separator;
        }
    }

    std::filesystem::path path(filename);
    return path.is_absolute() ? path : base_dir / path;
}

int ufbx_typed_id_to_int(std::uint32_t id)
{
    if (id > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return -1;
    }
    return static_cast<int>(id);
}

const ufbx_texture* first_file_texture(const ufbx_texture* texture)
{
    if (!texture) {
        return nullptr;
    }
    if (texture->type == UFBX_TEXTURE_FILE) {
        return texture;
    }
    for (std::size_t index = 0; index < texture->file_textures.count; ++index) {
        const ufbx_texture* candidate = texture->file_textures.data[index];
        if (candidate && candidate->type == UFBX_TEXTURE_FILE) {
            return candidate;
        }
    }
    return nullptr;
}

const ufbx_texture* diffuse_texture_for_fbx_material(const ufbx_material* material)
{
    if (!material) {
        return nullptr;
    }

    const std::array<const ufbx_material_map*, 2> maps = {
        &material->pbr.base_color,
        &material->fbx.diffuse_color,
    };
    for (const ufbx_material_map* map : maps) {
        if (map && map->texture && map->texture_enabled) {
            if (const ufbx_texture* texture = first_file_texture(map->texture)) {
                return texture;
            }
        }
    }
    return nullptr;
}

std::string fbx_texture_filename(const ufbx_texture* texture)
{
    if (!texture) {
        return {};
    }
    std::string filename = ufbx_string_to_std(texture->filename);
    if (filename.empty()) {
        filename = ufbx_string_to_std(texture->relative_filename);
    }
    if (filename.empty() && texture->video) {
        filename = ufbx_string_to_std(texture->video->filename);
    }
    if (filename.empty() && texture->video) {
        filename = ufbx_string_to_std(texture->video->relative_filename);
    }
    return filename;
}

const ufbx_blob* fbx_texture_content(const ufbx_texture* texture)
{
    if (!texture) {
        return nullptr;
    }
    if (texture->content.data && texture->content.size > 0U) {
        return &texture->content;
    }
    if (texture->video && texture->video->content.data && texture->video->content.size > 0U) {
        return &texture->video->content;
    }
    return nullptr;
}

ModelTexture load_fbx_texture(
    const std::filesystem::path& base_dir,
    const ufbx_texture* source,
    std::string& warning,
    bool& ok)
{
    ok = false;
    if (!source) {
        return {};
    }

    ModelTexture texture;
    const std::string filename = fbx_texture_filename(source);
    if (const ufbx_blob* content = fbx_texture_content(source)) {
        const auto* bytes = static_cast<const std::uint8_t*>(content->data);
        ImageLoadResult loaded = load_image_rgba_memory(bytes, content->size);
        if (!loaded.error.empty()) {
            add_warning(warning, "Embedded FBX texture could not be loaded; using fallback.");
            return {};
        }

        texture.embedded = true;
        texture.image = std::move(loaded.image);
        if (!filename.empty()) {
            texture.name = with_png_extension(file_name_or(std::filesystem::path(filename), "embedded_texture.png"));
        } else {
            const std::string source_name = ufbx_string_to_std(source->name);
            texture.name = source_name.empty() ? "embedded_texture.png" : with_png_extension(source_name);
        }
        ok = true;
        return texture;
    }

    if (filename.empty()) {
        add_warning(warning, "FBX texture has no image file reference; using fallback.");
        return {};
    }

    const std::filesystem::path texture_path = external_texture_path(base_dir, filename);
    ImageLoadResult loaded = load_image_rgba(texture_path.string());
    if (!loaded.error.empty()) {
        add_warning(warning, "Texture " + texture_path.filename().string() + " could not be loaded; using fallback.");
        return {};
    }

    texture.name = file_name_or(texture_path, filename);
    texture.source_path = texture_path;
    texture.embedded = false;
    texture.image = std::move(loaded.image);
    ok = true;
    return texture;
}

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

Mat4 translation_matrix(const std::vector<double>& translation)
{
    Mat4 result;
    if (translation.size() >= 3U) {
        result.m[12] = static_cast<float>(translation[0]);
        result.m[13] = static_cast<float>(translation[1]);
        result.m[14] = static_cast<float>(translation[2]);
    }
    return result;
}

Mat4 scale_matrix(const std::vector<double>& scale)
{
    Mat4 result;
    if (scale.size() >= 3U) {
        result.m[0] = static_cast<float>(scale[0]);
        result.m[5] = static_cast<float>(scale[1]);
        result.m[10] = static_cast<float>(scale[2]);
    }
    return result;
}

Mat4 rotation_matrix(const std::vector<double>& rotation)
{
    Mat4 result;
    if (rotation.size() < 4U) {
        return result;
    }

    const float x = static_cast<float>(rotation[0]);
    const float y = static_cast<float>(rotation[1]);
    const float z = static_cast<float>(rotation[2]);
    const float w = static_cast<float>(rotation[3]);
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    result.m = {
        1.0F - 2.0F * (yy + zz), 2.0F * (xy + wz), 2.0F * (xz - wy), 0.0F,
        2.0F * (xy - wz), 1.0F - 2.0F * (xx + zz), 2.0F * (yz + wx), 0.0F,
        2.0F * (xz + wy), 2.0F * (yz - wx), 1.0F - 2.0F * (xx + yy), 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    return result;
}

Mat4 axis_angle_matrix(const std::vector<double>& rotation)
{
    Mat4 result;
    if (rotation.size() < 4U) {
        return result;
    }

    float x = static_cast<float>(rotation[0]);
    float y = static_cast<float>(rotation[1]);
    float z = static_cast<float>(rotation[2]);
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0F) {
        return result;
    }

    x /= length;
    y /= length;
    z /= length;

    constexpr float kPi = 3.14159265358979323846F;
    const float radians = static_cast<float>(rotation[3]) * kPi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0F - c;

    result.m = {
        t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0.0F,
        t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0.0F,
        t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    return result;
}

Mat4 row_major_matrix(const std::vector<double>& values)
{
    Mat4 result;
    if (values.size() < 16U) {
        return result;
    }

    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            result.m[column * 4U + row] = static_cast<float>(values[row * 4U + column]);
        }
    }
    return result;
}

Mat4 collada_node_transform(const tinyxml2::XMLElement* node)
{
    Mat4 local;
    if (!node) {
        return local;
    }

    for (const tinyxml2::XMLElement* child = node->FirstChildElement(); child; child = child->NextSiblingElement()) {
        const std::string name = xml_local_name(child->Name());
        if (name == "matrix") {
            local = multiply(local, row_major_matrix(parse_double_list(xml_text(child))));
        } else if (name == "translate") {
            local = multiply(local, translation_matrix(parse_double_list(xml_text(child))));
        } else if (name == "scale") {
            local = multiply(local, scale_matrix(parse_double_list(xml_text(child))));
        } else if (name == "rotate") {
            local = multiply(local, axis_angle_matrix(parse_double_list(xml_text(child))));
        }
    }
    return local;
}

Mat4 node_matrix(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16U) {
        Mat4 result;
        for (std::size_t index = 0; index < result.m.size(); ++index) {
            result.m[index] = static_cast<float>(node.matrix[index]);
        }
        return result;
    }

    return multiply(multiply(translation_matrix(node.translation), rotation_matrix(node.rotation)), scale_matrix(node.scale));
}

std::array<float, 3> transform_point(Mat4 matrix, std::array<float, 3> point)
{
    return {
        matrix.m[0] * point[0] + matrix.m[4] * point[1] + matrix.m[8] * point[2] + matrix.m[12],
        matrix.m[1] * point[0] + matrix.m[5] * point[1] + matrix.m[9] * point[2] + matrix.m[13],
        matrix.m[2] * point[0] + matrix.m[6] * point[1] + matrix.m[10] * point[2] + matrix.m[14],
    };
}

void include_point(Bounds& bounds, std::array<float, 3> point)
{
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        bounds.minimum[axis] = std::min(bounds.minimum[axis], point[axis]);
        bounds.maximum[axis] = std::max(bounds.maximum[axis], point[axis]);
    }
    bounds.has_points = true;
}

void finish_bounds(ModelDocument& document, const Bounds& bounds)
{
    if (!bounds.has_points) {
        document.center = {0.0F, 0.0F, 0.0F};
        document.radius = 1.0F;
        return;
    }

    document.center = {
        (bounds.minimum[0] + bounds.maximum[0]) * 0.5F,
        (bounds.minimum[1] + bounds.maximum[1]) * 0.5F,
        (bounds.minimum[2] + bounds.maximum[2]) * 0.5F,
    };

    float radius = 0.0F;
    for (int corner = 0; corner < 8; ++corner) {
        const std::array<float, 3> point = {
            (corner & 1) != 0 ? bounds.maximum[0] : bounds.minimum[0],
            (corner & 2) != 0 ? bounds.maximum[1] : bounds.minimum[1],
            (corner & 4) != 0 ? bounds.maximum[2] : bounds.minimum[2],
        };
        const float dx = point[0] - document.center[0];
        const float dy = point[1] - document.center[1];
        const float dz = point[2] - document.center[2];
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    document.radius = std::max(radius, 0.1F);
}

int accessor_component_count(int type)
{
    switch (type) {
    case TINYGLTF_TYPE_SCALAR:
        return 1;
    case TINYGLTF_TYPE_VEC2:
        return 2;
    case TINYGLTF_TYPE_VEC3:
        return 3;
    case TINYGLTF_TYPE_VEC4:
        return 4;
    default:
        return 0;
    }
}

int component_byte_size(int component_type)
{
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    default:
        return 0;
    }
}

const unsigned char* accessor_data(const tinygltf::Model& model, const tinygltf::Accessor& accessor, const tinygltf::BufferView*& view)
{
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }

    view = &model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view->buffer < 0 || view->buffer >= static_cast<int>(model.buffers.size())) {
        return nullptr;
    }

    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view->buffer)];
    const std::size_t offset = view->byteOffset + accessor.byteOffset;
    if (offset >= buffer.data.size()) {
        return nullptr;
    }
    return buffer.data.data() + offset;
}

float read_component(const unsigned char* source, int component_type, bool normalized)
{
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        const auto value = *reinterpret_cast<const std::int8_t*>(source);
        return normalized ? std::max(-1.0F, static_cast<float>(value) / 127.0F) : static_cast<float>(value);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        const auto value = *source;
        return normalized ? static_cast<float>(value) / 255.0F : static_cast<float>(value);
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        std::int16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? std::max(-1.0F, static_cast<float>(value) / 32767.0F) : static_cast<float>(value);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        std::uint16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? static_cast<float>(value) / 65535.0F : static_cast<float>(value);
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        std::uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return static_cast<float>(value);
    }
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
        float value = 0.0F;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    default:
        return 0.0F;
    }
}

float read_accessor_float(const tinygltf::Model& model, int accessor_index, std::size_t element, int component)
{
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
        return 0.0F;
    }
    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(accessor_index)];
    const int component_count = accessor_component_count(accessor.type);
    const int byte_size = component_byte_size(accessor.componentType);
    if (component < 0 || component >= component_count || byte_size <= 0 || element >= accessor.count) {
        return 0.0F;
    }

    const tinygltf::BufferView* view = nullptr;
    const unsigned char* base = accessor_data(model, accessor, view);
    if (!base || !view) {
        return 0.0F;
    }

    const std::size_t stride = accessor.ByteStride(*view) > 0
        ? static_cast<std::size_t>(accessor.ByteStride(*view))
        : static_cast<std::size_t>(component_count * byte_size);
    const unsigned char* source = base + element * stride + static_cast<std::size_t>(component * byte_size);
    return read_component(source, accessor.componentType, accessor.normalized);
}

std::vector<std::uint32_t> read_indices(const tinygltf::Model& model, const tinygltf::Primitive& primitive, std::size_t vertex_count)
{
    if (primitive.indices < 0 || primitive.indices >= static_cast<int>(model.accessors.size())) {
        std::vector<std::uint32_t> generated(vertex_count);
        for (std::size_t index = 0; index < vertex_count; ++index) {
            generated[index] = static_cast<std::uint32_t>(index);
        }
        return generated;
    }

    const tinygltf::Accessor& accessor = model.accessors[static_cast<std::size_t>(primitive.indices)];
    const tinygltf::BufferView* view = nullptr;
    const unsigned char* base = accessor_data(model, accessor, view);
    if (!base || !view) {
        return {};
    }

    const int byte_size = component_byte_size(accessor.componentType);
    if (byte_size <= 0) {
        return {};
    }

    const std::size_t stride = accessor.ByteStride(*view) > 0
        ? static_cast<std::size_t>(accessor.ByteStride(*view))
        : static_cast<std::size_t>(byte_size);

    std::vector<std::uint32_t> indices;
    indices.reserve(accessor.count);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const unsigned char* source = base + index * stride;
        switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            indices.push_back(*source);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            std::uint16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            indices.push_back(value);
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            std::uint32_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            indices.push_back(value);
            break;
        }
        default:
            return {};
        }
    }
    return indices;
}

int texture_index_for_gltf_material(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    std::map<int, int>& image_to_texture,
    ModelDocument& document)
{
    if (primitive.material < 0 || primitive.material >= static_cast<int>(model.materials.size())) {
        return -1;
    }

    const tinygltf::Material& material = model.materials[static_cast<std::size_t>(primitive.material)];
    const int texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
    if (texture_index < 0 || texture_index >= static_cast<int>(model.textures.size())) {
        return -1;
    }

    const int image_index = model.textures[static_cast<std::size_t>(texture_index)].source;
    if (image_index < 0 || image_index >= static_cast<int>(model.images.size())) {
        return -1;
    }

    if (const auto found = image_to_texture.find(image_index); found != image_to_texture.end()) {
        return found->second;
    }

    const tinygltf::Image& source = model.images[static_cast<std::size_t>(image_index)];
    Image image = rgba_from_components(source.width, source.height, source.component, source.image);
    if (image.empty()) {
        return -1;
    }

    ModelTexture texture;
    const bool data_uri = source.uri.rfind("data:", 0) == 0;
    texture.embedded = source.uri.empty() || data_uri;
    if (!source.uri.empty() && !data_uri) {
        texture.source_path = document.path.parent_path() / source.uri;
        texture.name = file_name_or(texture.source_path, "texture_" + std::to_string(image_index) + ".png");
    } else if (!source.name.empty()) {
        texture.name = with_png_extension(source.name);
    } else {
        texture.name = "texture_" + std::to_string(image_index) + ".png";
    }
    texture.image = std::move(image);

    const int document_texture_index = static_cast<int>(document.textures.size());
    document.textures.push_back(std::move(texture));
    image_to_texture.emplace(image_index, document_texture_index);
    return document_texture_index;
}

void append_gltf_primitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& source,
    Mat4 transform,
    int texture_index,
    int mesh_index,
    int material_index,
    std::string mesh_name,
    std::string material_name,
    Bounds& bounds,
    ModelDocument& document)
{
    if (source.mode != TINYGLTF_MODE_TRIANGLES) {
        return;
    }

    const auto position_attribute = source.attributes.find("POSITION");
    if (position_attribute == source.attributes.end()) {
        return;
    }

    const int position_accessor = position_attribute->second;
    if (position_accessor < 0 || position_accessor >= static_cast<int>(model.accessors.size())) {
        return;
    }

    const std::size_t position_count = model.accessors[static_cast<std::size_t>(position_accessor)].count;
    const auto texcoord_attribute = source.attributes.find("TEXCOORD_0");
    const int texcoord_accessor = texcoord_attribute == source.attributes.end() ? -1 : texcoord_attribute->second;
    const std::vector<std::uint32_t> indices = read_indices(model, source, position_count);
    if (indices.empty() || indices.size() < 3U) {
        return;
    }

    ModelPrimitive primitive;
    primitive.texture_index = texture_index;
    primitive.mesh_index = mesh_index;
    primitive.material_index = material_index;
    primitive.mesh_name = std::move(mesh_name);
    primitive.material_name = std::move(material_name);
    primitive.vertices.reserve(indices.size());

    for (const std::uint32_t vertex_index : indices) {
        if (vertex_index >= position_count) {
            continue;
        }

        const std::array<float, 3> local = {
            read_accessor_float(model, position_accessor, vertex_index, 0),
            read_accessor_float(model, position_accessor, vertex_index, 1),
            read_accessor_float(model, position_accessor, vertex_index, 2),
        };
        const std::array<float, 3> world = transform_point(transform, local);
        include_point(bounds, world);

        ModelVertex vertex;
        vertex.x = world[0];
        vertex.y = world[1];
        vertex.z = world[2];
        if (texcoord_accessor >= 0) {
            vertex.u = read_accessor_float(model, texcoord_accessor, vertex_index, 0);
            vertex.v = read_accessor_float(model, texcoord_accessor, vertex_index, 1);
        }
        primitive.vertices.push_back(vertex);
    }

    if (!primitive.vertices.empty()) {
        document.primitives.push_back(std::move(primitive));
    }
}

void append_gltf_node(
    const tinygltf::Model& model,
    int node_index,
    Mat4 parent_transform,
    std::map<int, int>& image_to_texture,
    Bounds& bounds,
    ModelDocument& document)
{
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) {
        return;
    }

    const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(node_index)];
    const Mat4 transform = multiply(parent_transform, node_matrix(node));
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh& mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            const int texture_index = texture_index_for_gltf_material(model, primitive, image_to_texture, document);
            if (texture_index < 0) {
                document.used_fallback_texture = true;
            }
            std::string material_name;
            if (primitive.material >= 0 && primitive.material < static_cast<int>(model.materials.size())) {
                material_name = model.materials[static_cast<std::size_t>(primitive.material)].name;
            }
            append_gltf_primitive(
                model,
                primitive,
                transform,
                texture_index,
                node.mesh,
                primitive.material,
                mesh.name.empty() ? node.name : mesh.name,
                std::move(material_name),
                bounds,
                document);
        }
    }

    for (const int child : node.children) {
        append_gltf_node(model, child, transform, image_to_texture, bounds, document);
    }
}

ModelLoadResult load_gltf_model(const std::filesystem::path& path)
{
    ModelLoadResult result;
    result.model.path = path;

    tinygltf::TinyGLTF loader;
    tinygltf::Model source;
    std::string error;
    std::string warning;
    const std::string extension = lower_extension(path);
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromFile(&source, &error, &warning, path.string())
        : loader.LoadASCIIFromFile(&source, &error, &warning, path.string());

    add_warning(result.warning, warning);
    if (!loaded) {
        result.error = error.empty() ? "Unable to load glTF model." : error;
        return result;
    }

    std::map<int, int> image_to_texture;
    Bounds bounds;
    Mat4 identity;
    if (source.defaultScene >= 0 && source.defaultScene < static_cast<int>(source.scenes.size())) {
        for (const int node : source.scenes[static_cast<std::size_t>(source.defaultScene)].nodes) {
            append_gltf_node(source, node, identity, image_to_texture, bounds, result.model);
        }
    } else if (!source.scenes.empty()) {
        for (const int node : source.scenes.front().nodes) {
            append_gltf_node(source, node, identity, image_to_texture, bounds, result.model);
        }
    } else {
        for (std::size_t node = 0; node < source.nodes.size(); ++node) {
            append_gltf_node(source, static_cast<int>(node), identity, image_to_texture, bounds, result.model);
        }
    }

    finish_bounds(result.model, bounds);
    if (result.model.primitives.empty()) {
        result.error = "The model does not contain supported triangle meshes.";
    }
    return result;
}

const ufbx_material* fbx_material_for_face(
    const ufbx_node* node,
    const ufbx_mesh* mesh,
    std::size_t face_index,
    int& material_slot)
{
    material_slot = -1;
    if (!node || !mesh) {
        return nullptr;
    }

    if (face_index < mesh->face_material.count) {
        const std::uint32_t slot = mesh->face_material.data[face_index];
        material_slot = ufbx_typed_id_to_int(slot);
        if (slot < node->materials.count) {
            return node->materials.data[slot];
        }
        if (slot < mesh->materials.count) {
            return mesh->materials.data[slot];
        }
    }

    if (node->materials.count == 1U) {
        material_slot = 0;
        return node->materials.data[0];
    }
    if (mesh->materials.count == 1U) {
        material_slot = 0;
        return mesh->materials.data[0];
    }
    return nullptr;
}

int texture_index_for_fbx_material(
    const std::filesystem::path& base_dir,
    const ufbx_material* material,
    std::map<const ufbx_texture*, int>& texture_to_index,
    ModelDocument& document,
    std::string& warning)
{
    const ufbx_texture* texture = diffuse_texture_for_fbx_material(material);
    if (!texture) {
        return -1;
    }
    if (const auto found = texture_to_index.find(texture); found != texture_to_index.end()) {
        return found->second;
    }

    bool ok = false;
    ModelTexture loaded = load_fbx_texture(base_dir, texture, warning, ok);
    if (!ok) {
        return -1;
    }

    const int document_texture_index = static_cast<int>(document.textures.size());
    document.textures.push_back(std::move(loaded));
    texture_to_index.emplace(texture, document_texture_index);
    return document_texture_index;
}

void append_fbx_face(
    const ufbx_node* node,
    const ufbx_mesh* mesh,
    ufbx_face face,
    int texture_index,
    int material_index,
    std::string mesh_name,
    std::string material_name,
    Bounds& bounds,
    ModelDocument& document)
{
    if (!node || !mesh || face.num_indices < 3U || mesh->max_face_triangles == 0U) {
        return;
    }

    std::vector<std::uint32_t> triangle_indices(mesh->max_face_triangles * 3U);
    const std::uint32_t triangle_count = ufbx_triangulate_face(triangle_indices.data(), triangle_indices.size(), mesh, face);
    if (triangle_count == 0U) {
        return;
    }

    ModelPrimitive primitive;
    primitive.texture_index = texture_index;
    primitive.mesh_index = ufbx_typed_id_to_int(mesh->typed_id);
    primitive.material_index = material_index;
    primitive.mesh_name = std::move(mesh_name);
    primitive.material_name = std::move(material_name);
    primitive.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);

    for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        std::array<ModelVertex, 3> vertices{};
        bool triangle_valid = true;
        for (std::uint32_t corner = 0; corner < 3U; ++corner) {
            const std::uint32_t vertex_index = triangle_indices[static_cast<std::size_t>(triangle) * 3U + corner];
            if (vertex_index >= mesh->vertex_position.indices.count) {
                triangle_valid = false;
                break;
            }

            const ufbx_vec3 local_position = ufbx_get_vertex_vec3(&mesh->vertex_position, vertex_index);
            const ufbx_vec3 world_position = ufbx_transform_position(&node->geometry_to_world, local_position);

            ModelVertex vertex;
            vertex.x = static_cast<float>(world_position.x);
            vertex.y = static_cast<float>(world_position.y);
            vertex.z = static_cast<float>(world_position.z);
            if (vertex_index < mesh->vertex_uv.indices.count) {
                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, vertex_index);
                vertex.u = static_cast<float>(uv.x);
                vertex.v = 1.0F - static_cast<float>(uv.y);
            }
            vertices[corner] = vertex;
        }

        if (!triangle_valid) {
            continue;
        }
        for (const ModelVertex& vertex : vertices) {
            include_point(bounds, {vertex.x, vertex.y, vertex.z});
            primitive.vertices.push_back(vertex);
        }
    }

    if (!primitive.vertices.empty()) {
        document.primitives.push_back(std::move(primitive));
    }
}

ModelLoadResult load_fbx_model(const std::filesystem::path& path)
{
    ModelLoadResult result;
    result.model.path = path;

    ufbx_load_opts options = {};
    options.use_blender_pbr_material = true;
    ufbx_error error = {};
    const std::string filename = path.string();
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
        ufbx_load_file(filename.c_str(), &options, &error),
        ufbx_free_scene);
    if (!scene) {
        const std::string description = ufbx_error_description(error);
        result.error = description.empty() ? "Unable to load FBX model." : description;
        return result;
    }

    std::map<const ufbx_texture*, int> texture_to_index;
    Bounds bounds;
    const std::filesystem::path base_dir = path.parent_path();
    for (std::size_t node_index = 0; node_index < scene->nodes.count; ++node_index) {
        const ufbx_node* node = scene->nodes.data[node_index];
        if (!node || !node->mesh || node->is_root) {
            continue;
        }

        const ufbx_mesh* mesh = node->mesh;
        std::string mesh_name = ufbx_string_to_std(mesh->name);
        if (mesh_name.empty()) {
            mesh_name = ufbx_string_to_std(node->name);
        }

        for (std::size_t face_index = 0; face_index < mesh->faces.count; ++face_index) {
            int material_slot = -1;
            const ufbx_material* material = fbx_material_for_face(node, mesh, face_index, material_slot);
            const int texture_index = texture_index_for_fbx_material(
                base_dir,
                material,
                texture_to_index,
                result.model,
                result.warning);
            if (texture_index < 0) {
                result.model.used_fallback_texture = true;
            }

            append_fbx_face(
                node,
                mesh,
                mesh->faces.data[face_index],
                texture_index,
                material ? ufbx_typed_id_to_int(material->typed_id) : material_slot,
                mesh_name,
                material ? ufbx_string_to_std(material->name) : std::string{},
                bounds,
                result.model);
        }
    }

    finish_bounds(result.model, bounds);
    if (result.model.primitives.empty()) {
        result.error = "The FBX file does not contain supported triangle meshes.";
    }
    return result;
}

ColladaSource parse_collada_source(const tinyxml2::XMLElement* source)
{
    ColladaSource parsed;
    const tinyxml2::XMLElement* float_array = first_child_named(source, "float_array");
    if (!float_array) {
        return parsed;
    }

    const std::vector<double> values = parse_double_list(xml_text(float_array));
    parsed.values.reserve(values.size());
    for (double value : values) {
        parsed.values.push_back(static_cast<float>(value));
    }

    const tinyxml2::XMLElement* technique = first_child_named(source, "technique_common");
    const tinyxml2::XMLElement* accessor = first_child_named(technique, "accessor");
    int stride = 1;
    int offset = 0;
    if (accessor) {
        accessor->QueryIntAttribute("stride", &stride);
        accessor->QueryIntAttribute("offset", &offset);
    }
    parsed.stride = stride > 0 ? static_cast<std::size_t>(stride) : 1U;
    parsed.offset = offset > 0 ? static_cast<std::size_t>(offset) : 0U;
    return parsed;
}

std::vector<ColladaInput> parse_collada_inputs(const tinyxml2::XMLElement* primitive)
{
    std::vector<ColladaInput> inputs;
    for (const tinyxml2::XMLElement* input = first_child_named(primitive, "input"); input;
         input = next_sibling_named(input, "input")) {
        ColladaInput parsed;
        parsed.semantic = xml_attr(input, "semantic");
        parsed.source = strip_url_id(xml_attr(input, "source"));
        int offset = 0;
        input->QueryIntAttribute("offset", &offset);
        parsed.offset = offset > 0 ? static_cast<std::size_t>(offset) : 0U;
        inputs.push_back(std::move(parsed));
    }
    return inputs;
}

std::size_t collada_input_stride(const std::vector<ColladaInput>& inputs)
{
    std::size_t stride = 0;
    for (const ColladaInput& input : inputs) {
        stride = std::max(stride, input.offset + 1U);
    }
    return stride;
}

ColladaGeometry parse_collada_geometry(const tinyxml2::XMLElement* geometry, int geometry_index)
{
    ColladaGeometry parsed;
    parsed.index = geometry_index;
    parsed.id = xml_attr(geometry, "id");
    parsed.name = xml_attr(geometry, "name");

    const tinyxml2::XMLElement* mesh = first_child_named(geometry, "mesh");
    if (!mesh) {
        return parsed;
    }

    for (const tinyxml2::XMLElement* source = first_child_named(mesh, "source"); source;
         source = next_sibling_named(source, "source")) {
        const std::string id = xml_attr(source, "id");
        if (!id.empty()) {
            parsed.sources.emplace(id, parse_collada_source(source));
        }
    }

    for (const tinyxml2::XMLElement* vertices = first_child_named(mesh, "vertices"); vertices;
         vertices = next_sibling_named(vertices, "vertices")) {
        const std::string vertices_id = xml_attr(vertices, "id");
        if (vertices_id.empty()) {
            continue;
        }
        for (const tinyxml2::XMLElement* input = first_child_named(vertices, "input"); input;
             input = next_sibling_named(input, "input")) {
            if (xml_attr(input, "semantic") == "POSITION") {
                parsed.vertices_positions[vertices_id] = strip_url_id(xml_attr(input, "source"));
                break;
            }
        }
    }

    for (const tinyxml2::XMLElement* primitive = mesh->FirstChildElement(); primitive;
         primitive = primitive->NextSiblingElement()) {
        const std::string name = xml_local_name(primitive->Name());
        if (name != "triangles" && name != "polylist") {
            continue;
        }

        ColladaPrimitiveData parsed_primitive;
        parsed_primitive.kind = name == "triangles" ? ColladaPrimitiveKind::Triangles : ColladaPrimitiveKind::Polylist;
        parsed_primitive.material_symbol = xml_attr(primitive, "material");
        parsed_primitive.inputs = parse_collada_inputs(primitive);
        parsed_primitive.indices = parse_int_list(xml_text(first_child_named(primitive, "p")));
        if (parsed_primitive.kind == ColladaPrimitiveKind::Polylist) {
            parsed_primitive.vertex_counts = parse_int_list(xml_text(first_child_named(primitive, "vcount")));
        }
        parsed.primitives.push_back(std::move(parsed_primitive));
    }

    return parsed;
}

std::string collada_effect_diffuse_image(const tinyxml2::XMLElement* effect)
{
    const tinyxml2::XMLElement* profile = first_child_named(effect, "profile_COMMON");
    if (!profile) {
        return {};
    }

    std::map<std::string, std::string> surface_images;
    std::map<std::string, std::string> sampler_surfaces;
    for (const tinyxml2::XMLElement* newparam = first_child_named(profile, "newparam"); newparam;
         newparam = next_sibling_named(newparam, "newparam")) {
        const std::string sid = xml_attr(newparam, "sid");
        if (sid.empty()) {
            continue;
        }

        if (const tinyxml2::XMLElement* surface = first_child_named(newparam, "surface")) {
            if (const tinyxml2::XMLElement* init_from = first_child_named(surface, "init_from")) {
                surface_images[sid] = xml_text(init_from);
            }
        }
        if (const tinyxml2::XMLElement* sampler = first_child_named(newparam, "sampler2D")) {
            if (const tinyxml2::XMLElement* source = first_child_named(sampler, "source")) {
                sampler_surfaces[sid] = xml_text(source);
            }
        }
    }

    const tinyxml2::XMLElement* technique = first_child_named(profile, "technique");
    if (!technique) {
        return {};
    }

    for (const tinyxml2::XMLElement* shader = technique->FirstChildElement(); shader; shader = shader->NextSiblingElement()) {
        const tinyxml2::XMLElement* diffuse = first_child_named(shader, "diffuse");
        const tinyxml2::XMLElement* texture = first_child_named(diffuse, "texture");
        if (!texture) {
            continue;
        }

        const std::string sampler_id = xml_attr(texture, "texture");
        if (const auto surface = sampler_surfaces.find(sampler_id); surface != sampler_surfaces.end()) {
            if (const auto image = surface_images.find(surface->second); image != surface_images.end()) {
                return image->second;
            }
        }
        if (const auto image = surface_images.find(sampler_id); image != surface_images.end()) {
            return image->second;
        }
        return sampler_id;
    }

    return {};
}

ColladaDocument parse_collada_document(const tinyxml2::XMLDocument& xml)
{
    ColladaDocument parsed;
    const tinyxml2::XMLElement* root = xml.RootElement();
    if (!root) {
        return parsed;
    }

    const tinyxml2::XMLElement* library_images = first_child_named(root, "library_images");
    for (const tinyxml2::XMLElement* image = first_child_named(library_images, "image"); image;
         image = next_sibling_named(image, "image")) {
        ColladaImage parsed_image;
        parsed_image.id = xml_attr(image, "id");
        parsed_image.name = xml_attr(image, "name");
        parsed_image.uri = xml_text(first_child_named(image, "init_from"));
        if (!parsed_image.id.empty()) {
            parsed.images.emplace(parsed_image.id, std::move(parsed_image));
        }
    }

    const tinyxml2::XMLElement* library_effects = first_child_named(root, "library_effects");
    for (const tinyxml2::XMLElement* effect = first_child_named(library_effects, "effect"); effect;
         effect = next_sibling_named(effect, "effect")) {
        ColladaEffect parsed_effect;
        parsed_effect.id = xml_attr(effect, "id");
        parsed_effect.image_id = collada_effect_diffuse_image(effect);
        if (!parsed_effect.id.empty()) {
            parsed.effects.emplace(parsed_effect.id, std::move(parsed_effect));
        }
    }

    const tinyxml2::XMLElement* library_materials = first_child_named(root, "library_materials");
    int material_index = 0;
    for (const tinyxml2::XMLElement* material = first_child_named(library_materials, "material"); material;
         material = next_sibling_named(material, "material")) {
        ColladaMaterial parsed_material;
        parsed_material.index = material_index++;
        parsed_material.id = xml_attr(material, "id");
        parsed_material.name = xml_attr(material, "name");
        parsed_material.effect_id = strip_url_id(xml_attr(first_child_named(material, "instance_effect"), "url"));
        if (!parsed_material.id.empty()) {
            parsed.materials.emplace(parsed_material.id, std::move(parsed_material));
        }
    }

    const tinyxml2::XMLElement* library_geometries = first_child_named(root, "library_geometries");
    int geometry_index = 0;
    for (const tinyxml2::XMLElement* geometry = first_child_named(library_geometries, "geometry"); geometry;
         geometry = next_sibling_named(geometry, "geometry")) {
        ColladaGeometry parsed_geometry = parse_collada_geometry(geometry, geometry_index++);
        if (!parsed_geometry.id.empty()) {
            parsed.geometries.emplace(parsed_geometry.id, std::move(parsed_geometry));
        }
    }

    std::string scene_id;
    if (const tinyxml2::XMLElement* scene = first_child_named(root, "scene")) {
        scene_id = strip_url_id(xml_attr(first_child_named(scene, "instance_visual_scene"), "url"));
    }
    const tinyxml2::XMLElement* library_visual_scenes = first_child_named(root, "library_visual_scenes");
    for (const tinyxml2::XMLElement* visual_scene = first_child_named(library_visual_scenes, "visual_scene"); visual_scene;
         visual_scene = next_sibling_named(visual_scene, "visual_scene")) {
        if (scene_id.empty() || xml_attr(visual_scene, "id") == scene_id) {
            parsed.visual_scene = visual_scene;
            break;
        }
    }

    return parsed;
}

const ColladaSource* collada_source_for_input(const ColladaGeometry& geometry, const ColladaInput& input)
{
    std::string source_id = input.source;
    if (input.semantic == "VERTEX") {
        const auto found = geometry.vertices_positions.find(input.source);
        if (found == geometry.vertices_positions.end()) {
            return nullptr;
        }
        source_id = found->second;
    }

    const auto source = geometry.sources.find(source_id);
    return source == geometry.sources.end() ? nullptr : &source->second;
}

float read_collada_source_float(const ColladaSource& source, int index, std::size_t component)
{
    if (index < 0) {
        return 0.0F;
    }

    const std::size_t offset = source.offset + static_cast<std::size_t>(index) * source.stride + component;
    if (offset >= source.values.size()) {
        return 0.0F;
    }
    return source.values[offset];
}

const ColladaInput* find_collada_input(const std::vector<ColladaInput>& inputs, std::string_view semantic)
{
    for (const ColladaInput& input : inputs) {
        if (input.semantic == semantic) {
            return &input;
        }
    }
    return nullptr;
}

bool build_collada_vertex(
    const ColladaGeometry& geometry,
    const ColladaPrimitiveData& primitive,
    std::size_t tuple,
    std::size_t input_stride,
    Mat4 transform,
    ModelVertex& vertex)
{
    const ColladaInput* position_input = find_collada_input(primitive.inputs, "VERTEX");
    if (!position_input) {
        position_input = find_collada_input(primitive.inputs, "POSITION");
    }
    if (!position_input || position_input->offset >= input_stride) {
        return false;
    }

    const std::size_t position_index_offset = tuple * input_stride + position_input->offset;
    if (position_index_offset >= primitive.indices.size()) {
        return false;
    }

    const ColladaSource* position_source = collada_source_for_input(geometry, *position_input);
    if (!position_source) {
        return false;
    }

    const int position_index = primitive.indices[position_index_offset];
    const std::array<float, 3> local = {
        read_collada_source_float(*position_source, position_index, 0),
        read_collada_source_float(*position_source, position_index, 1),
        read_collada_source_float(*position_source, position_index, 2),
    };
    const std::array<float, 3> world = transform_point(transform, local);
    vertex.x = world[0];
    vertex.y = world[1];
    vertex.z = world[2];

    const ColladaInput* texcoord_input = find_collada_input(primitive.inputs, "TEXCOORD");
    if (texcoord_input && texcoord_input->offset < input_stride) {
        const std::size_t texcoord_index_offset = tuple * input_stride + texcoord_input->offset;
        if (texcoord_index_offset < primitive.indices.size()) {
            if (const ColladaSource* texcoord_source = collada_source_for_input(geometry, *texcoord_input)) {
                const int texcoord_index = primitive.indices[texcoord_index_offset];
                vertex.u = read_collada_source_float(*texcoord_source, texcoord_index, 0);
                vertex.v = 1.0F - read_collada_source_float(*texcoord_source, texcoord_index, 1);
            }
        }
    }

    return true;
}

ModelTexture load_collada_texture(
    const std::filesystem::path& base_dir,
    const ColladaImage& source,
    std::string& warning,
    bool& ok)
{
    ok = false;
    if (source.uri.empty()) {
        add_warning(warning, "COLLADA texture has no image file reference; using fallback.");
        return {};
    }

    std::string uri = source.uri;
    constexpr std::string_view kFilePrefix = "file://";
    if (uri.rfind(kFilePrefix, 0) == 0) {
        uri.erase(0, kFilePrefix.size());
        if (uri.size() >= 3U && uri[0] == '/' && std::isalpha(static_cast<unsigned char>(uri[1])) && uri[2] == ':') {
            uri.erase(uri.begin());
        }
    }

    const std::filesystem::path texture_path = external_texture_path(base_dir, uri);
    ImageLoadResult loaded = load_image_rgba(texture_path.string());
    if (!loaded.error.empty()) {
        add_warning(warning, "Texture " + texture_path.filename().string() + " could not be loaded; using fallback.");
        return {};
    }

    ModelTexture texture;
    texture.name = file_name_or(texture_path, source.name.empty() ? source.uri : source.name);
    texture.source_path = texture_path;
    texture.embedded = false;
    texture.image = std::move(loaded.image);
    ok = true;
    return texture;
}

int texture_index_for_collada_material(
    const std::filesystem::path& base_dir,
    const ColladaDocument& source,
    const std::string& material_id,
    std::map<std::string, int>& image_to_texture,
    ModelDocument& document,
    std::string& warning)
{
    const auto material = source.materials.find(material_id);
    if (material == source.materials.end()) {
        return -1;
    }
    const auto effect = source.effects.find(material->second.effect_id);
    if (effect == source.effects.end() || effect->second.image_id.empty()) {
        return -1;
    }
    const auto image = source.images.find(effect->second.image_id);
    if (image == source.images.end()) {
        return -1;
    }

    const std::string key = image->second.id.empty() ? image->second.uri : image->second.id;
    if (const auto found = image_to_texture.find(key); found != image_to_texture.end()) {
        return found->second;
    }

    bool ok = false;
    ModelTexture texture = load_collada_texture(base_dir, image->second, warning, ok);
    if (!ok) {
        return -1;
    }

    const int document_texture_index = static_cast<int>(document.textures.size());
    document.textures.push_back(std::move(texture));
    image_to_texture.emplace(key, document_texture_index);
    return document_texture_index;
}

std::string collada_material_id_for_symbol(
    const ColladaDocument& source,
    const std::map<std::string, std::string>& material_bindings,
    const std::string& symbol)
{
    if (const auto found = material_bindings.find(symbol); found != material_bindings.end()) {
        return found->second;
    }
    if (source.materials.contains(symbol)) {
        return symbol;
    }
    return {};
}

void append_collada_triangle(
    const ColladaGeometry& geometry,
    const ColladaPrimitiveData& source,
    std::array<std::size_t, 3> tuples,
    std::size_t input_stride,
    Mat4 transform,
    ModelPrimitive& primitive,
    Bounds& bounds)
{
    std::array<ModelVertex, 3> vertices{};
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (!build_collada_vertex(geometry, source, tuples[index], input_stride, transform, vertices[index])) {
            return;
        }
    }

    for (const ModelVertex& vertex : vertices) {
        include_point(bounds, {vertex.x, vertex.y, vertex.z});
        primitive.vertices.push_back(vertex);
    }
}

void append_collada_geometry(
    const ColladaDocument& source,
    const ColladaGeometry& geometry,
    Mat4 transform,
    const std::map<std::string, std::string>& material_bindings,
    const std::filesystem::path& base_dir,
    std::map<std::string, int>& image_to_texture,
    Bounds& bounds,
    std::string& warning,
    ModelDocument& document)
{
    const std::string mesh_name = geometry.name.empty() ? geometry.id : geometry.name;
    for (const ColladaPrimitiveData& source_primitive : geometry.primitives) {
        const std::size_t input_stride = collada_input_stride(source_primitive.inputs);
        if (input_stride == 0U || source_primitive.indices.empty()) {
            continue;
        }

        const std::string material_id = collada_material_id_for_symbol(
            source,
            material_bindings,
            source_primitive.material_symbol);
        const int texture_index = texture_index_for_collada_material(
            base_dir,
            source,
            material_id,
            image_to_texture,
            document,
            warning);
        if (texture_index < 0) {
            document.used_fallback_texture = true;
        }

        ModelPrimitive primitive;
        primitive.texture_index = texture_index;
        primitive.mesh_index = geometry.index;
        primitive.mesh_name = mesh_name;
        if (const auto material = source.materials.find(material_id); material != source.materials.end()) {
            primitive.material_index = material->second.index;
            primitive.material_name = material->second.name.empty() ? material_id : material->second.name;
        }

        if (source_primitive.kind == ColladaPrimitiveKind::Triangles) {
            const std::size_t tuple_count = source_primitive.indices.size() / input_stride;
            primitive.vertices.reserve(tuple_count);
            for (std::size_t tuple = 0; tuple + 2U < tuple_count; tuple += 3U) {
                append_collada_triangle(
                    geometry,
                    source_primitive,
                    {tuple, tuple + 1U, tuple + 2U},
                    input_stride,
                    transform,
                    primitive,
                    bounds);
            }
        } else {
            std::size_t tuple = 0;
            for (const int vertex_count : source_primitive.vertex_counts) {
                if (vertex_count >= 3) {
                    primitive.vertices.reserve(
                        primitive.vertices.size() + static_cast<std::size_t>((vertex_count - 2) * 3));
                    for (int corner = 1; corner + 1 < vertex_count; ++corner) {
                        append_collada_triangle(
                            geometry,
                            source_primitive,
                            {
                                tuple,
                                tuple + static_cast<std::size_t>(corner),
                                tuple + static_cast<std::size_t>(corner + 1),
                            },
                            input_stride,
                            transform,
                            primitive,
                            bounds);
                    }
                }
                if (vertex_count > 0) {
                    tuple += static_cast<std::size_t>(vertex_count);
                }
            }
        }

        if (!primitive.vertices.empty()) {
            document.primitives.push_back(std::move(primitive));
        }
    }
}

std::map<std::string, std::string> collada_material_bindings(const tinyxml2::XMLElement* instance_geometry)
{
    std::map<std::string, std::string> bindings;
    const tinyxml2::XMLElement* bind_material = first_child_named(instance_geometry, "bind_material");
    const tinyxml2::XMLElement* technique = first_child_named(bind_material, "technique_common");
    for (const tinyxml2::XMLElement* instance_material = first_child_named(technique, "instance_material");
         instance_material;
         instance_material = next_sibling_named(instance_material, "instance_material")) {
        const std::string target = strip_url_id(xml_attr(instance_material, "target"));
        if (target.empty()) {
            continue;
        }

        const std::string symbol = xml_attr(instance_material, "symbol");
        bindings[symbol.empty() ? target : symbol] = target;
    }
    return bindings;
}

void append_collada_node(
    const ColladaDocument& source,
    const tinyxml2::XMLElement* node,
    Mat4 parent_transform,
    const std::filesystem::path& base_dir,
    std::map<std::string, int>& image_to_texture,
    Bounds& bounds,
    std::string& warning,
    ModelDocument& document)
{
    if (!node) {
        return;
    }

    const Mat4 transform = multiply(parent_transform, collada_node_transform(node));
    for (const tinyxml2::XMLElement* instance_geometry = first_child_named(node, "instance_geometry"); instance_geometry;
         instance_geometry = next_sibling_named(instance_geometry, "instance_geometry")) {
        const std::string geometry_id = strip_url_id(xml_attr(instance_geometry, "url"));
        const auto geometry = source.geometries.find(geometry_id);
        if (geometry == source.geometries.end()) {
            continue;
        }

        append_collada_geometry(
            source,
            geometry->second,
            transform,
            collada_material_bindings(instance_geometry),
            base_dir,
            image_to_texture,
            bounds,
            warning,
            document);
    }

    for (const tinyxml2::XMLElement* child = first_child_named(node, "node"); child;
         child = next_sibling_named(child, "node")) {
        append_collada_node(source, child, transform, base_dir, image_to_texture, bounds, warning, document);
    }
}

ModelLoadResult load_collada_model(const std::filesystem::path& path)
{
    ModelLoadResult result;
    result.model.path = path;

    tinyxml2::XMLDocument xml;
    const tinyxml2::XMLError error = xml.LoadFile(path.string().c_str());
    if (error != tinyxml2::XML_SUCCESS) {
        result.error = xml.ErrorStr() ? std::string("Unable to load COLLADA model: ") + xml.ErrorStr()
                                      : "Unable to load COLLADA model.";
        return result;
    }

    const ColladaDocument source = parse_collada_document(xml);
    const std::filesystem::path base_dir = path.parent_path();
    std::map<std::string, int> image_to_texture;
    Bounds bounds;
    const Mat4 identity;

    if (source.visual_scene) {
        for (const tinyxml2::XMLElement* node = first_child_named(source.visual_scene, "node"); node;
             node = next_sibling_named(node, "node")) {
            append_collada_node(
                source,
                node,
                identity,
                base_dir,
                image_to_texture,
                bounds,
                result.warning,
                result.model);
        }
    } else {
        const std::map<std::string, std::string> material_bindings;
        for (const auto& [id, geometry] : source.geometries) {
            (void)id;
            append_collada_geometry(
                source,
                geometry,
                identity,
                material_bindings,
                base_dir,
                image_to_texture,
                bounds,
                result.warning,
                result.model);
        }
    }

    finish_bounds(result.model, bounds);
    if (result.model.primitives.empty()) {
        result.error = "The COLLADA file does not contain supported triangle meshes.";
    }
    return result;
}

ModelTexture load_obj_texture(
    const std::filesystem::path& base_dir,
    const std::string& diffuse_name,
    std::string& warning,
    bool& ok)
{
    ok = false;
    const std::filesystem::path texture_path = base_dir / diffuse_name;
    ImageLoadResult loaded = load_image_rgba(texture_path.string());
    if (!loaded.error.empty()) {
        add_warning(warning, "Texture " + texture_path.filename().string() + " could not be loaded; using fallback.");
        return {};
    }

    ModelTexture texture;
    texture.name = file_name_or(texture_path, diffuse_name);
    texture.source_path = texture_path;
    texture.embedded = false;
    texture.image = std::move(loaded.image);
    ok = true;
    return texture;
}

ModelLoadResult load_obj_model(const std::filesystem::path& path)
{
    ModelLoadResult result;
    result.model.path = path;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;
    const std::filesystem::path base_dir = path.parent_path();
    const std::string material_base_dir = base_dir.empty()
        ? std::string{}
        : base_dir.string() + std::string(1, std::filesystem::path::preferred_separator);
    const bool loaded = tinyobj::LoadObj(
        &attrib,
        &shapes,
        &materials,
        &warning,
        &error,
        path.string().c_str(),
        material_base_dir.empty() ? nullptr : material_base_dir.c_str(),
        true);

    add_warning(result.warning, warning);
    if (!loaded) {
        result.error = error.empty() ? "Unable to load OBJ model." : error;
        return result;
    }

    std::map<std::string, int> texture_name_to_index;
    std::vector<int> material_texture_indices(materials.size(), -1);
    for (std::size_t material_index = 0; material_index < materials.size(); ++material_index) {
        const std::string diffuse_name = materials[material_index].diffuse_texname;
        if (diffuse_name.empty()) {
            result.model.used_fallback_texture = true;
            continue;
        }

        if (const auto found = texture_name_to_index.find(diffuse_name); found != texture_name_to_index.end()) {
            material_texture_indices[material_index] = found->second;
            continue;
        }

        bool ok = false;
        ModelTexture texture = load_obj_texture(base_dir, diffuse_name, result.warning, ok);
        if (!ok) {
            result.model.used_fallback_texture = true;
            continue;
        }

        const int document_texture_index = static_cast<int>(result.model.textures.size());
        result.model.textures.push_back(std::move(texture));
        texture_name_to_index.emplace(diffuse_name, document_texture_index);
        material_texture_indices[material_index] = document_texture_index;
    }

    Bounds bounds;
    for (std::size_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
        const tinyobj::shape_t& shape = shapes[shape_index];
        std::size_t index_offset = 0;
        for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
            const int vertices_in_face = shape.mesh.num_face_vertices[face];
            int texture_index = -1;
            int material_id = -1;
            if (face < shape.mesh.material_ids.size()) {
                material_id = shape.mesh.material_ids[face];
                if (material_id >= 0 && material_id < static_cast<int>(material_texture_indices.size())) {
                    texture_index = material_texture_indices[static_cast<std::size_t>(material_id)];
                }
            }
            if (texture_index < 0) {
                result.model.used_fallback_texture = true;
            }

            ModelPrimitive primitive;
            primitive.texture_index = texture_index;
            primitive.mesh_index = static_cast<int>(shape_index);
            primitive.material_index = material_id;
            primitive.mesh_name = shape.name;
            if (material_id >= 0 && material_id < static_cast<int>(materials.size())) {
                primitive.material_name = materials[static_cast<std::size_t>(material_id)].name;
            }
            primitive.vertices.reserve(vertices_in_face >= 3 ? static_cast<std::size_t>((vertices_in_face - 2) * 3) : 0U);

            auto append_vertex = [&](const tinyobj::index_t& index) {
                ModelVertex vertex;
                if (index.vertex_index >= 0) {
                    const std::size_t position = static_cast<std::size_t>(index.vertex_index) * 3U;
                    if (position + 2U < attrib.vertices.size()) {
                        vertex.x = attrib.vertices[position];
                        vertex.y = attrib.vertices[position + 1U];
                        vertex.z = attrib.vertices[position + 2U];
                        include_point(bounds, {vertex.x, vertex.y, vertex.z});
                    }
                }
                if (index.texcoord_index >= 0) {
                    const std::size_t texcoord = static_cast<std::size_t>(index.texcoord_index) * 2U;
                    if (texcoord + 1U < attrib.texcoords.size()) {
                        vertex.u = attrib.texcoords[texcoord];
                        vertex.v = 1.0F - attrib.texcoords[texcoord + 1U];
                    }
                }
                primitive.vertices.push_back(vertex);
            };

            if (vertices_in_face >= 3) {
                const tinyobj::index_t first = shape.mesh.indices[index_offset];
                for (int corner = 1; corner + 1 < vertices_in_face; ++corner) {
                    append_vertex(first);
                    append_vertex(shape.mesh.indices[index_offset + static_cast<std::size_t>(corner)]);
                    append_vertex(shape.mesh.indices[index_offset + static_cast<std::size_t>(corner + 1)]);
                }
            }

            if (!primitive.vertices.empty()) {
                result.model.primitives.push_back(std::move(primitive));
            }
            index_offset += static_cast<std::size_t>(vertices_in_face);
        }
    }

    finish_bounds(result.model, bounds);
    if (result.model.primitives.empty()) {
        result.error = "The OBJ file does not contain supported triangle meshes.";
    }
    return result;
}

} // namespace

bool is_model_path(const std::filesystem::path& path)
{
    const std::string extension = lower_extension(path);
    return extension == ".glb" || extension == ".gltf" || extension == ".obj" || extension == ".fbx"
        || extension == ".dae";
}

ModelLoadResult load_model_document(const std::filesystem::path& path)
{
    const std::string extension = lower_extension(path);
    if (extension == ".glb" || extension == ".gltf") {
        return load_gltf_model(path);
    }
    if (extension == ".obj") {
        return load_obj_model(path);
    }
    if (extension == ".fbx") {
        return load_fbx_model(path);
    }
    if (extension == ".dae") {
        return load_collada_model(path);
    }

    ModelLoadResult result;
    result.error = "Unsupported model format.";
    return result;
}

std::string default_model_texture_export_name(const ModelTexture& texture, std::size_t index)
{
    if (!texture.source_path.empty()) {
        return with_png_extension(file_name_or(texture.source_path, "texture_" + std::to_string(index) + ".png"));
    }
    if (!texture.name.empty()) {
        return with_png_extension(texture.name);
    }
    return "texture_" + std::to_string(index) + ".png";
}

} // namespace pixelizer
