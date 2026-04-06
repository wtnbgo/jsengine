#include "jsengine.hpp"
#include "dukwebgl.h"
#include <duktape.h>
#include <SDL3/SDL.h>

// console.log バインディング
static duk_ret_t native_console_log(duk_context *ctx) {
    duk_push_string(ctx, " ");
    duk_insert(ctx, 0);
    duk_join(ctx, duk_get_top(ctx) - 1);
    SDL_Log("JS: %s", duk_safe_to_string(ctx, -1));
    return 0;
}

// console.error バインディング
static duk_ret_t native_console_error(duk_context *ctx) {
    duk_push_string(ctx, " ");
    duk_insert(ctx, 0);
    duk_join(ctx, duk_get_top(ctx) - 1);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS: %s", duk_safe_to_string(ctx, -1));
    return 0;
}

// SDL3 経由でファイルを読み込み、文字列として返す（呼び出し側で SDL_free）
static char* load_file_sdl(const char *path, size_t *out_size) {
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load file '%s': %s", path, SDL_GetError());
        return nullptr;
    }
    if (out_size) *out_size = size;
    return (char*)data;
}

// JS から呼び出せる loadScript("path") バインディング
static duk_ret_t native_load_script(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    size_t size = 0;
    char *source = load_file_sdl(path, &size);
    if (!source) {
        return duk_error(ctx, DUK_ERR_ERROR, "Cannot load file: %s", path);
    }
    duk_push_lstring(ctx, source, size);
    SDL_free(source);
    // ファイル名付きでコンパイル・実行
    duk_push_string(ctx, path);
    if (duk_pcompile(ctx, DUK_COMPILE_EVAL) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS compile error (%s): %s", path, duk_safe_to_string(ctx, -1));
        return duk_throw(ctx);
    }
    if (duk_pcall(ctx, 0) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS exec error (%s): %s", path, duk_safe_to_string(ctx, -1));
        return duk_throw(ctx);
    }
    return 1; // 実行結果を返す
}

// duktape fatal error handler
static void fatal_handler(void *udata, const char *msg) {
    (void)udata;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Duktape fatal error: %s", msg ? msg : "unknown");
}

JsEngine::JsEngine() : ctx_(nullptr) {
}

JsEngine::~JsEngine() {
    done();
}

bool JsEngine::init() {
    ctx_ = duk_create_heap(nullptr, nullptr, nullptr, nullptr, fatal_handler);
    if (!ctx_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create Duktape context");
        return false;
    }

    // console オブジェクト登録
    duk_push_object(ctx_);
    duk_push_c_function(ctx_, native_console_log, DUK_VARARGS);
    duk_put_prop_string(ctx_, -2, "log");
    duk_push_c_function(ctx_, native_console_error, DUK_VARARGS);
    duk_put_prop_string(ctx_, -2, "error");
    duk_put_global_string(ctx_, "console");

    // loadScript バインディング登録
    duk_push_c_function(ctx_, native_load_script, 1);
    duk_put_global_string(ctx_, "loadScript");

    // WebGL バインディング登録
    dukwebgl_bind(ctx_);

    SDL_Log("JsEngine initialized");
    return true;
}

bool JsEngine::loadFile(const char *path) {
    if (!ctx_) return false;

    size_t size = 0;
    char *source = load_file_sdl(path, &size);
    if (!source) return false;

    duk_push_lstring(ctx_, source, size);
    SDL_free(source);
    duk_push_string(ctx_, path);
    if (duk_pcompile(ctx_, DUK_COMPILE_EVAL) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS compile error (%s): %s", path, duk_safe_to_string(ctx_, -1));
        duk_pop(ctx_);
        return false;
    }
    if (duk_pcall(ctx_, 0) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS exec error (%s): %s", path, duk_safe_to_string(ctx_, -1));
        duk_pop(ctx_);
        return false;
    }
    duk_pop(ctx_); // 実行結果を捨てる
    SDL_Log("Loaded JS: %s", path);
    return true;
}

void JsEngine::update(uint32_t delta) {
    if (!ctx_) return;

    // グローバルに update 関数があれば呼び出す
    duk_get_global_string(ctx_, "update");
    if (duk_is_function(ctx_, -1)) {
        duk_push_uint(ctx_, delta);
        if (duk_pcall(ctx_, 1) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS update error: %s", duk_safe_to_string(ctx_, -1));
        }
    }
    duk_pop(ctx_);
}

void JsEngine::render() {
    if (!ctx_) return;

    duk_get_global_string(ctx_, "render");
    if (duk_is_function(ctx_, -1)) {
        if (duk_pcall(ctx_, 0) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS render error: %s", duk_safe_to_string(ctx_, -1));
        }
    }
    duk_pop(ctx_);
}

void JsEngine::done() {
    if (!ctx_) return;

    // グローバルに done 関数があれば呼び出す
    duk_get_global_string(ctx_, "done");
    if (duk_is_function(ctx_, -1)) {
        if (duk_pcall(ctx_, 0) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS done error: %s", duk_safe_to_string(ctx_, -1));
        }
    }
    duk_pop(ctx_);

    duk_destroy_heap(ctx_);
    ctx_ = nullptr;
    SDL_Log("JsEngine destroyed");
}
