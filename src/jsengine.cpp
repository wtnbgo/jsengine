#include "jsengine.hpp"
#include "dukwebgl.h"
#include <duktape.h>
#include <SDL3/SDL.h>
#include <cstring>

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

// ベースパスからの相対パスを解決するヘルパー（static 関数からアクセス用）
static std::string resolve_path(const char *path) {
    JsEngine *engine = JsEngine::getInstance();
    if (engine) return engine->resolvePath(path);
    return path;
}

// JS から呼び出せる loadScript("path") バインディング
static duk_ret_t native_load_script(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);
    std::string resolved = resolve_path(path);
    size_t size = 0;
    char *source = load_file_sdl(resolved.c_str(), &size);
    if (!source) {
        return duk_error(ctx, DUK_ERR_ERROR, "Cannot load file: %s", resolved.c_str());
    }
    duk_push_lstring(ctx, source, size);
    SDL_free(source);
    // ファイル名付きでコンパイル・実行
    duk_push_string(ctx, path);
    if (duk_pcompile(ctx, DUK_COMPILE_EVAL) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS compile error (%s): %s", resolved.c_str(), duk_safe_to_string(ctx, -1));
        return duk_throw(ctx);
    }
    if (duk_pcall(ctx, 0) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS exec error (%s): %s", resolved.c_str(), duk_safe_to_string(ctx, -1));
        return duk_throw(ctx);
    }
    return 1; // 実行結果を返す
}

// ============================================================
// addEventListener / removeEventListener
// ============================================================
// イベントリスナーは duktape のグローバル隠しオブジェクト __eventListeners に格納
// __eventListeners[type] = [callback, callback, ...]

// addEventListener(type, callback)
static duk_ret_t native_addEventListener(duk_context *ctx) {
    const char *type = duk_require_string(ctx, 0);
    duk_require_function(ctx, 1);

    // __eventListeners を取得（なければ作成）
    if (!duk_get_global_string(ctx, "__eventListeners")) {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_global_string(ctx, "__eventListeners");
    }

    // __eventListeners[type] を取得（なければ配列作成）
    if (!duk_get_prop_string(ctx, -1, type)) {
        duk_pop(ctx);
        duk_push_array(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, type);
    }

    // 配列の末尾に callback を追加
    duk_get_prop_string(ctx, -1, "length");
    duk_idx_t len = (duk_idx_t)duk_to_uint(ctx, -1);
    duk_pop(ctx);

    duk_dup(ctx, 1); // callback
    duk_put_prop_index(ctx, -2, (duk_uarridx_t)len);

    duk_pop_2(ctx); // array, __eventListeners
    return 0;
}

// removeEventListener(type, callback)
static duk_ret_t native_removeEventListener(duk_context *ctx) {
    const char *type = duk_require_string(ctx, 0);
    duk_require_function(ctx, 1);

    if (!duk_get_global_string(ctx, "__eventListeners")) {
        duk_pop(ctx);
        return 0;
    }
    if (!duk_get_prop_string(ctx, -1, type)) {
        duk_pop_2(ctx);
        return 0;
    }

    duk_get_prop_string(ctx, -1, "length");
    duk_idx_t len = (duk_idx_t)duk_to_uint(ctx, -1);
    duk_pop(ctx);

    for (duk_idx_t i = 0; i < len; i++) {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        if (duk_strict_equals(ctx, -1, 1)) {
            duk_pop(ctx);
            // 配列要素を削除（後ろを詰める）
            for (duk_idx_t j = i; j < len - 1; j++) {
                duk_get_prop_index(ctx, -1, (duk_uarridx_t)(j + 1));
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)j);
            }
            duk_del_prop_index(ctx, -1, (duk_uarridx_t)(len - 1));
            // length を更新
            duk_push_uint(ctx, (duk_uint_t)(len - 1));
            duk_put_prop_string(ctx, -2, "length");
            break;
        }
        duk_pop(ctx);
    }

    duk_pop_2(ctx); // array, __eventListeners
    return 0;
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
//
// JS 側:
//   var root = await navigator.storage.getDirectory()   に相当する
//   fs.getDirectoryHandle(path, {create:true})
//   fs.getFileHandle(path, {create:true})
//   handle.getFile() => { name, size, type, text(), arrayBuffer() }
//   handle.createWritable() => { write(data), close() }
//   dirHandle.entries() => [[name, handle], ...]
//   dirHandle.removeEntry(name, {recursive:true})
//   fs.exists(path), fs.remove(path), fs.mkdir(path), fs.stat(path)
// ============================================================

// --- ユーティリティ: options から create フラグ取得 ---
static bool get_create_option(duk_context *ctx, duk_idx_t opt_idx) {
    if (duk_is_object(ctx, opt_idx)) {
        duk_get_prop_string(ctx, opt_idx, "create");
        bool create = duk_to_boolean(ctx, -1) != 0;
        duk_pop(ctx);
        return create;
    }
    return false;
}

