#include "pixatto/file_command_pump.hpp"

#include "pixatto/image_formats.hpp"
#include "pixatto/video_backend.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <utility>

namespace pixatto {
namespace {

bool has_extension(const std::filesystem::path& path, const char* extension)
{
    std::string actual = path.extension().string();
    std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return actual == extension;
}

bool is_model_path(const std::filesystem::path& path)
{
    return has_extension(path, ".glb") || has_extension(path, ".gltf") || has_extension(path, ".obj")
        || has_extension(path, ".fbx") || has_extension(path, ".dae");
}

struct PendingDialog {
    FileCommandKind kind = FileCommandKind::OpenImage;
    std::filesystem::path path;
    std::vector<std::filesystem::path> paths;
    bool failed = false;
    bool canceled = false;
    std::string error;
    int index = -1;
};

} // namespace

namespace detail {

struct FileCommandPumpDialogState {
    std::mutex mutex;
    std::vector<PendingDialog> pending_dialogs;
};

} // namespace detail

namespace {

struct DialogPayload {
    std::shared_ptr<detail::FileCommandPumpDialogState> state;
    FileCommandKind kind = FileCommandKind::OpenImage;
    std::vector<std::string> filter_names;
    std::vector<std::string> filter_patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::filesystem::path default_path;
    std::string default_location;
    bool folder_dialog = false;
    bool allow_many = false;
    int index = -1;
};

void SDLCALL dialog_callback(void* userdata, const char* const* filelist, int)
{
    std::unique_ptr<DialogPayload> payload(static_cast<DialogPayload*>(userdata));
    if (!payload || !payload->state) {
        return;
    }

    PendingDialog dialog;
    dialog.kind = payload->kind;
    dialog.index = payload->index;
    if (!filelist) {
        dialog.failed = true;
        if (const char* error = SDL_GetError(); error && *error != '\0') {
            dialog.error = error;
        }
    } else if (filelist[0]) {
        for (const char* const* item = filelist; *item; ++item) {
            dialog.paths.emplace_back(*item);
        }
        dialog.path = dialog.paths.front();
    } else {
        dialog.canceled = true;
    }

    std::lock_guard<std::mutex> lock(payload->state->mutex);
    payload->state->pending_dialogs.push_back(std::move(dialog));
}

std::unique_ptr<DialogPayload> make_payload(
    std::shared_ptr<detail::FileCommandPumpDialogState> state,
    FileCommandKind command,
    std::vector<std::string> filter_names,
    std::vector<std::string> filter_patterns,
    std::filesystem::path default_path,
    bool folder_dialog,
    bool allow_many,
    int index)
{
    auto payload = std::make_unique<DialogPayload>();
    payload->state = std::move(state);
    payload->kind = command;
    payload->filter_names = std::move(filter_names);
    payload->filter_patterns = std::move(filter_patterns);
    payload->default_path = std::move(default_path);
    payload->default_location = payload->default_path.string();
    payload->folder_dialog = folder_dialog;
    payload->allow_many = allow_many;
    payload->index = index;
    payload->filters.reserve(payload->filter_names.size());

    for (std::size_t index = 0; index < payload->filter_names.size(); ++index) {
        payload->filters.push_back({
            payload->filter_names[index].c_str(),
            payload->filter_patterns[index].c_str(),
        });
    }

    return payload;
}

} // namespace

FileCommandPump::FileCommandPump()
    : dialog_state_(std::make_shared<detail::FileCommandPumpDialogState>())
{
}

FileCommandPump::~FileCommandPump() = default;

bool FileCommandPump::dialog_open() const noexcept
{
    return dialog_open_;
}

bool FileCommandPump::request_open_image_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::OpenImage,
        {labels.images_filter, labels.all_files_filter},
        {std::string(kImportableImageDialogPattern), "*"},
        false);
}

bool FileCommandPump::request_open_model_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::OpenModel,
        {labels.models_filter, labels.all_files_filter},
        {"glb;gltf;obj;fbx;dae", "*"},
        false);
}

bool FileCommandPump::request_open_video_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::OpenVideo,
        {labels.videos_filter, labels.all_files_filter},
        {std::string(kImportableVideoDialogPattern), "*"},
        false);
}

bool FileCommandPump::request_import_palette_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::ImportPalette,
        {labels.lospec_palettes_filter, labels.all_files_filter},
        {"hex", "*"},
        false);
}

bool FileCommandPump::request_export_png_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::ExportPng,
        {labels.png_image_filter},
        {"png"},
        true);
}

bool FileCommandPump::request_export_model_texture_png_dialog(
    SDL_Window* window,
    const FileDialogLabels& labels,
    const std::filesystem::path& default_path,
    int texture_index)
{
    return request_dialog(
        window,
        FileCommandKind::ExportModelTexturePng,
        {labels.png_image_filter},
        {"png"},
        true,
        false,
        false,
        default_path,
        texture_index);
}

