#pragma once

#include "pixelizer/image.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace pixelizer {

struct ModelVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

struct ModelPrimitive {
    std::vector<ModelVertex> vertices;
    int texture_index = -1;
    int mesh_index = -1;
    int material_index = -1;
    std::string mesh_name;
    std::string material_name;
};

struct ModelTexture {
    std::string name;
    std::filesystem::path source_path;
    bool embedded = false;
    Image image;
};

struct ModelDocument {
    std::filesystem::path path;
    std::vector<ModelTexture> textures;
    std::vector<ModelPrimitive> primitives;
    std::array<float, 3> center = {0.0F, 0.0F, 0.0F};
    float radius = 1.0F;
    bool used_fallback_texture = false;

    [[nodiscard]] bool empty() const noexcept
    {
        return primitives.empty();
    }
};

struct ModelLoadResult {
    ModelDocument model;
    std::string warning;
    std::string error;
};

[[nodiscard]] bool is_model_path(const std::filesystem::path& path);
[[nodiscard]] ModelLoadResult load_model_document(const std::filesystem::path& path);
[[nodiscard]] std::string default_model_texture_export_name(const ModelTexture& texture, std::size_t index);

} // namespace pixelizer
