#include "jsengine.hpp"
#include "dukwebgl.h"
#include "webaudio.h"
#include "canvas2d.h"
#include <quickjs.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

// ============================================================
// ヘルパー: JSValue の例外チェックとログ出力
// ============================================================
static void log_exception(JSContext *ctx, const char *label = nullptr) {
    JSValue ex = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, ex);
    if (label) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", label, str ? str : "unknown");
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS error: %s", str ? str : "unknown");
    }
    // スタックトレースを出力
    if (JS_IsObject(ex)) {
        JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stackStr = JS_ToCString(ctx, stack);
            if (stackStr && stackStr[0]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  stack: %s", stackStr);
            }
            JS_FreeCString(ctx, stackStr);
        }
        JS_FreeValue(ctx, stack);
    }
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, ex);
}

// console.log バインディング
static JSValue native_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    std::string result;
    for (int i = 0; i < argc; i++) {
        if (i > 0) result += ' ';
        const char *s = JS_ToCString(ctx, argv[i]);
        if (s) {
            result += s;
            JS_FreeCString(ctx, s);
        }
    }
    SDL_Log("JS: %s", result.c_str());
    return JS_UNDEFINED;
}

// console.error バインディング
static JSValue native_console_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    std::string result;
    for (int i = 0; i < argc; i++) {
        if (i > 0) result += ' ';
        const char *s = JS_ToCString(ctx, argv[i]);
        if (s) {
            result += s;
            JS_FreeCString(ctx, s);
        }
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS: %s", result.c_str());
    return JS_UNDEFINED;
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

// ベースパスからの相対パスを解決するヘルパー（static 関数からアクセス用）
static std::string resolve_path(const char *path) {
    JsEngine *engine = JsEngine::getInstance();
    if (engine) return engine->resolvePath(path);
    return path;
}

// JS から呼び出せる loadScript("path") バインディング
static JSValue native_load_script(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "loadScript requires a path argument");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::string resolved = resolve_path(path);
    size_t size = 0;
    char *source = load_file_sdl(resolved.c_str(), &size);
    if (!source) {
        JSValue err = JS_ThrowInternalError(ctx, "Cannot load file: %s", resolved.c_str());
        JS_FreeCString(ctx, path);
        return err;
    }
    // ファイル名付きで eval
    JSValue result = JS_Eval(ctx, source, size, path, JS_EVAL_TYPE_GLOBAL);
    SDL_free(source);
    JS_FreeCString(ctx, path);
    if (JS_IsException(result)) {
        log_exception(ctx, "JS loadScript error");
        return JS_EXCEPTION;
    }
    return result;
}

// ============================================================
// ES Module ローダー
// ============================================================

// モジュール名の正規化（相対パス解決）
static char *js_module_normalize(JSContext *ctx, const char *base_name,
                                  const char *name, void *opaque) {
    (void)opaque;
    // 相対パス（./ or ../）の場合、base_name のディレクトリからの相対パスに解決
    if (name[0] == '.') {
        const char *p = strrchr(base_name, '/');
        if (!p) p = strrchr(base_name, '\\');
        size_t dir_len = p ? (size_t)(p - base_name + 1) : 0;
        size_t name_len = strlen(name);
        char *result = (char*)js_malloc(ctx, dir_len + name_len + 1);
        if (!result) return nullptr;
        memcpy(result, base_name, dir_len);
        memcpy(result + dir_len, name, name_len + 1);
        return result;
    }
    // そのまま使えるか（既に解決済みパス）をチェック
    {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(name, &info) && info.type == SDL_PATHTYPE_FILE) {
            return js_strdup(ctx, name);
        }
    }
    // ベアスペシファイア — ベースパスからの解決を試行
    JsEngine *engine = JsEngine::getInstance();
    if (engine) {
        std::string resolved = engine->resolvePath(name);
        return js_strdup(ctx, resolved.c_str());
    }
    return js_strdup(ctx, name);
}

// モジュールファイルの読み込み・コンパイル
static JSModuleDef *js_module_loader(JSContext *ctx, const char *module_name,
                                      void *opaque) {
    (void)opaque;
    size_t buf_len = 0;
    char *buf = (char*)SDL_LoadFile(module_name, &buf_len);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load module '%s': %s", module_name, SDL_GetError());
        return nullptr;
    }
    // モジュールとしてコンパイル
    JSValue func_val = JS_Eval(ctx, buf, buf_len, module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    SDL_free(buf);
    if (JS_IsException(func_val))
        return nullptr;
    // コンパイル済みモジュールから JSModuleDef を取得
    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);
    return m;
}

// loadModule(path) — ESM ファイルを読み込み、export された名前空間オブジェクトを返す
static JSValue native_loadModule(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "loadModule requires a path argument");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::string resolved = resolve_path(path);
    JS_FreeCString(ctx, path);

    size_t size = 0;
    char *source = load_file_sdl(resolved.c_str(), &size);
    if (!source) {
        return JS_ThrowInternalError(ctx, "Cannot load module: %s", resolved.c_str());
    }

    // モジュールとしてコンパイル
    JSValue func_val = JS_Eval(ctx, source, size, resolved.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    SDL_free(source);
    if (JS_IsException(func_val)) {
        log_exception(ctx, "Module compile error");
        return JS_EXCEPTION;
    }

    // JSModuleDef を取得（EvalFunction で消費される前に）
    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);

    // モジュールを評価（func_val の所有権は EvalFunction に移る）
    JSValue result = JS_EvalFunction(ctx, func_val);
    if (JS_IsException(result)) {
        log_exception(ctx, "Module eval error");
        return JS_EXCEPTION;
    }

    // モジュール評価のペンディングジョブを全て処理
    // （TLA 非使用モジュールでも ES2023 仕様で Promise が返る場合がある）
    JSContext *ctx2;
    while (JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx2) > 0) {}

    JS_FreeValue(ctx, result);

    // モジュール名前空間（全 export を含むオブジェクト）を返す
    return JS_GetModuleNamespace(ctx, m);
}

// ============================================================
// setTimeout / setInterval / requestAnimationFrame
// ============================================================

struct TimerEntry {
    int id;
    uint32_t fireTime;   // SDL_GetTicks ベースの発火時刻
    bool interval;
    uint32_t delay;
    bool cancelled;
    JSValue callback;    // JS_DupValue で保持
};

static int g_timerNextId = 1;
static std::vector<TimerEntry> g_timers;
static uint32_t g_currentTime = 0;

// setTimeout(callback, delay) — isInterval フラグで setInterval と共用
static JSValue native_setTimeout_impl(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, bool isInterval) {
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowInternalError(ctx, "setTimeout/setInterval requires a function argument");
    }
    uint32_t delay = 0;
    if (argc > 1) {
        int32_t d = 0;
        JS_ToInt32(ctx, &d, argv[1]);
        if (d > 0) delay = (uint32_t)d;
    }

    int id = g_timerNextId++;
    TimerEntry entry;
    entry.id = id;
    entry.fireTime = g_currentTime + delay;
    entry.interval = isInterval;
    entry.delay = delay;
    entry.cancelled = false;
    entry.callback = JS_DupValue(ctx, argv[0]);
    g_timers.push_back(entry);

    return JS_NewInt32(ctx, id);
}

static JSValue native_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return native_setTimeout_impl(ctx, this_val, argc, argv, false);
}

static JSValue native_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return native_setTimeout_impl(ctx, this_val, argc, argv, true);
}

static JSValue native_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    for (auto &t : g_timers) {
        if (t.id == id) {
            t.cancelled = true;
            JS_FreeValue(ctx, t.callback);
            t.callback = JS_UNDEFINED;
            break;
        }
    }
    return JS_UNDEFINED;
}

// requestAnimationFrame(callback) => id
// RAF コールバックはグローバル __rafCallbacks 配列に格納
static JSValue native_requestAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowInternalError(ctx, "requestAnimationFrame requires a function argument");
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue arr = JS_GetPropertyStr(ctx, global, "__rafCallbacks");

    JSValue lenVal = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[0]));
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, global);

    return JS_NewUint32(ctx, len + 1);
}

static JSValue native_cancelAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

// performance.now()
static JSValue native_performance_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)SDL_GetTicks());
}

// ============================================================
// createImageBitmap(path) => ImageBitmap
// ImageBitmap: { width, height, data (ArrayBuffer, RGBA) }
// ============================================================

