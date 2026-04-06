#pragma once
#include <cstdint>
#include <string>

// forward declarations
struct duk_hthread;
typedef struct duk_hthread duk_context;
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

    bool init();
    void update(uint32_t delta);
    void processTimers();       // setTimeout コールバック実行
    void processRAF();          // requestAnimationFrame コールバック実行
    void render();
    void done();

    // SDL3 イベントをブラウザ互換イベントとして JS に配信
    void handleEvent(const SDL_Event *event);

    // SDL3 のファイル関数経由で JS ファイルを読み込み実行
    bool loadFile(const char *path);

    duk_context* getContext() const { return ctx_; }

    static JsEngine* getInstance() { return instance_; }

private:
    duk_context* ctx_;
    std::string basePath_;

    static JsEngine* instance_;

    // イベントディスパッチヘルパー
    void dispatchEvent(const char *type);
    void pushKeyboardEvent(const SDL_Event *event, const char *type);
    void pushMouseEvent(const SDL_Event *event, const char *type);
    void pushWheelEvent(const SDL_Event *event);
    void pushTouchEvent(const SDL_Event *event, const char *type);
};
