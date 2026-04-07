/**
 * Web Audio API 互換バインディング
 *
 * AudioEngine/AudioStream (miniaudio ベース) を使用
 *
 * JS API:
 *   var ctx = new AudioContext()
 *   ctx.sampleRate
 *   ctx.destination             // ダミーノード
 *   ctx.state                   // "running"
 *   ctx.masterVolume = 1.0
 *   ctx.resume() / ctx.suspend() / ctx.close()
 *
 *   var source = ctx.createBufferSource(path)
 *   source.loop = false
 *   source.volume = 1.0
 *   source.pan = 0.0
 *   source.start() / source.stop()
 *   source.ended
 */

#include "webaudio.h"
#include "jsengine.hpp"
#include "audio/AudioEngine.h"
#include "audio/AudioStream.h"
#include <quickjs.h>
#include <SDL3/SDL.h>
#include <vector>
#include <string>

// ============================================================
// webaudio_init / webaudio_uninit
// ============================================================

bool webaudio_init()
{
    // AudioEngine はシングルトンなので GetInstance() で初期化される
    AudioEngine& engine = AudioEngine::GetInstance();
    if (!engine.GetEngine()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AudioEngine initialization failed");
        return false;
    }
    SDL_Log("WebAudio initialized");
    return true;
}

// 再生終了待ちストリーム（source_finalizer から移動される）
static std::vector<AudioStream*> g_playingStreams;

void webaudio_uninit()
{
    AudioEngine::GetInstance().StopAll();
    // 遅延破棄待ちのストリームを全て解放
    for (auto *s : g_playingStreams) {
        delete s;
    }
    g_playingStreams.clear();
}

// 再生完了したストリームを回収（update 等から呼ぶ）
void webaudio_gc()
{
    for (auto it = g_playingStreams.begin(); it != g_playingStreams.end(); ) {
        if (!(*it)->IsPlaying()) {
            delete *it;
            it = g_playingStreams.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// JS バインディング: AudioBufferSourceNode
// ============================================================

static JSClassID js_audio_source_class_id;

// ファイナライザ: 再生中なら g_playingStreams に移して延命
static void js_audio_source_finalizer(JSRuntime *rt, JSValue val) {
    AudioStream *stream = (AudioStream *)JS_GetOpaque(val, js_audio_source_class_id);
    if (stream) {
        if (stream->IsPlaying()) {
            // 再生中なので破棄を遅延
            g_playingStreams.push_back(stream);
        } else {
            delete stream;
        }
    }
}

static JSClassDef js_audio_source_class = {
    "AudioBufferSourceNode",
    js_audio_source_finalizer, // finalizer
};

static JSValue source_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    if (s) s->Play(s->GetLooping());
    return JS_UNDEFINED;
}

static JSValue source_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    if (s) s->Stop();
    return JS_UNDEFINED;
}

// volume getter/setter (0.0 ~ 1.0+)
static JSValue source_get_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    return JS_NewFloat64(ctx, s ? s->GetVolume() / 100.0 : 1.0);
}

static JSValue source_set_volume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    double vol;
    JS_ToFloat64(ctx, &vol, argv[0]);
    if (s) s->SetVolume((int)(vol * 100.0));
    return JS_UNDEFINED;
}

// pan getter/setter (-1.0 ~ 1.0)
static JSValue source_get_pan(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    return JS_NewFloat64(ctx, s ? s->GetPan() / 100.0 : 0.0);
}

static JSValue source_set_pan(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    double pan;
    JS_ToFloat64(ctx, &pan, argv[0]);
    if (s) s->SetPan((int)(pan * 100.0));
    return JS_UNDEFINED;
}

// loop getter/setter
static JSValue source_get_loop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    return JS_NewBool(ctx, s ? s->GetLooping() : 0);
}

static JSValue source_set_loop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    int loop = JS_ToBool(ctx, argv[0]);
    if (s) s->SetLooping(loop != 0);
    return JS_UNDEFINED;
}

// ended getter
static JSValue source_get_ended(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioStream *s = (AudioStream *)JS_GetOpaque(this_val, js_audio_source_class_id);
    return JS_NewBool(ctx, s ? !s->IsPlaying() : 1);
}