// --- ユーティリティ: options から recursive フラグ取得 ---
static bool get_recursive_option(duk_context *ctx, duk_idx_t opt_idx) {
    if (duk_is_object(ctx, opt_idx)) {
        duk_get_prop_string(ctx, opt_idx, "recursive");
        bool r = duk_to_boolean(ctx, -1) != 0;
        duk_pop(ctx);
        return r;
    }
    return false;
}

// --- FileSystemFileHandle ---

// handle._path にファイルパスを格納
static void push_file_handle(duk_context *ctx, const char *path);
static void push_directory_handle(duk_context *ctx, const char *path);

// getFile() => { name, size, text(), arrayBuffer() }
static duk_ret_t filehandle_getFile(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *path = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return duk_error(ctx, DUK_ERR_ERROR, "File not found: %s", path);
    }

    // ファイル名抽出
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') name = p + 1;
        p++;
    }

    duk_idx_t obj = duk_push_object(ctx);

    duk_push_string(ctx, name);
    duk_put_prop_string(ctx, obj, "name");
    duk_push_number(ctx, (double)info.size);
    duk_put_prop_string(ctx, obj, "size");
    duk_push_string(ctx, "");
    duk_put_prop_string(ctx, obj, "type");

    // ファイルパスを隠しプロパティとしてコピー
    duk_push_string(ctx, path);
    duk_put_prop_string(ctx, obj, "_path");

    // text() メソッド
    duk_push_c_function(ctx, [](duk_context *c) -> duk_ret_t {
        duk_push_this(c);
        duk_get_prop_string(c, -1, "_path");
        const char *p = duk_get_string(c, -1);
        duk_pop_2(c);
        size_t sz = 0;
        void *data = SDL_LoadFile(p, &sz);
        if (!data) return duk_error(c, DUK_ERR_ERROR, "Cannot read file: %s", p);
        duk_push_lstring(c, (const char*)data, sz);
        SDL_free(data);
        return 1;
    }, 0);
    duk_put_prop_string(ctx, obj, "text");

    // arrayBuffer() メソッド
    duk_push_c_function(ctx, [](duk_context *c) -> duk_ret_t {
        duk_push_this(c);
        duk_get_prop_string(c, -1, "_path");
        const char *p = duk_get_string(c, -1);
        duk_pop_2(c);
        size_t sz = 0;
        void *data = SDL_LoadFile(p, &sz);
        if (!data) return duk_error(c, DUK_ERR_ERROR, "Cannot read file: %s", p);
        void *buf = duk_push_buffer(c, sz, 0);
        memcpy(buf, data, sz);
        SDL_free(data);
        return 1;
    }, 0);
    duk_put_prop_string(ctx, obj, "arrayBuffer");

    return 1;
}

// createWritable() => { _chunks: [], write(data), close() }
static duk_ret_t filehandle_createWritable(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *path = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    duk_idx_t obj = duk_push_object(ctx);

    duk_push_string(ctx, path);
    duk_put_prop_string(ctx, obj, "_path");

    // _chunks 配列
    duk_push_array(ctx);
    duk_put_prop_string(ctx, obj, "_chunks");

    // write(data) — 文字列またはバッファを _chunks に追加
    duk_push_c_function(ctx, [](duk_context *c) -> duk_ret_t {
        duk_push_this(c);
        duk_get_prop_string(c, -1, "_chunks");
        duk_get_prop_string(c, -1, "length");
        duk_uarridx_t len = (duk_uarridx_t)duk_to_uint(c, -1);
        duk_pop(c);
        duk_dup(c, 0); // data
        duk_put_prop_index(c, -2, len);
        duk_pop_2(c); // _chunks, this
        return 0;
    }, 1);
    duk_put_prop_string(ctx, obj, "write");

    // close() — _chunks を結合してファイルに書き出す
    duk_push_c_function(ctx, [](duk_context *c) -> duk_ret_t {
        duk_push_this(c);
        duk_get_prop_string(c, -1, "_path");
        const char *p = duk_get_string(c, -1);
        duk_pop(c);
        duk_get_prop_string(c, -1, "_chunks");

        duk_get_prop_string(c, -1, "length");
        duk_uint_t len = duk_to_uint(c, -1);
        duk_pop(c);

        // 全チャンクのサイズを計算
        size_t total = 0;
        for (duk_uint_t i = 0; i < len; i++) {
            duk_get_prop_index(c, -1, i);
            if (duk_is_buffer_data(c, -1)) {
                duk_size_t sz;
                duk_get_buffer_data(c, -1, &sz);
                total += sz;
            } else {
                duk_size_t sz;
                duk_to_lstring(c, -1, &sz);
                total += sz;
            }
            duk_pop(c);
        }

        // バッファに結合
        char *buf = (char*)SDL_malloc(total);
        if (!buf) {
            duk_pop_2(c);
            return duk_error(c, DUK_ERR_ERROR, "Out of memory");
        }
        size_t offset = 0;
        for (duk_uint_t i = 0; i < len; i++) {
            duk_get_prop_index(c, -1, i);
            if (duk_is_buffer_data(c, -1)) {
                duk_size_t sz;
                void *data = duk_get_buffer_data(c, -1, &sz);
                memcpy(buf + offset, data, sz);
                offset += sz;
            } else {
                duk_size_t sz;
                const char *s = duk_to_lstring(c, -1, &sz);
                memcpy(buf + offset, s, sz);
                offset += sz;
            }
            duk_pop(c);
        }

        bool ok = SDL_SaveFile(p, buf, total);
        SDL_free(buf);
        duk_pop_2(c); // _chunks, this

        if (!ok) {
            return duk_error(c, DUK_ERR_ERROR, "Failed to write file: %s", p);
        }
        return 0;
    }, 0);
    duk_put_prop_string(ctx, obj, "close");

    return 1;
}