static JSValue native_createImageBitmap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "createImageBitmap requires a path argument");
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    std::string resolved = resolve_path(path);
    JS_FreeCString(ctx, path);

    // SDL_image で画像読み込み
    SDL_Surface *surface = IMG_Load(resolved.c_str());
    if (!surface) {
        return JS_ThrowInternalError(ctx, "Cannot load image: %s (%s)", resolved.c_str(), SDL_GetError());
    }

    // RGBA8888 に変換
    SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) {
        return JS_ThrowInternalError(ctx, "Failed to convert image to RGBA: %s", SDL_GetError());
    }

    int w = rgba->w;
    int h = rgba->h;
    size_t dataSize = (size_t)w * h * 4;

    // ImageBitmap オブジェクト作成
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "width", JS_NewInt32(ctx, w));
    JS_SetPropertyStr(ctx, obj, "height", JS_NewInt32(ctx, h));

    // ピクセルデータをバッファにコピー
    uint8_t *buf = nullptr;
    if (rgba->pitch == w * 4) {
        JSValue ab = JS_NewArrayBufferCopy(ctx, (const uint8_t*)rgba->pixels, dataSize);
        JS_SetPropertyStr(ctx, obj, "data", ab);
    } else {
        // 行ごとにコピー
        buf = (uint8_t*)SDL_malloc(dataSize);
        for (int y = 0; y < h; y++) {
            memcpy(buf + y * w * 4,
                   (char*)rgba->pixels + y * rgba->pitch,
                   w * 4);
        }
        JSValue ab = JS_NewArrayBufferCopy(ctx, buf, dataSize);
        SDL_free(buf);
        JS_SetPropertyStr(ctx, obj, "data", ab);
    }

    SDL_DestroySurface(rgba);
    return obj;
}

// ============================================================
// addEventListener / removeEventListener
// ============================================================
// イベントリスナーは QuickJS のグローバル隠しオブジェクト __eventListeners に格納
// __eventListeners[type] = [callback, callback, ...]

// addEventListener(type, callback)
static JSValue native_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_EXCEPTION;

    // callback が null/undefined の場合は無視（passive event detection 等）
    if (!JS_IsFunction(ctx, argv[1])) {
        JS_FreeCString(ctx, type);
        return JS_UNDEFINED;
    }

    // __eventListeners を取得（なければ作成）
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue listeners = JS_GetPropertyStr(ctx, global, "__eventListeners");
    if (JS_IsUndefined(listeners)) {
        JS_FreeValue(ctx, listeners);
        listeners = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, "__eventListeners", JS_DupValue(ctx, listeners));
    }

    // __eventListeners[type] を取得（なければ配列作成）
    JSValue arr = JS_GetPropertyStr(ctx, listeners, type);
    if (JS_IsUndefined(arr)) {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, listeners, type, JS_DupValue(ctx, arr));
    }

    // 配列の末尾に callback を追加
    JSValue lenVal = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));

    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, listeners);
    JS_FreeValue(ctx, global);
    JS_FreeCString(ctx, type);
    return JS_UNDEFINED;
}

// removeEventListener(type, callback)
static JSValue native_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_EXCEPTION;

    if (!JS_IsFunction(ctx, argv[1])) {
        JS_FreeCString(ctx, type);
        return JS_UNDEFINED;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue listeners = JS_GetPropertyStr(ctx, global, "__eventListeners");
    if (JS_IsUndefined(listeners)) {
        JS_FreeValue(ctx, listeners);
        JS_FreeValue(ctx, global);
        JS_FreeCString(ctx, type);
        return JS_UNDEFINED;
    }

    JSValue arr = JS_GetPropertyStr(ctx, listeners, type);
    if (JS_IsUndefined(arr)) {
        JS_FreeValue(ctx, arr);
        JS_FreeValue(ctx, listeners);
        JS_FreeValue(ctx, global);
        JS_FreeCString(ctx, type);
        return JS_UNDEFINED;
    }

    JSValue lenVal = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    for (uint32_t i = 0; i < len; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, arr, i);
        // QuickJS では関数の同一性を比較（同じ JSValue かどうか）
        // JS_StrictEq は QuickJS にないので、JS_VALUE_GET_PTR で比較
        // ただし QuickJS の公開 API では直接比較が難しいため、
        // JS 側で === を使って比較する
        // ここでは簡易的に JS_Eval で比較
        bool match = false;
        {
            // argv[1] と elem を === で比較
            JSValue args[2] = { argv[1], elem };
            JSValue cmpFunc = JS_Eval(ctx,
                "(function(a,b){return a===b;})", 30, "<cmp>", JS_EVAL_TYPE_GLOBAL);
            if (!JS_IsException(cmpFunc)) {
                JSValue result = JS_Call(ctx, cmpFunc, JS_UNDEFINED, 2, args);
                if (!JS_IsException(result)) {
                    match = JS_ToBool(ctx, result) != 0;
                }
                JS_FreeValue(ctx, result);
            }
            JS_FreeValue(ctx, cmpFunc);
        }
        JS_FreeValue(ctx, elem);

        if (match) {
            // 配列要素を削除（後ろを詰める）
            for (uint32_t j = i; j < len - 1; j++) {
                JSValue next = JS_GetPropertyUint32(ctx, arr, j + 1);
                JS_SetPropertyUint32(ctx, arr, j, next);
            }
            {
                JSAtom atom = JS_NewAtomUInt32(ctx, (uint32_t)(len - 1));
                JS_DeleteProperty(ctx, arr, atom, 0);
                JS_FreeAtom(ctx, atom);
            }
            // length を更新
            JS_SetPropertyStr(ctx, arr, "length", JS_NewUint32(ctx, len - 1));
            break;
        }
    }

    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, listeners);
    JS_FreeValue(ctx, global);
    JS_FreeCString(ctx, type);
    return JS_UNDEFINED;
}

// ============================================================
// SDL_Keycode → ブラウザ key 文字列変換
// ============================================================

static const char* sdl_keycode_to_js_key(SDL_Keycode key) {
    // 特殊キー
    switch (key) {
    case SDLK_RETURN:       return "Enter";
    case SDLK_ESCAPE:       return "Escape";
    case SDLK_BACKSPACE:    return "Backspace";
    case SDLK_TAB:          return "Tab";
    case SDLK_SPACE:        return " ";
    case SDLK_DELETE:       return "Delete";
    case SDLK_INSERT:       return "Insert";
    case SDLK_HOME:         return "Home";
    case SDLK_END:          return "End";
    case SDLK_PAGEUP:       return "PageUp";
    case SDLK_PAGEDOWN:     return "PageDown";
    case SDLK_LEFT:         return "ArrowLeft";
    case SDLK_RIGHT:        return "ArrowRight";
    case SDLK_UP:           return "ArrowUp";
    case SDLK_DOWN:         return "ArrowDown";
    case SDLK_CAPSLOCK:     return "CapsLock";
    case SDLK_LSHIFT:       return "Shift";
    case SDLK_RSHIFT:       return "Shift";
    case SDLK_LCTRL:        return "Control";
    case SDLK_RCTRL:        return "Control";
    case SDLK_LALT:         return "Alt";
    case SDLK_RALT:         return "Alt";
    case SDLK_LGUI:         return "Meta";
    case SDLK_RGUI:         return "Meta";
    case SDLK_F1:           return "F1";
    case SDLK_F2:           return "F2";
    case SDLK_F3:           return "F3";
    case SDLK_F4:           return "F4";
    case SDLK_F5:           return "F5";
    case SDLK_F6:           return "F6";
    case SDLK_F7:           return "F7";
    case SDLK_F8:           return "F8";
    case SDLK_F9:           return "F9";
    case SDLK_F10:          return "F10";
    case SDLK_F11:          return "F11";
    case SDLK_F12:          return "F12";
    default:
        break;
    }
    // 通常文字キーは SDL_GetKeyName で取得
    return nullptr;
}

