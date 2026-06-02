/**
 * Web Audio API 互換バインディング
 *
 * AudioEngine/AudioStream (miniaudio ベース) を使用
 *
 * グラフモデル:
 *   AudioBufferSourceNode → [GainNode] → AudioContext.destination
 *
 * JS API:
 *   var ctx = new AudioContext()
 *   ctx.sampleRate / ctx.currentTime / ctx.state
 *   ctx.destination
 *   ctx.masterVolume = 1.0
 *   ctx.resume() / ctx.suspend() / ctx.close()
 *
 *   // 標準 API
 *   var buf    = await ctx.decodeAudioData(arrayBuffer)
 *   var source = ctx.createBufferSource()
 *   source.buffer = buf
 *   var gain   = ctx.createGain()
 *   gain.gain.value = 1.0
 *   gain.gain.setValueAtTime(1.0, ctx.currentTime)
 *   gain.gain.linearRampToValueAtTime(0.0, ctx.currentTime + 2.0)
 *   gain.gain.cancelScheduledValues(ctx.currentTime)
 *   source.connect(gain).connect(ctx.destination)
 *   source.start()
 *
 *   // 拡張 (jsengine 独自の簡略 API)
 *   var beep = ctx.createBufferSource("beep.wav")  // ファイル直接ロード
 *   beep.start()
 *
 *   source.loop / source.volume / source.pan / source.ended
 *   source.disconnect() / gain.disconnect()
 */

#include "webaudio.h"
#include "jsengine.hpp"
#include "audio/AudioEngine.h"
#include "audio/AudioStream.h"
#include <quickjs.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ============================================================
// 内部状態
// ============================================================

// AudioContext.currentTime (秒) — webaudio_update で進む
static double g_currentTimeSec = 0.0;

// AudioParam の自動化イベント
struct ParamEvent {
    enum Type { SetValue, LinearRamp, ExpRamp };
    Type   type;
    double value;
    double time; // 秒
};

// 前方宣言
struct JsAudioSource;
struct JsAudioGain;
struct JsAudioGroup;

// GainNode の内部状態
//   信号フローは src → gain → ... → destination の向き。
//   outputGain: 自分の出力を受ける次段の gain (gain→gain チェーン用)。
//   inputGains: 自分に入ってくる前段の gain 群 (値変更時に source 集合を辿るため)。
//   selfHold: WebAudio 仕様準拠の寿命管理用。下流に接続された時点で自分自身の JSValue
//             を JS_DupValue で保持しておき、disconnect/finalizer で開放する。これにより
//             JS 側で参照を捨てても接続中ノードが GC で消えず、グラフが切れない。
struct JsAudioGain {
    double baseValue = 1.0;                     // gain.value
    std::vector<ParamEvent> events;             // 時系列イベント (time 昇順)
    std::vector<JsAudioSource*> connectedSrcs;  // この gain に直接 connect() している source 群
    JsAudioGain* outputGain = nullptr;          // 自分の出力を受ける次段 gain (なければ direct/destination)
    std::vector<JsAudioGain*> inputGains;       // 自分の入力側に繋がっている前段 gain 群
    bool connectedToDestination = false;        // gain.connect(destination) されたか
    double cachedValue = 1.0;                   // 直前 tick で評価した baseValue/events 評価値
    JSContext* ctx = nullptr;                   // 自分の JSContext (selfHold 開放用)
    JSValue selfHold = JS_UNDEFINED;            // 接続中の自己参照 (WebAudio 仕様準拠の延命)
};

// AudioBufferSourceNode の内部状態
//   selfHold: source.start() で再生中の間、自分自身の JSValue を保持して GC を防ぐ。
//             webaudio_update で stream->IsPlaying() が false になったら開放。
//   group:    出力先 AudioGroup (jsengine 拡張)。nullptr なら endpoint (= master) 直結。
struct JsAudioSource {
    AudioStream* stream = nullptr;
    double localVolume = 1.0;          // source.volume (default 1.0)
    double localPan    = 0.0;          // source.pan
    bool   loop        = false;
    bool   started     = false;
    JsAudioGain* connectedGain = nullptr;
    bool   connectedToDestination = false; // gain を介さず destination に直結したか
    std::vector<uint8_t> encodedHold;  // .buffer = ab で割り当てたエンコード済バイト列 (ma_decoder が参照)
    JSContext* ctx = nullptr;          // 自分の JSContext (finalizer 用)
    JSValue selfHold = JS_UNDEFINED;   // 再生中の自己参照 (WebAudio 仕様準拠の延命)
    JsAudioGroup* group = nullptr;     // 所属する AudioGroup (jsengine 拡張、nullptr で master 直結)
};

// AudioGroup の内部状態 (jsengine 拡張、WebAudio に無い概念)
//   ma_sound_group の薄いラッパ。グループに attach した source は ma_node グラフ上で
//   group ノードを通って master に流れるので、GainNode チェーンと違って JS の GC とは
//   無関係に音量制御が効く。
struct JsAudioGroup {
    ma_sound_group* group = nullptr;   // AudioEngine::CreateGroupNode() で確保したノード
    double volume = 1.0;               // group.volume (0..1)
    std::vector<JsAudioSource*> sources; // この group に attach されている source 群 (group 破棄時に endpoint 退避用)
    JSContext* ctx = nullptr;
};

