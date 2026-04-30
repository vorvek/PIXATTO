#include "pixelizer/processing_edit_session.hpp"

#include <cmath>
#include <stdexcept>

namespace {

bool near(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) < 0.0001F;
}

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("processing edit session test failed");
    }
}

void continuous_edit_commits_one_undo_step()
{
    pixelizer::ProcessingEditSession session;

    auto drag = session.begin_edit();
    drag.settings().pixel_size = 12;
    session.finish_live_edit(drag, true);
    require(!session.can_undo());
    require(session.preview_dirty());

    session.clear_preview_dirty();
    auto release = session.begin_edit();
    session.finish_live_edit(release, false);
    require(session.can_undo());
    require(session.preview_dirty());

    require(session.undo(0));
    require(session.settings().pixel_size == 8);
}

void repeated_live_edits_keep_preview_dirty()
{
    pixelizer::ProcessingEditSession session;

    auto first = session.begin_edit();
    first.settings().pixel_size = 12;
    session.finish_live_edit(first, true);
    require(session.preview_dirty());

    session.clear_preview_dirty();
    auto second = session.begin_edit();
    second.settings().pixel_size = 16;
    session.finish_live_edit(second, true);
    require(session.preview_dirty());
    require(!session.can_undo());

    auto release = session.begin_edit();
    session.finish_live_edit(release, false);
    require(session.can_undo());
}

void atomic_edit_normalizes_levels()
{
    pixelizer::ProcessingEditSession session;

    auto edit = session.begin_edit();
    edit.settings().adjustments.input_white = 0.4F;
    edit.settings().adjustments.input_black = 0.9F;
    edit.commit();

    require(near(session.settings().adjustments.input_white, 0.4F));
    require(near(session.settings().adjustments.input_black, 0.39F));
    require(session.can_undo());
}

void bayer_sixteen_survives_normalization()
{
    pixelizer::ProcessingEditSession session;

    auto edit = session.begin_edit();
    edit.settings().bayer_matrix_size = 16;
    edit.commit();

    require(session.settings().bayer_matrix_size == 16);
}

void history_preserves_full_palette_colors()
{
    pixelizer::ProcessingEditSession session;

    auto first = session.begin_edit();
    first.settings().use_palette = true;
    first.settings().palette = {pixelizer::Color32{255, 0, 0, 255}};
    first.selected_palette() = 0;
    first.commit();

    auto second = session.begin_edit();
    second.settings().palette = {pixelizer::Color32{0, 0, 255, 255}};
    second.commit();

    require(session.undo(1));
    require(session.settings().palette.size() == 1U);
    const pixelizer::Color32 expected = {255, 0, 0, 255};
    require(session.settings().palette[0] == expected);
    require(session.selected_palette() == 0);
}

} // namespace

int main()
{
    continuous_edit_commits_one_undo_step();
    repeated_live_edits_keep_preview_dirty();
    atomic_edit_normalizes_levels();
    bayer_sixteen_survives_normalization();
    history_preserves_full_palette_colors();
    return 0;
}
