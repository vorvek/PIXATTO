#pragma once

#include "pixatto/file_command_pump.hpp"
#include "pixatto/gpu_image_processor.hpp"
#include "pixatto/image.hpp"
#include "pixatto/localization.hpp"
#include "pixatto/model.hpp"
#include "pixatto/model_renderer.hpp"
#include "pixatto/palette.hpp"
#include "pixatto/preset.hpp"
#include "pixatto/processing_edit_session.hpp"
#include "pixatto/video_backend.hpp"
#include "pixatto/video_playback_decoder.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixatto {

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
        unsigned int handle = 0;
        int width = 0;
        int height = 0;
    };

    enum class DocumentMode {
        Image,
        Model,
        Video,
    };

    enum class BatchExportFormat {
        Png,
        Raw,
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
    struct PresetSaveAsState {
        std::array<char, 128> name{};
        bool request_open = false;
    };
    struct PendingPresetOverwriteState {
        std::string name;
        ProcessSettings settings;
        bool request_open = false;
    };
    struct PendingPresetDeleteState {
        Preset preset;
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
    struct PendingModelLoadState {
        std::filesystem::path source;
        std::future<ModelLoadResult> result;
        std::chrono::steady_clock::time_point started_at;
        std::chrono::steady_clock::time_point last_log;
    };
    struct VideoPreviewResult {
        Image decoded;
        Image processed;
        std::string error;
    };
    struct PendingVideoPreviewState {
        double time_seconds = 0.0;
        double decode_time_seconds = 0.0;
        std::uint64_t generation = 0;
        std::future<VideoPreviewResult> result;
    };
    struct PendingVideoHardwareProbeState {
        std::future<std::vector<std::string>> result;
    };
    struct VideoDocumentState {
        std::filesystem::path source;
        VideoToolchain tools;
        VideoMetadata metadata;
        VideoCapabilities capabilities;
        std::vector<VideoExportProfile> profiles;
        double current_time = 0.0;
        bool playing = false;
        bool hardware_encoders_probed = false;
        bool playback_decoder_available = false;
        bool playback_seek_pending = false;
        bool playback_warning_reported = false;
        std::chrono::steady_clock::time_point last_tick;
        std::uint64_t preview_generation = 0;
    };
    struct VideoExportDialogState {
        int selected_profile = 0;
        VideoContainer container = VideoContainer::Mp4;
        VideoAudioMode audio_mode = VideoAudioMode::None;
        int crf = 18;
        int qp = 16;
        VideoHardwareSpeed hardware_speed = VideoHardwareSpeed::Balanced;
        bool gpu_processing = false;
        bool request_open = false;
    };
    struct VideoExportGpuRequest;
    struct VideoExportGpuQueue;
    struct PendingVideoExportState {
        VideoExportSettings settings;
        std::shared_ptr<VideoExportProgress> progress;
        std::future<VideoExportResult> result;
        std::chrono::steady_clock::time_point started_at;
        std::shared_ptr<VideoExportGpuQueue> gpu_queue;
        std::uint64_t displayed_preview_generation = 0;
    };
    struct VideoExportGpuRequest {
        const Image* source = nullptr;
        ProcessSettings settings;
        Image result;
        std::string error;
        std::mutex mutex;
        std::condition_variable cv;
        VideoGpuProcessResult status = VideoGpuProcessResult::Fallback;
        bool finished = false;
    };
    struct VideoExportGpuQueue {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::shared_ptr<VideoExportGpuRequest>> requests;
        std::string close_error;
        VideoGpuProcessResult closed_status = VideoGpuProcessResult::Fallback;
        bool closed = false;
    };
    struct ModelMaterialSlot {
        int mesh_index = -1;
        int material_index = -1;
        int texture_index = -1;
        std::size_t primitive_count = 0;
        std::string mesh_name;
        std::string material_name;
    };
    struct BatchState {
        std::vector<std::filesystem::path> images;
        std::filesystem::path output_dir;
        std::array<char, 128> suffix{};
        int selected_preset = -1;
        BatchExportFormat format = BatchExportFormat::Png;
        bool request_open = false;
        bool processing = false;
        bool cancel_requested = false;
        std::size_t processed = 0;
        std::size_t succeeded = 0;
        std::size_t failed = 0;
        std::string last_error;
    };

    void shutdown();
    void process_events(bool& running);
    void render_frame();
    void render_menu_bar();
    void render_language_picker_popup();
    void render_help_dialog();
    void render_about_dialog();
    void render_preset_picker(ProcessSettings& settings, int& selected_palette);
    void render_controls();
    void render_model_materials();
    void render_model_texture_drawer();
    void render_viewports();
    void render_original_view();
    void render_working_view();
    void render_image_view(TextId label, const char* id, Texture& texture, float& zoom, bool show_close_file);
    void render_close_file_button();
    void render_model_texture_gallery();
    void render_model_view();
    void render_video_view();
    void render_number_edit_popup();
    void render_drop_confirm_popup();
    void render_delete_palette_popup();
    void render_palette_import_conflict_popup();
    void render_palette_import_name_popup();
    void render_palette_color_popup();
    void render_save_palette_popup();
    void render_save_preset_popup();
    void render_preset_overwrite_popup();
    void render_delete_preset_popup();
    void render_batch_dialog();
    void render_video_export_dialog();
    void handle_shortcuts();
    void update_batch_processing();
    void update_video_playback();
    void update_pending_video_hardware_probe();
    void update_pending_video_preview();
    void update_pending_video_export();
    void update_video_export_preview();
    void service_video_export_gpu_queue();
    void close_video_export_gpu_queue(
        const std::shared_ptr<VideoExportGpuQueue>& queue,
        VideoGpuProcessResult status,
        std::string error);
    void set_video_export_fast_swap(bool enabled);
    void update_preview_if_needed();
    [[nodiscard]] bool decode_video_preview_with_playback_decoder(double requested_time, double decode_time);
    void rebuild_texture(Texture& texture, const Image& image, bool nearest);
    void configure_fonts();
    void destroy_texture(Texture& texture);
    void request_open_image();
    void request_open_model();
    void request_open_video();
    void request_import_palette();
    void request_export_png();
    void request_export_raw();
    void request_export_video();
    void request_batch();
    void request_batch_images();
    void request_batch_output_folder();
    void request_next_model_texture_export();
    void drain_file_commands();
    void handle_file_command(const FileCommand& command);
    void update_pending_model_load();
    void load_document_from_path(const std::filesystem::path& path);
    void load_image_from_path(const std::filesystem::path& path);
    void load_model_from_path(const std::filesystem::path& path);
    void load_video_from_path(const std::filesystem::path& path);
    void finish_model_load_from_path(const std::filesystem::path& path, ModelLoadResult loaded);
    void request_video_preview(double time_seconds, bool force);
    void start_video_export_to_path(const std::filesystem::path& path);
    void close_current_file();
    void import_model_texture_from_path(const std::filesystem::path& path);
    void assign_model_texture_to_slot(const ModelMaterialSlot& slot, std::size_t texture_index);
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
    void request_save_preset();
    bool save_preset_as_name(const std::string& name);
    bool overwrite_pending_preset();
    void request_delete_selected_preset();
    void delete_pending_preset();
    void apply_preset_settings(const Preset& preset, ProcessSettings& settings, int& selected_palette);
    void export_result_to_png_path(const std::filesystem::path& path);
    void export_model_texture_to_png_path(std::size_t texture_index, const std::filesystem::path& path);
    void export_result_to_raw_path(const std::filesystem::path& path);
    void add_batch_images(const std::vector<std::filesystem::path>& paths);
    void start_batch_processing();
    void process_next_batch_image();
    void clear_model_document();
    void clear_video_document();
    void reset_model_camera();
    void reset_model_camera_to_origin();
    void pan_model_camera(float delta_x, float delta_y);
    [[nodiscard]] std::vector<ModelMaterialSlot> model_material_slots() const;
    [[nodiscard]] std::string model_texture_display_name(std::size_t texture_index) const;
    void refresh_palettes();
    void refresh_presets();
    [[nodiscard]] int matching_preset_index(const ProcessSettings& settings) const;
    [[nodiscard]] int matching_palette_index(const ProcessSettings& settings) const;
    [[nodiscard]] int effective_selected_preset_index(const ProcessSettings& settings) const;
    [[nodiscard]] bool has_current_file() const noexcept;
    bool select_preset_by_path(const std::filesystem::path& path);
    void mark_dirty();
    void set_status(std::string message);
    [[nodiscard]] bool ensure_gl_context_current();
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
    SDL_GLContext gl_context_ = nullptr;

    DocumentMode document_mode_ = DocumentMode::Image;
    Image original_;
    Image result_;
    Image video_decoded_frame_;
    double video_decoded_frame_time_ = 0.0;
    bool video_decoded_frame_valid_ = false;
    Texture original_texture_;
    Texture result_texture_;
    ModelDocument model_;
    std::vector<Image> model_processed_textures_;
    std::vector<Texture> model_original_textures_;
    std::vector<Texture> model_result_textures_;
    ModelRenderer model_renderer_;
    std::unique_ptr<GpuImageProcessor> video_export_gpu_processor_;
    bool video_export_fast_swap_ = false;
    VideoDocumentState video_;
    VideoPlaybackDecoder video_playback_decoder_;

    ProcessingEditSession edit_session_;
    std::vector<Palette> palettes_;
    std::vector<Preset> presets_;
    int selected_preset_ = -1;

    float original_zoom_ = 1.0F;
    float result_zoom_ = 1.0F;
    float model_yaw_ = 0.65F;
    float model_pitch_ = 0.35F;
    float model_distance_ = 2.8F;
    float model_target_offset_x_ = 0.0F;
    float model_target_offset_y_ = 0.0F;
    float model_target_offset_z_ = 0.0F;
    int model_preview_log_frames_ = 0;
    float viewport_split_ratio_ = 0.5F;
    ViewportMode viewport_mode_ = ViewportMode::Single;
    ViewportLayout viewport_layout_ = ViewportLayout::SideBySide;
    Language language_ = Language::English;
    bool open_language_picker_ = false;
    bool open_help_dialog_ = false;
    bool open_about_dialog_ = false;
    bool running_ = false;
    std::string status_;
    std::filesystem::path current_image_path_;
    std::optional<std::filesystem::path> last_export_path_;
    std::optional<std::filesystem::path> pending_dropped_image_;
    std::optional<Palette> pending_delete_palette_;
    std::optional<PendingPaletteImportState> pending_palette_import_;
    std::optional<PendingModelLoadState> pending_model_load_;
    std::optional<PendingVideoPreviewState> pending_video_preview_;
    std::optional<PendingVideoHardwareProbeState> pending_video_hardware_probe_;
    std::optional<VideoExportDialogState> video_export_dialog_;
    std::optional<PendingVideoExportState> pending_video_export_;
    std::deque<std::size_t> pending_model_texture_exports_;
    bool open_drop_confirm_ = false;
    bool open_delete_palette_confirm_ = false;
    FileCommandPump file_commands_;
    std::optional<NumberEditState> number_edit_;
    std::optional<PaletteColorEditState> palette_color_edit_;
    std::optional<PaletteSaveAsState> palette_save_as_;
    std::optional<PresetSaveAsState> preset_save_as_;
    std::optional<PendingPresetOverwriteState> pending_preset_overwrite_;
    std::optional<PendingPresetDeleteState> pending_delete_preset_;
    std::optional<BatchState> batch_;
};

} // namespace pixatto
