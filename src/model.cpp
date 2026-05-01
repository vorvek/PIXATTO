#include "pixelizer/model.hpp"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

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
    return extension == ".glb" || extension == ".gltf" || extension == ".obj";
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
