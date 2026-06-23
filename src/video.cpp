// ============================================================
// WebM 動画再生 (movie-player の JS バインディング + SDL Audio Sink)
// ============================================================
#ifdef JSENGINE_USE_MOVIE_PLAYER

#include "video.h"
#include "jsengine.hpp"

#include <quickjs.h>
#include <SDL3/SDL.h>
#include <IMoviePlayer.h>
#include <IAudioSink.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================
// SDLAudioSink
//
// movie-player は内部に audio engine を持たないので、 host (= jsengine) が
// IAudioSink を実装してデフォルトデバイスへ流す。
//
// 実装ポリシー:
//   - Setup() で SDL_AudioStream を 1 本確保 (1 player につき 1 stream)。
//   - Enqueue(data, bytes, last, param) は data をストリームに丸ごとコピー
//     (SDL_PutAudioStreamData は内部 buffer に copy する) し、 param を
//     即 consumed queue に積んで返却。 これで movie-player 側の DecodedBuffer
//     はすぐ release され、 decoder の output slot 詰まりを防ぐ。
//   - GetSamplesPlayed() は SDL の AudioStream に流し込んだバイト数から
//     既に再生済み (= queued の総バイト数 - 現時点で残っている queued bytes)
//     を計算してサンプル数で返す (MediaClock の anchor)。
// ============================================================
namespace {

class SDLAudioSink : public IAudioSink {
public:
    SDLAudioSink() = default;
    ~SDLAudioSink() override { teardown(); }

    bool Setup(int channels, int sampleRate, int bitsPerSample, Encoding encoding) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (stream_) return false;  // 二重 Setup 禁止

        channels_ = channels;
        sampleRate_ = sampleRate;
        bitsPerSample_ = bitsPerSample;
        encoding_ = encoding;
        bytesPerSample_ = (bitsPerSample / 8) * channels;

        SDL_AudioSpec spec{};
        spec.freq = sampleRate;
        spec.channels = channels;
        switch (encoding) {
        case PCM_U8:  spec.format = SDL_AUDIO_U8;  break;
        case PCM_S16: spec.format = SDL_AUDIO_S16; break;
        case PCM_S32: spec.format = SDL_AUDIO_S32; break;
        case PCM_F32: spec.format = SDL_AUDIO_F32; break;
        default: return false;
        }
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec, nullptr, nullptr);
        if (!stream_) {
            SDL_Log("SDLAudioSink: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
            return false;
        }
        // 初期 mute 状態 (Start で resume)
        SDL_PauseAudioStreamDevice(stream_);
        applyVolumeLocked();
        return true;
    }

    void Enqueue(const void *data, size_t bytes, bool /*last*/, void *param) override {
        // data はその場で SDL に丸ごとコピーし、 param を即 consumed queue へ。
        // この方針なら movie-player 側の DecodedBuffer 寿命が短く、 decoder slot
        // が詰まらない (Android 経路と同じ推奨パターン)。
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stream_ && bytes > 0 && data) {
                SDL_PutAudioStreamData(stream_, data, (int)bytes);
                totalEnqueuedBytes_ += bytes;
            }
            consumed_.push_back(param);
        }
    }

    void Start() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (stream_) SDL_ResumeAudioStreamDevice(stream_);
    }
    void Stop() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (stream_) SDL_PauseAudioStreamDevice(stream_);
    }

    int64_t GetSamplesPlayed() const override {
        std::lock_guard<std::mutex> lk(mu_);
        if (!stream_ || bytesPerSample_ == 0) return 0;
        // 「投入総バイト数 - まだ stream 内に残っているバイト数」 = 再生済みバイト数
        int queued = SDL_GetAudioStreamQueued(stream_);
        if (queued < 0) queued = 0;
        int64_t playedBytes = (int64_t)totalEnqueuedBytes_ - queued;
        if (playedBytes < 0) playedBytes = 0;
        return playedBytes / bytesPerSample_;
    }

    bool TryPopConsumed(void **outParam) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (consumed_.empty()) return false;
        *outParam = consumed_.front();
        consumed_.pop_front();
        return true;
    }

    void Flush() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (stream_) SDL_ClearAudioStream(stream_);
        // 投入カウンタもリセット (Seek 直後の再生位置整合)
        totalEnqueuedBytes_ = 0;
    }

    void SetVolume(float v) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
        volume_ = v;
        applyVolumeLocked();
    }
    float Volume() const override {
        std::lock_guard<std::mutex> lk(mu_);
        return volume_;
    }

