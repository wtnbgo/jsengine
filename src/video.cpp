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
    // colorFormat = "rgba" (default), "i420", "nv12", "nv21".
    //
    // RGBA mode: 旧 API (SetOnVideoDecoded + DestUpdater) を使い、 decoder の packed RGBA
    //            を host バッファに直接書込ませる (余計な memcpy 無しの高速経路)。
    // YUV mode:  新 API (SetOnVideoDecodedPlanes + VideoFrameInfo) を使い、 decoder native
    //            の YUV plane を生で受け取って内部に row-by-row でコピー保持 (stride 補正)。
    //            shader 側で YUV→RGB 変換することで GPU 負荷分散できる。
    enum class OutputFormat { RGBA, I420, NV12, NV21 };

    JsVideoPlayer() = default;
    ~JsVideoPlayer() {
        if (player_) {
            player_->Stop();
            delete player_;
            player_ = nullptr;
        }
    }

    void setOutputFormat(OutputFormat f) { outFormat_ = f; }
    OutputFormat outputFormat() const { return outFormat_; }

    bool open(const std::string &path) {
        IMoviePlayer::InitParam p; p.Init();
        // colorFormat 設定に応じて decoder 側に要求するフォーマットを切替。
        switch (outFormat_) {
        case OutputFormat::I420: p.videoColorFormat = IMoviePlayer::COLOR_I420; break;
        case OutputFormat::NV12: p.videoColorFormat = IMoviePlayer::COLOR_NV12; break;
        case OutputFormat::NV21: p.videoColorFormat = IMoviePlayer::COLOR_NV21; break;
        default:                  p.videoColorFormat = IMoviePlayer::COLOR_RGBA; break;
        }
        p.audioSink = &sink_;
        player_ = IMoviePlayer::CreateMoviePlayer(path.c_str(), p);
        if (!player_) return false;

        // RGBA mode は旧 API (DestUpdater 経由で host バッファ直書込み、 余計な memcpy 無し)
        // YUV mode は新 API (VideoFrameInfo 経由で plane を生で受け取り、 内部にコピー保持)
        if (outFormat_ == OutputFormat::RGBA) {
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
        } else {
            player_->SetOnVideoDecodedPlanes(
                [this](const IMoviePlayer::VideoFrameInfo &frame) {
                    std::lock_guard<std::mutex> lk(pixMu_);
                    int W = frame.width, H = frame.height;
                    if (W != width_ || H != height_) {
                        width_ = W; height_ = H;
                        rebuildPlanes_locked();
                    }
                    copyPlanes_locked(frame);
                    newFrame_.store(true, std::memory_order_release);
                });
        }
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

    // --- YUV プレーンアクセス ---
    int planeCount() const {
        switch (outFormat_) {
        case OutputFormat::RGBA: return 1;
        case OutputFormat::I420: return 3;
        case OutputFormat::NV12: return 2;
        case OutputFormat::NV21: return 2;
        }
        return 1;
    }
    // plane index → { width, height, ArrayBuffer } を JS オブジェクトで返す。
    JSValue makePlaneObject(JSContext *ctx, int i) const {
        std::lock_guard<std::mutex> lk(pixMu_);
        if (outFormat_ == OutputFormat::RGBA) {
            if (i != 0 || pixels_.empty()) return JS_NULL;
            JSValue o = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, o, "width",  JS_NewInt32(ctx, width_));
            JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, height_));
            JS_SetPropertyStr(ctx, o, "data",   JS_NewArrayBufferCopy(ctx, pixels_.data(), pixels_.size()));
            return o;
        }
        if (i < 0 || i >= (int)planes_.size()) return JS_NULL;
        const PlaneInfo &pi = planes_[i];
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "width",  JS_NewInt32(ctx, pi.width));
        JS_SetPropertyStr(ctx, o, "height", JS_NewInt32(ctx, pi.height));
        JS_SetPropertyStr(ctx, o, "data",   JS_NewArrayBufferCopy(ctx, pi.data.data(), pi.data.size()));
        return o;
    }

