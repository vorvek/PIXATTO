#include "pixatto/image.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct PngChunk {
    std::size_t data_offset = 0;
    std::uint32_t length = 0;
};

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("image I/O test failed");
    }
}

std::filesystem::path test_dir()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "pixatto-image-io-tests";
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path);
    return path;
}

std::vector<unsigned char> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input));
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path);
    require(static_cast<bool>(input));
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_binary(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output));
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output));
}

std::uint32_t read_u32_be(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
        | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::optional<PngChunk> find_chunk(const std::vector<unsigned char>& png, const char* type)
{
    std::size_t offset = 8U;
    while (offset + 12U <= png.size()) {
        const std::uint32_t length = read_u32_be(png, offset);
        const std::size_t type_offset = offset + 4U;
        const std::size_t data_offset = offset + 8U;
        const std::size_t next_offset = data_offset + static_cast<std::size_t>(length) + 4U;
        require(next_offset <= png.size());

        if (png[type_offset] == static_cast<unsigned char>(type[0])
            && png[type_offset + 1U] == static_cast<unsigned char>(type[1])
            && png[type_offset + 2U] == static_cast<unsigned char>(type[2])
            && png[type_offset + 3U] == static_cast<unsigned char>(type[3])) {
            return PngChunk{data_offset, length};
        }

        offset = next_offset;
    }

    return std::nullopt;
}

void indexed_png_uses_palette_color_type()
{
    const std::filesystem::path path = test_dir() / "indexed.png";
    const pixatto::Image image = {
        2,
        1,
        {
            255, 0, 0, 255,
            0, 0, 255, 255,
        },
    };

    std::string error;
    require(pixatto::save_png_rgba(path.string(), image, error));

    const std::vector<unsigned char> png = read_binary(path);
    const std::optional<PngChunk> ihdr = find_chunk(png, "IHDR");
    require(ihdr.has_value());
    require(png[ihdr->data_offset + 8U] == 8U);
    require(png[ihdr->data_offset + 9U] == 3U);
    require(find_chunk(png, "PLTE").has_value());
    require(!find_chunk(png, "tRNS").has_value());
}

void png_with_256_opaque_colors_plus_transparency_uses_rgba()
{
    std::vector<unsigned char> pixels;
    pixels.reserve(257U * 4U);
    for (int index = 0; index < 256; ++index) {
        pixels.push_back(static_cast<unsigned char>(index));
        pixels.push_back(static_cast<unsigned char>(255 - index));
        pixels.push_back(static_cast<unsigned char>((index * 37) & 0xff));
        pixels.push_back(255U);
    }
    pixels.push_back(0U);
    pixels.push_back(0U);
    pixels.push_back(0U);
    pixels.push_back(0U);

    const std::filesystem::path path = test_dir() / "rgba.png";
    const pixatto::Image image = {257, 1, pixels};

    std::string error;
    require(pixatto::save_png_rgba(path.string(), image, error));

    const std::vector<unsigned char> png = read_binary(path);
    const std::optional<PngChunk> ihdr = find_chunk(png, "IHDR");
    require(ihdr.has_value());
    require(png[ihdr->data_offset + 8U] == 8U);
    require(png[ihdr->data_offset + 9U] == 6U);
    require(!find_chunk(png, "PLTE").has_value());
}

void indexed_png_with_transparency_round_trips()
{
    const std::filesystem::path path = test_dir() / "indexed-alpha.png";
    const pixatto::Image image = {
        2,
        1,
        {
            255, 0, 0, 255,
            12, 34, 56, 0,
        },
    };

    std::string error;
    require(pixatto::save_png_rgba(path.string(), image, error));

    const std::vector<unsigned char> png = read_binary(path);
    const std::optional<PngChunk> ihdr = find_chunk(png, "IHDR");
    require(ihdr.has_value());
    require(png[ihdr->data_offset + 9U] == 3U);
    require(find_chunk(png, "PLTE").has_value());
    require(find_chunk(png, "tRNS").has_value());

    const pixatto::ImageLoadResult loaded = pixatto::load_image_rgba(path.string());
    require(loaded.error.empty());
    require(loaded.image.width == image.width);
    require(loaded.image.height == image.height);
    require(loaded.image.rgba == image.rgba);
}

void tga_import_simplifies_alpha_to_binary()
{
    const std::filesystem::path path = test_dir() / "alpha.tga";
    write_binary(
        path,
        {
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            2, 0, 1, 0, 32, 8,
            0, 0, 255, 128,
            255, 0, 0, 0,
        });

    const pixatto::ImageLoadResult loaded = pixatto::load_image_rgba(path.string());
    require(loaded.error.empty());
    require(loaded.image.width == 2);
    require(loaded.image.height == 1);
    require((loaded.image.rgba == std::vector<unsigned char>{
        255, 0, 0, 255,
        0, 0, 255, 0,
    }));
}

void raw_export_writes_shared_palette_sidecar_and_image_mask()
{
    const std::filesystem::path dir = test_dir();
    const std::filesystem::path path = dir / "sprite.raw";
    const pixatto::Image image = {
        3,
        2,
        {
            255, 0, 0, 255,
            0, 0, 0, 0,
            0, 0, 255, 255,
            255, 0, 0, 255,
            0, 255, 0, 255,
            0, 0, 0, 0,
        },
    };
    const std::vector<pixatto::Color32> palette = {
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
    };

    std::string error;
    require(pixatto::save_raw_indexed(path.string(), image, palette, "Game Palette", error));

    const std::vector<unsigned char> raw = read_binary(path);
    require((raw == std::vector<unsigned char>{0U, 0U, 2U, 0U, 1U, 0U}));

    const std::vector<unsigned char> mask = read_binary(dir / "sprite.msk");
    require((mask == std::vector<unsigned char>{0x44U}));

    const std::string sidecar = read_text(dir / "game_palette.pal");
    require(sidecar == "FF0000\n00FF00\n0000FF\n");
}

} // namespace

int main()
{
    indexed_png_uses_palette_color_type();
    png_with_256_opaque_colors_plus_transparency_uses_rgba();
    indexed_png_with_transparency_round_trips();
    tga_import_simplifies_alpha_to_binary();
    raw_export_writes_shared_palette_sidecar_and_image_mask();
    return 0;
}