// AudioBuffer の内部状態 (decodeAudioData の戻り値)
struct JsAudioBuffer {
    std::vector<uint8_t> encoded; // エンコード済バイト列 (WAV/MP3/...). 複製してソースに配る
    uint32_t sampleRate  = 0;
    uint32_t numberOfChannels = 0;
    uint64_t length      = 0;     // チャンネルあたりのサンプル数
    double   duration    = 0.0;   // 秒
};

// 再生中のままファイナライズされた stream を保持する pending リスト
struct PendingStream {
    AudioStream* stream;
    std::vector<uint8_t> encodedHold;
};
static std::vector<PendingStream> g_pendingStreams;

// 生存中の GainNode 一覧 (ramp 更新用)
static std::vector<JsAudioGain*> g_allGains;

// 再生中で selfHold によって延命中の source 一覧 (webaudio_update で IsPlaying を見て開放)
static std::vector<JsAudioSource*> g_aliveSources;

// ノードの自己参照延命ヘルパー (WebAudio 仕様: 接続中 or 再生中のノードは alive)
static void source_keep_alive(JsAudioSource* s, JSContext* ctx, JSValueConst this_val) {
    if (!s) return;
    if (JS_IsUndefined(s->selfHold)) {
        s->selfHold = JS_DupValue(ctx, this_val);
        s->ctx = ctx;
        g_aliveSources.push_back(s);
    }
}
static void source_release_keep_alive(JsAudioSource* s) {
    if (!s) return;
    if (!JS_IsUndefined(s->selfHold) && s->ctx) {
        JSValue v = s->selfHold;
        s->selfHold = JS_UNDEFINED;
        g_aliveSources.erase(std::remove(g_aliveSources.begin(), g_aliveSources.end(), s), g_aliveSources.end());
        JS_FreeValue(s->ctx, v);  // ← 開放後に finalizer が走る可能性があるので最後
    }
}
static void gain_keep_alive(JsAudioGain* g, JSContext* ctx, JSValueConst this_val) {
    if (!g) return;
    if (JS_IsUndefined(g->selfHold)) {
        g->selfHold = JS_DupValue(ctx, this_val);
        g->ctx = ctx;
    }
}
static void gain_release_keep_alive(JsAudioGain* g) {
    if (!g) return;
    if (!JS_IsUndefined(g->selfHold) && g->ctx) {
        JSValue v = g->selfHold;
        g->selfHold = JS_UNDEFINED;
        JS_FreeValue(g->ctx, v);  // ← 開放後に finalizer が走る可能性があるので最後
    }
}

// クラス ID
static JSClassID js_audio_source_class_id;
static JSClassID js_audio_gain_class_id;
static JSClassID js_audio_buffer_class_id;
static JSClassID js_audio_param_class_id;  // gain.gain 用 (opaque は JsAudioGain* を共有、finalizer 無し)
static JSClassID js_audio_group_class_id;

// ============================================================
// 内部ヘルパー
// ============================================================

// AudioParam の自動化イベント列から、指定時刻の値を計算する
static double compute_param_value(const JsAudioGain* gain, double t) {
    if (gain->events.empty()) {
        return gain->baseValue;
    }

    double prevTime  = -1e18;
    double prevValue = gain->baseValue;

    for (const ParamEvent& e : gain->events) {
        if (t < e.time) {
            // (prevTime, e.time) の区間にいる
            if (e.type == ParamEvent::LinearRamp) {
                if (prevTime <= -1e17) {
                    // 直前イベントが無ければ baseValue を保持
                    return prevValue;
                }
                double r = (t - prevTime) / (e.time - prevTime);
                if (r < 0.0) r = 0.0;
                if (r > 1.0) r = 1.0;
                return prevValue + (e.value - prevValue) * r;
            } else if (e.type == ParamEvent::ExpRamp) {
                if (prevTime <= -1e17 || prevValue <= 0.0 || e.value <= 0.0) {
                    return prevValue;
                }
                double r = (t - prevTime) / (e.time - prevTime);
                if (r < 0.0) r = 0.0;
                if (r > 1.0) r = 1.0;
                return prevValue * std::pow(e.value / prevValue, r);
            } else {
                // SetValue が未来 → 直前値を維持
                return prevValue;
            }
        }
        prevTime  = e.time;
        prevValue = e.value;
    }
    return prevValue;
}

// source の実効ボリュームを miniaudio に反映する
// gain→gain チェーンも辿ってすべての段の cachedValue を掛け合わせる。
static void apply_source_volume(JsAudioSource* s) {
    if (!s || !s->stream) return;
    double effective = s->localVolume;
    JsAudioGain* g = s->connectedGain;
    int safety = 0;
    while (g && safety++ < 64) {  // 循環参照ガード (実際には作れないはず)
        effective *= g->cachedValue;
        g = g->outputGain;
    }
    if (effective < 0.0) effective = 0.0;
    int v = (int)(effective * 100.0 + 0.5);
    if (v < 0) v = 0;
    s->stream->SetVolume(v);
}

// 指定 gain (とその前段すべて) に繋がっている全 source の volume を再適用する。
// 主に master gain など下流側 gain の cachedValue が変わったとき呼ぶ。
static void apply_volume_to_subtree(JsAudioGain* g, int depth = 0) {
    if (!g || depth > 64) return;
    for (JsAudioSource* s : g->connectedSrcs) {
        apply_source_volume(s);
    }
    for (JsAudioGain* in : g->inputGains) {
        apply_volume_to_subtree(in, depth + 1);
    }
}

