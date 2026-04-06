#pragma once
#include <cstdint>

// forward declarations
struct duk_hthread;
typedef struct duk_hthread duk_context;
union SDL_Event;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    bool init();
    void update(uint32_t delta);
    void render();
    void done();

    // SDL3 イベントをブラウザ互換イベントとして JS に配信
    void handleEvent(const SDL_Event *event);

    // SDL3 のファイル関数経由で JS ファイルを読み込み実行
    bool loadFile(const char *path);

    duk_context* getContext() const { return ctx_; }

private:
    duk_context* ctx_;

    // イベントディスパッチヘルパー
    void dispatchEvent(const char *type);
    void pushKeyboardEvent(const SDL_Event *event, const char *type);
    void pushMouseEvent(const SDL_Event *event, const char *type);
    void pushWheelEvent(const SDL_Event *event);
    void pushTouchEvent(const SDL_Event *event, const char *type);
};
