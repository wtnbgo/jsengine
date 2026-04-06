#pragma once
#include <cstdint>

// duktape forward declaration
struct duk_hthread;
typedef struct duk_hthread duk_context;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    bool init();
    void update(uint32_t delta);
    void render();
    void done();

    // SDL3 のファイル関数経由で JS ファイルを読み込み実行
    bool loadFile(const char *path);

    duk_context* getContext() const { return ctx_; }

private:
    duk_context* ctx_;
};