static const char* sdl_scancode_to_js_code(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_A: return "KeyA";
    case SDL_SCANCODE_B: return "KeyB";
    case SDL_SCANCODE_C: return "KeyC";
    case SDL_SCANCODE_D: return "KeyD";
    case SDL_SCANCODE_E: return "KeyE";
    case SDL_SCANCODE_F: return "KeyF";
    case SDL_SCANCODE_G: return "KeyG";
    case SDL_SCANCODE_H: return "KeyH";
    case SDL_SCANCODE_I: return "KeyI";
    case SDL_SCANCODE_J: return "KeyJ";
    case SDL_SCANCODE_K: return "KeyK";
    case SDL_SCANCODE_L: return "KeyL";
    case SDL_SCANCODE_M: return "KeyM";
    case SDL_SCANCODE_N: return "KeyN";
    case SDL_SCANCODE_O: return "KeyO";
    case SDL_SCANCODE_P: return "KeyP";
    case SDL_SCANCODE_Q: return "KeyQ";
    case SDL_SCANCODE_R: return "KeyR";
    case SDL_SCANCODE_S: return "KeyS";
    case SDL_SCANCODE_T: return "KeyT";
    case SDL_SCANCODE_U: return "KeyU";
    case SDL_SCANCODE_V: return "KeyV";
    case SDL_SCANCODE_W: return "KeyW";
    case SDL_SCANCODE_X: return "KeyX";
    case SDL_SCANCODE_Y: return "KeyY";
    case SDL_SCANCODE_Z: return "KeyZ";
    case SDL_SCANCODE_0: return "Digit0";
    case SDL_SCANCODE_1: return "Digit1";
    case SDL_SCANCODE_2: return "Digit2";
    case SDL_SCANCODE_3: return "Digit3";
    case SDL_SCANCODE_4: return "Digit4";
    case SDL_SCANCODE_5: return "Digit5";
    case SDL_SCANCODE_6: return "Digit6";
    case SDL_SCANCODE_7: return "Digit7";
    case SDL_SCANCODE_8: return "Digit8";
    case SDL_SCANCODE_9: return "Digit9";
    case SDL_SCANCODE_RETURN:    return "Enter";
    case SDL_SCANCODE_ESCAPE:    return "Escape";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_TAB:       return "Tab";
    case SDL_SCANCODE_SPACE:     return "Space";
    case SDL_SCANCODE_MINUS:     return "Minus";
    case SDL_SCANCODE_EQUALS:    return "Equal";
    case SDL_SCANCODE_LEFTBRACKET:  return "BracketLeft";
    case SDL_SCANCODE_RIGHTBRACKET: return "BracketRight";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_GRAVE:     return "Backquote";
    case SDL_SCANCODE_COMMA:     return "Comma";
    case SDL_SCANCODE_PERIOD:    return "Period";
    case SDL_SCANCODE_SLASH:     return "Slash";
    case SDL_SCANCODE_CAPSLOCK:  return "CapsLock";
    case SDL_SCANCODE_F1:  return "F1";
    case SDL_SCANCODE_F2:  return "F2";
    case SDL_SCANCODE_F3:  return "F3";
    case SDL_SCANCODE_F4:  return "F4";
    case SDL_SCANCODE_F5:  return "F5";
    case SDL_SCANCODE_F6:  return "F6";
    case SDL_SCANCODE_F7:  return "F7";
    case SDL_SCANCODE_F8:  return "F8";
    case SDL_SCANCODE_F9:  return "F9";
    case SDL_SCANCODE_F10: return "F10";
    case SDL_SCANCODE_F11: return "F11";
    case SDL_SCANCODE_F12: return "F12";
    case SDL_SCANCODE_INSERT:    return "Insert";
    case SDL_SCANCODE_HOME:      return "Home";
    case SDL_SCANCODE_PAGEUP:    return "PageUp";
    case SDL_SCANCODE_DELETE:    return "Delete";
    case SDL_SCANCODE_END:       return "End";
    case SDL_SCANCODE_PAGEDOWN:  return "PageDown";
    case SDL_SCANCODE_RIGHT:     return "ArrowRight";
    case SDL_SCANCODE_LEFT:      return "ArrowLeft";
    case SDL_SCANCODE_DOWN:      return "ArrowDown";
    case SDL_SCANCODE_UP:        return "ArrowUp";
    case SDL_SCANCODE_LCTRL:     return "ControlLeft";
    case SDL_SCANCODE_LSHIFT:    return "ShiftLeft";
    case SDL_SCANCODE_LALT:      return "AltLeft";
    case SDL_SCANCODE_LGUI:      return "MetaLeft";
    case SDL_SCANCODE_RCTRL:     return "ControlRight";
    case SDL_SCANCODE_RSHIFT:    return "ShiftRight";
    case SDL_SCANCODE_RALT:      return "AltRight";
    case SDL_SCANCODE_RGUI:      return "MetaRight";
    default:
        break;
    }
    return SDL_GetScancodeName(sc);
}

// ============================================================
// File System Access API (同期版)
// ============================================================

// --- ユーティリティ: options から create フラグ取得 ---
static bool get_create_option(JSContext *ctx, JSValueConst opt) {
    if (JS_IsObject(opt)) {
        JSValue val = JS_GetPropertyStr(ctx, opt, "create");
        bool create = JS_ToBool(ctx, val) != 0;
        JS_FreeValue(ctx, val);
        return create;
    }
    return false;
}

// --- ユーティリティ: ファイル名抽出 ---
static const char* extract_filename(const char *path) {
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') name = p + 1;
        p++;
    }
    return name;
}

// --- ヘルパー: オブジェクトの _path プロパティを取得 ---
// 呼び出し側で JS_FreeCString すること
static const char* get_path_from_this(JSContext *ctx, JSValueConst this_val) {
    JSValue pathVal = JS_GetPropertyStr(ctx, this_val, "_path");
    const char *path = JS_ToCString(ctx, pathVal);
    JS_FreeValue(ctx, pathVal);
    return path;
}

// --- FileSystemFileHandle ---
static JSValue push_file_handle(JSContext *ctx, const char *path);
static JSValue push_directory_handle(JSContext *ctx, const char *path);

// getFile() => { name, size, text(), arrayBuffer() }
static JSValue filehandle_getFile(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    const char *path = get_path_from_this(ctx, this_val);
    if (!path) return JS_EXCEPTION;

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        JSValue err = JS_ThrowInternalError(ctx, "File not found: %s", path);
        JS_FreeCString(ctx, path);
        return err;
    }

    const char *name = extract_filename(path);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewFloat64(ctx, (double)info.size));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "_path", JS_NewString(ctx, path));
    JS_FreeCString(ctx, path);

    // text() メソッド
    JS_SetPropertyStr(ctx, obj, "text",
        JS_NewCFunction(ctx, [](JSContext *c, JSValueConst tv, int, JSValueConst *) -> JSValue {
            const char *p = get_path_from_this(c, tv);
            if (!p) return JS_EXCEPTION;
            size_t sz = 0;
            void *data = SDL_LoadFile(p, &sz);
            if (!data) {
                JSValue err = JS_ThrowInternalError(c, "Cannot read file: %s", p);
                JS_FreeCString(c, p);
                return err;
            }
            JSValue s = JS_NewStringLen(c, (const char*)data, sz);
            SDL_free(data);
            JS_FreeCString(c, p);
            return s;
        }, "text", 0));

    // arrayBuffer() メソッド
    JS_SetPropertyStr(ctx, obj, "arrayBuffer",
        JS_NewCFunction(ctx, [](JSContext *c, JSValueConst tv, int, JSValueConst *) -> JSValue {
            const char *p = get_path_from_this(c, tv);
            if (!p) return JS_EXCEPTION;
            size_t sz = 0;
            void *data = SDL_LoadFile(p, &sz);
            if (!data) {
                JSValue err = JS_ThrowInternalError(c, "Cannot read file: %s", p);
                JS_FreeCString(c, p);
                return err;
            }
            JSValue ab = JS_NewArrayBufferCopy(c, (const uint8_t*)data, sz);
            SDL_free(data);
            JS_FreeCString(c, p);
            return ab;
        }, "arrayBuffer", 0));

    return obj;
}