static void apply_source_pan(JsAudioSource* s) {
    if (!s || !s->stream) return;
    double p = s->localPan;
    if (p < -1.0) p = -1.0;
    if (p >  1.0) p =  1.0;
    s->stream->SetPan((int)(p * 100.0));
}

// AudioBuffer のメタ情報を ma_decoder で取得
static bool probe_audio_buffer(const uint8_t* data, size_t size,
                               uint32_t* outSampleRate, uint32_t* outChannels,
                               uint64_t* outLength, double* outDuration) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    AudioEngine& eng = AudioEngine::GetInstance();
    cfg.ppCustomBackendVTables = eng.GetCustomBackendVTables();
    cfg.customBackendCount     = eng.GetCustomBackendCount();

    ma_decoder dec;
    if (ma_decoder_init_memory(data, size, &cfg, &dec) != MA_SUCCESS) {
        return false;
    }
    ma_format fmt;
    ma_uint32 ch, sr;
    ma_decoder_get_data_format(&dec, &fmt, &ch, &sr, nullptr, 0);
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frames);
    ma_decoder_uninit(&dec);

    if (outSampleRate) *outSampleRate = sr;
    if (outChannels)   *outChannels   = ch;
    if (outLength)     *outLength     = frames;
    if (outDuration)   *outDuration   = sr > 0 ? (double)frames / (double)sr : 0.0;
    return true;
}

// ============================================================
// webaudio_init / uninit / update
// ============================================================

bool webaudio_init() {
    AudioEngine& engine = AudioEngine::GetInstance();
    if (!engine.GetEngine()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AudioEngine initialization failed");
        return false;
    }
    g_currentTimeSec = 0.0;
    SDL_Log("WebAudio initialized");
    return true;
}

void webaudio_uninit() {
    AudioEngine::GetInstance().StopAll();
    for (auto& p : g_pendingStreams) {
        delete p.stream;
    }
    g_pendingStreams.clear();
    // GainNode 自体は QuickJS の finalizer に任せる
}

void webaudio_update(uint32_t deltaMs) {
    g_currentTimeSec += (double)deltaMs / 1000.0;

    // GainNode の自動化を tick し、値が変わった gain の影響範囲 (前段サブツリー全体) に反映
    for (JsAudioGain* g : g_allGains) {
        double v = compute_param_value(g, g_currentTimeSec);
        if (v != g->cachedValue) {
            g->cachedValue = v;
            apply_volume_to_subtree(g);
        }
    }

    // 再生終了した「延命中の source」を回収。selfHold を開放することで JS 側から
    // 参照されていなければ finalizer が走り、stream も解放される。
    for (size_t i = g_aliveSources.size(); i-- > 0; ) {
        JsAudioSource* s = g_aliveSources[i];
        if (s->started && s->stream && !s->stream->IsPlaying()) {
            source_release_keep_alive(s);  // 配列から自身を削除する
        }
    }

    // 再生終了した pending stream を回収 (古い経路: 延命機構導入前の互換)
    for (auto it = g_pendingStreams.begin(); it != g_pendingStreams.end(); ) {
        if (!it->stream->IsPlaying()) {
            delete it->stream;
            it = g_pendingStreams.erase(it);
        } else {
            ++it;
        }
    }
}

// 旧 API 互換 (app.cpp が webaudio_gc() を呼ぶケース用)
void webaudio_gc() { webaudio_update(0); }

// ============================================================
// クラス finalizer
// ============================================================

static void js_audio_source_finalizer(JSRuntime* /*rt*/, JSValue val) {
    JsAudioSource* s = (JsAudioSource*)JS_GetOpaque(val, js_audio_source_class_id);
    if (!s) return;
    // 念のため g_aliveSources から外す (selfHold が UNDEFINED でないと finalizer は呼ばれないので
    // 通常ここには来ないが、防御的に)
    g_aliveSources.erase(std::remove(g_aliveSources.begin(), g_aliveSources.end(), s), g_aliveSources.end());
    // group からの参照を外す
    if (s->group) {
        auto& v = s->group->sources;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
        s->group = nullptr;
    }
    // gain からの参照を外す
    if (s->connectedGain) {
        auto& v = s->connectedGain->connectedSrcs;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
        s->connectedGain = nullptr;
    }
    if (s->stream) {
        if (s->stream->IsPlaying()) {
            // 再生中: pending に移して延命 (encoded メモリも一緒に移す)
            // 延命機構が機能していれば来ないが、未 start で接続されないまま GC される稀ケース用
            g_pendingStreams.push_back({ s->stream, std::move(s->encodedHold) });
        } else {
            delete s->stream;
        }
    }
    delete s;
}

static void js_audio_gain_finalizer(JSRuntime* /*rt*/, JSValue val) {
    JsAudioGain* g = (JsAudioGain*)JS_GetOpaque(val, js_audio_gain_class_id);
    if (!g) return;
    // つながっていた source 群から gain 参照を外す
    for (JsAudioSource* s : g->connectedSrcs) {
        if (s->connectedGain == g) {
            s->connectedGain = nullptr;
            apply_source_volume(s);
        }
    }
    // 自分の出力先 (outputGain) の inputGains リストから自分を取り除く
    if (g->outputGain) {
        auto& v = g->outputGain->inputGains;
        v.erase(std::remove(v.begin(), v.end(), g), v.end());
    }
    // 自分の入力側 gain (inputGains) の outputGain ポインタを null 化
    for (JsAudioGain* in : g->inputGains) {
        if (in->outputGain == g) in->outputGain = nullptr;
    }
    // グローバルリストから除去
    g_allGains.erase(std::remove(g_allGains.begin(), g_allGains.end(), g), g_allGains.end());
    delete g;
}