private:
    // YUV プレーン要求時の再アロケート (pixMu_ 保持で呼ぶ)。 RGBA 要求時は何もしない。
    void rebuildPlanes_locked() {
        planes_.clear();
        if (outFormat_ == OutputFormat::RGBA) return;
        int W = width_, H = height_;
        int W2 = (W + 1) / 2, H2 = (H + 1) / 2;  // chroma plane size (4:2:0 のサブサンプル)
        if (outFormat_ == OutputFormat::I420) {
            planes_.push_back({W,  H,  std::vector<uint8_t>((size_t)W * H,  0)});  // Y
            planes_.push_back({W2, H2, std::vector<uint8_t>((size_t)W2 * H2, 0)});  // U
            planes_.push_back({W2, H2, std::vector<uint8_t>((size_t)W2 * H2, 0)});  // V
        } else {
            // NV12 / NV21: 2 プレーン (Y + UV / VU interleaved)
            planes_.push_back({W,  H,  std::vector<uint8_t>((size_t)W * H,  0)});             // Y
            planes_.push_back({W2, H2, std::vector<uint8_t>((size_t)W2 * H2 * 2, 0)});         // UV (interleaved, 2 bytes/pixel)
        }
    }
    // movie-player から渡された VideoFrameInfo を内部バッファに row-by-row でコピー。
    // src stride と dst stride (= plane.width or width*4) が異なる場合に備え、 各 row
    // を個別に memcpy する。 RGBA mode は frame.planes[0] が w*h*4 の RGBA pack。
    // YUV mode は 2〜3 planes が plane size に応じてコピーされる。
    void copyPlanes_locked(const IMoviePlayer::VideoFrameInfo &frame) {
        auto copyPlane = [](uint8_t *dst, int dstStride,
                            const uint8_t *src, int srcStride,
                            int rowBytes, int rows)
        {
            if (!src || !dst || rowBytes <= 0 || rows <= 0) return;
            for (int y = 0; y < rows; y++) {
                memcpy(dst + (size_t)y * dstStride, src + (size_t)y * srcStride, rowBytes);
            }
        };
        if (outFormat_ == OutputFormat::RGBA) {
            // packed: planes[0] → pixels_ にコピー (data getter 用)
            if (frame.planeCount >= 1 && !pixels_.empty()) {
                const auto &P = frame.planes[IMoviePlayer::VIDEO_PLANE_PACKED];
                copyPlane(pixels_.data(), width_ * 4, P.data, P.stride, width_ * 4, height_);
            }
            return;
        }
        // YUV: planes_ に分割保持
        if (outFormat_ == OutputFormat::I420 && frame.planeCount >= 3 && planes_.size() >= 3) {
            const auto &Y = frame.planes[IMoviePlayer::VIDEO_PLANE_Y];
            const auto &U = frame.planes[IMoviePlayer::VIDEO_PLANE_U];
            const auto &V = frame.planes[IMoviePlayer::VIDEO_PLANE_V];
            copyPlane(planes_[0].data.data(), planes_[0].width, Y.data, Y.stride, planes_[0].width, planes_[0].height);
            copyPlane(planes_[1].data.data(), planes_[1].width, U.data, U.stride, planes_[1].width, planes_[1].height);
            copyPlane(planes_[2].data.data(), planes_[2].width, V.data, V.stride, planes_[2].width, planes_[2].height);
        } else if ((outFormat_ == OutputFormat::NV12 || outFormat_ == OutputFormat::NV21)
                   && frame.planeCount >= 2 && planes_.size() >= 2) {
            const auto &Y  = frame.planes[IMoviePlayer::VIDEO_PLANE_Y];
            const auto &UV = frame.planes[1];
            // NV の UV plane は 2 byte/pixel (UV interleaved) = plane.width * 2 byte/row
            copyPlane(planes_[0].data.data(), planes_[0].width,     Y.data,  Y.stride,  planes_[0].width, planes_[0].height);
            copyPlane(planes_[1].data.data(), planes_[1].width * 2, UV.data, UV.stride, planes_[1].width * 2, planes_[1].height);
        }
    }

    struct PlaneInfo {
        int width;
        int height;
        std::vector<uint8_t> data;
    };

    IMoviePlayer *player_ = nullptr;
    SDLAudioSink sink_;
    mutable std::mutex pixMu_;
    std::vector<uint8_t> pixels_;
    std::vector<PlaneInfo> planes_;       // YUV 要求時のみ非空。 RGBA は pixels_ から直接配信。
    OutputFormat outFormat_ = OutputFormat::RGBA;
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
    // optsObj: { loop?: bool, volume?: number, colorFormat?: "rgba"|"i420"|"nv12"|"nv21" }
    // colorFormat は open() より先に適用しないと初回 OnVideoDecoded で plane が組まれない。
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue cf = JS_GetPropertyStr(ctx, argv[1], "colorFormat");
        if (JS_IsString(cf)) {
            const char *s = JS_ToCString(ctx, cf);
            if (s) {
                if      (!strcmp(s, "rgba")) vp->setOutputFormat(JsVideoPlayer::OutputFormat::RGBA);
                else if (!strcmp(s, "i420")) vp->setOutputFormat(JsVideoPlayer::OutputFormat::I420);
                else if (!strcmp(s, "nv12")) vp->setOutputFormat(JsVideoPlayer::OutputFormat::NV12);
                else if (!strcmp(s, "nv21")) vp->setOutputFormat(JsVideoPlayer::OutputFormat::NV21);
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, cf);
    }
    if (!vp->open(rpath)) {
        delete vp;
        return JS_ThrowInternalError(ctx, "MoviePlayer: failed to open %s", rpath.c_str());
    }
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
// "rgba" / "i420" / "nv12" / "nv21" を返す。
static JSValue video_get_colorFormat(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    if (!p) return JS_NewString(ctx, "rgba");
    switch (p->outputFormat()) {
    case JsVideoPlayer::OutputFormat::I420: return JS_NewString(ctx, "i420");
    case JsVideoPlayer::OutputFormat::NV12: return JS_NewString(ctx, "nv12");
    case JsVideoPlayer::OutputFormat::NV21: return JS_NewString(ctx, "nv21");
    default: return JS_NewString(ctx, "rgba");
    }
}
static JSValue video_get_planeCount(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->planeCount() : 0);
}
// player.planes → [{width,height,data}, ...]。 colorFormat に応じて 1〜3 要素。
static JSValue video_get_planes(JSContext *ctx, JSValueConst this_val, int, JSValueConst*) {
    auto *p = get_player(ctx, this_val);
    if (!p) return JS_NULL;
    int n = p->planeCount();
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) {
        JSValue o = p->makePlaneObject(ctx, i);
        JS_SetPropertyUint32(ctx, arr, i, o);
    }
    return arr;
}
// player.getPlane(i) → 単一プレーン取得 (毎フレーム planes 配列を作るより安い)。
static JSValue video_getPlane(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *p = get_player(ctx, this_val); if (!p || argc < 1) return JS_NULL;
    int i = 0; JS_ToInt32(ctx, &i, argv[0]);
    return p->makePlaneObject(ctx, i);
}

static void video_bind_impl(JSContext *ctx) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClassID(rt, &g_class_id);
    JS_NewClass(rt, g_class_id, &g_class_def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "play",     JS_NewCFunction(ctx, video_play,     "play",     1));
    JS_SetPropertyStr(ctx, proto, "pause",    JS_NewCFunction(ctx, video_pause,    "pause",    0));
    JS_SetPropertyStr(ctx, proto, "resume",   JS_NewCFunction(ctx, video_resume,   "resume",   0));
    JS_SetPropertyStr(ctx, proto, "stop",     JS_NewCFunction(ctx, video_stop,     "stop",     0));
    JS_SetPropertyStr(ctx, proto, "seek",     JS_NewCFunction(ctx, video_seek,     "seek",     1));
    JS_SetPropertyStr(ctx, proto, "getPlane", JS_NewCFunction(ctx, video_getPlane, "getPlane", 1));

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
    defGetter("colorFormat", video_get_colorFormat);
    defGetter("planeCount",  video_get_planeCount);
    defGetter("planes",      video_get_planes);

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