static void push_file_handle(duk_context *ctx, const char *path) {
    duk_idx_t obj = duk_push_object(ctx);

    duk_push_string(ctx, path);
    duk_put_prop_string(ctx, obj, "_path");
    duk_push_string(ctx, "file");
    duk_put_prop_string(ctx, obj, "kind");

    // ファイル名抽出
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') name = p + 1;
        p++;
    }
    duk_push_string(ctx, name);
    duk_put_prop_string(ctx, obj, "name");

    duk_push_c_function(ctx, filehandle_getFile, 0);
    duk_put_prop_string(ctx, obj, "getFile");
    duk_push_c_function(ctx, filehandle_createWritable, 0);
    duk_put_prop_string(ctx, obj, "createWritable");
}

// --- FileSystemDirectoryHandle ---

// entries() => [[name, handle], ...]
struct EnumCtx {
    duk_context *ctx;
    duk_idx_t arr_idx;
    duk_uint_t count;
    const char *dirname;
};

static SDL_EnumerationResult dir_enum_callback(void *userdata, const char *dirname, const char *fname) {
    EnumCtx *ec = (EnumCtx*)userdata;

    // エントリ配列 [name, handle]
    duk_push_array(ec->ctx);

    duk_push_string(ec->ctx, fname);
    duk_put_prop_index(ec->ctx, -2, 0);

    // フルパス構築
    size_t len = strlen(dirname) + strlen(fname) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dirname, fname);

    SDL_PathInfo info;
    if (SDL_GetPathInfo(fullpath, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        push_directory_handle(ec->ctx, fullpath);
    } else {
        push_file_handle(ec->ctx, fullpath);
    }
    SDL_free(fullpath);
    duk_put_prop_index(ec->ctx, -2, 1);

    // 外側の配列に追加
    duk_put_prop_index(ec->ctx, ec->arr_idx, ec->count);
    ec->count++;

    return SDL_ENUM_CONTINUE;
}

static duk_ret_t dirhandle_entries(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *path = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    duk_idx_t arr = duk_push_array(ctx);
    EnumCtx ec = { ctx, arr, 0, path };
    SDL_EnumerateDirectory(path, dir_enum_callback, &ec);
    return 1;
}

// getFileHandle(name, options)
static duk_ret_t dirhandle_getFileHandle(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *dir = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    const char *name = duk_require_string(ctx, 0);
    bool create = get_create_option(ctx, 1);

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(fullpath, &info) || info.type == SDL_PATHTYPE_NONE) {
        if (create) {
            // 空ファイル作成
            SDL_SaveFile(fullpath, "", 0);
        } else {
            SDL_free(fullpath);
            return duk_error(ctx, DUK_ERR_ERROR, "File not found: %s/%s", dir, name);
        }
    }

    push_file_handle(ctx, fullpath);
    SDL_free(fullpath);
    return 1;
}

// getDirectoryHandle(name, options)
static duk_ret_t dirhandle_getDirectoryHandle(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *dir = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    const char *name = duk_require_string(ctx, 0);
    bool create = get_create_option(ctx, 1);

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(fullpath, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        if (create) {
            SDL_CreateDirectory(fullpath);
        } else {
            SDL_free(fullpath);
            return duk_error(ctx, DUK_ERR_ERROR, "Directory not found: %s/%s", dir, name);
        }
    }

    push_directory_handle(ctx, fullpath);
    SDL_free(fullpath);
    return 1;
}

// removeEntry(name, options)
static duk_ret_t dirhandle_removeEntry(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_path");
    const char *dir = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    const char *name = duk_require_string(ctx, 0);
    // recursive オプション（今は SDL_RemovePath のみ）
    // bool recursive = get_recursive_option(ctx, 1);

    size_t len = strlen(dir) + strlen(name) + 2;
    char *fullpath = (char*)SDL_malloc(len);
    snprintf(fullpath, len, "%s/%s", dir, name);

    if (!SDL_RemovePath(fullpath)) {
        SDL_free(fullpath);
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to remove: %s/%s", dir, name);
    }
    SDL_free(fullpath);
    return 0;
}

