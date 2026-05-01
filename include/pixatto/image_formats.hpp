#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace pixatto {

inline constexpr std::array<std::string_view, 17> kImportableImageExtensions = {
    ".png",
    ".jpg",
    ".jpeg",
    ".jpe",
    ".jfif",
    ".bmp",
    ".tga",
    ".gif",
    ".webp",
    ".jxl",
    ".qoi",
    ".tif",
    ".tiff",
    ".pnm",
    ".ppm",
    ".pgm",
    ".pbm",
};

inline constexpr std::string_view kImportableImageDialogPattern =
    "png;jpg;jpeg;jpe;jfif;bmp;tga;gif;webp;jxl;qoi;tif;tiff;pnm;ppm;pgm;pbm";

inline std::string lowercase_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

inline bool is_importable_image_path(const std::filesystem::path& path)
{
    const std::string extension = lowercase_extension(path);
    return std::find(kImportableImageExtensions.begin(), kImportableImageExtensions.end(), extension)
        != kImportableImageExtensions.end();
}

} // namespace pixatto