// createWritable() => { _chunks: [], write(data), close() }
static JSValue filehandle_createWritable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    const char *path = get_path_from_this(ctx, this_val);
    if (!path) return JS_EXCEPTION;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_path", JS_NewString(ctx, path));
    JS_FreeCString(ctx, path);

    // _chunks 配列
    JS_SetPropertyStr(ctx, obj, "_chunks", JS_NewArray(ctx));

    // write(data) — 文字列またはバッファを _chunks に追加
    JS_SetPropertyStr(ctx, obj, "write",
        JS_NewCFunction(ctx, [](JSContext *c, JSValueConst tv, int ac, JSValueConst *av) -> JSValue {
            if (ac < 1) return JS_UNDEFINED;
            JSValue chunks = JS_GetPropertyStr(c, tv, "_chunks");
            JSValue lenVal = JS_GetPropertyStr(c, chunks, "length");
            uint32_t len = 0;
            JS_ToUint32(c, &len, lenVal);
            JS_FreeValue(c, lenVal);
            JS_SetPropertyUint32(c, chunks, len, JS_DupValue(c, av[0]));
            JS_FreeValue(c, chunks);
            return JS_UNDEFINED;
        }, "write", 1));

    // close() — _chunks を結合してファイルに書き出す
    JS_SetPropertyStr(ctx, obj, "close",
        JS_NewCFunction(ctx, [](JSContext *c, JSValueConst tv, int, JSValueConst *) -> JSValue {
            JSValue pathVal = JS_GetPropertyStr(c, tv, "_path");
            const char *p = JS_ToCString(c, pathVal);
            JS_FreeValue(c, pathVal);
            if (!p) return JS_EXCEPTION;

            JSValue chunks = JS_GetPropertyStr(c, tv, "_chunks");
            JSValue lenVal = JS_GetPropertyStr(c, chunks, "length");
            uint32_t len = 0;
            JS_ToUint32(c, &len, lenVal);
            JS_FreeValue(c, lenVal);

            // 全チャンクのサイズを計算
            size_t total = 0;
            for (uint32_t i = 0; i < len; i++) {
                JSValue chunk = JS_GetPropertyUint32(c, chunks, i);
                size_t sz = 0;
                uint8_t *buf = JS_GetArrayBuffer(c, &sz, chunk);
                if (buf) {
                    total += sz;
                } else {
                    const char *s = JS_ToCString(c, chunk);
                    if (s) {
                        total += strlen(s);
                        JS_FreeCString(c, s);
                    }
                }
                JS_FreeValue(c, chunk);
            }

            // バッファに結合
            char *buf = (char*)SDL_malloc(total);
            if (!buf) {
                JS_FreeValue(c, chunks);
                JS_FreeCString(c, p);
                return JS_ThrowInternalError(c, "Out of memory");
            }
            size_t offset = 0;
            for (uint32_t i = 0; i < len; i++) {
                JSValue chunk = JS_GetPropertyUint32(c, chunks, i);
                size_t sz = 0;
                uint8_t *abuf = JS_GetArrayBuffer(c, &sz, chunk);
                if (abuf) {
                    memcpy(buf + offset, abuf, sz);
                    offset += sz;
                } else {
                    const char *s = JS_ToCString(c, chunk);
                    if (s) {
                        size_t slen = strlen(s);
                        memcpy(buf + offset, s, slen);
                        offset += slen;
                        JS_FreeCString(c, s);
                    }
                }
                JS_FreeValue(c, chunk);
            }

            bool ok = SDL_SaveFile(p, buf, total);
            SDL_free(buf);
            JS_FreeValue(c, chunks);

            if (!ok) {
                JSValue err = JS_ThrowInternalError(c, "Failed to write file: %s", p);
                JS_FreeCString(c, p);
                return err;
            }
            JS_FreeCString(c, p);
            return JS_UNDEFINED;
        }, "close", 0));

    return obj;
}

static JSValue push_file_handle(JSContext *ctx, const char *path) {
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "_path", JS_NewString(ctx, path));
    JS_SetPropertyStr(ctx, obj, "kind", JS_NewString(ctx, "file"));

    const char *name = extract_filename(path);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));

    JS_SetPropertyStr(ctx, obj, "getFile", JS_NewCFunction(ctx, filehandle_getFile, "getFile", 0));
    JS_SetPropertyStr(ctx, obj, "createWritable", JS_NewCFunction(ctx, filehandle_createWritable, "createWritable", 0));

    return obj;
}

// --- FileSystemDirectoryHandle ---

// entries() で使うコールバック用コンテキスト
struct EnumCtx {
    JSContext *ctx;
    JSValue arr;
    uint32_t count;
    const char *dirname;
};

static SDL_EnumerationResult dir_enum_callback(void *userdata, const char *dirname, const char *fname) {
    EnumCtx *ec = (EnumCtx*)userdata;
    JSContext *ctx = ec->ctx;

    // エントリ配列 [name, handle]
    JSValue entry = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, entry, 0, JS_NewString(ctx, fname));

    // フルパス構築
    size_t len = strlen(dirname) + strlen(fname) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dirname, fname);

    SDL_PathInfo info;
    if (SDL_GetPathInfo(fullpath, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        JS_SetPropertyUint32(ctx, entry, 1, push_directory_handle(ctx, fullpath));
    } else {
        JS_SetPropertyUint32(ctx, entry, 1, push_file_handle(ctx, fullpath));
    }
    SDL_free(fullpath);

    // 外側の配列に追加
    JS_SetPropertyUint32(ctx, ec->arr, ec->count, entry);
    ec->count++;

    return SDL_ENUM_CONTINUE;
}

static JSValue dirhandle_entries(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    const char *path = get_path_from_this(ctx, this_val);
    if (!path) return JS_EXCEPTION;

    JSValue arr = JS_NewArray(ctx);
    EnumCtx ec = { ctx, arr, 0, path };
    SDL_EnumerateDirectory(path, dir_enum_callback, &ec);
    JS_FreeCString(ctx, path);
    return arr;
}

// getFileHandle(name, options)
static JSValue dirhandle_getFileHandle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *dir = get_path_from_this(ctx, this_val);
    if (!dir) return JS_EXCEPTION;
    if (argc < 1) {
        JS_FreeCString(ctx, dir);
        return JS_ThrowInternalError(ctx, "getFileHandle requires a name argument");
    }

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) { JS_FreeCString(ctx, dir); return JS_EXCEPTION; }
    bool create = (argc > 1) ? get_create_option(ctx, argv[1]) : false;

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(fullpath, &info) || info.type == SDL_PATHTYPE_NONE) {
        if (create) {
            SDL_SaveFile(fullpath, "", 0);
        } else {
            JSValue err = JS_ThrowInternalError(ctx, "File not found: %s/%s", dir, name);
            SDL_free(fullpath);
            JS_FreeCString(ctx, name);
            JS_FreeCString(ctx, dir);
            return err;
        }
    }

    JSValue handle = push_file_handle(ctx, fullpath);
    SDL_free(fullpath);
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, dir);
    return handle;
}

// getDirectoryHandle(name, options)
static JSValue dirhandle_getDirectoryHandle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *dir = get_path_from_this(ctx, this_val);
    if (!dir) return JS_EXCEPTION;
    if (argc < 1) {
        JS_FreeCString(ctx, dir);
        return JS_ThrowInternalError(ctx, "getDirectoryHandle requires a name argument");
    }

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) { JS_FreeCString(ctx, dir); return JS_EXCEPTION; }
    bool create = (argc > 1) ? get_create_option(ctx, argv[1]) : false;

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(fullpath, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        if (create) {
            SDL_CreateDirectory(fullpath);
        } else {
            JSValue err = JS_ThrowInternalError(ctx, "Directory not found: %s/%s", dir, name);
            SDL_free(fullpath);
            JS_FreeCString(ctx, name);
            JS_FreeCString(ctx, dir);
            return err;
        }
    }

    JSValue handle = push_directory_handle(ctx, fullpath);
    SDL_free(fullpath);
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, dir);
    return handle;
}

// removeEntry(name, options)
static JSValue dirhandle_removeEntry(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *dir = get_path_from_this(ctx, this_val);
    if (!dir) return JS_EXCEPTION;
    if (argc < 1) {
        JS_FreeCString(ctx, dir);
        return JS_ThrowInternalError(ctx, "removeEntry requires a name argument");
    }

    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) { JS_FreeCString(ctx, dir); return JS_EXCEPTION; }

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    if (!SDL_RemovePath(fullpath)) {
        JSValue err = JS_ThrowInternalError(ctx, "Failed to remove: %s/%s", dir, name);
        SDL_free(fullpath);
        JS_FreeCString(ctx, name);
        JS_FreeCString(ctx, dir);
        return err;
    }
    SDL_free(fullpath);
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, dir);
    return JS_UNDEFINED;
}

static JSValue push_directory_handle(JSContext *ctx, const char *path) {
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "_path", JS_NewString(ctx, path));
    JS_SetPropertyStr(ctx, obj, "kind", JS_NewString(ctx, "directory"));

    const char *name = extract_filename(path);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));

    JS_SetPropertyStr(ctx, obj, "entries", JS_NewCFunction(ctx, dirhandle_entries, "entries", 0));
    JS_SetPropertyStr(ctx, obj, "getFileHandle", JS_NewCFunction(ctx, dirhandle_getFileHandle, "getFileHandle", 2));
    JS_SetPropertyStr(ctx, obj, "getDirectoryHandle", JS_NewCFunction(ctx, dirhandle_getDirectoryHandle, "getDirectoryHandle", 2));
    JS_SetPropertyStr(ctx, obj, "removeEntry", JS_NewCFunction(ctx, dirhandle_removeEntry, "removeEntry", 2));

    return obj;
}

// --- グローバル fs オブジェクト ---

// fs.getFileHandle(path, options)
static JSValue fs_getFileHandle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "getFileHandle requires a path argument");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    bool create = (argc > 1) ? get_create_option(ctx, argv[1]) : false;

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type == SDL_PATHTYPE_NONE) {
        if (create) {
            SDL_SaveFile(rpath.c_str(), "", 0);
        } else {
            return JS_ThrowInternalError(ctx, "File not found: %s", rpath.c_str());
        }
    }
    return push_file_handle(ctx, rpath.c_str());
}

