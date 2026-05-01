#include "pixatto/image.hpp"
#include "pixatto/model.hpp"

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
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "pixatto-model-tests";
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
    const pixatto::Image image = {
        2,
        1,
        {
            255, 0, 0, 255,
            0, 255, 0, 255,
        },
    };
    std::string error;
    require(pixatto::save_png_rgba(path.string(), image, error));
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

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "model.gltf");
    require(loaded.error.empty());
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.primitives.size() == 2U);
    require(loaded.model.primitives[0].texture_index == 0);
    require(loaded.model.primitives[1].texture_index == 0);
    require(pixatto::default_model_texture_export_name(loaded.model.textures[0], 0) == "diffuse.png");
}

void fbx_extension_is_supported()
{
    require(pixatto::is_model_path("models/hero.fbx"));
    require(pixatto::is_model_path("models/HERO.FBX"));
}

void dae_extension_is_supported()
{
    require(pixatto::is_model_path("models/hero.dae"));
    require(pixatto::is_model_path("models/HERO.DAE"));
}

void fbx_ascii_triangle_loads_with_fallback_material()
{
    const std::filesystem::path dir = test_dir();
    write_text(
        dir / "triangle.fbx",
        R"(; FBX 7.7.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1004
    FBXVersion: 7700
}
GlobalSettings:  {
    Version: 1000
    Properties70:  {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Definitions:  {
    Version: 100
    Count: 3
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Model" { Count: 1 }
    ObjectType: "Material" { Count: 1 }
}
Objects:  {
    Geometry: 1, "Geometry::TriangleMesh", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        GeometryVersion: 124
        LayerElementUV: 0 {
            Version: 101
            Name: "map1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "IndexToDirect"
            UV: *6 { a: 0,0,1,0,0,1 }
            UVIndex: *3 { a: 0,1,2 }
        }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement:  { Type: "LayerElementMaterial" TypedIndex: 0 }
            LayerElement:  { Type: "LayerElementUV" TypedIndex: 0 }
        }
    }
    Model: 2, "Model::Triangle", "Mesh" {
        Version: 232
        Shading: T
        Culling: "CullingOff"
    }
    Material: 3, "Material::lambert1", "" {
        Version: 102
        ShadingModel: "lambert"
        Properties70:  {
            P: "DiffuseColor", "Color", "", "A",0.5,0.5,0.5
        }
    }
}
Connections:  {
    C: "OO",2,0
    C: "OO",1,2
    C: "OO",3,2
}
)");

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "triangle.fbx");
    require(loaded.error.empty());
    require(loaded.model.used_fallback_texture);
    require(loaded.model.textures.empty());
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].vertices.size() == 3U);
    require(loaded.model.primitives[0].texture_index == -1);
    require(loaded.model.primitives[0].material_name == "lambert1");
    require(loaded.model.radius > 0.7F);
}

void fbx_ascii_external_diffuse_texture_loads()
{
    const std::filesystem::path dir = test_dir();
    write_texture_png(dir / "diffuse.png");
    write_text(
        dir / "textured.fbx",
        R"(; FBX 7.7.0 project file
FBXHeaderExtension:  {
    FBXHeaderVersion: 1004
    FBXVersion: 7700
}
GlobalSettings:  {
    Version: 1000
    Properties70:  {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Definitions:  {
    Version: 100
    Count: 5
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Model" { Count: 1 }
    ObjectType: "Material" { Count: 1 }
    ObjectType: "Video" { Count: 1 }
    ObjectType: "Texture" { Count: 1 }
}
Objects:  {
    Geometry: 1, "Geometry::TriangleMesh", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        GeometryVersion: 124
        LayerElementUV: 0 {
            Version: 101
            Name: "map1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "IndexToDirect"
            UV: *6 { a: 0,0,1,0,0,1 }
            UVIndex: *3 { a: 0,1,2 }
        }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement:  { Type: "LayerElementMaterial" TypedIndex: 0 }
            LayerElement:  { Type: "LayerElementUV" TypedIndex: 0 }
        }
    }
    Model: 2, "Model::Triangle", "Mesh" {
        Version: 232
        Shading: T
        Culling: "CullingOff"
    }
    Material: 3, "Material::lambert1", "" {
        Version: 102
        ShadingModel: "lambert"
        Properties70:  {
            P: "DiffuseColor", "Color", "", "A",0.5,0.5,0.5
        }
    }
    Video: 4, "Video::diffuse.png", "Clip" {
        Type: "Clip"
        UseMipMap: 0
        Filename: "diffuse.png"
        RelativeFilename: "diffuse.png"
    }
    Texture: 5, "Texture::diffuse.png", "" {
        Type: "TextureVideoClip"
        Version: 202
        TextureName: "Texture::diffuse.png"
        Media: "Video::diffuse.png"
        FileName: "diffuse.png"
        RelativeFilename: "diffuse.png"
        ModelUVTranslation: 0,0
        ModelUVScaling: 1,1
        Texture_Alpha_Source: "None"
        Cropping: 0,0,0,0
    }
}
Connections:  {
    C: "OO",2,0
    C: "OO",1,2
    C: "OO",3,2
    C: "OP",5,3, "DiffuseColor"
    C: "OO",4,5
}
)");

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "textured.fbx");
    require(loaded.error.empty());
    require(!loaded.model.used_fallback_texture);
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].texture_index == 0);
    require(pixatto::default_model_texture_export_name(loaded.model.textures[0], 0) == "diffuse.png");
}

