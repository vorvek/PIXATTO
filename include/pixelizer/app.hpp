#pragma once

#include "pixelizer/image.hpp"
#include "pixelizer/image_processing.hpp"
#include "pixelizer/palette.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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

    enum class DialogKind {
        OpenImage,
        ImportPalette,
        ExportPng,
    };

    enum class ViewportLayout {
        SideBySide,
        Stacked,
    };

    struct PendingDialog {
        DialogKind kind;
        std::string path;
    };

    struct DialogState;
    struct DialogPayload;
    struct HistorySnapshot {
        ProcessSettings settings;
        int selected_palette = -1;

        bool operator==(const HistorySnapshot&) const = default;
    };
    struct NumberEditState {
        std::string label;
        std::string format;
        double value = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
        bool integer = false;
        bool request_open = false;
        HistorySnapshot before;
        std::function<void(double)> apply;
    };

    static void SDLCALL dialog_callback(void* userdata, const char* const* filelist, int filter);

    void shutdown();
    void process_events(bool& running);
    void render_frame();
    void render_menu_bar();
    void render_controls();
    void render_viewports();
    void render_image_view(const char* label, Texture& texture, float& zoom);
    void render_number_edit_popup();
    void render_drop_confirm_popup();
    void handle_shortcuts();
    void update_preview_if_needed();
    void rebuild_texture(Texture& texture, const Image& image, bool nearest);
    void destroy_texture(Texture& texture);
    void request_open_image();
    void request_import_palette();
    void request_export_png();
    void handle_pending_dialogs();
    void handle_dropped_image(const std::filesystem::path& path);
    void load_image_from_path(const std::filesystem::path& path);
    void import_palette_from_path(const std::filesystem::path& path);
    void export_result_to_path(const std::filesystem::path& path);
    void refresh_palettes();
    void mark_dirty();
    void set_status(std::string message);
    void normalize_settings();
    HistorySnapshot capture_history_snapshot() const;
    void record_control_history(const HistorySnapshot& before);
    void commit_history_change(const HistorySnapshot& before);
    void apply_history_snapshot(const HistorySnapshot& snapshot);
    void undo();
    void redo();
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    bool slider_int_direct(const char* label, int& value, int minimum, int maximum);
    bool slider_float_direct(const char* label, float& value, float minimum, float maximum, const char* format);
    bool slider_float_direct_value(const char* label, float value, float minimum, float maximum, const char* format, std::function<void(float)> apply);
    void open_number_edit(std::string label, double value, double minimum, double maximum, bool integer, std::string format, std::function<void(double)> apply);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    Image original_;
    Image result_;
    Texture original_texture_;
    Texture result_texture_;

    ProcessSettings settings_;
    std::vector<Palette> palettes_;
    int selected_palette_ = -1;
    bool preview_dirty_ = false;

    float original_zoom_ = 1.0F;
    float result_zoom_ = 1.0F;
    float viewport_split_ratio_ = 0.5F;
    ViewportLayout viewport_layout_ = ViewportLayout::SideBySide;
    bool running_ = false;
    std::string status_ = "Open an image to begin.";
    std::filesystem::path current_image_path_;
    std::optional<std::filesystem::path> last_export_path_;
    std::optional<std::filesystem::path> pending_dropped_image_;
    bool open_drop_confirm_ = false;
    std::optional<HistorySnapshot> active_edit_snapshot_;
    std::optional<NumberEditState> number_edit_;
    std::vector<HistorySnapshot> undo_stack_;
    std::vector<HistorySnapshot> redo_stack_;

    std::shared_ptr<DialogState> dialog_state_;
};

} // namespace pixelizer
