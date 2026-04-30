#include "pixelizer/localization.hpp"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("localization test failed");
    }
}

void translation_catalog_validates()
{
    const auto issues = pixelizer::validate_translation_catalog();
    if (!issues.empty()) {
        throw std::runtime_error(issues.front().message);
    }
}

void english_covers_every_text_id()
{
    for (std::size_t index = 0; index < pixelizer::kTextCount; ++index) {
        const char* translated = pixelizer::translate(pixelizer::Language::English, static_cast<pixelizer::TextId>(index));
        require(translated && translated[0] != '\0');
    }
}

void partial_language_catalogs_fall_back_to_english()
{
    const char* english = pixelizer::translate(pixelizer::Language::English, pixelizer::TextId::StatusFileDialogFailedFormat);
    const char* korean = pixelizer::translate(pixelizer::Language::Korean, pixelizer::TextId::StatusFileDialogFailedFormat);
    require(std::string(korean) == english);
}

void format_translation_replaces_placeholders_after_fallback()
{
    const std::string formatted = pixelizer::format_translation(
        pixelizer::Language::Korean,
        pixelizer::TextId::StatusFileDialogFailedFormat,
        {{"error", "boom"}});
    require(formatted.find("boom") != std::string::npos);
    require(formatted.find("{error}") == std::string::npos);
}

} // namespace

int main()
{
    translation_catalog_validates();
    english_covers_every_text_id();
    partial_language_catalogs_fall_back_to_english();
    format_translation_replaces_placeholders_after_fallback();
    return 0;
}
