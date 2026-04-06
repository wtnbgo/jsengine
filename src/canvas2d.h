#pragma once

struct duk_hthread;
typedef struct duk_hthread duk_context;

// ThorVG 初期化/終了
void canvas2d_init();
void canvas2d_uninit();

// duktape に Canvas2D バインディングを登録
void canvas2d_bind(duk_context *ctx);
