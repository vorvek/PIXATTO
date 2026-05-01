#include "pixelizer/image.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string_view>
#include <unordered_map>

#include <stb_image.h>
#include <stb_image_write.h>

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace pixelizer {
namespace {

struct IndexedImage {
    std::vector<Color32> palette;
    std::vector<std::uint8_t> indices;
    bool has_alpha = false;
};

struct RawIndexedImage {
    std::vector<Color32> palette;
    std::vector<std::uint8_t> indices;
    std::vector<std::uint8_t> transparency_mask;
    bool has_transparency = false;
};

std::uint32_t rgba_key(const std::uint8_t* pixel)
{
    return (static_cast<std::uint32_t>(pixel[0U]) << 24U)
        | (static_cast<std::uint32_t>(pixel[1U]) << 16U)
        | (static_cast<std::uint32_t>(pixel[2U]) << 8U)
        | static_cast<std::uint32_t>(pixel[3U]);
}

std::uint32_t rgb_key(Color32 color)
{
    return (static_cast<std::uint32_t>(color.r) << 16U)
        | (static_cast<std::uint32_t>(color.g) << 8U)
        | static_cast<std::uint32_t>(color.b);
}

bool validate_image_buffer(const Image& image, std::size_t& pixels, std::string& error)
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

    pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U || image.rgba.size() != pixels * 4U) {
        error = "Image buffer does not match its dimensions.";
        return false;
    }

    return true;
}

bool ensure_parent_dir(const std::filesystem::path& path, std::string& error)
{
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    return true;
}

bool write_binary_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes, std::string& error)
{
    if (!ensure_parent_dir(path, error)) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write " + path.filename().string() + ".";
        return false;
    }

    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    if (!output) {
        error = "Write failed for " + path.filename().string() + ".";
        return false;
    }

    return true;
}

bool build_png_indexed_image(const Image& image, std::size_t pixels, IndexedImage& indexed)
{
    std::unordered_map<std::uint32_t, std::uint8_t> color_to_index;
    color_to_index.reserve(256U);
    indexed.palette.clear();
    indexed.indices.clear();
    indexed.indices.reserve(pixels);
    indexed.has_alpha = false;

    const std::uint8_t* pixel = image.rgba.data();
    for (std::size_t index = 0; index < pixels; ++index) {
        const std::uint32_t key = rgba_key(pixel);
        const auto found = color_to_index.find(key);
        if (found != color_to_index.end()) {
            indexed.indices.push_back(found->second);
        } else {
            if (indexed.palette.size() >= 256U) {
                return false;
            }

            const auto palette_index = static_cast<std::uint8_t>(indexed.palette.size());
            color_to_index.emplace(key, palette_index);
            indexed.palette.push_back({pixel[0U], pixel[1U], pixel[2U], pixel[3U]});
            indexed.indices.push_back(palette_index);
            indexed.has_alpha = indexed.has_alpha || pixel[3U] != 255U;
        }

        pixel += 4U;
    }

    return true;
}