static void push_directory_handle(duk_context *ctx, const char *path) {
    duk_idx_t obj = duk_push_object(ctx);

    duk_push_string(ctx, path);
    duk_put_prop_string(ctx, obj, "_path");
    duk_push_string(ctx, "directory");
    duk_put_prop_string(ctx, obj, "kind");

    // ディレクトリ名抽出
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') name = p + 1;
        p++;
    }
    duk_push_string(ctx, name);
    duk_put_prop_string(ctx, obj, "name");

    duk_push_c_function(ctx, dirhandle_entries, 0);
    duk_put_prop_string(ctx, obj, "entries");
    duk_push_c_function(ctx, dirhandle_getFileHandle, 2);
    duk_put_prop_string(ctx, obj, "getFileHandle");
    duk_push_c_function(ctx, dirhandle_getDirectoryHandle, 2);
    duk_put_prop_string(ctx, obj, "getDirectoryHandle");
    duk_push_c_function(ctx, dirhandle_removeEntry, 2);
    duk_put_prop_string(ctx, obj, "removeEntry");
}

// --- グローバル fs オブジェクト ---

// fs.getFileHandle(path, options)
static duk_ret_t fs_getFileHandle(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    bool create = get_create_option(ctx, 1);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type == SDL_PATHTYPE_NONE) {
        if (create) {
            SDL_SaveFile(rpath.c_str(), "", 0);
        } else {
            return duk_error(ctx, DUK_ERR_ERROR, "File not found: %s", rpath.c_str());
        }
    }
    push_file_handle(ctx, rpath.c_str());
    return 1;
}

// fs.getDirectoryHandle(path, options)
static duk_ret_t fs_getDirectoryHandle(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    bool create = get_create_option(ctx, 1);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        if (create) {
            SDL_CreateDirectory(rpath.c_str());
        } else {
            return duk_error(ctx, DUK_ERR_ERROR, "Directory not found: %s", rpath.c_str());
        }
    }
    push_directory_handle(ctx, rpath.c_str());
    return 1;
}

// fs.exists(path) => boolean
static duk_ret_t fs_exists(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    SDL_PathInfo info;
    duk_push_boolean(ctx, SDL_GetPathInfo(rpath.c_str(), &info) && info.type != SDL_PATHTYPE_NONE);
    return 1;
}

// fs.stat(path) => { type, size, createTime, modifyTime, accessTime } | null
static duk_ret_t fs_stat(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(rpath.c_str(), &info) || info.type == SDL_PATHTYPE_NONE) {
        duk_push_null(ctx);
        return 1;
    }
    duk_idx_t obj = duk_push_object(ctx);
    const char *typeStr = "other";
    if (info.type == SDL_PATHTYPE_FILE) typeStr = "file";
    else if (info.type == SDL_PATHTYPE_DIRECTORY) typeStr = "directory";
    duk_push_string(ctx, typeStr);
    duk_put_prop_string(ctx, obj, "type");
    duk_push_number(ctx, (double)info.size);
    duk_put_prop_string(ctx, obj, "size");
    duk_push_number(ctx, (double)info.create_time);
    duk_put_prop_string(ctx, obj, "createTime");
    duk_push_number(ctx, (double)info.modify_time);
    duk_put_prop_string(ctx, obj, "modifyTime");
    duk_push_number(ctx, (double)info.access_time);
    duk_put_prop_string(ctx, obj, "accessTime");
    return 1;
}

// fs.mkdir(path)
static duk_ret_t fs_mkdir(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    if (!SDL_CreateDirectory(rpath.c_str())) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to create directory: %s", rpath.c_str());
    }
    return 0;
}

// fs.remove(path)
static duk_ret_t fs_remove(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    if (!SDL_RemovePath(rpath.c_str())) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to remove: %s", rpath.c_str());
    }
    return 0;
}

// fs.rename(oldPath, newPath)
static duk_ret_t fs_rename(duk_context *ctx) {
    std::string rold = resolve_path(duk_require_string(ctx, 0));
    std::string rnew = resolve_path(duk_require_string(ctx, 1));
    if (!SDL_RenamePath(rold.c_str(), rnew.c_str())) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to rename: %s -> %s", rold.c_str(), rnew.c_str());
    }
    return 0;
}

// fs.readText(path) => string
static duk_ret_t fs_readText(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    size_t sz = 0;
    void *data = SDL_LoadFile(rpath.c_str(), &sz);
    if (!data) return duk_error(ctx, DUK_ERR_ERROR, "Cannot read file: %s", rpath.c_str());
    duk_push_lstring(ctx, (const char*)data, sz);
    SDL_free(data);
    return 1;
}

// fs.writeText(path, text)
static duk_ret_t fs_writeText(duk_context *ctx) {
    std::string rpath = resolve_path(duk_require_string(ctx, 0));
    duk_size_t len;
    const char *text = duk_require_lstring(ctx, 1, &len);
    if (!SDL_SaveFile(rpath.c_str(), text, len)) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to write file: %s", rpath.c_str());
    }
    return 0;
}

