#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pixelizer {

struct Color32 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    bool operator==(const Color32&) const = default;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const noexcept
    {
        return width <= 0 || height <= 0 || rgba.empty();
    }
};

struct ImageLoadResult {
    Image image;
    std::string error;
};

ImageLoadResult load_image_rgba(const std::string& path);
bool save_png_rgba(const std::string& path, const Image& image, std::string& error);

} // namespace pixelizer
