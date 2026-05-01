#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "pixatto/app.hpp"

int main(int, char**)
{
    pixatto::App app;
    if (!app.initialize()) {
        return 1;
    }

    return app.run();
}