void dae_external_diffuse_texture_loads()
{
    const std::filesystem::path dir = test_dir();
    write_texture_png(dir / "diffuse.png");
    write_text(
        dir / "model.dae",
        R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <unit name="meter" meter="1"/>
    <up_axis>Y_UP</up_axis>
  </asset>
  <library_images>
    <image id="diffuse-image" name="diffuse">
      <init_from>diffuse.png</init_from>
    </image>
  </library_images>
  <library_effects>
    <effect id="mat-effect">
      <profile_COMMON>
        <newparam sid="diffuse-surface">
          <surface type="2D">
            <init_from>diffuse-image</init_from>
          </surface>
        </newparam>
        <newparam sid="diffuse-sampler">
          <sampler2D>
            <source>diffuse-surface</source>
          </sampler2D>
        </newparam>
        <technique sid="common">
          <phong>
            <diffuse>
              <texture texture="diffuse-sampler" texcoord="UVMap"/>
            </diffuse>
          </phong>
        </technique>
      </profile_COMMON>
    </effect>
  </library_effects>
  <library_materials>
    <material id="mat" name="mat">
      <instance_effect url="#mat-effect"/>
    </material>
  </library_materials>
  <library_geometries>
    <geometry id="tri-geom" name="Tri">
      <mesh>
        <source id="tri-positions">
          <float_array id="tri-positions-array" count="9">0 0 0 1 0 0 0 1 0</float_array>
          <technique_common>
            <accessor source="#tri-positions-array" count="3" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="tri-map">
          <float_array id="tri-map-array" count="6">0 0 1 0 0 1</float_array>
          <technique_common>
            <accessor source="#tri-map-array" count="3" stride="2">
              <param name="S" type="float"/>
              <param name="T" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="tri-vertices">
          <input semantic="POSITION" source="#tri-positions"/>
        </vertices>
        <triangles material="mat-symbol" count="1">
          <input semantic="VERTEX" source="#tri-vertices" offset="0"/>
          <input semantic="TEXCOORD" source="#tri-map" offset="1" set="0"/>
          <p>0 0 1 1 2 2</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene" name="Scene">
      <node id="Node" name="Node">
        <translate>1 0 0</translate>
        <instance_geometry url="#tri-geom">
          <bind_material>
            <technique_common>
              <instance_material symbol="mat-symbol" target="#mat">
                <bind_vertex_input semantic="UVMap" input_semantic="TEXCOORD" input_set="0"/>
              </instance_material>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
    </visual_scene>
  </library_visual_scenes>
  <scene>
    <instance_visual_scene url="#Scene"/>
  </scene>
</COLLADA>
)");

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "model.dae");
    require(loaded.error.empty());
    require(!loaded.model.used_fallback_texture);
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].vertices.size() == 3U);
    require(loaded.model.primitives[0].texture_index == 0);
    require(loaded.model.primitives[0].mesh_index == 0);
    require(loaded.model.primitives[0].material_index == 0);
    require(loaded.model.primitives[0].mesh_name == "Tri");
    require(loaded.model.primitives[0].material_name == "mat");
    require(loaded.model.primitives[0].vertices[0].x == 1.0F);
    require(pixatto::default_model_texture_export_name(loaded.model.textures[0], 0) == "diffuse.png");
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

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "hero.gltf");
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

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "model.glb");
    require(loaded.error.empty());
    require(loaded.model.textures.size() == 1U);
    require(loaded.model.textures[0].embedded);
    require(pixatto::default_model_texture_export_name(loaded.model.textures[0], 0) == "texture_0.png");
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

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "model.obj");
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

    const pixatto::ModelLoadResult loaded = pixatto::load_model_document(dir / "plain.obj");
    require(loaded.error.empty());
    require(loaded.model.textures.empty());
    require(loaded.model.used_fallback_texture);
    require(loaded.model.primitives.size() == 1U);
    require(loaded.model.primitives[0].texture_index == -1);
}

void export_name_defaults()
{
    pixatto::ModelTexture external;
    external.source_path = "C:/assets/painted-diffuse.jpg";
    require(pixatto::default_model_texture_export_name(external, 4) == "painted-diffuse.png");

    pixatto::ModelTexture embedded;
    require(pixatto::default_model_texture_export_name(embedded, 4) == "texture_4.png");
}

} // namespace

int main()
{
    fbx_extension_is_supported();
    dae_extension_is_supported();
    fbx_ascii_triangle_loads_with_fallback_material();
    fbx_ascii_external_diffuse_texture_loads();
    dae_external_diffuse_texture_loads();
    gltf_external_texture_loads_and_dedupes();
    gltf_without_material_uses_grey_fallback();
    glb_embedded_texture_loads();
    obj_diffuse_texture_loads();
    model_without_texture_uses_fallback();
    export_name_defaults();
    return 0;
}
