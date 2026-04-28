#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "pixelizer/app.hpp"

int main(int, char**)
{
    pixelizer::App app;
    if (!app.initialize()) {
        return 1;
    }

    return app.run();
}

