
#include <SDL3/SDL.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "app.hpp"

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) 
{
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS; // アプリを正常終了    
    case SDL_EVENT_TERMINATING:
        break;
    }
	return SDL_APP_CONTINUE; // イベントを無視
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
	}

    // Parse command line arguments for log level
    SDL_LogPriority logLevel = SDL_LOG_PRIORITY_INFO;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "-debug") == 0) {
            logLevel = SDL_LOG_PRIORITY_DEBUG;
            break;
        }
        else if (SDL_strcmp(argv[i], "-quiet") == 0) {
            logLevel = SDL_LOG_PRIORITY_WARN;
            break;
        }
    }
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, logLevel);

    // アプリ初期化
    App *app = new App();
    if (!app->init(argc, argv)) {
        delete app;
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    *appstate = app;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) 
{
    delete static_cast<App*>(appstate);
}

SDL_AppResult SDL_AppIterate(void *appstate) 
{
    App *app = static_cast<App*>(appstate);

    static Uint64 lastTick = SDL_GetTicks();
    Uint64 currentTick = SDL_GetTicks();
    long delta = static_cast<long>(currentTick - lastTick);
    if (delta > 0) {
        auto result = app->update(delta);
        lastTick += delta;
    }
    app->draw();
    return app->getResult();
}
