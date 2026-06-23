// ============================================================
// 画像デコード並列化プール
//
// SDL_image での PNG/JPEG/BMP デコードを std::thread プールで並列実行する。
// GLTFLoader が `Promise.all(images.map(loadImage))` で大量の画像を流す経路で
// 効く。 同期版 `decodeImageBuffer` は残し、 新規 `decodeImageBufferAsync` を
// 追加して並存させる。
//
// 仕組み:
//   1. JS から submitDecode(buf) が呼ばれると、 メインスレッドで JS の Promise を
//      作って resolve/reject 関数をキャプチャ + 入力 buf を内部コピー。
//   2. ワーカースレッドが pending queue から取って SDL_image でデコード、
//      RGBA pixel 配列を Job に格納して completed queue に push。
//   3. App::update の頭で drainCompleted() を呼ぶと、 メインスレッドで結果を
//      JSValue 化して resolve(...) を呼ぶ → JS の .then が動く。
//
// QuickJS の値は必ずメインスレッド上で触れる縛りなので、 ワーカーは生バイトしか
// 扱わない。 resolve/reject も Job が完了したあとメインスレッドが呼ぶ。
// ============================================================
#pragma once

#include <quickjs.h>
#include <cstdint>
#include <cstddef>

namespace jsengine {

// グローバルプール (今のところ 1 つで十分なのでシングルトン)。
// numWorkers = 0 でデフォルト (=std::thread::hardware_concurrency 半分くらい、 最低 2)。
void image_decode_pool_init(int numWorkers = 0);
void image_decode_pool_shutdown();

// 入力 ArrayBuffer (PNG/JPEG/BMP) を非同期にデコードして、 解決値が
// { width, height, data: ArrayBuffer (RGBA) } の Promise を返す。
// 失敗時は reject される。 メインスレッドからのみ呼ぶこと。
JSValue submit_decode_async(JSContext *ctx, const uint8_t *buf, size_t bufSize);

// メインスレッドから per-frame で呼ぶ。 完了済みジョブを取り出して
// Promise を resolve/reject する。
void drain_completed(JSContext *ctx);

// pool に in-flight (pending or processing) な仕事があるか。
// VRM ロード時の coroutine ループで、 「JS は暇だが workers がまだ動いてる」
// 状況を検知するのに使う。
bool has_in_flight();

}  // namespace jsengine
