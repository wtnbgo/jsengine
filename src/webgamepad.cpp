/**
 * W3C Gamepad API 互換バインディング
 *
 * SDL3 の SDL_Gamepad を使ってコントローラ入力を JS に公開する。
 *
 * 提供する JS API:
 *   navigator.getGamepads()  → (Gamepad | null)[]  (length は最大の index + 1)
 *   addEventListener("gamepadconnected",    function(e) { e.gamepad ... })
 *   addEventListener("gamepaddisconnected", function(e) { e.gamepad ... })
 *
 * Gamepad オブジェクト:
 *   id        : "<name> (XInput Style)" 等のラベル
 *   index     : 0..n-1 (接続順)
 *   connected : true
 *   timestamp : 接続時刻 (performance.now() ベースの ms)
 *   mapping   : "standard"
 *   axes      : Float64Array (-1.0 ~ 1.0)
 *               [0]=LX, [1]=LY, [2]=RX, [3]=RY
 *   buttons   : { pressed:bool, touched:bool, value:0..1 }[]
 *               W3C 標準レイアウト 17 個 + （存在しないボタンは pressed=false, value=0）
 */

#include "webgamepad.h"
#include "jsengine.hpp"
#include <SDL3/SDL.h>
#include <quickjs.h>
#include <vector>
#include <unordered_map>
#include <string>

namespace {

struct OpenedGamepad {
    SDL_JoystickID instance_id = 0;
    SDL_Gamepad*   gp = nullptr;
    int            index = -1;        // navigator.getGamepads() のスロット
    double         timestamp = 0.0;   // 接続時刻 (ms)
    std::string    id;                // ラベル文字列
};

// instance_id → スロット情報
static std::unordered_map<SDL_JoystickID, OpenedGamepad> g_pads;
// スロット使用状況 (index → instance_id, 空スロットは 0)
static std::vector<SDL_JoystickID> g_slots;

static int allocate_slot(SDL_JoystickID id) {
    for (size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i] == 0) { g_slots[i] = id; return (int)i; }
    }
    g_slots.push_back(id);
    return (int)g_slots.size() - 1;
}

static void release_slot(int idx) {
    if (idx >= 0 && idx < (int)g_slots.size()) g_slots[idx] = 0;
    // 末尾の空きを縮める
    while (!g_slots.empty() && g_slots.back() == 0) g_slots.pop_back();
}

// SDL の軸値 (-32768 .. 32767) を -1.0 .. 1.0 にマップ
static double axis_to_float(int16_t v) {
    return v < 0 ? (double)v / 32768.0 : (double)v / 32767.0;
}

// W3C 標準ボタンレイアウト ← SDL3 SDL_GamepadButton マッピング
// index 6 (LT) と 7 (RT) は SDL では軸なので別途処理
static const SDL_GamepadButton kButtonMap[17] = {
    SDL_GAMEPAD_BUTTON_SOUTH,         // 0: A
    SDL_GAMEPAD_BUTTON_EAST,          // 1: B
    SDL_GAMEPAD_BUTTON_WEST,          // 2: X
    SDL_GAMEPAD_BUTTON_NORTH,         // 3: Y
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, // 4: LB
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,// 5: RB
    SDL_GAMEPAD_BUTTON_INVALID,       // 6: LT (axis 経由)
    SDL_GAMEPAD_BUTTON_INVALID,       // 7: RT (axis 経由)
    SDL_GAMEPAD_BUTTON_BACK,          // 8: Back/Select
    SDL_GAMEPAD_BUTTON_START,         // 9: Start
    SDL_GAMEPAD_BUTTON_LEFT_STICK,    // 10: L3
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,   // 11: R3
    SDL_GAMEPAD_BUTTON_DPAD_UP,       // 12: DPad Up
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,     // 13: DPad Down
    SDL_GAMEPAD_BUTTON_DPAD_LEFT,     // 14: DPad Left
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    // 15: DPad Right
    SDL_GAMEPAD_BUTTON_GUIDE,         // 16: Guide
};

static double now_ms() {
    return (double)SDL_GetTicks();
}

