#pragma once

#include "pixelizer/image_processing.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace pixelizer {

class ProcessingEditSession {
public:
    struct Snapshot {
        ProcessSettings settings;
        int selected_palette = -1;

        bool operator==(const Snapshot&) const = default;
    };

    class EditScope {
    public:
        [[nodiscard]] ProcessSettings& settings() noexcept;
        [[nodiscard]] int& selected_palette() noexcept;
        [[nodiscard]] const Snapshot& before() const noexcept;
        [[nodiscard]] bool changed() const;
        void commit();

    private:
        friend class ProcessingEditSession;

        EditScope(ProcessingEditSession& session, Snapshot before);

        ProcessingEditSession* session_ = nullptr;
        Snapshot before_;
    };

    [[nodiscard]] EditScope begin_edit();
    [[nodiscard]] const ProcessSettings& settings() const noexcept;
    [[nodiscard]] ProcessSettings& settings_for_edit() noexcept;
    [[nodiscard]] int selected_palette() const noexcept;
    [[nodiscard]] int& selected_palette_for_edit() noexcept;
    [[nodiscard]] Snapshot capture_snapshot() const;

    void finish_live_edit(const Snapshot& before, bool editing);
    void finish_live_edit(const EditScope& scope, bool editing);
    void commit_edit(const Snapshot& before);
    void cancel_live_edit() noexcept;

    void normalize();
    void mark_dirty() noexcept;
    [[nodiscard]] bool preview_dirty() const noexcept;
    void clear_preview_dirty() noexcept;

    [[nodiscard]] bool undo(std::size_t saved_palette_count);
    [[nodiscard]] bool redo(std::size_t saved_palette_count);
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    void reconcile_selected_palette(std::size_t saved_palette_count) noexcept;

private:
    void apply_snapshot(const Snapshot& snapshot, std::size_t saved_palette_count);

    ProcessSettings settings_;
    int selected_palette_ = -1;
    bool preview_dirty_ = false;
    std::optional<Snapshot> active_edit_snapshot_;
    std::vector<Snapshot> undo_stack_;
    std::vector<Snapshot> redo_stack_;
};

} // namespace pixelizer
