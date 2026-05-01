#include "pixatto/localization.hpp"

#include <array>
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
    const auto issues = pixatto::validate_translation_catalog();
    if (!issues.empty()) {
        throw std::runtime_error(issues.front().message);
    }
}

void english_covers_every_text_id()
{
    for (std::size_t index = 0; index < pixatto::kTextCount; ++index) {
        const char* translated = pixatto::translate(pixatto::Language::English, static_cast<pixatto::TextId>(index));
        require(translated && translated[0] != '\0');
    }
}

void partial_language_catalogs_fall_back_to_english()
{
    const char* english = pixatto::translate(pixatto::Language::English, pixatto::TextId::StatusFileDialogFailedFormat);
    const char* korean = pixatto::translate(pixatto::Language::Korean, pixatto::TextId::StatusFileDialogFailedFormat);
    require(std::string(korean) == english);
}

void format_translation_replaces_placeholders_after_fallback()
{
    const std::string formatted = pixatto::format_translation(
        pixatto::Language::Korean,
        pixatto::TextId::StatusFileDialogFailedFormat,
        {{"error", "boom"}});
    require(formatted.find("boom") != std::string::npos);
    require(formatted.find("{error}") == std::string::npos);
}

void preset_management_text_is_localized_for_every_language()
{
    constexpr std::array preset_text = {
        pixatto::TextId::Presets,
        pixatto::TextId::NoPresetsSaved,
        pixatto::TextId::SavePreset,
        pixatto::TextId::DeletePreset,
        pixatto::TextId::SavePresetAsTitle,
        pixatto::TextId::PresetName,
        pixatto::TextId::PresetAlreadyExistsTitle,
        pixatto::TextId::PresetAlreadyExistsFormat,
        pixatto::TextId::DeletePresetTitle,
        pixatto::TextId::DeleteSavedPresetFormat,
        pixatto::TextId::StatusPresetSaveFailedFormat,
        pixatto::TextId::StatusSavedPresetFormat,
        pixatto::TextId::StatusOverwrotePresetFormat,
        pixatto::TextId::StatusAppliedPresetFormat,
        pixatto::TextId::StatusPresetDeleteFailedFormat,
        pixatto::TextId::StatusDeletedPresetFormat,
    };

    for (std::size_t language_index = 0; language_index < pixatto::kLanguageCount; ++language_index) {
        const auto language = static_cast<pixatto::Language>(language_index);
        for (const pixatto::TextId id : preset_text) {
            const std::string translated = pixatto::translate(language, id);
            require(!translated.empty());
            if (language != pixatto::Language::English) {
                require(translated != pixatto::translate(pixatto::Language::English, id));
            }
        }
    }
}

} // namespace

int main()
{
    translation_catalog_validates();
    english_covers_every_text_id();
    partial_language_catalogs_fall_back_to_english();
    format_translation_replaces_placeholders_after_fallback();
    preset_management_text_is_localized_for_every_language();
    return 0;
}