// fs.getDirectoryHandle(path, options)
static JSValue fs_getDirectoryHandle(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "getDirectoryHandle requires a path argument");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    bool create = (argc > 1) ? get_create_option(ctx, argv[1]) : false;

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        if (create) {
            SDL_CreateDirectory(rpath.c_str());
        } else {
            return JS_ThrowInternalError(ctx, "Directory not found: %s", rpath.c_str());
        }
    }
    return push_directory_handle(ctx, rpath.c_str());
}

// fs.exists(path) => boolean
static JSValue fs_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_FALSE;
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    SDL_PathInfo info;
    return JS_NewBool(ctx, SDL_GetPathInfo(rpath.c_str(), &info) && info.type != SDL_PATHTYPE_NONE);
}

// fs.stat(path) => { type, size, createTime, modifyTime, accessTime } | null
static JSValue fs_stat(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_NULL;
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type == SDL_PATHTYPE_NONE) {
        return JS_NULL;
    }
    JSValue obj = JS_NewObject(ctx);
    const char *typeStr = "other";
    if (info.type == SDL_PATHTYPE_FILE) typeStr = "file";
    else if (info.type == SDL_PATHTYPE_DIRECTORY) typeStr = "directory";
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, typeStr));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewFloat64(ctx, (double)info.size));
    JS_SetPropertyStr(ctx, obj, "createTime", JS_NewFloat64(ctx, (double)info.create_time));
    JS_SetPropertyStr(ctx, obj, "modifyTime", JS_NewFloat64(ctx, (double)info.modify_time));
    JS_SetPropertyStr(ctx, obj, "accessTime", JS_NewFloat64(ctx, (double)info.access_time));
    return obj;
}

// fs.mkdir(path)
static JSValue fs_mkdir(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "mkdir requires a path argument");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    if (!SDL_CreateDirectory(rpath.c_str())) {
        return JS_ThrowInternalError(ctx, "Failed to create directory: %s", rpath.c_str());
    }
    return JS_UNDEFINED;
}

// fs.remove(path)
static JSValue fs_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "remove requires a path argument");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    if (!SDL_RemovePath(rpath.c_str())) {
        return JS_ThrowInternalError(ctx, "Failed to remove: %s", rpath.c_str());
    }
    return JS_UNDEFINED;
}

// fs.rename(oldPath, newPath)
static JSValue fs_rename(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_ThrowInternalError(ctx, "rename requires oldPath and newPath arguments");
    const char *oldStr = JS_ToCString(ctx, argv[0]);
    const char *newStr = JS_ToCString(ctx, argv[1]);
    if (!oldStr || !newStr) {
        if (oldStr) JS_FreeCString(ctx, oldStr);
        if (newStr) JS_FreeCString(ctx, newStr);
        return JS_EXCEPTION;
    }
    std::string rold = resolve_path(oldStr);
    std::string rnew = resolve_path(newStr);
    JS_FreeCString(ctx, oldStr);
    JS_FreeCString(ctx, newStr);
    if (!SDL_RenamePath(rold.c_str(), rnew.c_str())) {
        return JS_ThrowInternalError(ctx, "Failed to rename: %s -> %s", rold.c_str(), rnew.c_str());
    }
    return JS_UNDEFINED;
}

// fs.readText(path) => string
static JSValue fs_readText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowInternalError(ctx, "readText requires a path argument");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    size_t sz = 0;
    void *data = SDL_LoadFile(rpath.c_str(), &sz);
    if (!data) return JS_ThrowInternalError(ctx, "Cannot read file: %s", rpath.c_str());
    JSValue s = JS_NewStringLen(ctx, (const char*)data, sz);
    SDL_free(data);
    return s;
}

// fs.writeText(path, text)
static JSValue fs_writeText(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_ThrowInternalError(ctx, "writeText requires path and text arguments");
    const char *pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) return JS_EXCEPTION;
    std::string rpath = resolve_path(pathStr);
    JS_FreeCString(ctx, pathStr);
    size_t len = 0;
    const char *text = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!text) return JS_EXCEPTION;
    bool ok = SDL_SaveFile(rpath.c_str(), text, len);
    JS_FreeCString(ctx, text);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "Failed to write file: %s", rpath.c_str());
    }
    return JS_UNDEFINED;
}

static void fs_register(JSContext *ctx) {
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "getFileHandle", JS_NewCFunction(ctx, fs_getFileHandle, "getFileHandle", 2));
    JS_SetPropertyStr(ctx, obj, "getDirectoryHandle", JS_NewCFunction(ctx, fs_getDirectoryHandle, "getDirectoryHandle", 2));
    JS_SetPropertyStr(ctx, obj, "exists", JS_NewCFunction(ctx, fs_exists, "exists", 1));
    JS_SetPropertyStr(ctx, obj, "stat", JS_NewCFunction(ctx, fs_stat, "stat", 1));
    JS_SetPropertyStr(ctx, obj, "mkdir", JS_NewCFunction(ctx, fs_mkdir, "mkdir", 1));
    JS_SetPropertyStr(ctx, obj, "remove", JS_NewCFunction(ctx, fs_remove, "remove", 1));
    JS_SetPropertyStr(ctx, obj, "rename", JS_NewCFunction(ctx, fs_rename, "rename", 2));
    JS_SetPropertyStr(ctx, obj, "readText", JS_NewCFunction(ctx, fs_readText, "readText", 1));
    JS_SetPropertyStr(ctx, obj, "writeText", JS_NewCFunction(ctx, fs_writeText, "writeText", 2));

    // fs.basePath を設定
    JsEngine *engine = JsEngine::getInstance();
    if (engine) {
        JS_SetPropertyStr(ctx, obj, "basePath", JS_NewString(ctx, engine->getBasePath().c_str()));
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "fs", obj);
    JS_FreeValue(ctx, global);
}

// ============================================================
// localStorage (Web Storage API)
// データは SDL_GetPrefPath 配下の localStorage.json に保存
// ============================================================

static char* get_storage_path() {
    char *pref = SDL_GetPrefPath("jsengine", "jsengine");
    if (!pref) return nullptr;
    size_t len = strlen(pref) + 32;
    char *path = (char*)SDL_malloc(len);
    snprintf(path, len, "%slocalStorage.json", pref);
    SDL_free(pref);
    return path;
}

// __localStorageData を JSON 文字列にして保存
static void storage_save(JSContext *ctx) {
    char *path = get_storage_path();
    if (!path) return;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
    JSValue stringify = JS_GetPropertyStr(ctx, json, "stringify");
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");

    JSValue result = JS_Call(ctx, stringify, json, 1, &data);
    if (!JS_IsException(result)) {
        const char *jsonStr = JS_ToCString(ctx, result);
        if (jsonStr) {
            SDL_IOStream *io = SDL_IOFromFile(path, "w");
            if (io) {
                SDL_WriteIO(io, jsonStr, strlen(jsonStr));
                SDL_CloseIO(io);
            }
            JS_FreeCString(ctx, jsonStr);
        }
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, stringify);
    JS_FreeValue(ctx, json);
    JS_FreeValue(ctx, global);
    SDL_free(path);
}

// JSON ファイルから __localStorageData を復元
static void storage_load(JSContext *ctx) {
    char *path = get_storage_path();
    JSValue global = JS_GetGlobalObject(ctx);

    if (!path) {
        JS_SetPropertyStr(ctx, global, "__localStorageData", JS_NewObject(ctx));
        JS_FreeValue(ctx, global);
        return;
    }

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    SDL_free(path);

    if (data && size > 0) {
        JSValue json = JS_GetPropertyStr(ctx, global, "JSON");
        JSValue parse = JS_GetPropertyStr(ctx, json, "parse");
        JSValue str = JS_NewStringLen(ctx, (const char*)data, size);
        JSValue result = JS_Call(ctx, parse, json, 1, &str);
        JS_FreeValue(ctx, str);
        JS_FreeValue(ctx, parse);
        JS_FreeValue(ctx, json);

        if (!JS_IsException(result) && JS_IsObject(result)) {
            JS_SetPropertyStr(ctx, global, "__localStorageData", result);
        } else {
            JS_FreeValue(ctx, result);
            JS_SetPropertyStr(ctx, global, "__localStorageData", JS_NewObject(ctx));
        }
    } else {
        JS_SetPropertyStr(ctx, global, "__localStorageData", JS_NewObject(ctx));
    }
    if (data) SDL_free(data);
    JS_FreeValue(ctx, global);
}

// localStorage.getItem(key) => string | null
static JSValue native_storage_getItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_NULL;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");
    JSValue val = JS_GetPropertyStr(ctx, data, key);
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, global);
    JS_FreeCString(ctx, key);

    if (JS_IsUndefined(val)) {
        JS_FreeValue(ctx, val);
        return JS_NULL;
    }
    return val;
}