static void js_audio_buffer_finalizer(JSRuntime* /*rt*/, JSValue val) {
    JsAudioBuffer* b = (JsAudioBuffer*)JS_GetOpaque(val, js_audio_buffer_class_id);
    if (b) delete b;
}

static void js_audio_group_finalizer(JSRuntime* /*rt*/, JSValue val) {
    JsAudioGroup* g = (JsAudioGroup*)JS_GetOpaque(val, js_audio_group_class_id);
    if (!g) return;
    // group に attach されていた source の group ポインタを null 化
    // (AudioEngine::DestroyGroupNode 側で ma_node 再 attach も担当する)
    for (JsAudioSource* s : g->sources) {
        if (s->group == g) s->group = nullptr;
    }
    if (g->group) {
        AudioEngine::GetInstance().DestroyGroupNode(g->group);
        g->group = nullptr;
    }
    delete g;
}

static JSClassDef js_audio_source_class = { "AudioBufferSourceNode", js_audio_source_finalizer };
static JSClassDef js_audio_gain_class   = { "GainNode",              js_audio_gain_finalizer };
static JSClassDef js_audio_buffer_class = { "AudioBuffer",           js_audio_buffer_finalizer };
static JSClassDef js_audio_param_class  = { "AudioParam",            nullptr /* opaque は GainNode が所有 */ };
static JSClassDef js_audio_group_class  = { "AudioGroup",            js_audio_group_finalizer };

// ============================================================
// AudioBufferSourceNode メソッド
// ============================================================

static JsAudioSource* opaq_source(JSValueConst v) {
    return (JsAudioSource*)JS_GetOpaque(v, js_audio_source_class_id);
}
static JsAudioGain* opaq_gain(JSValueConst v) {
    return (JsAudioGain*)JS_GetOpaque(v, js_audio_gain_class_id);
}
static JsAudioBuffer* opaq_buffer(JSValueConst v) {
    return (JsAudioBuffer*)JS_GetOpaque(v, js_audio_buffer_class_id);
}
static JsAudioGroup* opaq_group(JSValueConst v) {
    return (JsAudioGroup*)JS_GetOpaque(v, js_audio_group_class_id);
}

// source を group に attach する (stream があれば即時 ma_node 再接続、なければ予約)
static void source_set_group(JsAudioSource* s, JsAudioGroup* newGroup) {
    if (!s) return;
    // 旧 group の sources リストから外す
    if (s->group && s->group != newGroup) {
        auto& v = s->group->sources;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
    }
    s->group = newGroup;
    if (newGroup) {
        // 重複登録防止
        auto& v = newGroup->sources;
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    }
    // stream があれば即時 attach (Open 前なら後で再呼出される)
    if (s->stream) {
        s->stream->SetGroupNode(newGroup ? newGroup->group : nullptr);
    }
}

static JSValue source_start(JSContext* ctx, JSValueConst this_val, int /*argc*/, JSValueConst* /*argv*/) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s || !s->stream) return JS_UNDEFINED;
    s->started = true;
    s->stream->Play(s->loop);
    // 再生中は JS 参照が切れても延命する (WebAudio 仕様)
    source_keep_alive(s, ctx, this_val);
    return JS_UNDEFINED;
}

static JSValue source_stop(JSContext* /*ctx*/, JSValueConst this_val, int /*argc*/, JSValueConst* /*argv*/) {
    JsAudioSource* s = opaq_source(this_val);
    if (s && s->stream) s->stream->Stop();
    // 延命解除は次の webaudio_update で IsPlaying==false を検知してから (即時 release も可だが、
    // stop() 直後に再 start() するケースを考えて遅延に統一)
    return JS_UNDEFINED;
}

static JSValue source_get_volume(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    return JS_NewFloat64(ctx, s ? s->localVolume : 1.0);
}
static JSValue source_set_volume(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    s->localVolume = v;
    apply_source_volume(s);
    return JS_UNDEFINED;
}

static JSValue source_get_pan(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    return JS_NewFloat64(ctx, s ? s->localPan : 0.0);
}
static JSValue source_set_pan(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    s->localPan = v;
    apply_source_pan(s);
    return JS_UNDEFINED;
}

static JSValue source_get_loop(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    return JS_NewBool(ctx, s ? s->loop : 0);
}
static JSValue source_set_loop(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    int b = JS_ToBool(ctx, argv[0]);
    s->loop = (b != 0);
    if (s->stream) s->stream->SetLooping(s->loop);
    return JS_UNDEFINED;
}

static JSValue source_get_ended(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    return JS_NewBool(ctx, (s && s->stream) ? !s->stream->IsPlaying() : 1);
}