// ソースノードオブジェクトを生成
static JSValue push_source_node(JSContext *ctx, AudioStream *stream) {
    JSValue obj = JS_NewObjectClass(ctx, js_audio_source_class_id);
    JS_SetOpaque(obj, stream);

    // メソッド
    JS_SetPropertyStr(ctx, obj, "start", JS_NewCFunction(ctx, source_start, "start", 0));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, source_stop, "stop", 0));

    // volume getter/setter
    JSAtom atom = JS_NewAtom(ctx, "volume");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, source_get_volume, "get volume", 0),
        JS_NewCFunction(ctx, source_set_volume, "set volume", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // pan getter/setter
    atom = JS_NewAtom(ctx, "pan");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, source_get_pan, "get pan", 0),
        JS_NewCFunction(ctx, source_set_pan, "set pan", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // loop getter/setter
    atom = JS_NewAtom(ctx, "loop");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, source_get_loop, "get loop", 0),
        JS_NewCFunction(ctx, source_set_loop, "set loop", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // ended getter (read-only)
    atom = JS_NewAtom(ctx, "ended");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, source_get_ended, "get ended", 0),
        JS_UNDEFINED,
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    return obj;
}

// ============================================================
// JS バインディング: AudioContext
// ============================================================

// createBufferSource(path)
static JSValue actx_createBufferSource(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    JsEngine *engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;
    JS_FreeCString(ctx, path);

    AudioStream *stream = AudioEngine::GetInstance().CreateStream(0);
    if (!stream) {
        return JS_ThrowInternalError(ctx, "Failed to create audio stream");
    }

    if (!stream->Open(resolved.c_str())) {
        delete stream;
        return JS_ThrowInternalError(ctx, "Cannot load audio: %s", resolved.c_str());
    }

    return push_source_node(ctx, stream);
}

// resume()
static JSValue actx_resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// suspend()
static JSValue actx_suspend(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// close()
static JSValue actx_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AudioEngine::GetInstance().StopAll();
    return JS_UNDEFINED;
}

// sampleRate getter
static JSValue actx_get_sampleRate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ma_engine *eng = AudioEngine::GetInstance().GetEngine();
    if (eng) {
        return JS_NewUint32(ctx, ma_engine_get_sample_rate(eng));
    } else {
        return JS_NewUint32(ctx, 48000);
    }
}

// state getter
static JSValue actx_get_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewString(ctx, AudioEngine::GetInstance().GetEngine() ? "running" : "closed");
}

// masterVolume getter/setter
static JSValue actx_get_masterVolume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewFloat64(ctx, AudioEngine::GetInstance().GetMasterVolume() / 100.0);
}

static JSValue actx_set_masterVolume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double vol;
    JS_ToFloat64(ctx, &vol, argv[0]);
    AudioEngine::GetInstance().SetMasterVolume((int)(vol * 100.0));
    return JS_UNDEFINED;
}

// AudioContext コンストラクタ
static JSValue actx_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);

    // メソッド
    JS_SetPropertyStr(ctx, obj, "createBufferSource", JS_NewCFunction(ctx, actx_createBufferSource, "createBufferSource", 1));
    JS_SetPropertyStr(ctx, obj, "resume", JS_NewCFunction(ctx, actx_resume, "resume", 0));
    JS_SetPropertyStr(ctx, obj, "suspend", JS_NewCFunction(ctx, actx_suspend, "suspend", 0));
    JS_SetPropertyStr(ctx, obj, "close", JS_NewCFunction(ctx, actx_close, "close", 0));

    // destination ダミーノード
    JS_SetPropertyStr(ctx, obj, "destination", JS_NewObject(ctx));

    // sampleRate getter
    JSAtom atom = JS_NewAtom(ctx, "sampleRate");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, actx_get_sampleRate, "get sampleRate", 0),
        JS_UNDEFINED,
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // state getter
    atom = JS_NewAtom(ctx, "state");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, actx_get_state, "get state", 0),
        JS_UNDEFINED,
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    // masterVolume getter/setter
    atom = JS_NewAtom(ctx, "masterVolume");
    JS_DefinePropertyGetSet(ctx, obj, atom,
        JS_NewCFunction(ctx, actx_get_masterVolume, "get masterVolume", 0),
        JS_NewCFunction(ctx, actx_set_masterVolume, "set masterVolume", 1),
        JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);

    return obj;
}

// ============================================================
// バインディング登録
// ============================================================

void webaudio_bind(JSContext *ctx) {
    // AudioBufferSourceNode クラスを登録
    JS_NewClassID(JS_GetRuntime(ctx), &js_audio_source_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_audio_source_class_id, &js_audio_source_class);

    // AudioContext コンストラクタをグローバルに登録
    JSValue ctor = JS_NewCFunction2(ctx, actx_constructor, "AudioContext", 0, JS_CFUNC_constructor, 0);

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "AudioContext", ctor);
    JS_FreeValue(ctx, global);
}
