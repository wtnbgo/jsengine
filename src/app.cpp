#include "app.hpp"
#include "jsengine.hpp"
#include "webaudio.h"
#include "glad/gles2.h"

// 静的メンバ初期化
App* App::instance_ = nullptr;

App::App() : window_(nullptr), context_(nullptr), result_(SDL_APP_CONTINUE)
{
    instance_ = this;
}

bool App::init(int argc, char *argv[]) 
{
    int flags = SDL_WINDOW_OPENGL;
#if __IPHONEOS__ || __ANDROID__ || __WINRT__
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    flags |= SDL_WINDOW_RESIZABLE;
#endif

    // これを指定すると egl ベースでの dll 読み込み処理になる（Angleなどが使える）
    // 指定が無い場合は wgl ベースでの GLES のロードを試みて駄目な場合はDLLを読む
    //SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // ウィンドウ生成
    window_ = SDL_CreateWindow("JsEngine", 1280, 720, flags);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window: %s", SDL_GetError());
        return false;
    }

   if ((context_ = SDL_GL_CreateContext(window_)) == NULL) {
      SDL_Log("Unable to create GL context: %s", SDL_GetError());
      return false;
    }
    SDL_GL_MakeCurrent(window_, context_);

    // load gles2
    int gles_version = gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress);
    if (!gles_version) {
        SDL_Log("Unable to load GLES.\n");
        return false;
    }
    SDL_Log("Loaded GLES %d.%d.\n",
           GLAD_VERSION_MAJOR(gles_version), GLAD_VERSION_MINOR(gles_version));

    SDL_GL_SetSwapInterval(1); // 1: VSYNC

    // データパス決定（デフォルト: data、-data オプションで変更可）
    const char *dataPath = "data";
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "-data") == 0 && i + 1 < argc) {
            dataPath = argv[i + 1];
            break;
        }
    }

    // オーディオシステム初期化
    webaudio_init();

    // JsEngine 初期化
    jsEngine_ = std::make_unique<JsEngine>();
    jsEngine_->setBasePath(dataPath);
    SDL_Log("Data path: %s", jsEngine_->getBasePath().c_str());
    if (!jsEngine_->init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize JsEngine");
        return false;
    }

    // main.js を読み込み実行
    if (!jsEngine_->loadFile("main.js")) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load main.js");
        return false;
    }

    return true;
}

App::~App()
{
    // JsEngine 終了
    if (jsEngine_) {
        jsEngine_.reset();
    }

    // オーディオシステム終了
    webaudio_uninit();

    if (context_) {
        SDL_GL_DestroyContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }    
    instance_ = nullptr;
    SDL_Log("App cleaned up");
}


SDL_AppResult App::update(uint32_t delta)
{
    if (jsEngine_) {
        jsEngine_->update(delta);
    }
    webaudio_gc();
    return result_;
}

void App::render()
{
    if (!window_ || !context_) return;

    // JS の render() 関数を呼び出す
    if (jsEngine_) {
        jsEngine_->render();
    }

    SDL_GL_SwapWindow(window_);
}

void App::handleEvent(const SDL_Event *event)
{
    if (jsEngine_) {
        jsEngine_->handleEvent(event);
    }
}
