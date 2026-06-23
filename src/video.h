// ============================================================
// WebM 動画再生 (wamsoft/movie-player + libvpx + libvorbis/libopus)
//
// CMake オプション `JSENGINE_USE_MOVIE_PLAYER` (default ON) で有効化。
//
// 公開する JS API:
//   - グローバル `MoviePlayer` クラス (jsengine 拡張):
//       const m = new MoviePlayer("path/to/video.webm", { loop, volume });
//       m.play(); m.pause(); m.stop(); m.seek(t);
//       m.currentTime, m.duration, m.width, m.height,
//       m.paused, m.ended, m.loop, m.volume
//       m.data  // 最新フレームの RGBA を ArrayBuffer で取得 (texImage2D 用)
//
//   - ブラウザシム HTMLVideoElement: sysinit.js 側が `MoviePlayer` をラップして
//     `video.src = "..."` / play() / pause() / `gl.texImage2D(..., video)` 等の
//     標準ブラウザ API を擬似的に提供する。
//
//   - WebGL 連携: MoviePlayer / HTMLVideoElement は `width`, `height`, `data` を
//     公開するので、 既存の qjs_get_pixels 経由で `gl.texImage2D(target, level,
//     RGBA, RGBA, UNSIGNED_BYTE, player)` がそのまま動く (Image と同じ流儀)。
//
// 音声: SDL3 の SDL_AudioStream を IAudioSink で実装し、 デフォルト出力デバイス
//   へ流す。 movie-player の MediaClock は audio sink の getSamplesPlayed を
//   anchor にする (audio が無い時は video-master モードへ自動切替)。
// ============================================================
#pragma once
#ifdef JSENGINE_USE_MOVIE_PLAYER

#include <quickjs.h>

void video_bind(JSContext *ctx);     // `MoviePlayer` クラスを global に登録
void video_uninit();                 // App 終了時。 全 player に Stop() を投げる

#endif