bool image_has_alpha(const Image& image)
{
    for (std::size_t index = 3U; index < image.rgba.size(); index += 4U) {
        if (image.rgba[index] != 255U) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint8_t> build_rgb_buffer(const Image& image, std::size_t pixels)
{
    std::vector<std::uint8_t> rgb;
    rgb.reserve(pixels * 3U);
    const std::uint8_t* pixel = image.rgba.data();
    for (std::size_t index = 0; index < pixels; ++index) {
        rgb.push_back(pixel[0U]);
        rgb.push_back(pixel[1U]);
        rgb.push_back(pixel[2U]);
        pixel += 4U;
    }
    return rgb;
}

void append_u32_be(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xffffffffU;
}

void append_png_chunk(
    std::vector<std::uint8_t>& png,
    std::string_view type,
    const std::uint8_t* data,
    std::size_t size)
{
    append_u32_be(png, static_cast<std::uint32_t>(size));
    const std::size_t crc_start = png.size();
    png.insert(png.end(), type.begin(), type.end());
    if (size > 0U) {
        png.insert(png.end(), data, data + size);
    }
    append_u32_be(png, crc32(png.data() + crc_start, 4U + size));
}

void append_png_chunk(std::vector<std::uint8_t>& png, std::string_view type, const std::vector<std::uint8_t>& data)
{
    append_png_chunk(png, type, data.data(), data.size());
}

bool save_png_indexed(const std::filesystem::path& path, const Image& image, const IndexedImage& indexed, std::string& error)
{
    if (image.width <= 0 || image.height <= 0 || indexed.palette.empty()) {
        error = "Image buffer does not match its dimensions.";
        return false;
    }

    if (indexed.indices.size() > (std::numeric_limits<std::size_t>::max() / 2U) - static_cast<std::size_t>(image.height)) {
        error = "Image dimensions are too large.";
        return false;
    }

    std::vector<std::uint8_t> scanlines;
    scanlines.reserve((static_cast<std::size_t>(image.width) + 1U) * static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        scanlines.push_back(0U);
        const auto row_start = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width);
        scanlines.insert(
            scanlines.end(),
            indexed.indices.begin() + static_cast<std::ptrdiff_t>(row_start),
            indexed.indices.begin() + static_cast<std::ptrdiff_t>(row_start + static_cast<std::size_t>(image.width)));
    }

    if (scanlines.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        error = "Image dimensions are too large.";
        return false;
    }

    int compressed_size = 0;
    unsigned char* compressed = stbi_zlib_compress(
        scanlines.data(),
        static_cast<int>(scanlines.size()),
        &compressed_size,
        stbi_write_png_compression_level);
    if (!compressed || compressed_size <= 0) {
        std::free(compressed);
        error = "stb_image_write failed to compress the indexed PNG.";
        return false;
    }

    std::vector<std::uint8_t> png = {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};

    std::vector<std::uint8_t> ihdr;
    ihdr.reserve(13U);
    append_u32_be(ihdr, static_cast<std::uint32_t>(image.width));
    append_u32_be(ihdr, static_cast<std::uint32_t>(image.height));
    ihdr.push_back(8U);
    ihdr.push_back(3U);
    ihdr.push_back(0U);
    ihdr.push_back(0U);
    ihdr.push_back(0U);
    append_png_chunk(png, "IHDR", ihdr);

    std::vector<std::uint8_t> plte;
    plte.reserve(indexed.palette.size() * 3U);
    for (const Color32 color : indexed.palette) {
        plte.push_back(color.r);
        plte.push_back(color.g);
        plte.push_back(color.b);
    }
    append_png_chunk(png, "PLTE", plte);

    if (indexed.has_alpha) {
        std::vector<std::uint8_t> trns;
        trns.reserve(indexed.palette.size());
        for (const Color32 color : indexed.palette) {
            trns.push_back(color.a);
        }
        append_png_chunk(png, "tRNS", trns);
    }

    append_png_chunk(
        png,
        "IDAT",
        reinterpret_cast<const std::uint8_t*>(compressed),
        static_cast<std::size_t>(compressed_size));
    std::free(compressed);
    append_png_chunk(png, "IEND", nullptr, 0U);

    if (!write_binary_file(path, png, error)) {
        return false;
    }

    return true;
}

std::string sanitized_sidecar_stem(std::string value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    bool previous_separator = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            sanitized.push_back(static_cast<char>(std::tolower(ch)));
            previous_separator = false;
        } else if (ch == '-' || ch == '_') {
            sanitized.push_back(static_cast<char>(ch));
            previous_separator = false;
        } else if (!previous_separator) {
            sanitized.push_back('_');
            previous_separator = true;
        }
    }

    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }
    while (!sanitized.empty() && sanitized.front() == '_') {
        sanitized.erase(sanitized.begin());
    }

    return sanitized.empty() ? "palette" : sanitized;
}

std::filesystem::path raw_palette_path(const std::filesystem::path& raw_path, const std::string& palette_name)
{
    std::string stem = palette_name.empty() ? raw_path.stem().string() : palette_name;
    std::filesystem::path path = raw_path.parent_path() / sanitized_sidecar_stem(std::move(stem));
    path.replace_extension(".pal");
    return path;
}

std::filesystem::path raw_mask_path(const std::filesystem::path& raw_path)
{
    std::filesystem::path path = raw_path;
    path.replace_extension(".msk");
    return path;
}

void add_preferred_palette(
    const std::vector<Color32>& preferred_palette,
    RawIndexedImage& indexed,
    std::unordered_map<std::uint32_t, std::uint8_t>& color_to_index)
{
    indexed.palette.reserve(std::min<std::size_t>(preferred_palette.size(), 256U));
    for (const Color32 color : preferred_palette) {
        if (indexed.palette.size() >= 256U) {
            break;
        }

        const std::uint32_t key = rgb_key(color);
        if (!color_to_index.contains(key)) {
            color_to_index.emplace(key, static_cast<std::uint8_t>(indexed.palette.size()));
        }
        indexed.palette.push_back({color.r, color.g, color.b, 255U});
    }
}

