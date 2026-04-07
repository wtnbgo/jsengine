#pragma once
#include <quickjs.h>

// ThorVG 初期化/終了
void canvas2d_init();
void canvas2d_uninit();

// QuickJS に Canvas2D バインディングを登録
void canvas2d_bind(JSContext *ctx);
