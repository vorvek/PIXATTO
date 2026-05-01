#include "pixatto/file_command_pump.hpp"

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
    pixatto::FileCommandPump pump;

    pump.submit_drop("palettes/example.HEX", true);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixatto::FileCommandKind::ImportPalette);
    require(commands[0].path == "palettes/example.HEX");
}

void dropped_image_opens_when_no_image_is_loaded()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop("images/example.WEBP", false);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixatto::FileCommandKind::OpenImage);
    require(commands[0].path == "images/example.WEBP");
}

void dropped_image_requires_confirmation_when_image_is_loaded()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop("images/replacement.png", true);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixatto::FileCommandKind::ConfirmOpenImage);
    require(commands[0].path == "images/replacement.png");
}

void dropped_model_opens_when_no_document_is_loaded()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop("models/hero.dae", false);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixatto::FileCommandKind::OpenModel);
    require(commands[0].path == "models/hero.dae");
}

void dropped_model_requires_confirmation_when_document_is_loaded()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop("models/hero.obj", true);
    const auto commands = pump.drain_commands();

    require(commands.size() == 1U);
    require(commands[0].kind == pixatto::FileCommandKind::ConfirmOpenImage);
    require(commands[0].path == "models/hero.obj");
}

void empty_drop_is_ignored()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop({}, true);
    const auto commands = pump.drain_commands();

    require(commands.empty());
}

void unsupported_drop_is_ignored()
{
    pixatto::FileCommandPump pump;

    pump.submit_drop("notes/readme.txt", false);
    const auto commands = pump.drain_commands();

    require(commands.empty());
}

void drain_clears_queued_commands()
{
    pixatto::FileCommandPump pump;

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
    dropped_model_opens_when_no_document_is_loaded();
    dropped_model_requires_confirmation_when_document_is_loaded();
    empty_drop_is_ignored();
    unsupported_drop_is_ignored();
    drain_clears_queued_commands();
    return 0;
}