// localStorage.setItem(key, value)
static JSValue native_storage_setItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    const char *value = JS_ToCString(ctx, argv[1]);
    if (!value) { JS_FreeCString(ctx, key); return JS_EXCEPTION; }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");
    JS_SetPropertyStr(ctx, data, key, JS_NewString(ctx, value));
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, global);
    JS_FreeCString(ctx, value);
    JS_FreeCString(ctx, key);

    storage_save(ctx);
    return JS_UNDEFINED;
}

// localStorage.removeItem(key)
static JSValue native_storage_removeItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");
    JSAtom atom = JS_NewAtom(ctx, key);
    JS_DeleteProperty(ctx, data, atom, 0);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, global);
    JS_FreeCString(ctx, key);

    storage_save(ctx);
    return JS_UNDEFINED;
}

// localStorage.clear()
static JSValue native_storage_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__localStorageData", JS_NewObject(ctx));
    JS_FreeValue(ctx, global);
    storage_save(ctx);
    return JS_UNDEFINED;
}

// localStorage.key(index) => string | null
static JSValue native_storage_key(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_NULL;
    uint32_t index = 0;
    JS_ToUint32(ctx, &index, argv[0]);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");

    // プロパティを列挙
    JSPropertyEnum *tab = nullptr;
    uint32_t len = 0;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, data, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        JSValue result = JS_NULL;
        if (index < len) {
            result = JS_AtomToString(ctx, tab[index].atom);
        }
        for (uint32_t i = 0; i < len; i++) {
            JS_FreeAtom(ctx, tab[i].atom);
        }
        js_free(ctx, tab);
        JS_FreeValue(ctx, data);
        JS_FreeValue(ctx, global);
        return result;
    }

    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, global);
    return JS_NULL;
}

// localStorage.length (getter)
static JSValue native_storage_length(JSContext *ctx, JSValueConst this_val) {
    (void)this_val;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue data = JS_GetPropertyStr(ctx, global, "__localStorageData");

    JSPropertyEnum *tab = nullptr;
    uint32_t len = 0;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, data, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        count = len;
        for (uint32_t i = 0; i < len; i++) {
            JS_FreeAtom(ctx, tab[i].atom);
        }
        js_free(ctx, tab);
    }

    JS_FreeValue(ctx, data);
    JS_FreeValue(ctx, global);
    return JS_NewUint32(ctx, count);
}

static void storage_register(JSContext *ctx) {
    // ファイルからデータ復元
    storage_load(ctx);

    // localStorage オブジェクト作成
    JSValue ls = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ls, "getItem", JS_NewCFunction(ctx, native_storage_getItem, "getItem", 1));
    JS_SetPropertyStr(ctx, ls, "setItem", JS_NewCFunction(ctx, native_storage_setItem, "setItem", 2));
    JS_SetPropertyStr(ctx, ls, "removeItem", JS_NewCFunction(ctx, native_storage_removeItem, "removeItem", 1));
    JS_SetPropertyStr(ctx, ls, "clear", JS_NewCFunction(ctx, native_storage_clear, "clear", 0));
    JS_SetPropertyStr(ctx, ls, "key", JS_NewCFunction(ctx, native_storage_key, "key", 1));

    // length を getter プロパティとして定義
    JSAtom lengthAtom = JS_NewAtom(ctx, "length");
    JSValue getter = JS_NewCFunction2(ctx, (JSCFunction*)native_storage_length, "length", 0, JS_CFUNC_getter, 0);
    JS_DefinePropertyGetSet(ctx, ls, lengthAtom, getter, JS_UNDEFINED, JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, lengthAtom);

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "localStorage", ls);
    JS_FreeValue(ctx, global);
}

// ============================================================
// JsEngine クラス実装
// ============================================================

JsEngine* JsEngine::instance_ = nullptr;

JsEngine::JsEngine() : rt_(nullptr), ctx_(nullptr) {
    instance_ = this;
}

JsEngine::~JsEngine() {
    done();
    instance_ = nullptr;
}

void JsEngine::setBasePath(const char *path) {
    basePath_ = path;
    // 末尾に / がなければ追加
    if (!basePath_.empty() && basePath_.back() != '/' && basePath_.back() != '\\') {
        basePath_ += '/';
    }
}

std::string JsEngine::resolvePath(const char *path) const {
    if (!path || path[0] == '\0') return basePath_;
    // 絶対パスならそのまま返す
    if (path[0] == '/' || path[0] == '\\') return path;
#ifdef _WIN32
    // ドライブレター付き (C:\... 等)
    if (path[1] == ':') return path;
#endif
    return basePath_ + path;
}

