#include "pixelizer/file_command_pump.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <utility>

namespace pixelizer {
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
    return has_extension(path, ".glb") || has_extension(path, ".gltf") || has_extension(path, ".obj");
}

struct PendingDialog {
    FileCommandKind kind = FileCommandKind::OpenImage;
    std::filesystem::path path;
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
        dialog.path = filelist[0];
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
    int index)
{
    auto payload = std::make_unique<DialogPayload>();
    payload->state = std::move(state);
    payload->kind = command;
    payload->filter_names = std::move(filter_names);
    payload->filter_patterns = std::move(filter_patterns);
    payload->default_path = std::move(default_path);
    payload->default_location = payload->default_path.string();
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
        {"png;jpg;jpeg;bmp", "*"},
        false);
}

bool FileCommandPump::request_open_model_dialog(SDL_Window* window, const FileDialogLabels& labels)
{
    return request_dialog(
        window,
        FileCommandKind::OpenModel,
        {labels.models_filter, labels.all_files_filter},
        {"glb;gltf;obj", "*"},
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

void FileCommandPump::submit_drop(std::filesystem::path path, bool has_open_document)
{
    if (path.empty()) {
        return;
    }

    if (has_extension(path, ".hex")) {
        queued_commands_.push_back({FileCommandKind::ImportPalette, std::move(path), {}});
        return;
    }

    if (is_model_path(path)) {
        queued_commands_.push_back({has_open_document ? FileCommandKind::ConfirmOpenImage : FileCommandKind::OpenModel, std::move(path), {}});
        return;
    }

    if (has_open_document) {
        queued_commands_.push_back({FileCommandKind::ConfirmOpenImage, std::move(path), {}});
        return;
    }

    queued_commands_.push_back({FileCommandKind::OpenImage, std::move(path), {}});
}

std::vector<FileCommand> FileCommandPump::drain_commands()
{
    std::vector<PendingDialog> dialogs;
    if (dialog_state_) {
        std::lock_guard<std::mutex> lock(dialog_state_->mutex);
        dialogs.swap(dialog_state_->pending_dialogs);
    }

    for (const PendingDialog& dialog : dialogs) {
        dialog_open_ = false;
        if (dialog.failed) {
            queued_commands_.push_back({FileCommandKind::DialogFailed, {}, dialog.error, dialog.index});
            continue;
        }
        if (dialog.canceled) {
            queued_commands_.push_back({FileCommandKind::DialogCanceled, {}, {}, dialog.index});
            continue;
        }
        if (dialog.path.empty()) {
            continue;
        }
        queued_commands_.push_back({dialog.kind, dialog.path, {}, dialog.index});
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
        index);
    auto* raw_payload = payload.release();
    const char* default_location_ptr = raw_payload->default_location.empty() ? nullptr : raw_payload->default_location.c_str();
    if (save_dialog) {
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
            false);
    }
    return true;
}

} // namespace pixelizer
