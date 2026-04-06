#pragma once
#include <cstdint>
#include <SDL3/SDL.h>
#include <memory>

class App {
public:
    App();
    ~App();
    
    bool init(int argc, char *argv[]);
    SDL_AppResult update(uint32_t delta);
    void draw();
    
    SDL_Window* getWindow() const { return window_; }

    void setResult(SDL_AppResult res) { result_ = res; }
    SDL_AppResult getResult() const { return result_; }

    static App* getInstance() { return instance_; }

private:
    SDL_Window* window_;
    SDL_GLContext context_;
    SDL_AppResult result_;

    static App* instance_;
};