static void fs_register(duk_context *ctx) {
    duk_idx_t obj = duk_push_object(ctx);

    duk_push_c_function(ctx, fs_getFileHandle, 2);
    duk_put_prop_string(ctx, obj, "getFileHandle");
    duk_push_c_function(ctx, fs_getDirectoryHandle, 2);
    duk_put_prop_string(ctx, obj, "getDirectoryHandle");
    duk_push_c_function(ctx, fs_exists, 1);
    duk_put_prop_string(ctx, obj, "exists");
    duk_push_c_function(ctx, fs_stat, 1);
    duk_put_prop_string(ctx, obj, "stat");
    duk_push_c_function(ctx, fs_mkdir, 1);
    duk_put_prop_string(ctx, obj, "mkdir");
    duk_push_c_function(ctx, fs_remove, 1);
    duk_put_prop_string(ctx, obj, "remove");
    duk_push_c_function(ctx, fs_rename, 2);
    duk_put_prop_string(ctx, obj, "rename");
    duk_push_c_function(ctx, fs_readText, 1);
    duk_put_prop_string(ctx, obj, "readText");
    duk_push_c_function(ctx, fs_writeText, 2);
    duk_put_prop_string(ctx, obj, "writeText");

    // fs.basePath を設定
    JsEngine *engine = JsEngine::getInstance();
    if (engine) {
        duk_push_string(ctx, engine->getBasePath().c_str());
        duk_put_prop_string(ctx, obj, "basePath");
    }

    duk_put_global_string(ctx, "fs");
}

// ============================================================
// localStorage (Web Storage API)
// データは SDL_GetPrefPath 配下の localStorage.json に保存
// ============================================================

// localStorage のデータは duktape のグローバル隠しオブジェクト __localStorageData に保持
// 変更時に JSON ファイルに書き出す

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
static void storage_save(duk_context *ctx) {
    char *path = get_storage_path();
    if (!path) return;

    duk_get_global_string(ctx, "JSON");
    duk_get_prop_string(ctx, -1, "stringify");
    duk_get_global_string(ctx, "__localStorageData");
    duk_call(ctx, 1);
    const char *json = duk_get_string(ctx, -1);

    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (io) {
        SDL_WriteIO(io, json, strlen(json));
        SDL_CloseIO(io);
    }
    duk_pop_2(ctx); // json string, JSON object
    SDL_free(path);
}

// JSON ファイルから __localStorageData を復元
static void storage_load(duk_context *ctx) {
    char *path = get_storage_path();
    if (!path) {
        duk_push_object(ctx);
        duk_put_global_string(ctx, "__localStorageData");
        return;
    }

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    SDL_free(path);

    if (data && size > 0) {
        duk_get_global_string(ctx, "JSON");
        duk_get_prop_string(ctx, -1, "parse");
        duk_push_lstring(ctx, (const char*)data, size);
        if (duk_pcall(ctx, 1) == 0 && duk_is_object(ctx, -1)) {
            duk_put_global_string(ctx, "__localStorageData");
            duk_pop(ctx); // JSON object
        } else {
            duk_pop_2(ctx); // error/result, JSON object
            duk_push_object(ctx);
            duk_put_global_string(ctx, "__localStorageData");
        }
    } else {
        duk_push_object(ctx);
        duk_put_global_string(ctx, "__localStorageData");
    }
    if (data) SDL_free(data);
}

// localStorage.getItem(key) => string | null
static duk_ret_t native_storage_getItem(duk_context *ctx) {
    const char *key = duk_require_string(ctx, 0);
    duk_get_global_string(ctx, "__localStorageData");
    if (duk_get_prop_string(ctx, -1, key)) {
        // 値あり — string として返す
        duk_remove(ctx, -2); // __localStorageData
        return 1;
    }
    duk_pop_2(ctx); // undefined, __localStorageData
    duk_push_null(ctx);
    return 1;
}

// localStorage.setItem(key, value)
static duk_ret_t native_storage_setItem(duk_context *ctx) {
    const char *key = duk_require_string(ctx, 0);
    const char *value = duk_to_string(ctx, 1);
    duk_get_global_string(ctx, "__localStorageData");
    duk_push_string(ctx, value);
    duk_put_prop_string(ctx, -2, key);
    duk_pop(ctx); // __localStorageData
    storage_save(ctx);
    return 0;
}

// localStorage.removeItem(key)
static duk_ret_t native_storage_removeItem(duk_context *ctx) {
    const char *key = duk_require_string(ctx, 0);
    duk_get_global_string(ctx, "__localStorageData");
    duk_del_prop_string(ctx, -1, key);
    duk_pop(ctx);
    storage_save(ctx);
    return 0;
}

// localStorage.clear()
static duk_ret_t native_storage_clear(duk_context *ctx) {
    duk_push_object(ctx);
    duk_put_global_string(ctx, "__localStorageData");
    storage_save(ctx);
    return 0;
}

// localStorage.key(index) => string | null
static duk_ret_t native_storage_key(duk_context *ctx) {
    duk_uint_t index = duk_require_uint(ctx, 0);
    duk_get_global_string(ctx, "__localStorageData");
    duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
    duk_uint_t i = 0;
    while (duk_next(ctx, -1, 0)) {
        if (i == index) {
            // key はスタックトップ
            duk_remove(ctx, -2); // enum
            duk_remove(ctx, -2); // __localStorageData
            return 1;
        }
        duk_pop(ctx); // key
        i++;
    }
    duk_pop_2(ctx); // enum, __localStorageData
    duk_push_null(ctx);
    return 1;
}