// source.buffer = audioBuffer
static JSValue source_set_buffer(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    JsAudioBuffer* b = opaq_buffer(argv[0]);
    if (!b || b->encoded.empty()) {
        return JS_ThrowTypeError(ctx, "source.buffer requires an AudioBuffer");
    }
    // 既存 stream があれば破棄
    if (s->stream) {
        if (s->stream->IsPlaying()) {
            g_pendingStreams.push_back({ s->stream, std::move(s->encodedHold) });
        } else {
            delete s->stream;
        }
        s->stream = nullptr;
        s->encodedHold.clear();
    }
    // エンコード済バイトを複製して保持 (ma_decoder が参照するため寿命延長)
    s->encodedHold = b->encoded;
    AudioStream* stream = AudioEngine::GetInstance().CreateStream(0);
    if (!stream) {
        return JS_ThrowInternalError(ctx, "Failed to create audio stream");
    }
    if (!stream->Open((const char*)s->encodedHold.data(), s->encodedHold.size())) {
        delete stream;
        s->encodedHold.clear();
        return JS_ThrowInternalError(ctx, "Failed to decode AudioBuffer");
    }
    s->stream = stream;
    s->stream->SetLooping(s->loop);
    // group を先に設定済みなら、新しい stream にも反映する
    if (s->group) {
        s->stream->SetGroupNode(s->group->group);
    }
    apply_source_volume(s);
    apply_source_pan(s);
    return JS_UNDEFINED;
}

static JSValue source_get_buffer(JSContext* /*ctx*/, JSValueConst /*this_val*/, int, JSValueConst*) {
    // JSValue として保持していないので null を返す (set 値の保持は JS 側でやる)
    return JS_NULL;
}

// source.group = group / source.group = null
static JSValue source_get_group(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s || !s->group) return JS_NULL;
    // group の JSObject を直接保持していないので null を返す (JS 側で参照を保持する想定)
    return JS_NULL;
}
static JSValue source_set_group(JSContext* /*ctx*/, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    JsAudioGroup* g = opaq_group(argv[0]);
    source_set_group(s, g);
    return JS_UNDEFINED;
}

// source.connect(node) — node が GainNode か AudioContext.destination かを判別
static JSValue source_connect(JSContext* ctx, JSValueConst this_val, int /*argc*/, JSValueConst* argv) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    // 既存接続を外す
    if (s->connectedGain) {
        auto& v = s->connectedGain->connectedSrcs;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
        s->connectedGain = nullptr;
    }
    s->connectedToDestination = false;

    JsAudioGain* g = opaq_gain(argv[0]);
    if (g) {
        s->connectedGain = g;
        g->connectedSrcs.push_back(s);
    } else {
        // destination とみなす (AudioContext.destination は普通の JSObject)
        s->connectedToDestination = true;
    }
    apply_source_volume(s);
    // node を返してチェーン可能に
    return JS_DupValue(ctx, argv[0]);
}

static JSValue source_disconnect(JSContext* /*ctx*/, JSValueConst this_val, int, JSValueConst*) {
    JsAudioSource* s = opaq_source(this_val);
    if (!s) return JS_UNDEFINED;
    if (s->connectedGain) {
        auto& v = s->connectedGain->connectedSrcs;
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
        s->connectedGain = nullptr;
    }
    s->connectedToDestination = false;
    apply_source_volume(s);
    return JS_UNDEFINED;
}

// AudioBufferSourceNode オブジェクトを構築 (path 指定なら即時ロード)
static JSValue push_source_node(JSContext* ctx, AudioStream* preloadedStream) {
    JSValue obj = JS_NewObjectClass(ctx, js_audio_source_class_id);
    JsAudioSource* s = new JsAudioSource();
    s->ctx = ctx;
    s->stream = preloadedStream;
    if (preloadedStream) {
        preloadedStream->SetVolume(100);
        preloadedStream->SetPan(0);
    }
    JS_SetOpaque(obj, s);

    JS_SetPropertyStr(ctx, obj, "start",      JS_NewCFunction(ctx, source_start,      "start",      0));
    JS_SetPropertyStr(ctx, obj, "stop",       JS_NewCFunction(ctx, source_stop,       "stop",       0));
    JS_SetPropertyStr(ctx, obj, "connect",    JS_NewCFunction(ctx, source_connect,    "connect",    1));
    JS_SetPropertyStr(ctx, obj, "disconnect", JS_NewCFunction(ctx, source_disconnect, "disconnect", 0));

    auto def_acc = [&](const char* name, JSCFunction* getter, JSCFunction* setter) {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, obj, atom,
            getter ? JS_NewCFunction(ctx, getter, name, 0) : JS_UNDEFINED,
            setter ? JS_NewCFunction(ctx, setter, name, 1) : JS_UNDEFINED,
            JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, atom);
    };
    def_acc("volume", source_get_volume, source_set_volume);
    def_acc("pan",    source_get_pan,    source_set_pan);
    def_acc("loop",   source_get_loop,   source_set_loop);
    def_acc("ended",  source_get_ended,  nullptr);
    def_acc("buffer", source_get_buffer, source_set_buffer);
    def_acc("group",  source_get_group,  source_set_group);
    return obj;
}

// ============================================================
// GainNode の AudioParam (gain)
// ============================================================

// AudioParam オブジェクトは gain ノードと同じ opaque を共有する単純な実装
// (gain.gain は gain 自身に向くプロキシ的)

// AudioParam (gain.gain) — js_audio_param_class_id を使い、opaque は GainNode 側の JsAudioGain* を共有

static JsAudioGain* opaq_param(JSValueConst v) {
    return (JsAudioGain*)JS_GetOpaque(v, js_audio_param_class_id);
}

static JSValue gain_param_get_value(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioGain* g = opaq_param(this_val);
    return JS_NewFloat64(ctx, g ? g->baseValue : 1.0);
}
static JSValue gain_param_set_value(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioGain* g = opaq_param(this_val);
    if (!g) return JS_UNDEFINED;
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    g->baseValue = v;
    if (g->events.empty()) {
        g->cachedValue = v;
        apply_volume_to_subtree(g);
    }
    return JS_UNDEFINED;
}