bool JsEngine::init(int argc, char **argv) {
    rt_ = JS_NewRuntime();
    if (!rt_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create QuickJS runtime");
        return false;
    }

    // ES Module ローダーを登録
    JS_SetModuleLoaderFunc(rt_, js_module_normalize, js_module_loader, this);

    ctx_ = JS_NewContext(rt_);
    if (!ctx_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create QuickJS context");
        JS_FreeRuntime(rt_);
        rt_ = nullptr;
        return false;
    }

    JSValue global = JS_GetGlobalObject(ctx_);

    // コマンドライン引数を JS グローバル __args に設定
    JSValue args = JS_NewArray(ctx_);
    if (argv) {
        for (int i = 0; i < argc; i++) {
            JS_SetPropertyUint32(ctx_, args, (uint32_t)i, JS_NewString(ctx_, argv[i]));
        }
    }
    JS_SetPropertyStr(ctx_, global, "__args", args);

    // console オブジェクト登録
    JSValue console = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, console, "log", JS_NewCFunction(ctx_, native_console_log, "log", 0));
    JS_SetPropertyStr(ctx_, console, "warn", JS_NewCFunction(ctx_, native_console_log, "warn", 0));
    JS_SetPropertyStr(ctx_, console, "info", JS_NewCFunction(ctx_, native_console_log, "info", 0));
    JS_SetPropertyStr(ctx_, console, "debug", JS_NewCFunction(ctx_, native_console_log, "debug", 0));
    JS_SetPropertyStr(ctx_, console, "error", JS_NewCFunction(ctx_, native_console_error, "error", 0));
    JS_SetPropertyStr(ctx_, global, "console", console);

    // loadScript / loadModule バインディング登録
    JS_SetPropertyStr(ctx_, global, "loadScript", JS_NewCFunction(ctx_, native_load_script, "loadScript", 1));
    JS_SetPropertyStr(ctx_, global, "loadModule", JS_NewCFunction(ctx_, native_loadModule, "loadModule", 1));

    // createImageBitmap 登録
    JS_SetPropertyStr(ctx_, global, "createImageBitmap", JS_NewCFunction(ctx_, native_createImageBitmap, "createImageBitmap", 1));

    // addEventListener / removeEventListener 登録
    JS_SetPropertyStr(ctx_, global, "addEventListener", JS_NewCFunction(ctx_, native_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(ctx_, global, "removeEventListener", JS_NewCFunction(ctx_, native_removeEventListener, "removeEventListener", 2));

    // イベントリスナー格納用オブジェクト
    JS_SetPropertyStr(ctx_, global, "__eventListeners", JS_NewObject(ctx_));

    // タイマー格納用
    JS_SetPropertyStr(ctx_, global, "__rafCallbacks", JS_NewArray(ctx_));

    // setTimeout / setInterval
    JS_SetPropertyStr(ctx_, global, "setTimeout", JS_NewCFunction(ctx_, native_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx_, global, "setInterval", JS_NewCFunction(ctx_, native_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx_, global, "clearTimeout", JS_NewCFunction(ctx_, native_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx_, global, "clearInterval", JS_NewCFunction(ctx_, native_clearTimeout, "clearInterval", 1));

    // requestAnimationFrame
    JS_SetPropertyStr(ctx_, global, "requestAnimationFrame", JS_NewCFunction(ctx_, native_requestAnimationFrame, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx_, global, "cancelAnimationFrame", JS_NewCFunction(ctx_, native_cancelAnimationFrame, "cancelAnimationFrame", 1));

    // performance.now()
    JSValue perf = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, perf, "now", JS_NewCFunction(ctx_, native_performance_now, "now", 0));
    JS_SetPropertyStr(ctx_, global, "performance", perf);

    JS_FreeValue(ctx_, global);

    // File System Access API 登録
    fs_register(ctx_);

    // localStorage 登録
    storage_register(ctx_);

    // WebGL バインディング登録
    dukwebgl_bind(ctx_);

    // Web Audio API バインディング登録
    webaudio_bind(ctx_);

    // Canvas 2D API バインディング登録
    canvas2d_bind(ctx_);

    SDL_Log("JsEngine initialized (QuickJS)");
    return true;
}

bool JsEngine::loadFile(const char *path) {
    if (!ctx_) return false;

    std::string resolved = resolvePath(path);
    size_t size = 0;
    char *source = load_file_sdl(resolved.c_str(), &size);
    if (!source) return false;

    JSValue result = JS_Eval(ctx_, source, size, path, JS_EVAL_TYPE_GLOBAL);
    SDL_free(source);

    if (JS_IsException(result)) {
        log_exception(ctx_, resolved.c_str());
        return false;
    }
    JS_FreeValue(ctx_, result);
    SDL_Log("Loaded JS: %s", resolved.c_str());
    return true;
}

void JsEngine::processTimers() {
    if (!ctx_) return;
    g_currentTime = (uint32_t)SDL_GetTicks();

    size_t i = 0;
    while (i < g_timers.size()) {
        if (g_timers[i].cancelled) {
            g_timers.erase(g_timers.begin() + (ptrdiff_t)i);
            continue;
        }
        if (g_currentTime >= g_timers[i].fireTime) {
            int id = g_timers[i].id;
            bool isInterval = g_timers[i].interval;
            uint32_t delay = g_timers[i].delay;
            JSValue cb = g_timers[i].callback;

            JSValue result = JS_Call(ctx_, cb, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(result)) {
                log_exception(ctx_, "Timer error");
            }
            JS_FreeValue(ctx_, result);

            if (isInterval) {
                g_timers[i].fireTime = g_currentTime + delay;
                i++;
            } else {
                JS_FreeValue(ctx_, cb);
                g_timers.erase(g_timers.begin() + (ptrdiff_t)i);
            }
        } else {
            i++;
        }
    }
}

void JsEngine::processRAF() {
    if (!ctx_) return;

    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue oldArr = JS_GetPropertyStr(ctx_, global, "__rafCallbacks");
    JSValue lenVal = JS_GetPropertyStr(ctx_, oldArr, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx_, &len, lenVal);
    JS_FreeValue(ctx_, lenVal);

    if (len == 0) {
        JS_FreeValue(ctx_, oldArr);
        JS_FreeValue(ctx_, global);
        return;
    }

    // 空の配列に入れ替え
    JS_SetPropertyStr(ctx_, global, "__rafCallbacks", JS_NewArray(ctx_));

    // コールバック実行
    double now = (double)SDL_GetTicks();
    JSValue nowVal = JS_NewFloat64(ctx_, now);
    for (uint32_t i = 0; i < len; i++) {
        JSValue cb = JS_GetPropertyUint32(ctx_, oldArr, i);
        if (JS_IsFunction(ctx_, cb)) {
            JSValue result = JS_Call(ctx_, cb, JS_UNDEFINED, 1, &nowVal);
            if (JS_IsException(result)) {
                log_exception(ctx_, "RAF error");
            }
            JS_FreeValue(ctx_, result);
        }
        JS_FreeValue(ctx_, cb);
    }
    JS_FreeValue(ctx_, nowVal);
    JS_FreeValue(ctx_, oldArr);
    JS_FreeValue(ctx_, global);
}

void JsEngine::update(uint32_t delta) {
    if (!ctx_) return;

    processTimers();
    processRAF();

    // グローバルに update 関数があれば呼び出す
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue updateFn = JS_GetPropertyStr(ctx_, global, "update");
    if (JS_IsFunction(ctx_, updateFn)) {
        JSValue arg = JS_NewUint32(ctx_, delta);
        JSValue result = JS_Call(ctx_, updateFn, JS_UNDEFINED, 1, &arg);
        if (JS_IsException(result)) {
            log_exception(ctx_, "JS update error");
        }
        JS_FreeValue(ctx_, result);
        JS_FreeValue(ctx_, arg);
    }
    JS_FreeValue(ctx_, updateFn);
    JS_FreeValue(ctx_, global);
}

void JsEngine::render() {
    if (!ctx_) return;

    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue renderFn = JS_GetPropertyStr(ctx_, global, "render");
    if (JS_IsFunction(ctx_, renderFn)) {
        JSValue result = JS_Call(ctx_, renderFn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result)) {
            log_exception(ctx_, "JS render error");
        }
        JS_FreeValue(ctx_, result);
    }
    JS_FreeValue(ctx_, renderFn);
    JS_FreeValue(ctx_, global);
}

// ============================================================
// イベントディスパッチ: __eventListeners[type] の全コールバックを呼ぶ
// ============================================================
void JsEngine::dispatchEvent(const char *type, JSValue event_obj) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue listeners = JS_GetPropertyStr(ctx_, global, "__eventListeners");
    JS_FreeValue(ctx_, global);

    if (JS_IsUndefined(listeners)) {
        JS_FreeValue(ctx_, listeners);
        JS_FreeValue(ctx_, event_obj);
        return;
    }

    JSValue arr = JS_GetPropertyStr(ctx_, listeners, type);
    JS_FreeValue(ctx_, listeners);

    if (JS_IsUndefined(arr)) {
        JS_FreeValue(ctx_, arr);
        JS_FreeValue(ctx_, event_obj);
        return;
    }

    JSValue lenVal = JS_GetPropertyStr(ctx_, arr, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx_, &len, lenVal);
    JS_FreeValue(ctx_, lenVal);

    for (uint32_t i = 0; i < len; i++) {
        JSValue cb = JS_GetPropertyUint32(ctx_, arr, i);
        if (JS_IsFunction(ctx_, cb)) {
            JSValue result = JS_Call(ctx_, cb, JS_UNDEFINED, 1, &event_obj);
            if (JS_IsException(result)) {
                log_exception(ctx_, type);
            }
            JS_FreeValue(ctx_, result);
        }
        JS_FreeValue(ctx_, cb);
    }
    JS_FreeValue(ctx_, arr);
    JS_FreeValue(ctx_, event_obj);
}

// ============================================================
// キーボードイベント構築
// ============================================================
JSValue JsEngine::pushKeyboardEvent(const SDL_Event *event, const char *type) {
    const SDL_KeyboardEvent &key = event->key;
    JSValue obj = JS_NewObject(ctx_);

    JS_SetPropertyStr(ctx_, obj, "type", JS_NewString(ctx_, type));

    // key: ブラウザ互換の文字列
    const char *jsKey = sdl_keycode_to_js_key(key.key);
    if (jsKey) {
        JS_SetPropertyStr(ctx_, obj, "key", JS_NewString(ctx_, jsKey));
    } else {
        const char *name = SDL_GetKeyName(key.key);
        JS_SetPropertyStr(ctx_, obj, "key", JS_NewString(ctx_, name ? name : "Unidentified"));
    }

    // code: 物理キー名
    const char *jsCode = sdl_scancode_to_js_code(key.scancode);
    JS_SetPropertyStr(ctx_, obj, "code", JS_NewString(ctx_, jsCode ? jsCode : ""));

    // keyCode (レガシー互換)
    JS_SetPropertyStr(ctx_, obj, "keyCode", JS_NewUint32(ctx_, (uint32_t)key.key));

    // 修飾キー
    JS_SetPropertyStr(ctx_, obj, "altKey", JS_NewBool(ctx_, (key.mod & SDL_KMOD_ALT) != 0));
    JS_SetPropertyStr(ctx_, obj, "ctrlKey", JS_NewBool(ctx_, (key.mod & SDL_KMOD_CTRL) != 0));
    JS_SetPropertyStr(ctx_, obj, "shiftKey", JS_NewBool(ctx_, (key.mod & SDL_KMOD_SHIFT) != 0));
    JS_SetPropertyStr(ctx_, obj, "metaKey", JS_NewBool(ctx_, (key.mod & SDL_KMOD_GUI) != 0));

    JS_SetPropertyStr(ctx_, obj, "repeat", JS_NewBool(ctx_, key.repeat));

    return obj;
}

// ============================================================
// マウスイベント構築
// ============================================================
JSValue JsEngine::pushMouseEvent(const SDL_Event *event, const char *type) {
    JSValue obj = JS_NewObject(ctx_);

    JS_SetPropertyStr(ctx_, obj, "type", JS_NewString(ctx_, type));

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const SDL_MouseMotionEvent &m = event->motion;
        JS_SetPropertyStr(ctx_, obj, "clientX", JS_NewFloat64(ctx_, m.x));
        JS_SetPropertyStr(ctx_, obj, "clientY", JS_NewFloat64(ctx_, m.y));
        JS_SetPropertyStr(ctx_, obj, "movementX", JS_NewFloat64(ctx_, m.xrel));
        JS_SetPropertyStr(ctx_, obj, "movementY", JS_NewFloat64(ctx_, m.yrel));
        // buttons ビットマスク（ブラウザ互換: 1=左, 2=右, 4=中）
        JS_SetPropertyStr(ctx_, obj, "buttons", JS_NewUint32(ctx_, (uint32_t)m.state));
        JS_SetPropertyStr(ctx_, obj, "button", JS_NewInt32(ctx_, 0));
    } else {
        const SDL_MouseButtonEvent &b = event->button;
        JS_SetPropertyStr(ctx_, obj, "clientX", JS_NewFloat64(ctx_, b.x));
        JS_SetPropertyStr(ctx_, obj, "clientY", JS_NewFloat64(ctx_, b.y));
        // SDL button: 1=左,2=中,3=右 → ブラウザ button: 0=左,1=中,2=右
        int jsButton = 0;
        switch (b.button) {
        case SDL_BUTTON_LEFT:   jsButton = 0; break;
        case SDL_BUTTON_MIDDLE: jsButton = 1; break;
        case SDL_BUTTON_RIGHT:  jsButton = 2; break;
        case SDL_BUTTON_X1:     jsButton = 3; break;
        case SDL_BUTTON_X2:     jsButton = 4; break;
        }
        JS_SetPropertyStr(ctx_, obj, "button", JS_NewInt32(ctx_, jsButton));
        JS_SetPropertyStr(ctx_, obj, "movementX", JS_NewFloat64(ctx_, 0));
        JS_SetPropertyStr(ctx_, obj, "movementY", JS_NewFloat64(ctx_, 0));
    }

    // 修飾キー（現在の状態）
    SDL_Keymod mod = SDL_GetModState();
    JS_SetPropertyStr(ctx_, obj, "altKey", JS_NewBool(ctx_, (mod & SDL_KMOD_ALT) != 0));
    JS_SetPropertyStr(ctx_, obj, "ctrlKey", JS_NewBool(ctx_, (mod & SDL_KMOD_CTRL) != 0));
    JS_SetPropertyStr(ctx_, obj, "shiftKey", JS_NewBool(ctx_, (mod & SDL_KMOD_SHIFT) != 0));
    JS_SetPropertyStr(ctx_, obj, "metaKey", JS_NewBool(ctx_, (mod & SDL_KMOD_GUI) != 0));

    return obj;
}

// ============================================================
// ホイールイベント構築
// ============================================================
JSValue JsEngine::pushWheelEvent(const SDL_Event *event) {
    const SDL_MouseWheelEvent &w = event->wheel;
    JSValue obj = JS_NewObject(ctx_);

    JS_SetPropertyStr(ctx_, obj, "type", JS_NewString(ctx_, "wheel"));

    // ブラウザの deltaX/deltaY はピクセル単位（SDL は行単位なので ×100 で近似）
    float dirMul = (w.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
    JS_SetPropertyStr(ctx_, obj, "deltaX", JS_NewFloat64(ctx_, w.x * dirMul * 100.0));
    JS_SetPropertyStr(ctx_, obj, "deltaY", JS_NewFloat64(ctx_, -w.y * dirMul * 100.0)); // ブラウザは下方向が正
    JS_SetPropertyStr(ctx_, obj, "deltaZ", JS_NewFloat64(ctx_, 0));

    JS_SetPropertyStr(ctx_, obj, "clientX", JS_NewFloat64(ctx_, w.mouse_x));
    JS_SetPropertyStr(ctx_, obj, "clientY", JS_NewFloat64(ctx_, w.mouse_y));

    // deltaMode: 0 = DOM_DELTA_PIXEL
    JS_SetPropertyStr(ctx_, obj, "deltaMode", JS_NewInt32(ctx_, 0));

    SDL_Keymod mod = SDL_GetModState();
    JS_SetPropertyStr(ctx_, obj, "altKey", JS_NewBool(ctx_, (mod & SDL_KMOD_ALT) != 0));
    JS_SetPropertyStr(ctx_, obj, "ctrlKey", JS_NewBool(ctx_, (mod & SDL_KMOD_CTRL) != 0));
    JS_SetPropertyStr(ctx_, obj, "shiftKey", JS_NewBool(ctx_, (mod & SDL_KMOD_SHIFT) != 0));
    JS_SetPropertyStr(ctx_, obj, "metaKey", JS_NewBool(ctx_, (mod & SDL_KMOD_GUI) != 0));

    return obj;
}

// ============================================================
// タッチイベント構築
// ============================================================
JSValue JsEngine::pushTouchEvent(const SDL_Event *event, const char *type) {
    const SDL_TouchFingerEvent &t = event->tfinger;
    JSValue obj = JS_NewObject(ctx_);

    JS_SetPropertyStr(ctx_, obj, "type", JS_NewString(ctx_, type));

    // タッチ座標をウィンドウピクセルに変換（SDL3 は正規化 0..1）
    int winW = 1, winH = 1;
    SDL_Window *window = SDL_GetWindowFromID(t.windowID);
    if (window) {
        SDL_GetWindowSize(window, &winW, &winH);
    }
    float px = t.x * winW;
    float py = t.y * winH;

    // touches 配列（簡易: 現在のタッチ1つ分）
    JSValue touchArr = JS_NewArray(ctx_);
    JSValue touch = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, touch, "identifier", JS_NewFloat64(ctx_, (double)t.fingerID));
    JS_SetPropertyStr(ctx_, touch, "clientX", JS_NewFloat64(ctx_, px));
    JS_SetPropertyStr(ctx_, touch, "clientY", JS_NewFloat64(ctx_, py));
    JS_SetPropertyStr(ctx_, touch, "pageX", JS_NewFloat64(ctx_, px));
    JS_SetPropertyStr(ctx_, touch, "pageY", JS_NewFloat64(ctx_, py));
    JS_SetPropertyStr(ctx_, touch, "force", JS_NewFloat64(ctx_, t.pressure));
    JS_SetPropertyUint32(ctx_, touchArr, 0, touch);

    // touchend では changedTouches に入れ、touches は空
    if (strcmp(type, "touchend") == 0 || strcmp(type, "touchcancel") == 0) {
        JS_SetPropertyStr(ctx_, obj, "changedTouches", touchArr);
        JS_SetPropertyStr(ctx_, obj, "touches", JS_NewArray(ctx_));
    } else {
        // touchstart / touchmove: touches と changedTouches 両方に同じ配列
        JS_SetPropertyStr(ctx_, obj, "changedTouches", JS_DupValue(ctx_, touchArr));
        JS_SetPropertyStr(ctx_, obj, "touches", touchArr);
    }

    return obj;
}

// ============================================================
// SDL イベント → JS イベントディスパッチ
// ============================================================
void JsEngine::handleEvent(const SDL_Event *event) {
    if (!ctx_) return;

    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
        dispatchEvent("keydown", pushKeyboardEvent(event, "keydown"));
        break;
    case SDL_EVENT_KEY_UP:
        dispatchEvent("keyup", pushKeyboardEvent(event, "keyup"));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        dispatchEvent("mousemove", pushMouseEvent(event, "mousemove"));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        dispatchEvent("mousedown", pushMouseEvent(event, "mousedown"));
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        dispatchEvent("mouseup", pushMouseEvent(event, "mouseup"));
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        dispatchEvent("wheel", pushWheelEvent(event));
        break;
    case SDL_EVENT_FINGER_DOWN:
        dispatchEvent("touchstart", pushTouchEvent(event, "touchstart"));
        break;
    case SDL_EVENT_FINGER_MOTION:
        dispatchEvent("touchmove", pushTouchEvent(event, "touchmove"));
        break;
    case SDL_EVENT_FINGER_UP:
        dispatchEvent("touchend", pushTouchEvent(event, "touchend"));
        break;
    case SDL_EVENT_FINGER_CANCELED:
        dispatchEvent("touchcancel", pushTouchEvent(event, "touchcancel"));
        break;
    default:
        break;
    }
}

void JsEngine::done() {
    if (!ctx_) return;

    // グローバルに done 関数があれば呼び出す
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue doneFn = JS_GetPropertyStr(ctx_, global, "done");
    if (JS_IsFunction(ctx_, doneFn)) {
        JSValue result = JS_Call(ctx_, doneFn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(result)) {
            log_exception(ctx_, "JS done error");
        }
        JS_FreeValue(ctx_, result);
    }
    JS_FreeValue(ctx_, doneFn);
    JS_FreeValue(ctx_, global);

    // 残っているタイマーのコールバックを解放
    for (auto &t : g_timers) {
        if (!t.cancelled) {
            JS_FreeValue(ctx_, t.callback);
        }
    }
    g_timers.clear();
    g_timerNextId = 1;

    JS_FreeContext(ctx_);
    ctx_ = nullptr;
    JS_FreeRuntime(rt_);
    rt_ = nullptr;
    SDL_Log("JsEngine destroyed");
}