static JSValue make_button_state(JSContext *ctx, bool pressed, double value) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "pressed", JS_NewBool(ctx, pressed));
    JS_SetPropertyStr(ctx, obj, "touched", JS_NewBool(ctx, pressed));
    JS_SetPropertyStr(ctx, obj, "value",   JS_NewFloat64(ctx, value));
    return obj;
}

// SDL_Gamepad の現在状態を Gamepad オブジェクトに焼き付けて返す
static JSValue snapshot_gamepad(JSContext *ctx, const OpenedGamepad &p) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "id",        JS_NewString(ctx, p.id.c_str()));
    JS_SetPropertyStr(ctx, obj, "index",     JS_NewInt32(ctx, p.index));
    JS_SetPropertyStr(ctx, obj, "connected", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "timestamp", JS_NewFloat64(ctx, p.timestamp));
    JS_SetPropertyStr(ctx, obj, "mapping",   JS_NewString(ctx, "standard"));

    // axes (4 軸)
    JSValue axes = JS_NewArray(ctx);
    int16_t lx = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_LEFTX);
    int16_t ly = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_LEFTY);
    int16_t rx = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHTX);
    int16_t ry = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHTY);
    JS_SetPropertyUint32(ctx, axes, 0, JS_NewFloat64(ctx, axis_to_float(lx)));
    JS_SetPropertyUint32(ctx, axes, 1, JS_NewFloat64(ctx, axis_to_float(ly)));
    JS_SetPropertyUint32(ctx, axes, 2, JS_NewFloat64(ctx, axis_to_float(rx)));
    JS_SetPropertyUint32(ctx, axes, 3, JS_NewFloat64(ctx, axis_to_float(ry)));
    JS_SetPropertyStr(ctx, obj, "axes", axes);

    // buttons (17 個)
    JSValue btns = JS_NewArray(ctx);
    for (int i = 0; i < 17; ++i) {
        bool pressed = false;
        double value = 0.0;
        if (i == 6) {
            // LT: SDL_GAMEPAD_AXIS_LEFT_TRIGGER (0..32767)
            int16_t v = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            value = (double)v / 32767.0;
            if (value < 0.0) value = 0.0;
            pressed = value > 0.05;
        } else if (i == 7) {
            int16_t v = SDL_GetGamepadAxis(p.gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            value = (double)v / 32767.0;
            if (value < 0.0) value = 0.0;
            pressed = value > 0.05;
        } else {
            SDL_GamepadButton bid = kButtonMap[i];
            if (bid != SDL_GAMEPAD_BUTTON_INVALID) {
                pressed = SDL_GetGamepadButton(p.gp, bid) != 0;
                value = pressed ? 1.0 : 0.0;
            }
        }
        JS_SetPropertyUint32(ctx, btns, i, make_button_state(ctx, pressed, value));
    }
    JS_SetPropertyStr(ctx, obj, "buttons", btns);
    return obj;
}

// 切断済の空 Gamepad
static JSValue make_disconnected_gamepad(JSContext *ctx, int index, const char *id, double ts) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "id",        JS_NewString(ctx, id));
    JS_SetPropertyStr(ctx, obj, "index",     JS_NewInt32(ctx, index));
    JS_SetPropertyStr(ctx, obj, "connected", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "timestamp", JS_NewFloat64(ctx, ts));
    JS_SetPropertyStr(ctx, obj, "mapping",   JS_NewString(ctx, "standard"));

    JSValue axes = JS_NewArray(ctx);
    for (int i = 0; i < 4; ++i) JS_SetPropertyUint32(ctx, axes, i, JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, obj, "axes", axes);

    JSValue btns = JS_NewArray(ctx);
    for (int i = 0; i < 17; ++i) JS_SetPropertyUint32(ctx, btns, i, make_button_state(ctx, false, 0.0));
    JS_SetPropertyStr(ctx, obj, "buttons", btns);
    return obj;
}

} // namespace

// ============================================================
// 初期化 / 終了
// ============================================================