static bool compare_event_time(const ParamEvent& a, const ParamEvent& b) {
    return a.time < b.time;
}

static JSValue gain_param_setValueAtTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsAudioGain* g = opaq_param(this_val);
    if (!g || argc < 2) return JS_UNDEFINED;
    double v, t;
    JS_ToFloat64(ctx, &v, argv[0]);
    JS_ToFloat64(ctx, &t, argv[1]);
    g->events.push_back({ ParamEvent::SetValue, v, t });
    std::sort(g->events.begin(), g->events.end(), compare_event_time);
    return JS_UNDEFINED;
}

static JSValue gain_param_linearRamp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsAudioGain* g = opaq_param(this_val);
    if (!g || argc < 2) return JS_UNDEFINED;
    double v, t;
    JS_ToFloat64(ctx, &v, argv[0]);
    JS_ToFloat64(ctx, &t, argv[1]);
    if (g->events.empty() || g->events.back().time > t) {
        double anchor = compute_param_value(g, g_currentTimeSec);
        g->events.push_back({ ParamEvent::SetValue, anchor, g_currentTimeSec });
    }
    g->events.push_back({ ParamEvent::LinearRamp, v, t });
    std::sort(g->events.begin(), g->events.end(), compare_event_time);
    return JS_UNDEFINED;
}

static JSValue gain_param_expRamp(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsAudioGain* g = opaq_param(this_val);
    if (!g || argc < 2) return JS_UNDEFINED;
    double v, t;
    JS_ToFloat64(ctx, &v, argv[0]);
    JS_ToFloat64(ctx, &t, argv[1]);
    if (g->events.empty() || g->events.back().time > t) {
        double anchor = compute_param_value(g, g_currentTimeSec);
        g->events.push_back({ ParamEvent::SetValue, anchor, g_currentTimeSec });
    }
    g->events.push_back({ ParamEvent::ExpRamp, v, t });
    std::sort(g->events.begin(), g->events.end(), compare_event_time);
    return JS_UNDEFINED;
}

static JSValue gain_param_cancel(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JsAudioGain* g = opaq_param(this_val);
    if (!g || argc < 1) return JS_UNDEFINED;
    double t; JS_ToFloat64(ctx, &t, argv[0]);
    g->events.erase(std::remove_if(g->events.begin(), g->events.end(),
        [t](const ParamEvent& e) { return e.time >= t; }), g->events.end());
    return JS_UNDEFINED;
}

// AudioParam オブジェクトを構築 (opaque として GainNode の JsAudioGain* を借りる)
static JSValue make_audio_param(JSContext* ctx, JSValueConst gain_node) {
    JSValue obj = JS_NewObjectClass(ctx, js_audio_param_class_id);
    JS_SetOpaque(obj, JS_GetOpaque(gain_node, js_audio_gain_class_id));

    JS_SetPropertyStr(ctx, obj, "setValueAtTime",          JS_NewCFunction(ctx, gain_param_setValueAtTime, "setValueAtTime", 2));
    JS_SetPropertyStr(ctx, obj, "linearRampToValueAtTime", JS_NewCFunction(ctx, gain_param_linearRamp,     "linearRampToValueAtTime", 2));
    JS_SetPropertyStr(ctx, obj, "exponentialRampToValueAtTime", JS_NewCFunction(ctx, gain_param_expRamp,   "exponentialRampToValueAtTime", 2));
    JS_SetPropertyStr(ctx, obj, "cancelScheduledValues",   JS_NewCFunction(ctx, gain_param_cancel,         "cancelScheduledValues", 1));

    JSAtom atom = JS_NewAtom(ctx, "value");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, gain_param_get_value, "get value", 0),
        JS_NewCFunction(ctx, gain_param_set_value, "set value", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    return obj;
}

// 既存接続を切る (gain → gain or gain → destination の片方が立っていれば外す)
static void gain_clear_output(JsAudioGain* g) {
    if (!g) return;
    if (g->outputGain) {
        auto& v = g->outputGain->inputGains;
        v.erase(std::remove(v.begin(), v.end(), g), v.end());
        g->outputGain = nullptr;
    }
    g->connectedToDestination = false;
}

