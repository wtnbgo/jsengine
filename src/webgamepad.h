#pragma once
#include <cstdint>
#include <quickjs.h>

union SDL_Event;

// ゲームパッドサブシステム初期化/終了 (App から呼ぶ)
bool webgamepad_init();
void webgamepad_uninit();

// SDL3 ゲームパッドイベント (ADDED / REMOVED) を JS に転送
// (それ以外のイベントは無視するので、handleEvent からそのまま渡せる)
void webgamepad_handleEvent(const SDL_Event *event);

// QuickJS に navigator.getGamepads() を登録
void webgamepad_bind(JSContext *ctx);