private:
    void teardown() {
        std::lock_guard<std::mutex> lk(mu_);
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        consumed_.clear();
    }
    void applyVolumeLocked() {
        if (stream_) SDL_SetAudioStreamGain(stream_, volume_);
    }

    mutable std::mutex mu_;
    SDL_AudioStream *stream_ = nullptr;
    int channels_ = 0;
    int sampleRate_ = 0;
    int bitsPerSample_ = 0;
    int bytesPerSample_ = 0;
    Encoding encoding_ = PCM_S16;
    size_t totalEnqueuedBytes_ = 0;
    std::deque<void*> consumed_;
    float volume_ = 1.0f;
};

// ============================================================
// JsVideoPlayer
//
// JS の `MoviePlayer` インスタンスにぶら下がる C++ オブジェクト。
// movie-player の IMoviePlayer を所有し、 decode された RGBA バッファを
// 最新フレームとして保持する。 JS から `data` getter で参照される。
// ============================================================
class JsVideoPlayer {
public:
    JsVideoPlayer() = default;
    ~JsVideoPlayer() {
        if (player_) {
            player_->Stop();
            delete player_;
            player_ = nullptr;
        }
    }

    bool open(const std::string &path) {
        IMoviePlayer::InitParam p; p.Init();
        // 内部で YUV → RGBA 変換まで済ませて、 そのまま GL に上げられる形にする。
        p.videoColorFormat = IMoviePlayer::COLOR_RGBA;
        p.audioSink = &sink_;
        player_ = IMoviePlayer::CreateMoviePlayer(path.c_str(), p);
        if (!player_) return false;

        player_->SetOnVideoDecoded(
            [this](int w, int h, IMoviePlayer::DestUpdater updater) {
                std::lock_guard<std::mutex> lk(pixMu_);
                if (w != width_ || h != height_ || pixels_.size() != (size_t)w * h * 4) {
                    pixels_.assign((size_t)w * h * 4, 0);
                    width_ = w; height_ = h;
                }
                updater((char*)pixels_.data(), w * 4);
                newFrame_.store(true, std::memory_order_release);
            });
        player_->SetOnState(
            [](void *self, IMoviePlayer::State st) -> int32_t {
                auto *me = static_cast<JsVideoPlayer*>(self);
                me->state_.store(st, std::memory_order_release);
                return 0;
            }, this);

        // 開いた直後にビデオフォーマットが分かっていれば width/height をキャッシュ。
        if (player_->IsVideoAvailable()) {
            IMoviePlayer::VideoFormat vf{};
            player_->GetVideoFormat(&vf);
            std::lock_guard<std::mutex> lk(pixMu_);
            width_ = vf.width;
            height_ = vf.height;
            pixels_.assign((size_t)width_ * height_ * 4, 0);
        }
        return true;
    }

    void play(bool loop) { if (player_) player_->Play(loop); }
    void pause()         { if (player_) player_->Pause(); }
    void resume()        { if (player_) player_->Resume(); }
    void stop()          { if (player_) player_->Stop(); }
    void seek(double sec){ if (player_) player_->Seek((int64_t)(sec * 1e6)); }
    void setLoop(bool l) { if (player_) player_->SetLoop(l); }
    void setVolume(float v){ if (player_) player_->SetVolume(v); }

    int width()  const { std::lock_guard<std::mutex> lk(pixMu_); return width_; }
    int height() const { std::lock_guard<std::mutex> lk(pixMu_); return height_; }
    double duration() const {
        if (!player_) return 0;
        return (double)player_->Duration() / 1e6;
    }
    double currentTime() const {
        if (!player_) return 0;
        return (double)player_->Position() / 1e6;
    }
    bool isPlaying() const { return player_ ? player_->IsPlaying() : false; }
    bool isPaused() const  {
        return player_ ? (player_->GetState() == IMoviePlayer::STATE_PAUSE) : false;
    }
    bool isEnded() const   {
        return player_ ? (player_->GetState() == IMoviePlayer::STATE_FINISH) : false;
    }
    bool isLoop() const    { return player_ ? player_->Loop() : false; }
    float volume() const   { return player_ ? player_->Volume() : 1.f; }