// gain.connect / gain.disconnect
//   gain → gain チェーンと gain → destination の両方をサポート。
//   connect 後は前段サブツリーの実効ボリュームを再計算する。
static JSValue gain_connect(JSContext* ctx, JSValueConst this_val, int /*argc*/, JSValueConst* argv) {
    JsAudioGain* g = opaq_gain(this_val);
    if (!g) return JS_UNDEFINED;
    gain_clear_output(g);
    JsAudioGain* next = opaq_gain(argv[0]);
    if (next) {
        g->outputGain = next;
        next->inputGains.push_back(g);
    } else {
        // destination とみなす (AudioContext.destination は普通の JSObject)
        g->connectedToDestination = true;
    }
    // 接続中は JS 参照が切れても延命する (WebAudio 仕様)
    gain_keep_alive(g, ctx, this_val);
    // チェーン構成が変わったので、自分の前段にぶら下がる source 全てを再評価
    apply_volume_to_subtree(g);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue gain_disconnect(JSContext* /*ctx*/, JSValueConst this_val, int, JSValueConst*) {
    JsAudioGain* g = opaq_gain(this_val);
    if (!g) return JS_UNDEFINED;
    gain_clear_output(g);
    apply_volume_to_subtree(g);
    // 接続が無くなったので延命解除 (この後 JS 参照が無ければ finalizer が走る)
    gain_release_keep_alive(g);
    return JS_UNDEFINED;
}

// ============================================================
// AudioBuffer メソッド
// ============================================================

static JSValue ab_get_sampleRate(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioBuffer* b = opaq_buffer(this_val);
    return JS_NewUint32(ctx, b ? b->sampleRate : 0);
}
static JSValue ab_get_length(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioBuffer* b = opaq_buffer(this_val);
    return JS_NewInt64(ctx, b ? (int64_t)b->length : 0);
}
static JSValue ab_get_duration(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioBuffer* b = opaq_buffer(this_val);
    return JS_NewFloat64(ctx, b ? b->duration : 0.0);
}
static JSValue ab_get_numberOfChannels(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioBuffer* b = opaq_buffer(this_val);
    return JS_NewUint32(ctx, b ? b->numberOfChannels : 0);
}
// getChannelData は未実装 (再生のみの用途なので空 Float32Array を返す)
static JSValue ab_getChannelData(JSContext* ctx, JSValueConst /*this_val*/, int, JSValueConst*) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "Float32Array");
    JSValue len = JS_NewInt32(ctx, 0);
    JSValue arr = JS_CallConstructor(ctx, ctor, 1, &len);
    JS_FreeValue(ctx, len);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, global);
    return arr;
}

static JSValue make_audio_buffer(JSContext* ctx, JsAudioBuffer* b) {
    JSValue obj = JS_NewObjectClass(ctx, js_audio_buffer_class_id);
    JS_SetOpaque(obj, b);
    auto def_get = [&](const char* name, JSCFunction* fn) {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, obj, atom,
            JS_NewCFunction(ctx, fn, name, 0), JS_UNDEFINED, JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, atom);
    };
    def_get("sampleRate",       ab_get_sampleRate);
    def_get("length",           ab_get_length);
    def_get("duration",         ab_get_duration);
    def_get("numberOfChannels", ab_get_numberOfChannels);
    JS_SetPropertyStr(ctx, obj, "getChannelData", JS_NewCFunction(ctx, ab_getChannelData, "getChannelData", 1));
    return obj;
}

// ============================================================
// AudioContext メソッド
// ============================================================

// createBufferSource(): 空ソース / createBufferSource(path): ファイル直接読み込み (jsengine 拡張)
static JSValue actx_createBufferSource(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        return push_source_node(ctx, nullptr);
    }
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    JsEngine* engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;
    JS_FreeCString(ctx, path);

    AudioStream* stream = AudioEngine::GetInstance().CreateStream(0);
    if (!stream) {
        return JS_ThrowInternalError(ctx, "Failed to create audio stream");
    }
    if (!stream->Open(resolved.c_str())) {
        delete stream;
        return JS_ThrowInternalError(ctx, "Cannot load audio: %s", resolved.c_str());
    }
    return push_source_node(ctx, stream);
}

// AudioGroup プロパティ
static JSValue group_get_volume(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsAudioGroup* g = opaq_group(this_val);
    return JS_NewFloat64(ctx, g ? g->volume : 1.0);
}
static JSValue group_set_volume(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsAudioGroup* g = opaq_group(this_val);
    if (!g) return JS_UNDEFINED;
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    if (v < 0.0) v = 0.0;
    g->volume = v;
    if (g->group) {
        ma_sound_group_set_volume(g->group, (float)v);
    }
    return JS_UNDEFINED;
}

// createGroup(parent?) — jsengine 拡張 (WebAudio に無い)
static JSValue actx_createGroup(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    JsAudioGroup* parent = (argc >= 1) ? opaq_group(argv[0]) : nullptr;
    ma_sound_group* parentNode = parent ? parent->group : nullptr;

    ma_sound_group* node = AudioEngine::GetInstance().CreateGroupNode(parentNode);
    if (!node) {
        return JS_ThrowInternalError(ctx, "createGroup: failed to create sound group");
    }

    JSValue obj = JS_NewObjectClass(ctx, js_audio_group_class_id);
    JsAudioGroup* g = new JsAudioGroup();
    g->group = node;
    g->ctx = ctx;
    JS_SetOpaque(obj, g);

    JSAtom atom = JS_NewAtom(ctx, "volume");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, group_get_volume, "get volume", 0),
        JS_NewCFunction(ctx, group_set_volume, "set volume", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    return obj;
}

// createGain()
static JSValue actx_createGain(JSContext* ctx, JSValueConst /*this_val*/, int, JSValueConst*) {
    JSValue obj = JS_NewObjectClass(ctx, js_audio_gain_class_id);
    JsAudioGain* g = new JsAudioGain();
    JS_SetOpaque(obj, g);
    g_allGains.push_back(g);

    JS_SetPropertyStr(ctx, obj, "connect",    JS_NewCFunction(ctx, gain_connect,    "connect",    1));
    JS_SetPropertyStr(ctx, obj, "disconnect", JS_NewCFunction(ctx, gain_disconnect, "disconnect", 0));
    JS_SetPropertyStr(ctx, obj, "gain", make_audio_param(ctx, obj));
    return obj;
}

