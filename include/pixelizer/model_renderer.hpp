#pragma once

#include "pixelizer/image.hpp"
#include "pixelizer/model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pixelizer {

class ModelRenderer {
public:
    ModelRenderer() = default;
    ~ModelRenderer();

    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    [[nodiscard]] bool initialize(std::string glsl_version, std::string& error);
    void shutdown();

    [[nodiscard]] bool upload_model(
        const ModelDocument& model,
        const std::vector<Image>& processed_textures,
        std::string& error);
    void update_processed_textures(const std::vector<Image>& processed_textures);
    [[nodiscard]] std::uintptr_t render_preview(
        const ModelDocument& model,
        int width,
        int height,
        float yaw,
        float pitch,
        float distance,
        float target_offset_x,
        float target_offset_y,
        float target_offset_z,
        std::string& error);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace pixelizer
