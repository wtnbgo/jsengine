#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <memory>

class JsEngine;

class App {
public:
    App();
    ~App();

    bool init(int argc, char *argv[]);
    SDL_AppResult update(uint32_t delta);
    void render();
    void handleEvent(const SDL_Event *event);

    SDL_Window* getWindow() const { return window_; }

    void setResult(SDL_AppResult res) { result_ = res; }
    SDL_AppResult getResult() const { return result_; }

    static App* getInstance() { return instance_; }

private:
    SDL_Window* window_;
    SDL_GLContext context_;
    SDL_AppResult result_;
    std::unique_ptr<JsEngine> jsEngine_;

    static App* instance_;
};
