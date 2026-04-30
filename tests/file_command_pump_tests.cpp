#include "pixelizer/file_command_pump.hpp"

#include <stdexcept>

namespace {

void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("file command pump test failed");
    }
}

void dropped_palette_imports_immediately()
{
    pixelizer::FileCommandPump pump;

    pump.submit_drop("palettes/example.HEX", true);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixelizer::FileCommandKind::ImportPalette);
    require(commands[0].path == "palettes/example.HEX");
}

void dropped_image_opens_when_no_image_is_loaded()
{
    pixelizer::FileCommandPump pump;

    pump.submit_drop("images/example.png", false);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixelizer::FileCommandKind::OpenImage);
    require(commands[0].path == "images/example.png");
}

void dropped_image_requires_confirmation_when_image_is_loaded()
{
    pixelizer::FileCommandPump pump;

    pump.submit_drop("images/replacement.png", true);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixelizer::FileCommandKind::ConfirmOpenImage);
    require(commands[0].path == "images/replacement.png");
}

void empty_drop_is_ignored()
{
    pixelizer::FileCommandPump pump;

    pump.submit_drop({}, true);
    const auto commands = pump.drain_commands();

    require(commands.empty());
}

void drain_clears_queued_commands()
{
    pixelizer::FileCommandPump pump;

    pump.submit_drop("images/example.png", false);
    require(pump.drain_commands().size() == 1U);
    require(pump.drain_commands().empty());
}

} // namespace

int main()
{
    dropped_palette_imports_immediately();
    dropped_image_opens_when_no_image_is_loaded();
    dropped_image_requires_confirmation_when_image_is_loaded();
    empty_drop_is_ignored();
    drain_clears_queued_commands();
    return 0;
}