// localStorage.length (getter)
static duk_ret_t native_storage_length(duk_context *ctx) {
    duk_get_global_string(ctx, "__localStorageData");
    duk_enum(ctx, -1, DUK_ENUM_OWN_PROPERTIES_ONLY);
    duk_uint_t count = 0;
    while (duk_next(ctx, -1, 0)) {
        duk_pop(ctx);
        count++;
    }
    duk_pop_2(ctx); // enum, __localStorageData
    duk_push_uint(ctx, count);
    return 1;
}

static void storage_register(duk_context *ctx) {
    // ファイルからデータ復元
    storage_load(ctx);

    // localStorage オブジェクト作成
    duk_push_object(ctx);

    duk_push_c_function(ctx, native_storage_getItem, 1);
    duk_put_prop_string(ctx, -2, "getItem");
    duk_push_c_function(ctx, native_storage_setItem, 2);
    duk_put_prop_string(ctx, -2, "setItem");
    duk_push_c_function(ctx, native_storage_removeItem, 1);
    duk_put_prop_string(ctx, -2, "removeItem");
    duk_push_c_function(ctx, native_storage_clear, 0);
    duk_put_prop_string(ctx, -2, "clear");
    duk_push_c_function(ctx, native_storage_key, 1);
    duk_put_prop_string(ctx, -2, "key");

    // length を getter プロパティとして定義
    duk_push_string(ctx, "length");
    duk_push_c_function(ctx, native_storage_length, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_ENUMERABLE);

    duk_put_global_string(ctx, "localStorage");
}

// duktape fatal error handler
static void fatal_handler(void *udata, const char *msg) {
    (void)udata;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Duktape fatal error: %s", msg ? msg : "unknown");
}

JsEngine* JsEngine::instance_ = nullptr;

JsEngine::JsEngine() : ctx_(nullptr) {
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

    // addEventListener / removeEventListener 登録
    duk_push_c_function(ctx_, native_addEventListener, 2);
    duk_put_global_string(ctx_, "addEventListener");
    duk_push_c_function(ctx_, native_removeEventListener, 2);
    duk_put_global_string(ctx_, "removeEventListener");

    // イベントリスナー格納用オブジェクト
    duk_push_object(ctx_);
    duk_put_global_string(ctx_, "__eventListeners");

    // File System Access API 登録
    fs_register(ctx_);

    // localStorage 登録
    storage_register(ctx_);

    // WebGL バインディング登録
    dukwebgl_bind(ctx_);

    SDL_Log("JsEngine initialized");
    return true;
}

bool JsEngine::loadFile(const char *path) {
    if (!ctx_) return false;

    std::string resolved = resolvePath(path);
    size_t size = 0;
    char *source = load_file_sdl(resolved.c_str(), &size);
    if (!source) return false;

    duk_push_lstring(ctx_, source, size);
    SDL_free(source);
    duk_push_string(ctx_, path);
    if (duk_pcompile(ctx_, DUK_COMPILE_EVAL) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS compile error (%s): %s", resolved.c_str(), duk_safe_to_string(ctx_, -1));
        duk_pop(ctx_);
        return false;
    }
    if (duk_pcall(ctx_, 0) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "JS exec error (%s): %s", resolved.c_str(), duk_safe_to_string(ctx_, -1));
        duk_pop(ctx_);
        return false;
    }
    duk_pop(ctx_); // 実行結果を捨てる
    SDL_Log("Loaded JS: %s", resolved.c_str());
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

// ============================================================
// イベントディスパッチ: __eventListeners[type] の全コールバックを呼ぶ
// スタックトップにイベントオブジェクトが積まれた状態で呼ぶ
// ============================================================
void JsEngine::dispatchEvent(const char *type) {
    // stack: [ event_obj ]
    if (!duk_get_global_string(ctx_, "__eventListeners")) {
        duk_pop_2(ctx_); // undefined, event_obj
        return;
    }
    if (!duk_get_prop_string(ctx_, -1, type)) {
        duk_pop_3(ctx_); // undefined, __eventListeners, event_obj
        return;
    }
    // stack: [ event_obj, __eventListeners, array ]
    duk_get_prop_string(ctx_, -1, "length");
    duk_idx_t len = (duk_idx_t)duk_to_uint(ctx_, -1);
    duk_pop(ctx_);

    for (duk_idx_t i = 0; i < len; i++) {
        duk_get_prop_index(ctx_, -1, (duk_uarridx_t)i);
        if (duk_is_function(ctx_, -1)) {
            duk_dup(ctx_, -4); // event_obj をコピー
            if (duk_pcall(ctx_, 1) != 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "JS %s handler error: %s", type, duk_safe_to_string(ctx_, -1));
            }
        }
        duk_pop(ctx_); // 戻り値 or error
    }
    duk_pop_3(ctx_); // array, __eventListeners, event_obj
}