    // 最新の RGBA フレームを JS の ArrayBuffer として返す。 毎回 copy (texImage2D
    // は GL 呼出後すぐ free して良いので、 単純化のためコピー戦略を採用)。
    JSValue makeDataArrayBuffer(JSContext *ctx) const {
        std::lock_guard<std::mutex> lk(pixMu_);
        if (pixels_.empty()) return JS_NULL;
        return JS_NewArrayBufferCopy(ctx, pixels_.data(), pixels_.size());
    }

private:
    IMoviePlayer *player_ = nullptr;
    SDLAudioSink sink_;
    mutable std::mutex pixMu_;
    std::vector<uint8_t> pixels_;
    int width_ = 0, height_ = 0;
    std::atomic<bool> newFrame_{false};
    std::atomic<int> state_{IMoviePlayer::STATE_UNINIT};
};

// 生存中の player を追跡 (video_uninit で全停止用)
std::mutex g_aliveMu;
std::unordered_set<JsVideoPlayer*> g_alivePlayers;

// ============================================================
// JS バインディング
// ============================================================
static JSClassID g_class_id;

static void videoFinalizer(JSRuntime *rt, JSValue val) {
    auto *p = static_cast<JsVideoPlayer*>(JS_GetOpaque(val, g_class_id));
    if (p) {
        {
            std::lock_guard<std::mutex> lk(g_aliveMu);
            g_alivePlayers.erase(p);
        }
        delete p;
    }
}

static JSClassDef g_class_def = { "MoviePlayer", videoFinalizer, nullptr, nullptr, nullptr };

static JsVideoPlayer* get_player(JSContext *ctx, JSValueConst this_val) {
    return static_cast<JsVideoPlayer*>(JS_GetOpaque(this_val, g_class_id));
}

// new MoviePlayer(path, optsObj?)
static JSValue video_ctor(JSContext *ctx, JSValueConst /*new_target*/, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "MoviePlayer requires a path");
    const char *p = JS_ToCString(ctx, argv[0]);
    if (!p) return JS_EXCEPTION;
    std::string rpath;
    if (JsEngine *e = JsEngine::getInstance()) rpath = e->resolvePath(p);
    else rpath = p;
    JS_FreeCString(ctx, p);

    auto *vp = new JsVideoPlayer();
    if (!vp->open(rpath)) {
        delete vp;
        return JS_ThrowInternalError(ctx, "MoviePlayer: failed to open %s", rpath.c_str());
    }
    // optsObj: { loop?: bool, volume?: number }
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue lv = JS_GetPropertyStr(ctx, argv[1], "loop");
        if (!JS_IsUndefined(lv)) vp->setLoop(JS_ToBool(ctx, lv) != 0);
        JS_FreeValue(ctx, lv);
        JSValue vv = JS_GetPropertyStr(ctx, argv[1], "volume");
        if (!JS_IsUndefined(vv)) {
            double v = 1.0; JS_ToFloat64(ctx, &v, vv);
            vp->setVolume((float)v);
        }
        JS_FreeValue(ctx, vv);
    }
    {
        std::lock_guard<std::mutex> lk(g_aliveMu);
        g_alivePlayers.insert(vp);
    }
    JSValue obj = JS_NewObjectClass(ctx, g_class_id);
    JS_SetOpaque(obj, vp);
    return obj;
}

// メソッド類
static JSValue video_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *p = get_player(ctx, this_val); if (!p) return JS_UNDEFINED;
    bool loop = (argc > 0) ? (JS_ToBool(ctx, argv[0]) != 0) : false;
    p->play(loop);
    return JS_UNDEFINED;
}
static JSValue video_pause(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    if (auto *p = get_player(ctx, this_val)) p->pause();
    return JS_UNDEFINED;
}
static JSValue video_resume(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    if (auto *p = get_player(ctx, this_val)) p->resume();
    return JS_UNDEFINED;
}
static JSValue video_stop(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    if (auto *p = get_player(ctx, this_val)) p->stop();
    return JS_UNDEFINED;
}
static JSValue video_seek(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (auto *p = get_player(ctx, this_val)) {
        double t = 0; if (argc > 0) JS_ToFloat64(ctx, &t, argv[0]);
        p->seek(t);
    }
    return JS_UNDEFINED;
}

