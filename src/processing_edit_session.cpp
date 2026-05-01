#include "pixatto/processing_edit_session.hpp"

#include <algorithm>
#include <utility>

namespace pixatto {
namespace {

constexpr std::size_t kMaxHistoryEntries = 100;

} // namespace

ProcessingEditSession::EditScope::EditScope(ProcessingEditSession& session, Snapshot before)
    : session_(&session)
    , before_(std::move(before))
{
}

ProcessSettings& ProcessingEditSession::EditScope::settings() noexcept
{
    return session_->settings_for_edit();
}

int& ProcessingEditSession::EditScope::selected_palette() noexcept
{
    return session_->selected_palette_for_edit();
}

const ProcessingEditSession::Snapshot& ProcessingEditSession::EditScope::before() const noexcept
{
    return before_;
}

bool ProcessingEditSession::EditScope::changed() const
{
    return !(before_ == session_->capture_snapshot());
}

void ProcessingEditSession::EditScope::commit()
{
    session_->commit_edit(before_);
}

ProcessingEditSession::EditScope ProcessingEditSession::begin_edit()
{
    return {*this, capture_snapshot()};
}

const ProcessSettings& ProcessingEditSession::settings() const noexcept
{
    return settings_;
}

ProcessSettings& ProcessingEditSession::settings_for_edit() noexcept
{
    return settings_;
}

int ProcessingEditSession::selected_palette() const noexcept
{
    return selected_palette_;
}

int& ProcessingEditSession::selected_palette_for_edit() noexcept
{
    return selected_palette_;
}

ProcessingEditSession::Snapshot ProcessingEditSession::capture_snapshot() const
{
    return {settings_, selected_palette_};
}

void ProcessingEditSession::finish_live_edit(const Snapshot& before, bool editing)
{
    normalize();
    const bool changed = !(before == capture_snapshot());

    if (editing) {
        if (changed) {
            mark_dirty();
            if (!active_edit_snapshot_) {
                active_edit_snapshot_ = before;
            }
        }
        return;
    }

    if (active_edit_snapshot_) {
        commit_edit(*active_edit_snapshot_);
        active_edit_snapshot_.reset();
        return;
    }

    if (changed) {
        commit_edit(before);
    }
}

void ProcessingEditSession::finish_live_edit(const EditScope& scope, bool editing)
{
    finish_live_edit(scope.before(), editing);
}

void ProcessingEditSession::commit_edit(const Snapshot& before)
{
    normalize();
    if (before == capture_snapshot()) {
        return;
    }

    undo_stack_.push_back(before);
    if (undo_stack_.size() > kMaxHistoryEntries) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
    mark_dirty();
}

void ProcessingEditSession::cancel_live_edit() noexcept
{
    active_edit_snapshot_.reset();
}

void ProcessingEditSession::normalize()
{
    settings_.pixel_size = std::clamp(settings_.pixel_size, 1, 128);
    settings_.color_levels = std::clamp(settings_.color_levels, 2, 64);
    settings_.reduction_max_colors = std::clamp(settings_.reduction_max_colors, 0, 256);
    if (settings_.bayer_matrix_size <= 2) {
        settings_.bayer_matrix_size = 2;
    } else if (settings_.bayer_matrix_size <= 4) {
        settings_.bayer_matrix_size = 4;
    } else if (settings_.bayer_matrix_size <= 8) {
        settings_.bayer_matrix_size = 8;
    } else {
        settings_.bayer_matrix_size = 16;
    }
    settings_.dither_amount = std::clamp(settings_.dither_amount, 0.0F, 1.0F);

    settings_.adjustments.brightness = std::clamp(settings_.adjustments.brightness, -1.0F, 1.0F);
    settings_.adjustments.contrast = std::clamp(settings_.adjustments.contrast, -1.0F, 1.0F);
    settings_.adjustments.gamma = std::clamp(settings_.adjustments.gamma, 0.1F, 4.0F);
    settings_.adjustments.saturation = std::clamp(settings_.adjustments.saturation, 0.0F, 2.5F);
    settings_.adjustments.tint_strength = std::clamp(settings_.adjustments.tint_strength, 0.0F, 1.0F);

    settings_.adjustments.input_black = std::clamp(settings_.adjustments.input_black, 0.0F, 0.95F);
    settings_.adjustments.input_white = std::clamp(settings_.adjustments.input_white, 0.05F, 1.0F);
    if (settings_.adjustments.input_black >= settings_.adjustments.input_white) {
        settings_.adjustments.input_black = std::max(0.0F, settings_.adjustments.input_white - 0.01F);
    }

    settings_.adjustments.output_black = std::clamp(settings_.adjustments.output_black, 0.0F, 0.95F);
    settings_.adjustments.output_white = std::clamp(settings_.adjustments.output_white, 0.05F, 1.0F);
    if (settings_.adjustments.output_black >= settings_.adjustments.output_white) {
        settings_.adjustments.output_black = std::max(0.0F, settings_.adjustments.output_white - 0.01F);
    }
}

void ProcessingEditSession::mark_dirty() noexcept
{
    preview_dirty_ = true;
}

bool ProcessingEditSession::preview_dirty() const noexcept
{
    return preview_dirty_;
}

void ProcessingEditSession::clear_preview_dirty() noexcept
{
    preview_dirty_ = false;
}

bool ProcessingEditSession::undo(std::size_t saved_palette_count)
{
    if (!can_undo()) {
        return false;
    }

    redo_stack_.push_back(capture_snapshot());
    const Snapshot snapshot = undo_stack_.back();
    undo_stack_.pop_back();
    apply_snapshot(snapshot, saved_palette_count);
    return true;
}

bool ProcessingEditSession::redo(std::size_t saved_palette_count)
{
    if (!can_redo()) {
        return false;
    }

    undo_stack_.push_back(capture_snapshot());
    const Snapshot snapshot = redo_stack_.back();
    redo_stack_.pop_back();
    apply_snapshot(snapshot, saved_palette_count);
    return true;
}

bool ProcessingEditSession::can_undo() const noexcept
{
    return !undo_stack_.empty();
}

bool ProcessingEditSession::can_redo() const noexcept
{
    return !redo_stack_.empty();
}

void ProcessingEditSession::reconcile_selected_palette(std::size_t saved_palette_count) noexcept
{
    if (selected_palette_ < 0 || selected_palette_ >= static_cast<int>(saved_palette_count)) {
        selected_palette_ = -1;
    }
}

void ProcessingEditSession::apply_snapshot(const Snapshot& snapshot, std::size_t saved_palette_count)
{
    settings_ = snapshot.settings;
    selected_palette_ = snapshot.selected_palette;
    reconcile_selected_palette(saved_palette_count);
    active_edit_snapshot_.reset();
    mark_dirty();
}

} // namespace pixatto
