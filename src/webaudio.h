#pragma once

struct duk_hthread;
typedef struct duk_hthread duk_context;

// オーディオシステム初期化/終了（App から呼ぶ）
bool webaudio_init();
void webaudio_uninit();

// 再生完了したストリームの回収（毎フレーム呼ぶ）
void webaudio_gc();

// duktape に Web Audio API バインディングを登録
void webaudio_bind(duk_context *ctx);
