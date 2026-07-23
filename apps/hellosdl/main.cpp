#include <SDL.h>

int main(int argc, char *argv[]) {
    SDL_Log("Hello, World! from SDL2 + C++ on Android (SDL_Renderer)");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "HelloSDL",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        0, 0,
        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);

    SDL_Rect box{ winW / 2 - 50, winH / 2 - 50, 100, 100 };
    int vx = 4, vy = 4;

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode == SDL_SCANCODE_AC_BACK ||
                        event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        running = false;
                    }
                    break;
                case SDL_FINGERDOWN:
                    SDL_Log("Touch at (%.2f, %.2f)", event.tfinger.x, event.tfinger.y);
                    break;
                default:
                    break;
            }
        }

        box.x += vx;
        box.y += vy;
        if (box.x < 0 || box.x + box.w > winW) vx = -vx;
        if (box.y < 0 || box.y + box.h > winH) vy = -vy;

        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 80, 200, 255, 255);
        SDL_RenderFillRect(renderer, &box);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
