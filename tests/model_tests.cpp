#include "pixelizer/image.hpp"
#include "pixelizer/model.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("model test failed");
    }
}

std::filesystem::path test_dir()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "pixelizer-model-tests";
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path);
    return path;
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::trunc);
    require(static_cast<bool>(output));
    output << text;
}

void write_binary(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<unsigned char> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input));
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void append_u32(std::vector<unsigned char>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xffU));
}

void append_float(std::vector<unsigned char>& bytes, float value)
{
    unsigned char raw[sizeof(float)] = {};
    std::memcpy(raw, &value, sizeof(float));
    bytes.insert(bytes.end(), raw, raw + sizeof(float));
}

void pad4(std::vector<unsigned char>& bytes, unsigned char value = 0)
{
    while ((bytes.size() % 4U) != 0U) {
        bytes.push_back(value);
    }
}

std::vector<unsigned char> triangle_buffer()
{
    std::vector<unsigned char> bytes;
    for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        append_float(bytes, value);
    }
    for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        append_float(bytes, value);
    }
    return bytes;
}

void write_texture_png(const std::filesystem::path& path)
{
    const pixelizer::Image image = {
        2,
        1,
        {
            255, 0, 0, 255,
            0, 255, 0, 255,
        },
    };
    std::string error;
    require(pixelizer::save_png_rgba(path.string(), image, error));
}

std::string gltf_json(const std::string& image_uri, bool two_primitives)
{
    const char* primitives = two_primitives
        ? R"({"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0},{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":1})"
        : R"({"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0})";

    return std::string(R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "mesh.bin", "byteLength": 60}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"}
  ],
  "images": [{"uri": ")") + image_uri + R"("}],
  "textures": [{"source": 0}],
  "materials": [
    {"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
    {"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}
  ],
  "meshes": [{"primitives": [)" + primitives + R"(]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
}

void gltf_external_texture_loads_and_dedupes()
{
    const std::filesystem::path dir = test_dir();
    write_texture_png(dir / "diffuse.png");
    write_binary(dir / "mesh.bin", triangle_buffer());
    write_text(dir / "model.gltf", gltf_json("diffuse.png", true));

    const pixelizer::ModelLoadResult loaded = pixelizer::load_model_document(dir / "model.gltf");
    require(loaded.error.empty());
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.primitives.size() == 2U);
    require(loaded.model.primitives[0].texture_index == 0);
    require(loaded.model.primitives[1].texture_index == 0);
    require(pixelizer::default_model_texture_export_name(loaded.model.textures[0], 0) == "diffuse.png");
}

void gltf_without_material_uses_grey_fallback()
{
    const std::filesystem::path dir = test_dir();
    write_binary(dir / "mesh.bin", triangle_buffer());
    write_text(
        dir / "hero.gltf",
        R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "mesh.bin", "byteLength": 60}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"}
  ],
  "meshes": [{"name": "BODY", "primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})");

    const pixelizer::ModelLoadResult loaded = pixelizer::load_model_document(dir / "hero.gltf");
    require(loaded.error.empty());
    require(loaded.model.used_fallback_texture);
    require(loaded.model.textures.empty());
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].texture_index == -1);
    require(loaded.model.primitives[0].mesh_index == 0);
    require(loaded.model.primitives[0].material_index == -1);
    require(loaded.model.primitives[0].mesh_name == "BODY");
}

void glb_embedded_texture_loads()
{
    const std::filesystem::path dir = test_dir();
    write_texture_png(dir / "embedded.png");
    std::vector<unsigned char> png = read_binary(dir / "embedded.png");

    std::vector<unsigned char> bin = triangle_buffer();
    const std::size_t image_offset = bin.size();
    bin.insert(bin.end(), png.begin(), png.end());
    const std::size_t image_length = png.size();
    pad4(bin);

    const std::string json = std::string(R"({
  "asset": {"version": "2.0"},
  "buffers": [{"byteLength": )") + std::to_string(bin.size()) + R"(}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 24},
    {"buffer": 0, "byteOffset": )" + std::to_string(image_offset) + R"(, "byteLength": )" + std::to_string(image_length) + R"(}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"}
  ],
  "images": [{"bufferView": 2, "mimeType": "image/png"}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "material": 0}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";

    std::vector<unsigned char> json_bytes(json.begin(), json.end());
    pad4(json_bytes, ' ');

    std::vector<unsigned char> glb;
    append_u32(glb, 0x46546c67U);
    append_u32(glb, 2U);
    append_u32(glb, static_cast<std::uint32_t>(12U + 8U + json_bytes.size() + 8U + bin.size()));
    append_u32(glb, static_cast<std::uint32_t>(json_bytes.size()));
    append_u32(glb, 0x4e4f534aU);
    glb.insert(glb.end(), json_bytes.begin(), json_bytes.end());
    append_u32(glb, static_cast<std::uint32_t>(bin.size()));
    append_u32(glb, 0x004e4942U);
    glb.insert(glb.end(), bin.begin(), bin.end());
    write_binary(dir / "model.glb", glb);

    const pixelizer::ModelLoadResult loaded = pixelizer::load_model_document(dir / "model.glb");
    require(loaded.error.empty());
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.textures[0].embedded);
    require(pixelizer::default_model_texture_export_name(loaded.model.textures[0], 0) == "texture_0.png");
}

void obj_diffuse_texture_loads()
{
    const std::filesystem::path dir = test_dir();
    write_texture_png(dir / "diffuse.png");
    write_text(dir / "model.mtl", "newmtl mat\nmap_Kd diffuse.png\n");
    write_text(
        dir / "model.obj",
        "mtllib model.mtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "usemtl mat\n"
        "f 1/1 2/2 3/3\n");

    const pixelizer::ModelLoadResult loaded = pixelizer::load_model_document(dir / "model.obj");
    require(loaded.error.empty());
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].vertices.size() == 3U);
    require(loaded.model.primitives[0].texture_index == 0);
    require(loaded.model.primitives[0].material_name == "mat");
}

void model_without_texture_uses_fallback()
{
    const std::filesystem::path dir = test_dir();
    write_text(
        dir / "plain.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    const pixelizer::ModelLoadResult loaded = pixelizer::load_model_document(dir / "plain.obj");
    require(loaded.error.empty());
    require(loaded.model.textures.empty());
    require(loaded.model.used_fallback_texture);
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].texture_index == -1);
}

void export_name_defaults()
{
    pixelizer::ModelTexture external;
    external.source_path = "C:/assets/painted-diffuse.jpg";
    require(pixelizer::default_model_texture_export_name(external, 4) == "painted-diffuse.png");

    pixelizer::ModelTexture embedded;
    require(pixelizer::default_model_texture_export_name(embedded, 4) == "texture_4.png");
}

} // namespace

int main()
{
    gltf_external_texture_loads_and_dedupes();
    gltf_without_material_uses_grey_fallback();
    glb_embedded_texture_loads();
    obj_diffuse_texture_loads();
    model_without_texture_uses_fallback();
    export_name_defaults();
    return 0;
}
