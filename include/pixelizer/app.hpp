#pragma once

#include "pixelizer/file_command_pump.hpp"
#include "pixelizer/image.hpp"
#include "pixelizer/localization.hpp"
#include "pixelizer/palette.hpp"
#include "pixelizer/processing_edit_session.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixelizer {

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool initialize();
    int run();

private:
    struct Texture {
        SDL_Texture* handle = nullptr;
        int width = 0;
        int height = 0;
    };

    enum class ViewportLayout {
        SideBySide,
        Stacked,
    };

    enum class ViewportMode {
        Single,
        Split,
    };

    using HistorySnapshot = ProcessingEditSession::Snapshot;
    struct NumberEditState {
        std::string label;
        std::string format;
        std::array<char, 64> input{};
        double value = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        bool integer = false;
        bool request_open = false;
        HistorySnapshot before;
        std::function<void(double)> apply;
    };
    struct PaletteColorEditState {
        int index = -1;
        bool adding = false;
        bool request_open = false;
        std::array<float, 3> color{};
        HistorySnapshot before;
    };
    struct PaletteSaveAsState {
        std::array<char, 128> name{};
        bool request_open = false;
    };
    struct PendingPaletteImportState {
        std::filesystem::path source;
        Palette parsed;
        Palette existing;
        std::array<char, 128> name{};
        bool request_conflict_open = false;
        bool request_name_open = false;
    };

    void shutdown();
    void process_events(bool& running);
    void render_frame();
    void render_menu_bar();
    void render_language_picker_popup();
    void render_about_dialog();
    void render_controls();
    void render_viewports();
    void render_image_view(TextId label, const char* id, Texture& texture, float& zoom);
    void render_number_edit_popup();
    void render_drop_confirm_popup();
    void render_delete_palette_popup();
    void render_palette_import_conflict_popup();
    void render_palette_import_name_popup();
    void render_palette_color_popup();
    void render_save_palette_popup();
    void handle_shortcuts();
    void update_preview_if_needed();
    void rebuild_texture(Texture& texture, const Image& image, bool nearest);
    void configure_fonts();
    void destroy_texture(Texture& texture);
    void request_open_image();
    void request_import_palette();
    void request_export_png();
    void request_export_raw();
    void drain_file_commands();
    void handle_file_command(const FileCommand& command);
    void load_image_from_path(const std::filesystem::path& path);
    void import_palette_from_path(const std::filesystem::path& path);
    bool import_pending_palette(PaletteImportMode mode);
    bool save_pending_palette_import_as_name(const std::string& name);
    void finish_palette_import(const Palette& palette, TextId message);
    void request_new_palette();
    void request_add_palette_color();
    void request_edit_palette_color(std::size_t index);
    void request_save_palette();
    void request_save_palette_as();
    void request_delete_selected_palette();
    bool save_palette_as_name(const std::string& name);
    bool select_palette_by_path(const std::filesystem::path& path);
    void delete_pending_palette();
    void export_result_to_png_path(const std::filesystem::path& path);
    void export_result_to_raw_path(const std::filesystem::path& path);
    void refresh_palettes();
    void mark_dirty();
    void set_status(std::string message);
    [[nodiscard]] const char* text(TextId id) const;
    [[nodiscard]] std::string textf(
        TextId id,
        std::initializer_list<std::pair<std::string_view, std::string_view>> values) const;
    [[nodiscard]] std::string imgui_label(TextId label, const char* id) const;
    [[nodiscard]] FileDialogLabels file_dialog_labels() const;
    void normalize_settings();
    HistorySnapshot capture_history_snapshot() const;
    void record_control_history(const HistorySnapshot& before);
    void commit_history_change(const HistorySnapshot& before);
    void undo();
    void redo();
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool slider_int_direct(TextId label, const char* id, int& value, int minimum, int maximum);
    bool slider_float_direct(TextId label, const char* id, float& value, float minimum, float maximum, const char* format);
    bool slider_float_direct_value(TextId label, const char* id, float value, float minimum, float maximum, const char* format, std::function<void(float)> apply);
    void open_number_edit(std::string label, double value, double minimum, double maximum, bool integer, std::string format, std::function<void(double)> apply);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    Image original_;
    Image result_;
    Texture original_texture_;
    Texture result_texture_;

    ProcessingEditSession edit_session_;
    std::vector<Palette> palettes_;

    float original_zoom_ = 1.0F;
    float result_zoom_ = 1.0F;
    float viewport_split_ratio_ = 0.5F;
    ViewportMode viewport_mode_ = ViewportMode::Single;
    ViewportLayout viewport_layout_ = ViewportLayout::SideBySide;
    Language language_ = Language::English;
    bool open_language_picker_ = false;
    bool open_about_dialog_ = false;
    bool running_ = false;
    std::string status_;
    std::filesystem::path current_image_path_;
    std::optional<std::filesystem::path> last_export_path_;
    std::optional<std::filesystem::path> pending_dropped_image_;
    std::optional<Palette> pending_delete_palette_;
    std::optional<PendingPaletteImportState> pending_palette_import_;
    bool open_drop_confirm_ = false;
    bool open_delete_palette_confirm_ = false;
    FileCommandPump file_commands_;
    std::optional<NumberEditState> number_edit_;
    std::optional<PaletteColorEditState> palette_color_edit_;
    std::optional<PaletteSaveAsState> palette_save_as_;
};

} // namespace pixelizer