// ============================================================
// キーボードイベント構築
// ============================================================
void JsEngine::pushKeyboardEvent(const SDL_Event *event, const char *type) {
    const SDL_KeyboardEvent &key = event->key;
    duk_idx_t obj = duk_push_object(ctx_);

    duk_push_string(ctx_, type);
    duk_put_prop_string(ctx_, obj, "type");

    // key: ブラウザ互換の文字列
    const char *jsKey = sdl_keycode_to_js_key(key.key);
    if (jsKey) {
        duk_push_string(ctx_, jsKey);
    } else {
        const char *name = SDL_GetKeyName(key.key);
        duk_push_string(ctx_, name ? name : "Unidentified");
    }
    duk_put_prop_string(ctx_, obj, "key");

    // code: 物理キー名
    const char *jsCode = sdl_scancode_to_js_code(key.scancode);
    duk_push_string(ctx_, jsCode ? jsCode : "");
    duk_put_prop_string(ctx_, obj, "code");

    // keyCode (レガシー互換)
    duk_push_uint(ctx_, (duk_uint_t)key.key);
    duk_put_prop_string(ctx_, obj, "keyCode");

    // 修飾キー
    duk_push_boolean(ctx_, (key.mod & SDL_KMOD_ALT) != 0);
    duk_put_prop_string(ctx_, obj, "altKey");
    duk_push_boolean(ctx_, (key.mod & SDL_KMOD_CTRL) != 0);
    duk_put_prop_string(ctx_, obj, "ctrlKey");
    duk_push_boolean(ctx_, (key.mod & SDL_KMOD_SHIFT) != 0);
    duk_put_prop_string(ctx_, obj, "shiftKey");
    duk_push_boolean(ctx_, (key.mod & SDL_KMOD_GUI) != 0);
    duk_put_prop_string(ctx_, obj, "metaKey");

    duk_push_boolean(ctx_, key.repeat);
    duk_put_prop_string(ctx_, obj, "repeat");

    // event_obj がスタックトップに残っている
}

