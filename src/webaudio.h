#pragma once
#include <quickjs.h>

// オーディオシステム初期化/終了（App から呼ぶ）
bool webaudio_init();
void webaudio_uninit();

// 再生完了したストリームの回収（毎フレーム呼ぶ）
void webaudio_gc();

// QuickJS に Web Audio API バインディングを登録
void webaudio_bind(JSContext *ctx);
