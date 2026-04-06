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
#include <duktape.h>
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

void webaudio_uninit()
{
    AudioEngine::GetInstance().StopAll();
}

// ============================================================
// JS バインディング: AudioBufferSourceNode
// ============================================================

static AudioStream* get_audio_stream(duk_context *ctx) {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "\xff" "ptr");
    AudioStream *stream = (AudioStream*)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);
    return stream;
}

// ファイナライザ
static duk_ret_t source_finalizer(duk_context *ctx) {
    duk_get_prop_string(ctx, 0, "\xff" "ptr");
    if (duk_is_pointer(ctx, -1)) {
        AudioStream *stream = (AudioStream*)duk_get_pointer(ctx, -1);
        delete stream;
    }
    duk_pop(ctx);
    return 0;
}

static duk_ret_t source_start(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    if (s) s->Play(s->GetLooping());
    return 0;
}

static duk_ret_t source_stop(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    if (s) s->Stop();
    return 0;
}

// volume getter/setter (0.0 ~ 1.0+)
static duk_ret_t source_get_volume(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    duk_push_number(ctx, s ? s->GetVolume() / 100.0 : 1.0);
    return 1;
}

static duk_ret_t source_set_volume(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    float vol = (float)duk_require_number(ctx, 0);
    if (s) s->SetVolume((int)(vol * 100.0f));
    return 0;
}

// pan getter/setter (-1.0 ~ 1.0)
static duk_ret_t source_get_pan(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    duk_push_number(ctx, s ? s->GetPan() / 100.0 : 0.0);
    return 1;
}

static duk_ret_t source_set_pan(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    float pan = (float)duk_require_number(ctx, 0);
    if (s) s->SetPan((int)(pan * 100.0f));
    return 0;
}

// loop getter/setter
static duk_ret_t source_get_loop(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    duk_push_boolean(ctx, s ? s->GetLooping() : 0);
    return 1;
}

static duk_ret_t source_set_loop(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    bool loop = duk_require_boolean(ctx, 0) != 0;
    if (s) s->SetLooping(loop);
    return 0;
}

// ended getter
static duk_ret_t source_get_ended(duk_context *ctx) {
    AudioStream *s = get_audio_stream(ctx);
    duk_push_boolean(ctx, s ? !s->IsPlaying() : 1);
    return 1;
}

static void push_source_node(duk_context *ctx, AudioStream *stream) {
    duk_idx_t obj = duk_push_object(ctx);

    duk_push_pointer(ctx, stream);
    duk_put_prop_string(ctx, obj, "\xff" "ptr");

    duk_push_c_function(ctx, source_start, 0);
    duk_put_prop_string(ctx, obj, "start");
    duk_push_c_function(ctx, source_stop, 0);
    duk_put_prop_string(ctx, obj, "stop");

    // volume
    duk_push_string(ctx, "volume");
    duk_push_c_function(ctx, source_get_volume, 0);
    duk_push_c_function(ctx, source_set_volume, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);

    // pan
    duk_push_string(ctx, "pan");
    duk_push_c_function(ctx, source_get_pan, 0);
    duk_push_c_function(ctx, source_set_pan, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);

    // loop
    duk_push_string(ctx, "loop");
    duk_push_c_function(ctx, source_get_loop, 0);
    duk_push_c_function(ctx, source_set_loop, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);

    // ended
    duk_push_string(ctx, "ended");
    duk_push_c_function(ctx, source_get_ended, 0);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_ENUMERABLE);

    // ファイナライザ
    duk_push_c_function(ctx, source_finalizer, 1);
    duk_set_finalizer(ctx, obj);
}

// ============================================================
// JS バインディング: AudioContext
// ============================================================

// createBufferSource(path)
static duk_ret_t actx_createBufferSource(duk_context *ctx) {
    const char *path = duk_require_string(ctx, 0);

    JsEngine *engine = JsEngine::getInstance();
    std::string resolved = engine ? engine->resolvePath(path) : path;

    AudioStream *stream = AudioEngine::GetInstance().CreateStream(0);
    if (!stream) {
        return duk_error(ctx, DUK_ERR_ERROR, "Failed to create audio stream");
    }

    if (!stream->Open(resolved.c_str())) {
        delete stream;
        return duk_error(ctx, DUK_ERR_ERROR, "Cannot load audio: %s", resolved.c_str());
    }

    push_source_node(ctx, stream);
    return 1;
}

// resume()
static duk_ret_t actx_resume(duk_context *ctx) {
    (void)ctx;
    return 0;
}

// suspend()
static duk_ret_t actx_suspend(duk_context *ctx) {
    (void)ctx;
    return 0;
}

// close()
static duk_ret_t actx_close(duk_context *ctx) {
    AudioEngine::GetInstance().StopAll();
    (void)ctx;
    return 0;
}

// sampleRate getter
static duk_ret_t actx_get_sampleRate(duk_context *ctx) {
    ma_engine *eng = AudioEngine::GetInstance().GetEngine();
    if (eng) {
        duk_push_uint(ctx, ma_engine_get_sample_rate(eng));
    } else {
        duk_push_uint(ctx, 48000);
    }
    return 1;
}

// state getter
static duk_ret_t actx_get_state(duk_context *ctx) {
    duk_push_string(ctx, AudioEngine::GetInstance().GetEngine() ? "running" : "closed");
    return 1;
}

// masterVolume getter/setter
static duk_ret_t actx_get_masterVolume(duk_context *ctx) {
    duk_push_number(ctx, AudioEngine::GetInstance().GetMasterVolume() / 100.0);
    return 1;
}

static duk_ret_t actx_set_masterVolume(duk_context *ctx) {
    float vol = (float)duk_require_number(ctx, 0);
    AudioEngine::GetInstance().SetMasterVolume((int)(vol * 100.0f));
    return 0;
}

// AudioContext コンストラクタ
static duk_ret_t actx_constructor(duk_context *ctx) {
    if (!duk_is_constructor_call(ctx)) {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);
    duk_idx_t obj = duk_get_top_index(ctx);

    duk_push_c_function(ctx, actx_createBufferSource, 1);
    duk_put_prop_string(ctx, obj, "createBufferSource");
    duk_push_c_function(ctx, actx_resume, 0);
    duk_put_prop_string(ctx, obj, "resume");
    duk_push_c_function(ctx, actx_suspend, 0);
    duk_put_prop_string(ctx, obj, "suspend");
    duk_push_c_function(ctx, actx_close, 0);
    duk_put_prop_string(ctx, obj, "close");

    duk_push_object(ctx);
    duk_put_prop_string(ctx, obj, "destination");

    duk_push_string(ctx, "sampleRate");
    duk_push_c_function(ctx, actx_get_sampleRate, 0);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_ENUMERABLE);

    duk_push_string(ctx, "state");
    duk_push_c_function(ctx, actx_get_state, 0);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_ENUMERABLE);

    duk_push_string(ctx, "masterVolume");
    duk_push_c_function(ctx, actx_get_masterVolume, 0);
    duk_push_c_function(ctx, actx_set_masterVolume, 1);
    duk_def_prop(ctx, obj, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);

    duk_pop(ctx);
    return 0;
}

// ============================================================
// バインディング登録
// ============================================================

void webaudio_bind(duk_context *ctx) {
    duk_push_c_function(ctx, actx_constructor, 0);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_global_string(ctx, "AudioContext");
}