bool webgamepad_init() {
    // SDL_INIT_GAMEPAD は main.cpp で済んでいる前提。すでに接続されているパッドを開く
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; ++i) {
            SDL_JoystickID id = ids[i];
            SDL_Gamepad *gp = SDL_OpenGamepad(id);
            if (gp) {
                OpenedGamepad p;
                p.instance_id = id;
                p.gp = gp;
                p.index = allocate_slot(id);
                p.timestamp = now_ms();
                const char *name = SDL_GetGamepadName(gp);
                p.id = name ? name : "Gamepad";
                g_pads[id] = std::move(p);
            }
        }
        SDL_free(ids);
    }
    SDL_Log("WebGamepad initialized (%d device(s) connected)", (int)g_pads.size());
    return true;
}

void webgamepad_uninit() {
    for (auto &kv : g_pads) {
        if (kv.second.gp) SDL_CloseGamepad(kv.second.gp);
    }
    g_pads.clear();
    g_slots.clear();
}

// ============================================================
// イベント処理
// ============================================================

void webgamepad_handleEvent(const SDL_Event *event) {
    JsEngine *engine = JsEngine::getInstance();
    if (!engine) return;
    JSContext *ctx = engine->getContext();
    if (!ctx) return;

    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_JoystickID id = event->gdevice.which;
        if (g_pads.count(id)) return; // 既に開いている
        SDL_Gamepad *gp = SDL_OpenGamepad(id);
        if (!gp) return;
        OpenedGamepad p;
        p.instance_id = id;
        p.gp = gp;
        p.index = allocate_slot(id);
        p.timestamp = now_ms();
        const char *name = SDL_GetGamepadName(gp);
        p.id = name ? name : "Gamepad";
        g_pads[id] = p;

        JSValue evt = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, evt, "type", JS_NewString(ctx, "gamepadconnected"));
        JS_SetPropertyStr(ctx, evt, "gamepad", snapshot_gamepad(ctx, g_pads[id]));
        engine->dispatchEvent("gamepadconnected", evt);
    }
    else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        SDL_JoystickID id = event->gdevice.which;
        auto it = g_pads.find(id);
        if (it == g_pads.end()) return;

        // 切断イベントには disconnected snapshot を載せる (axes/buttons は無効値)
        int idx = it->second.index;
        double ts = it->second.timestamp;
        std::string padId = it->second.id;
        if (it->second.gp) SDL_CloseGamepad(it->second.gp);
        g_pads.erase(it);
        release_slot(idx);

        JSValue evt = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, evt, "type", JS_NewString(ctx, "gamepaddisconnected"));
        JS_SetPropertyStr(ctx, evt, "gamepad", make_disconnected_gamepad(ctx, idx, padId.c_str(), ts));
        engine->dispatchEvent("gamepaddisconnected", evt);
    }
}

// ============================================================
// JS バインディング: navigator.getGamepads()
// ============================================================

static JSValue js_getGamepads(JSContext *ctx, JSValueConst /*this_val*/, int, JSValueConst*) {
    JSValue arr = JS_NewArray(ctx);
    uint32_t len = (uint32_t)g_slots.size();
    for (uint32_t i = 0; i < len; ++i) {
        SDL_JoystickID id = g_slots[i];
        if (id == 0) {
            JS_SetPropertyUint32(ctx, arr, i, JS_NULL);
            continue;
        }
        auto it = g_pads.find(id);
        if (it == g_pads.end()) {
            JS_SetPropertyUint32(ctx, arr, i, JS_NULL);
            continue;
        }
        JS_SetPropertyUint32(ctx, arr, i, snapshot_gamepad(ctx, it->second));
    }
    return arr;
}

void webgamepad_bind(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue navi = JS_GetPropertyStr(ctx, global, "navigator");
    if (JS_IsUndefined(navi) || JS_IsNull(navi)) {
        JS_FreeValue(ctx, navi);
        navi = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, "navigator", JS_DupValue(ctx, navi));
    }
    JS_SetPropertyStr(ctx, navi, "getGamepads",
        JS_NewCFunction(ctx, js_getGamepads, "getGamepads", 0));
    JS_FreeValue(ctx, navi);
    JS_FreeValue(ctx, global);
}
