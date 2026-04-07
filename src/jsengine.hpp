#pragma once
#include <quickjs.h>
#include <cstdint>
#include <string>

union SDL_Event;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    // ベースパスを設定（init 前に呼ぶ）
    void setBasePath(const char *path);
    const std::string& getBasePath() const { return basePath_; }

    // ベースパスからの相対パスをフルパスに解決
    std::string resolvePath(const char *path) const;

    bool init(int argc = 0, char **argv = nullptr);
    void update(uint32_t delta);
    void processTimers();       // setTimeout コールバック実行
    void processRAF();          // requestAnimationFrame コールバック実行
    void render();
    void done();

    // SDL3 イベントをブラウザ互換イベントとして JS に配信
    void handleEvent(const SDL_Event *event);

    // SDL3 のファイル関数経由で JS ファイルを読み込み実行
    bool loadFile(const char *path);

    JSContext* getContext() const { return ctx_; }

    static JsEngine* getInstance() { return instance_; }

private:
    JSRuntime* rt_;
    JSContext* ctx_;
    std::string basePath_;

    static JsEngine* instance_;

    // イベントディスパッチヘルパー
    void dispatchEvent(const char *type, JSValue event_obj);
    JSValue pushKeyboardEvent(const SDL_Event *event, const char *type);
    JSValue pushMouseEvent(const SDL_Event *event, const char *type);
    JSValue pushWheelEvent(const SDL_Event *event);
    JSValue pushTouchEvent(const SDL_Event *event, const char *type);
};
