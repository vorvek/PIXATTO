#include "pixelizer/image.hpp"

#include <filesystem>
#include <limits>

#include <stb_image.h>
#include <stb_image_write.h>

namespace pixelizer {

ImageLoadResult load_image_rgba(const std::string& path)
{
    ImageLoadResult result;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        result.error = stbi_failure_reason() ? stbi_failure_reason() : "Unable to load image.";
        return result;
    }

    const std::size_t byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    result.image.width = width;
    result.image.height = height;
    result.image.rgba.assign(pixels, pixels + byte_count);
    stbi_image_free(pixels);
    return result;
}

bool save_png_rgba(const std::string& path, const Image& image, std::string& error)
{
    if (image.empty()) {
        error = "There is no result image to export.";
        return false;
    }

    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        error = "Image dimensions are too large.";
        return false;
    }

    const auto pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U || image.rgba.size() != pixels * 4U) {
        error = "Image buffer does not match its dimensions.";
        return false;
    }

    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
    }

    const int stride = image.width * 4;
    if (stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), stride) == 0) {
        error = "stb_image_write failed to write the PNG.";
        return false;
    }

    return true;
}

} // namespace pixelizer