// decodeAudioData(arrayBuffer, [successCb, [errorCb]]) → Promise<AudioBuffer>
static JSValue actx_decodeAudioData(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "decodeAudioData requires an ArrayBuffer");
    }
    size_t size = 0;
    uint8_t* data = JS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!data) {
        // TypedArray の場合も受ける
        size_t offset = 0, length = 0, bpe = 0;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, &bpe);
        if (!JS_IsException(ab)) {
            size_t total = 0;
            uint8_t* buf = JS_GetArrayBuffer(ctx, &total, ab);
            JS_FreeValue(ctx, ab);
            if (buf) { data = buf + offset; size = length; }
        }
    }
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) return promise;

    if (!data || size == 0) {
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "decodeAudioData: invalid buffer"));
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return promise;
    }

    JsAudioBuffer* b = new JsAudioBuffer();
    b->encoded.assign(data, data + size);
    if (!probe_audio_buffer(b->encoded.data(), b->encoded.size(),
                            &b->sampleRate, &b->numberOfChannels, &b->length, &b->duration)) {
        delete b;
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "decodeAudioData: decode failed"));
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return promise;
    }

    JSValue audioBuf = make_audio_buffer(ctx, b);
    // 旧 API の success コールバックも一応サポート
    if (argc >= 2 && JS_IsFunction(ctx, argv[1])) {
        JSValue cb = JS_DupValue(ctx, argv[1]);
        JSValue r = JS_Call(ctx, cb, JS_UNDEFINED, 1, &audioBuf);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, cb);
    }
    JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &audioBuf);
    JS_FreeValue(ctx, audioBuf);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

static JSValue actx_resume(JSContext*, JSValueConst, int, JSValueConst*)  { return JS_UNDEFINED; }
static JSValue actx_suspend(JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }
static JSValue actx_close(JSContext*, JSValueConst, int, JSValueConst*) {
    AudioEngine::GetInstance().StopAll();
    return JS_UNDEFINED;
}

static JSValue actx_get_sampleRate(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    ma_engine* eng = AudioEngine::GetInstance().GetEngine();
    return JS_NewUint32(ctx, eng ? ma_engine_get_sample_rate(eng) : 48000);
}
static JSValue actx_get_currentTime(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx, g_currentTimeSec);
}
static JSValue actx_get_state(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewString(ctx, AudioEngine::GetInstance().GetEngine() ? "running" : "closed");
}
static JSValue actx_get_masterVolume(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx, AudioEngine::GetInstance().GetMasterVolume() / 100.0);
}
static JSValue actx_set_masterVolume(JSContext* ctx, JSValueConst, int, JSValueConst* argv) {
    double v; JS_ToFloat64(ctx, &v, argv[0]);
    AudioEngine::GetInstance().SetMasterVolume((int)(v * 100.0));
    return JS_UNDEFINED;
}

// AudioContext コンストラクタ
static JSValue actx_constructor(JSContext* ctx, JSValueConst /*new_target*/, int, JSValueConst*) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "createBufferSource", JS_NewCFunction(ctx, actx_createBufferSource, "createBufferSource", 1));
    JS_SetPropertyStr(ctx, obj, "createGain",         JS_NewCFunction(ctx, actx_createGain,         "createGain",         0));
    JS_SetPropertyStr(ctx, obj, "createGroup",        JS_NewCFunction(ctx, actx_createGroup,        "createGroup",        1));
    JS_SetPropertyStr(ctx, obj, "decodeAudioData",    JS_NewCFunction(ctx, actx_decodeAudioData,    "decodeAudioData",    3));
    JS_SetPropertyStr(ctx, obj, "resume",  JS_NewCFunction(ctx, actx_resume,  "resume",  0));
    JS_SetPropertyStr(ctx, obj, "suspend", JS_NewCFunction(ctx, actx_suspend, "suspend", 0));
    JS_SetPropertyStr(ctx, obj, "close",   JS_NewCFunction(ctx, actx_close,   "close",   0));
    JS_SetPropertyStr(ctx, obj, "destination", JS_NewObject(ctx));

    auto def_get = [&](const char* name, JSCFunction* fn, JSCFunction* setter = nullptr) {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, obj, atom,
            JS_NewCFunction(ctx, fn, name, 0),
            setter ? JS_NewCFunction(ctx, setter, name, 1) : JS_UNDEFINED,
            JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, atom);
    };
    def_get("sampleRate",   actx_get_sampleRate);
    def_get("currentTime",  actx_get_currentTime);
    def_get("state",        actx_get_state);
    def_get("masterVolume", actx_get_masterVolume, actx_set_masterVolume);
    return obj;
}

// ============================================================
// バインディング登録
// ============================================================

void webaudio_bind(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    JS_NewClassID(rt, &js_audio_source_class_id);
    JS_NewClass(rt, js_audio_source_class_id, &js_audio_source_class);
    JS_NewClassID(rt, &js_audio_gain_class_id);
    JS_NewClass(rt, js_audio_gain_class_id, &js_audio_gain_class);
    JS_NewClassID(rt, &js_audio_buffer_class_id);
    JS_NewClass(rt, js_audio_buffer_class_id, &js_audio_buffer_class);
    JS_NewClassID(rt, &js_audio_param_class_id);
    JS_NewClass(rt, js_audio_param_class_id, &js_audio_param_class);
    JS_NewClassID(rt, &js_audio_group_class_id);
    JS_NewClass(rt, js_audio_group_class_id, &js_audio_group_class);

    JSValue ctor = JS_NewCFunction2(ctx, actx_constructor, "AudioContext", 0, JS_CFUNC_constructor, 0);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "AudioContext", ctor);
    JS_FreeValue(ctx, global);
}