bool build_raw_indexed_image(
    const Image& image,
    std::size_t pixels,
    const std::vector<Color32>& preferred_palette,
    RawIndexedImage& indexed,
    std::string& error)
{
    std::unordered_map<std::uint32_t, std::uint8_t> color_to_index;
    color_to_index.reserve(256U);
    indexed = {};
    indexed.indices.reserve(pixels);
    add_preferred_palette(preferred_palette, indexed, color_to_index);

    const std::uint8_t* pixel = image.rgba.data();
    for (std::size_t index = 0; index < pixels; ++index) {
        const std::uint8_t alpha = pixel[3U];
        if (alpha == 0U) {
            indexed.has_transparency = true;
            indexed.indices.push_back(0U);
        } else {
            if (alpha != 255U) {
                error = "Raw export supports only binary transparency.";
                return false;
            }

            const Color32 color = {pixel[0U], pixel[1U], pixel[2U], 255U};
            const std::uint32_t key = rgb_key(color);
            const auto found = color_to_index.find(key);
            if (found != color_to_index.end()) {
                indexed.indices.push_back(found->second);
            } else {
                if (indexed.palette.size() >= 256U) {
                    error = "Raw export requires 256 opaque colors or fewer.";
                    return false;
                }

                const auto palette_index = static_cast<std::uint8_t>(indexed.palette.size());
                color_to_index.emplace(key, palette_index);
                indexed.palette.push_back(color);
                indexed.indices.push_back(palette_index);
            }
        }

        pixel += 4U;
    }

    if (indexed.palette.empty()) {
        indexed.palette.push_back({0U, 0U, 0U, 255U});
    }

    if (indexed.has_transparency) {
        indexed.transparency_mask.assign((pixels + 7U) / 8U, 0U);
        for (std::size_t index = 0; index < pixels; ++index) {
            if (image.rgba[index * 4U + 3U] == 0U) {
                indexed.transparency_mask[index / 8U] |= static_cast<std::uint8_t>(0x80U >> (index % 8U));
            }
        }
    }

    return true;
}

bool write_raw_palette_sidecar(
    const std::filesystem::path& path,
    const std::vector<Color32>& palette,
    std::string& error)
{
    if (!ensure_parent_dir(path, error)) {
        return false;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "Unable to write palette sidecar.";
        return false;
    }

    output << std::uppercase << std::hex << std::setfill('0');
    for (const Color32 color : palette) {
        output << std::setw(2) << static_cast<int>(color.r)
               << std::setw(2) << static_cast<int>(color.g)
               << std::setw(2) << static_cast<int>(color.b)
               << '\n';
    }

    if (!output) {
        error = "Palette sidecar write failed.";
        return false;
    }

    return true;
}

void remove_stale_mask(const std::filesystem::path& path)
{
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        std::filesystem::remove(path, ec);
    }
}

} // namespace

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

ImageLoadResult load_image_rgba_memory(const std::uint8_t* bytes, std::size_t byte_count)
{
    ImageLoadResult result;
    if (!bytes || byte_count == 0U || byte_count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        result.error = "Unable to load image.";
        return result;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        bytes,
        static_cast<int>(byte_count),
        &width,
        &height,
        &channels,
        4);
    if (!pixels) {
        result.error = stbi_failure_reason() ? stbi_failure_reason() : "Unable to load image.";
        return result;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    result.image.width = width;
    result.image.height = height;
    result.image.rgba.assign(pixels, pixels + pixel_count * 4U);
    stbi_image_free(pixels);
    return result;
}

bool save_png_rgba(const std::string& path, const Image& image, std::string& error)
{
    std::size_t pixels = 0;
    if (!validate_image_buffer(image, pixels, error)) {
        return false;
    }

    const std::filesystem::path destination(path);
    if (!ensure_parent_dir(destination, error)) {
        return false;
    }

    IndexedImage indexed;
    if (build_png_indexed_image(image, pixels, indexed)) {
        return save_png_indexed(destination, image, indexed, error);
    }

    if (image_has_alpha(image)) {
        const int stride = image.width * 4;
        if (stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), stride) != 0) {
            return true;
        }
    } else {
        std::vector<std::uint8_t> rgb = build_rgb_buffer(image, pixels);
        const int stride = image.width * 3;
        if (stbi_write_png(path.c_str(), image.width, image.height, 3, rgb.data(), stride) != 0) {
            return true;
        }
    }

    error = "stb_image_write failed to write the PNG.";
    return false;
}

bool save_raw_indexed(
    const std::string& path,
    const Image& image,
    const std::vector<Color32>& preferred_palette,
    const std::string& palette_name,
    std::string& error)
{
    std::size_t pixels = 0;
    if (!validate_image_buffer(image, pixels, error)) {
        return false;
    }

    RawIndexedImage indexed;
    if (!build_raw_indexed_image(image, pixels, preferred_palette, indexed, error)) {
        return false;
    }

    const std::filesystem::path raw_path(path);
    if (!write_binary_file(raw_path, indexed.indices, error)) {
        error = "Unable to write raw file.";
        return false;
    }

    const std::filesystem::path palette_path = raw_palette_path(raw_path, palette_name);
    if (!write_raw_palette_sidecar(palette_path, indexed.palette, error)) {
        return false;
    }

    const std::filesystem::path mask_path = raw_mask_path(raw_path);
    if (indexed.has_transparency) {
        if (!write_binary_file(mask_path, indexed.transparency_mask, error)) {
            error = "Unable to write transparency mask.";
            return false;
        }
    } else {
        remove_stale_mask(mask_path);
    }

    return true;
}

bool save_raw_indexed(
    const std::string& path,
    const Image& image,
    const std::vector<Color32>& preferred_palette,
    std::string& error)
{
    return save_raw_indexed(path, image, preferred_palette, {}, error);
}

} // namespace pixelizer
