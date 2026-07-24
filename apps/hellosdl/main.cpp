#include <SDL.h>

// A handful of distinguishable colors, cycled through by touch slot index
// so simultaneous fingers are easy to tell apart on screen.
struct Color { Uint8 r, g, b; };
static const Color kTouchColors[] = {
    { 255,  90,  90 },  // red
    {  90, 255, 120 },  // green
    { 255, 210,  60 },  // yellow
    { 180, 120, 255 },  // purple
    {  60, 220, 220 },  // cyan
};
static const int kTouchColorCount = sizeof(kTouchColors) / sizeof(kTouchColors[0]);

struct TouchPoint {
    SDL_FingerID id = 0;
    float x = 0, y = 0;
    bool active = false;
};
static const int kMaxTouches = 10;
static TouchPoint touches[kMaxTouches];

static TouchPoint *findTouch(SDL_FingerID id) {
    for (auto &t : touches) {
        if (t.active && t.id == id) return &t;
    }
    return nullptr;
}

static TouchPoint *allocTouch() {
    for (auto &t : touches) {
        if (!t.active) return &t;
    }
    return nullptr; // all slots in use; extra fingers are just ignored
}

int main(int argc, char *argv[]) {
    SDL_Log("Hello, World! from SDL2 + C++ on Android (SDL_Renderer)");

    // Keep touch and mouse input independent: without this, SDL synthesizes
    // a mouse event for every touch (and vice versa), which would make the
    // mouse-cursor square and the finger squares overlap/fight each other.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

#ifdef __ANDROID__
    const int kWinW = 0, kWinH = 0; // fullscreen sizes itself
    const Uint32 kWinFlags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN;
#else
    const int kWinW = 800, kWinH = 600;
    const Uint32 kWinFlags = SDL_WINDOW_SHOWN;
#endif

    SDL_Window *window = SDL_CreateWindow(
        "HelloSDL",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        kWinW, kWinH,
        kWinFlags);
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

    bool haveMouse = false;
    float mouseX = 0, mouseY = 0;
    const int kMouseSize = 28;
    const int kTouchSize = 60; // bigger than the mouse square: easier to hit with a finger

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
                case SDL_MOUSEMOTION:
                    mouseX = (float)event.motion.x;
                    mouseY = (float)event.motion.y;
                    haveMouse = true;
                    break;
                case SDL_FINGERDOWN: {
                    TouchPoint *t = findTouch(event.tfinger.fingerId);
                    if (!t) t = allocTouch();
                    if (t) {
                        t->id = event.tfinger.fingerId;
                        t->x = event.tfinger.x * (float)winW;
                        t->y = event.tfinger.y * (float)winH;
                        t->active = true;
                    }
                    SDL_Log("Finger down id=%lld at (%.2f, %.2f)",
                            (long long)event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
                    break;
                }
                case SDL_FINGERMOTION: {
                    TouchPoint *t = findTouch(event.tfinger.fingerId);
                    if (t) {
                        t->x = event.tfinger.x * (float)winW;
                        t->y = event.tfinger.y * (float)winH;
                    }
                    break;
                }
                case SDL_FINGERUP: {
                    TouchPoint *t = findTouch(event.tfinger.fingerId);
                    if (t) t->active = false;
                    SDL_Log("Finger up id=%lld", (long long)event.tfinger.fingerId);
                    break;
                }
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

        if (haveMouse) {
            SDL_Rect cursor{
                (int)mouseX - kMouseSize / 2,
                (int)mouseY - kMouseSize / 2,
                kMouseSize, kMouseSize
            };
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(renderer, &cursor);
        }

        for (int i = 0; i < kMaxTouches; ++i) {
            if (!touches[i].active) continue;
            SDL_Rect touchRect{
                (int)touches[i].x - kTouchSize / 2,
                (int)touches[i].y - kTouchSize / 2,
                kTouchSize, kTouchSize
            };
            const Color &c = kTouchColors[i % kTouchColorCount];
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
            SDL_RenderFillRect(renderer, &touchRect);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