bool FileCommandPump::request_export_raw_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::ExportRaw,
        {labels.raw_image_filter},
        {"raw"},
        true);
}

bool FileCommandPump::request_export_video_dialog(
    SDL_Window* window,
    std::string filter_name,
    std::string filter_pattern,
    const std::filesystem::path& default_path,
    int profile_index)
{
    return request_dialog(
        window,
        FileCommandKind::ExportVideo,
        {std::move(filter_name)},
        {std::move(filter_pattern)},
        true,
        false,
        false,
        default_path,
        profile_index);
}

bool FileCommandPump::request_batch_images_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::BatchImages,
        {labels.images_filter},
        {std::string(kImportableImageDialogPattern)},
        false,
        false,
        true);
}

bool FileCommandPump::request_batch_output_folder_dialog(SDL_Window* window, const std::filesystem::path& default_path)
{
    return request_dialog(
        window,
        FileCommandKind::BatchOutputFolder,
        {},
        {},
        false,
        true,
        false,
        default_path);
}

void FileCommandPump::submit_drop(std::filesystem::path path, bool has_open_document)
{
    if (path.empty()) {
        return;
    }

    if (has_extension(path, ".hex")) {
        queued_commands_.push_back({FileCommandKind::ImportPalette, std::move(path), {}, {}});
        return;
    }

    if (is_model_path(path)) {
        queued_commands_.push_back(
            {has_open_document ? FileCommandKind::ConfirmOpenImage : FileCommandKind::OpenModel, std::move(path), {}, {}});
        return;
    }

    if (is_importable_video_path(path)) {
        queued_commands_.push_back(
            {has_open_document ? FileCommandKind::ConfirmOpenImage : FileCommandKind::OpenVideo, std::move(path), {}, {}});
        return;
    }

    if (!is_importable_image_path(path)) {
        return;
    }

    if (has_open_document) {
        queued_commands_.push_back({FileCommandKind::ConfirmOpenImage, std::move(path), {}, {}});
        return;
    }

    queued_commands_.push_back({FileCommandKind::OpenImage, std::move(path), {}, {}});
}

std::vector<FileCommand> FileCommandPump::drain_commands()
{
    std::vector<PendingDialog> dialogs;
    if (dialog_state_) {
        std::lock_guard<std::mutex> lock(dialog_state_->mutex);
        dialogs.swap(dialog_state_->pending_dialogs);
    }

    for (PendingDialog& dialog : dialogs) {
        dialog_open_ = false;
        if (dialog.failed) {
            queued_commands_.push_back({FileCommandKind::DialogFailed, {}, {}, dialog.error, dialog.index});
            continue;
        }
        if (dialog.canceled) {
            queued_commands_.push_back({FileCommandKind::DialogCanceled, {}, {}, {}, dialog.index});
            continue;
        }
        if (dialog.path.empty()) {
            continue;
        }
        queued_commands_.push_back({dialog.kind, dialog.path, std::move(dialog.paths), {}, dialog.index});
    }

    std::vector<FileCommand> commands;
    commands.swap(queued_commands_);
    return commands;
}

bool FileCommandPump::request_dialog(
    SDL_Window* window,
    FileCommandKind command,
    std::vector<std::string> filter_names,
    std::vector<std::string> filter_patterns,
    bool save_dialog,
    bool folder_dialog,
    bool allow_many,
    std::filesystem::path default_path,
    int index)
{
    if (dialog_open_ || filter_names.size() != filter_patterns.size()) {
        return false;
    }

    dialog_open_ = true;
    auto payload = make_payload(
        dialog_state_,
        command,
        std::move(filter_names),
        std::move(filter_patterns),
        std::move(default_path),
        folder_dialog,
        allow_many,
        index);
    auto* raw_payload = payload.release();
    const char* default_location_ptr = raw_payload->default_location.empty() ? nullptr : raw_payload->default_location.c_str();
    if (folder_dialog) {
        SDL_ShowOpenFolderDialog(
            dialog_callback,
            raw_payload,
            window,
            default_location_ptr,
            raw_payload->allow_many);
    } else if (save_dialog) {
        SDL_ShowSaveFileDialog(
            dialog_callback,
            raw_payload,
            window,
            raw_payload->filters.data(),
            static_cast<int>(raw_payload->filters.size()),
            default_location_ptr);
    } else {
        SDL_ShowOpenFileDialog(
            dialog_callback,
            raw_payload,
            window,
            raw_payload->filters.data(),
            static_cast<int>(raw_payload->filters.size()),
            default_location_ptr,
            raw_payload->allow_many);
    }
    return true;
}

} // namespace pixatto
