#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace pixelizer {

namespace detail {
struct FileCommandPumpDialogState;
} // namespace detail

enum class FileCommandKind {
    OpenImage,
    OpenModel,
    ImportPalette,
    ExportPng,
    ExportModelTexturePng,
    ExportRaw,
    BatchImages,
    BatchOutputFolder,
    ConfirmOpenImage,
    DialogFailed,
    DialogCanceled,
};

struct FileCommand {
    FileCommandKind kind = FileCommandKind::OpenImage;
    std::filesystem::path path;
    std::vector<std::filesystem::path> paths;
    std::string error;
    int index = -1;
};

struct FileDialogLabels {
    std::string images_filter;
    std::string models_filter;
    std::string all_files_filter;
    std::string lospec_palettes_filter;
    std::string png_image_filter;
    std::string raw_image_filter;
};

class FileCommandPump {
public:
    FileCommandPump();
    ~FileCommandPump();

    FileCommandPump(const FileCommandPump&) = delete;
    FileCommandPump& operator=(const FileCommandPump&) = delete;

    [[nodiscard]] bool dialog_open() const noexcept;
    [[nodiscard]] bool request_open_image_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_open_model_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_import_palette_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_export_png_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_export_model_texture_png_dialog(
        SDL_Window* window,
        const FileDialogLabels& labels,
        const std::filesystem::path& default_path,
        int texture_index);
    [[nodiscard]] bool request_export_raw_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_batch_images_dialog(SDL_Window* window, const FileDialogLabels& labels);
    [[nodiscard]] bool request_batch_output_folder_dialog(SDL_Window* window, const std::filesystem::path& default_path);

    void submit_drop(std::filesystem::path path, bool has_open_document);
    [[nodiscard]] std::vector<FileCommand> drain_commands();

private:
    [[nodiscard]] bool request_dialog(
        SDL_Window* window,
        FileCommandKind command,
        std::vector<std::string> filter_names,
        std::vector<std::string> filter_patterns,
        bool save_dialog,
        bool folder_dialog = false,
        bool allow_many = false,
        std::filesystem::path default_path = {},
        int index = -1);

    std::shared_ptr<detail::FileCommandPumpDialogState> dialog_state_;
    bool dialog_open_ = false;
    std::vector<FileCommand> queued_commands_;
};

} // namespace pixelizer