// ============================================================
// マウスイベント構築
// ============================================================
void JsEngine::pushMouseEvent(const SDL_Event *event, const char *type) {
    duk_idx_t obj = duk_push_object(ctx_);

    duk_push_string(ctx_, type);
    duk_put_prop_string(ctx_, obj, "type");

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        const SDL_MouseMotionEvent &m = event->motion;
        duk_push_number(ctx_, m.x);
        duk_put_prop_string(ctx_, obj, "clientX");
        duk_push_number(ctx_, m.y);
        duk_put_prop_string(ctx_, obj, "clientY");
        duk_push_number(ctx_, m.xrel);
        duk_put_prop_string(ctx_, obj, "movementX");
        duk_push_number(ctx_, m.yrel);
        duk_put_prop_string(ctx_, obj, "movementY");
        // buttons ビットマスク（ブラウザ互換: 1=左, 2=右, 4=中）
        duk_push_uint(ctx_, (duk_uint_t)m.state);
        duk_put_prop_string(ctx_, obj, "buttons");
        duk_push_int(ctx_, 0);
        duk_put_prop_string(ctx_, obj, "button");
    } else {
        const SDL_MouseButtonEvent &b = event->button;
        duk_push_number(ctx_, b.x);
        duk_put_prop_string(ctx_, obj, "clientX");
        duk_push_number(ctx_, b.y);
        duk_put_prop_string(ctx_, obj, "clientY");
        // SDL button: 1=左,2=中,3=右 → ブラウザ button: 0=左,1=中,2=右
        int jsButton = 0;
        switch (b.button) {
        case SDL_BUTTON_LEFT:   jsButton = 0; break;
        case SDL_BUTTON_MIDDLE: jsButton = 1; break;
        case SDL_BUTTON_RIGHT:  jsButton = 2; break;
        case SDL_BUTTON_X1:     jsButton = 3; break;
        case SDL_BUTTON_X2:     jsButton = 4; break;
        }
        duk_push_int(ctx_, jsButton);
        duk_put_prop_string(ctx_, obj, "button");
        duk_push_number(ctx_, 0);
        duk_put_prop_string(ctx_, obj, "movementX");
        duk_push_number(ctx_, 0);
        duk_put_prop_string(ctx_, obj, "movementY");
    }

    // 修飾キー（現在の状態）
    SDL_Keymod mod = SDL_GetModState();
    duk_push_boolean(ctx_, (mod & SDL_KMOD_ALT) != 0);
    duk_put_prop_string(ctx_, obj, "altKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_CTRL) != 0);
    duk_put_prop_string(ctx_, obj, "ctrlKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_SHIFT) != 0);
    duk_put_prop_string(ctx_, obj, "shiftKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_GUI) != 0);
    duk_put_prop_string(ctx_, obj, "metaKey");
}

// ============================================================
// ホイールイベント構築
// ============================================================
void JsEngine::pushWheelEvent(const SDL_Event *event) {
    const SDL_MouseWheelEvent &w = event->wheel;
    duk_idx_t obj = duk_push_object(ctx_);

    duk_push_string(ctx_, "wheel");
    duk_put_prop_string(ctx_, obj, "type");

    // ブラウザの deltaX/deltaY はピクセル単位（SDL は行単位なので ×100 で近似）
    float dirMul = (w.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
    duk_push_number(ctx_, w.x * dirMul * 100.0);
    duk_put_prop_string(ctx_, obj, "deltaX");
    duk_push_number(ctx_, -w.y * dirMul * 100.0); // ブラウザは下方向が正
    duk_put_prop_string(ctx_, obj, "deltaY");
    duk_push_number(ctx_, 0);
    duk_put_prop_string(ctx_, obj, "deltaZ");

    duk_push_number(ctx_, w.mouse_x);
    duk_put_prop_string(ctx_, obj, "clientX");
    duk_push_number(ctx_, w.mouse_y);
    duk_put_prop_string(ctx_, obj, "clientY");

    // deltaMode: 0 = DOM_DELTA_PIXEL
    duk_push_int(ctx_, 0);
    duk_put_prop_string(ctx_, obj, "deltaMode");

    SDL_Keymod mod = SDL_GetModState();
    duk_push_boolean(ctx_, (mod & SDL_KMOD_ALT) != 0);
    duk_put_prop_string(ctx_, obj, "altKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_CTRL) != 0);
    duk_put_prop_string(ctx_, obj, "ctrlKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_SHIFT) != 0);
    duk_put_prop_string(ctx_, obj, "shiftKey");
    duk_push_boolean(ctx_, (mod & SDL_KMOD_GUI) != 0);
    duk_put_prop_string(ctx_, obj, "metaKey");
}

// ============================================================
// タッチイベント構築
// ============================================================
void JsEngine::pushTouchEvent(const SDL_Event *event, const char *type) {
    const SDL_TouchFingerEvent &t = event->tfinger;
    duk_idx_t obj = duk_push_object(ctx_);

    duk_push_string(ctx_, type);
    duk_put_prop_string(ctx_, obj, "type");

    // タッチ座標をウィンドウピクセルに変換（SDL3 は正規化 0..1）
    int winW = 1, winH = 1;
    SDL_Window *window = SDL_GetWindowFromID(t.windowID);
    if (window) {
        SDL_GetWindowSize(window, &winW, &winH);
    }
    float px = t.x * winW;
    float py = t.y * winH;

    // touches 配列（簡易: 現在のタッチ1つ分）
    duk_push_array(ctx_);
    duk_idx_t touch = duk_push_object(ctx_);
    duk_push_number(ctx_, (double)t.fingerID);
    duk_put_prop_string(ctx_, touch, "identifier");
    duk_push_number(ctx_, px);
    duk_put_prop_string(ctx_, touch, "clientX");
    duk_push_number(ctx_, py);
    duk_put_prop_string(ctx_, touch, "clientY");
    duk_push_number(ctx_, px);
    duk_put_prop_string(ctx_, touch, "pageX");
    duk_push_number(ctx_, py);
    duk_put_prop_string(ctx_, touch, "pageY");
    duk_push_number(ctx_, t.pressure);
    duk_put_prop_string(ctx_, touch, "force");
    duk_put_prop_index(ctx_, -2, 0);

    // touchend では changedTouches に入れ、touches は空
    if (strcmp(type, "touchend") == 0 || strcmp(type, "touchcancel") == 0) {
        duk_put_prop_string(ctx_, obj, "changedTouches");
        duk_push_array(ctx_);
        duk_put_prop_string(ctx_, obj, "touches");
    } else {
        // touchstart / touchmove: touches と changedTouches 両方に同じ配列
        duk_dup(ctx_, -1);
        duk_put_prop_string(ctx_, obj, "changedTouches");
        duk_put_prop_string(ctx_, obj, "touches");
    }
}

// ============================================================
// SDL イベント → JS イベントディスパッチ
// ============================================================
void JsEngine::handleEvent(const SDL_Event *event) {
    if (!ctx_) return;

    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
        pushKeyboardEvent(event, "keydown");
        dispatchEvent("keydown");
        break;
    case SDL_EVENT_KEY_UP:
        pushKeyboardEvent(event, "keyup");
        dispatchEvent("keyup");
        break;
    case SDL_EVENT_MOUSE_MOTION:
        pushMouseEvent(event, "mousemove");
        dispatchEvent("mousemove");
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        pushMouseEvent(event, "mousedown");
        dispatchEvent("mousedown");
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        pushMouseEvent(event, "mouseup");
        dispatchEvent("mouseup");
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        pushWheelEvent(event);
        dispatchEvent("wheel");
        break;
    case SDL_EVENT_FINGER_DOWN:
        pushTouchEvent(event, "touchstart");
        dispatchEvent("touchstart");
        break;
    case SDL_EVENT_FINGER_MOTION:
        pushTouchEvent(event, "touchmove");
        dispatchEvent("touchmove");
        break;
    case SDL_EVENT_FINGER_UP:
        pushTouchEvent(event, "touchend");
        dispatchEvent("touchend");
        break;
    case SDL_EVENT_FINGER_CANCELED:
        pushTouchEvent(event, "touchcancel");
        dispatchEvent("touchcancel");
        break;
    default:
        break;
    }
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
