#pragma once
#include <quickjs.h>
#include <cstdint>
#include <string>

union SDL_Event;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    // ベースパスを設定（init 前に呼ぶ）。データ参照の起点。
    void setBasePath(const char *path);
    const std::string& getBasePath() const { return basePath_; }

    // テンポラリ領域のパス (環境別: 例 NX なら "temp:/")
    void setTempPath(const char *path);
    const std::string& getTempPath() const { return tempPath_; }

    // 設定/セーブデータ用パス (SDL_GetPrefPath 相当, NX なら save: マウント)
    void setPrefPath(const char *path);
    const std::string& getPrefPath() const { return prefPath_; }

    // ベースパスからの相対パスをフルパスに解決。
    // 絶対パス (/, \, scheme:/, scheme:\) はそのまま返す。
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

    // 内蔵 sysinit.js (CMake で C++ に埋め込まれたブラウザシム) を評価。
    // overridePath が非 nullptr なら、 内蔵ではなく指定ファイルから読む (開発用、 -sysinit オプション)。
    // 全 binding (gl/Canvas2D/AudioContext/fs 等) 登録後、 main.js ロード前に呼ぶこと。
    bool loadSysinit(const char *overridePath);

    JSContext* getContext() const { return ctx_; }

    // 任意の type と event オブジェクトを addEventListener で登録されたリスナへ配信。
    // event_obj は呼び出し後に JS_FreeValue されるので、呼び元で free しないこと。
    void dispatchEvent(const char *type, JSValue event_obj);

    static JsEngine* getInstance() { return instance_; }

private:
    JSRuntime* rt_;
    JSContext* ctx_;
    std::string basePath_;
    std::string tempPath_;
    std::string prefPath_;

    static JsEngine* instance_;
    JSValue pushKeyboardEvent(const SDL_Event *event, const char *type);
    JSValue pushMouseEvent(const SDL_Event *event, const char *type);
    JSValue pushWheelEvent(const SDL_Event *event);
    JSValue pushTouchEvent(const SDL_Event *event, const char *type);
    JSValue pushPointerEventFromMouse(const SDL_Event *event, const char *type);
    JSValue pushPointerEventFromTouch(const SDL_Event *event, const char *type);
};
