#pragma once
#include <cstdint>
#include <quickjs.h>

// オーディオシステム初期化/終了（App から呼ぶ）
bool webaudio_init();
void webaudio_uninit();

// 毎フレーム呼ぶ: AudioContext.currentTime を進め、GainNode の自動化を更新し、
// 再生完了したストリームを回収する
void webaudio_update(uint32_t deltaMs);

// 互換用 (deltaMs=0 で webaudio_update を呼ぶだけ)
void webaudio_gc();

// QuickJS に Web Audio API バインディングを登録
void webaudio_bind(JSContext *ctx);

// JS_FreeContext より前に呼ぶこと: 再生中 source の selfHold と GainNode の selfHold
// を放出し、 自己ループ参照が残って JS_FreeRuntime で assert する事態を防ぐ。
void webaudio_release_self_holds(JSContext *ctx);