// プロパティ getter/setter
//
// 注意: JS_NewCFunction で登録する関数は標準 JSCFunction シグネチャ
//   `JSValue (*)(JSContext*, JSValueConst this_val, int argc, JSValueConst *argv)`
// が必須。 getter は argc=0 で argv 未使用、 setter は argv[0] に代入値が来る。
// 専用 getter/setter signature (3 引数版) は JS_DefinePropertyGetSet では
// JS_NewCFunction 経由では使えない (signature 不一致でガベージ引数が混入し、
// `val` が壊れて UB → SEGV)。
static JSValue video_get_width(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->width() : 0);
}
static JSValue video_get_height(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->height() : 0);
}
static JSValue video_get_duration(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->duration() : 0);
}
static JSValue video_get_currentTime(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->currentTime() : 0);
}
static JSValue video_set_currentTime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (auto *p = get_player(ctx, this_val); p && argc > 0) {
        double t = 0; JS_ToFloat64(ctx, &t, argv[0]);
        p->seek(t);
    }
    return JS_UNDEFINED;
}
static JSValue video_get_paused(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewBool(ctx, p ? p->isPaused() : true);
}
static JSValue video_get_ended(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewBool(ctx, p ? p->isEnded() : false);
}
static JSValue video_get_loop(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewBool(ctx, p ? p->isLoop() : false);
}
static JSValue video_set_loop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (auto *p = get_player(ctx, this_val); p && argc > 0) p->setLoop(JS_ToBool(ctx, argv[0]) != 0);
    return JS_UNDEFINED;
}
static JSValue video_get_volume(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->volume() : 0);
}
static JSValue video_set_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (auto *p = get_player(ctx, this_val); p && argc > 0) {
        double v = 1; JS_ToFloat64(ctx, &v, argv[0]);
        p->setVolume((float)v);
    }
    return JS_UNDEFINED;
}
static JSValue video_get_data(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return p ? p->makeDataArrayBuffer(ctx) : JS_NULL;
}

static void video_bind_impl(JSContext *ctx) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClassID(rt, &g_class_id);
    JS_NewClass(rt, g_class_id, &g_class_def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "play",   JS_NewCFunction(ctx, video_play,   "play",   1));
    JS_SetPropertyStr(ctx, proto, "pause",  JS_NewCFunction(ctx, video_pause,  "pause",  0));
    JS_SetPropertyStr(ctx, proto, "resume", JS_NewCFunction(ctx, video_resume, "resume", 0));
    JS_SetPropertyStr(ctx, proto, "stop",   JS_NewCFunction(ctx, video_stop,   "stop",   0));
    JS_SetPropertyStr(ctx, proto, "seek",   JS_NewCFunction(ctx, video_seek,   "seek",   1));

    // CGetSet API (getter/setter)。 標準 JSCFunction シグネチャの関数を
    // 直接 JS_DefinePropertyGetSet に渡す。
    auto defGetter = [&](const char *name, JSCFunction *fn) {
        JSValue f = JS_NewCFunction(ctx, fn, name, 0);
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, proto, atom, f, JS_UNDEFINED, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
    };
    auto defGetSet = [&](const char *name, JSCFunction *getFn, JSCFunction *setFn) {
        JSValue g = JS_NewCFunction(ctx, getFn, name, 0);
        JSValue s = JS_NewCFunction(ctx, setFn, name, 1);
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, proto, atom, g, s, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
    };
    defGetter("width",       video_get_width);
    defGetter("height",      video_get_height);
    // HTMLVideoElement 互換性のため videoWidth / videoHeight も同じ getter で公開
    defGetter("videoWidth",  video_get_width);
    defGetter("videoHeight", video_get_height);
    defGetter("duration",    video_get_duration);
    defGetSet("currentTime", video_get_currentTime, video_set_currentTime);
    defGetter("paused",      video_get_paused);
    defGetter("ended",       video_get_ended);
    defGetSet("loop",        video_get_loop,    video_set_loop);
    defGetSet("volume",      video_get_volume,  video_set_volume);
    defGetter("data",        video_get_data);

    JS_SetClassProto(ctx, g_class_id, proto);

    JSValue ctor = JS_NewCFunction2(ctx, video_ctor, "MoviePlayer", 2,
        JS_CFUNC_constructor, 0);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "MoviePlayer", ctor);
    JS_FreeValue(ctx, global);
}

static void video_uninit_impl() {
    // 生きてる player に Stop を投げて audio thread を終わらせる。
    // JS 側 finalizer は JsEngine 解放時に走るのでここでは delete しない。
    std::lock_guard<std::mutex> lk(g_aliveMu);
    for (auto *p : g_alivePlayers) p->stop();
}

} // anonymous

void video_bind(JSContext *ctx) { video_bind_impl(ctx); }
void video_uninit()             { video_uninit_impl(); }

#endif // JSENGINE_USE_MOVIE_PLAYER
