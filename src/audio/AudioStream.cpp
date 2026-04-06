#include "AudioStream.h"
#include "AudioEngine.h"
#include <cstring>
#include <SDL3/SDL.h>

namespace {
    class SDLAudioStream : public IAudioReadStream {
    public:
        SDLAudioStream(const char* filename) : m_refCount(1), m_io(nullptr) {
            m_io = SDL_IOFromFile(filename, "rb");
        }
    
        virtual ~SDLAudioStream() {
            if (m_io) {
                SDL_CloseIO(m_io);
            }
        }
    
        bool IsValid() const { return m_io != nullptr; }
    
        virtual int AddRef(void) override {
            return ++m_refCount;
        }
    
        virtual int Release(void) override {
            int ref = --m_refCount;
            if (ref == 0) {
                delete this;
            }
            return ref;
        }
    
        virtual size_t Read(void *buf, size_t size) override {
            if (!m_io) return 0;
            return SDL_ReadIO(m_io, buf, size);
        }
    
        virtual int64_t Tell() const override {
            if (!m_io) return -1;
            return SDL_TellIO(m_io);
        }
    
        virtual void Seek(int64_t offset, int origin) override {
            if (!m_io) return;
            SDL_IOWhence whence = SDL_IO_SEEK_SET;
            switch (origin) {
                case 0: whence = SDL_IO_SEEK_SET; break;
                case 1: whence = SDL_IO_SEEK_CUR; break;
                case 2: whence = SDL_IO_SEEK_END; break;
            }
            SDL_SeekIO(m_io, offset, whence);
        }
    
        virtual size_t Size() const override {
            if (!m_io) return 0;
            return (size_t)SDL_GetIOSize(m_io);
        }
    
    private:
        int m_refCount;
        SDL_IOStream* m_io;
    };
}

// ============================================================
//  コンストラクタ / デストラクタ
// ============================================================
AudioStream::AudioStream(AudioEngine* engine, int groupId)
    : m_engine(engine)
    , m_groupId(groupId)
    , m_decoderInited(false)
    , m_soundInited(false)
    , m_state(SoundState_None)
    , m_paused(false)
    , m_volume(100)
    , m_pan(0)
    , m_frequency(0)
    , m_baseFrequency(0)
    , m_readStream(nullptr)
{
    memset(&m_decoder, 0, sizeof(m_decoder));
    memset(&m_sound, 0, sizeof(m_sound));

    if (m_engine) {
        m_engine->RegisterStream(this);
    }
}

AudioStream::~AudioStream()
{
    if (m_soundInited) {
        ma_sound_uninit(&m_sound);
        m_soundInited = false;
    }
    if (m_decoderInited) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInited = false;
    }
    if (m_readStream) {
        m_readStream->Release();
        m_readStream = nullptr;
    }
    if (m_engine) {
        m_engine->UnregisterStream(this);
    }
}

// ============================================================
//  コールバック
// ============================================================
void AudioStream::addStatusCallback(std::function<void(SoundState)> callback)
{
    m_callbacks.push_back(callback);
}

void AudioStream::ChangeState(SoundState newState)
{
    if (m_state == newState) return;
    m_state = newState;
    for (auto& cb : m_callbacks) {
        cb(m_state);
    }
}

void AudioStream::OnSoundEnd(void* pUserData, ma_sound* /*pSound*/)
{
    AudioStream* self = static_cast<AudioStream*>(pUserData);
    if (self) {
        self->ChangeState(SoundState_Stopped);
    }
}

// ============================================================
//  内部ヘルパー
// ============================================================
void AudioStream::CleanupDecoder()
{
    if (m_soundInited) {
        ma_sound_uninit(&m_sound);
        m_soundInited = false;
    }
    if (m_decoderInited) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInited = false;
    }
    if (m_readStream) {
        m_readStream->Release();
        m_readStream = nullptr;
    }
    ChangeState(SoundState_None);
}

bool AudioStream::InitSoundFromDecoder()
{
    ma_engine* pEngine = m_engine->GetEngine();

    // デコーダからフォーマット情報取得
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    ma_decoder_get_data_format(&m_decoder, &format, &channels, &sampleRate, nullptr, 0);
    
    if (channels == 0) {
        return false;
    }
    
    m_baseFrequency = (int)sampleRate;
    m_frequency     = m_baseFrequency;

    // グループ取得
    ma_sound_group* pGroup = m_engine->GetGroup(m_groupId);

    ma_sound_config soundConfig = ma_sound_config_init_2(pEngine);
    soundConfig.pDataSource = &m_decoder;
    soundConfig.pInitialAttachment = nullptr;
    soundConfig.flags = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;

    ma_result result = ma_sound_init_ex(pEngine, &soundConfig, &m_sound);
    if (result != MA_SUCCESS) {
        return false;
    }
    m_soundInited = true;
    
    // グループまたはエンドポイントに接続
    if (pGroup) {
        ma_node_attach_output_bus(&m_sound, 0, pGroup, 0);
    } else {
        ma_node_attach_output_bus(&m_sound, 0, ma_engine_get_endpoint(pEngine), 0);
    }

    // 終了コールバック設定
    ma_sound_set_end_callback(&m_sound, OnSoundEnd, this);

    // 初期パラメータ反映
    ma_sound_set_volume(&m_sound, m_volume / 100.0f);
    ma_sound_set_pan(&m_sound, m_pan / 100.0f);

    ChangeState(SoundState_Ready);
    return true;
}

// ============================================================
//  miniaudio デコーダ用ブリッジコールバック
// ============================================================
ma_result AudioStream::OnDecoderRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
    IAudioReadStream* stream = static_cast<IAudioReadStream*>(pDecoder->pUserData);
    if (!stream) return MA_ERROR;

    size_t read = stream->Read(pBufferOut, bytesToRead);
    if (pBytesRead) *pBytesRead = read;

    if (read == 0) return MA_AT_END;
    if (read < bytesToRead) return MA_AT_END;
    return MA_SUCCESS;
}

ma_result AudioStream::OnDecoderSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin)
{
    IAudioReadStream* stream = static_cast<IAudioReadStream*>(pDecoder->pUserData);
    if (!stream) return MA_ERROR;

    int seekOrigin;
    switch (origin) {
        case ma_seek_origin_start:   seekOrigin = 0; break; // SEEK_SET
        case ma_seek_origin_current: seekOrigin = 1; break; // SEEK_CUR
        case ma_seek_origin_end:     seekOrigin = 2; break; // SEEK_END
        default:                     seekOrigin = 0; break;
    }
    stream->Seek(byteOffset, seekOrigin);
    return MA_SUCCESS;
}

ma_result AudioStream::OnDecoderTell(ma_decoder* pDecoder, ma_int64* pCursor)
{
    IAudioReadStream* stream = static_cast<IAudioReadStream*>(pDecoder->pUserData);
    if (!stream) return MA_ERROR;

    int64_t pos = stream->Tell();
    if (pos < 0) return MA_ERROR;

    if (pCursor) *pCursor = pos;
    return MA_SUCCESS;
}

// ============================================================
//  Open (メモリ)
// ============================================================
bool AudioStream::Open(const char* data, size_t size)
{
    CleanupDecoder();

    ma_engine* pEngine = m_engine ? m_engine->GetEngine() : nullptr;
    if (!pEngine) return false;

    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        0,
        ma_engine_get_sample_rate(pEngine)
    );
    
    // カスタムバックエンドを設定 (Vorbis サポート用)
    decoderConfig.ppCustomBackendVTables = m_engine->GetCustomBackendVTables();
    decoderConfig.customBackendCount = m_engine->GetCustomBackendCount();

    ma_result result = ma_decoder_init_memory(data, size, &decoderConfig, &m_decoder);
    if (result != MA_SUCCESS) {
        return false;
    }
    m_decoderInited = true;

    if (!InitSoundFromDecoder()) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInited = false;
        return false;
    }
    return true;
}

// ============================================================
//  Open (ファイル名)
// ============================================================
bool AudioStream::Open(const char* filename)
{
    SDLAudioStream* io = new SDLAudioStream(filename);
    if (!io->IsValid()) {
        io->Release();
        return false;
    }

    bool result = Open(io);
    io->Release();
    return result;
}

// ============================================================
//  Open (IAudioReadStream)
// ============================================================
bool AudioStream::Open(IAudioReadStream* stream)
{
    if (!stream) return false;
    CleanupDecoder();

    ma_engine* pEngine = m_engine ? m_engine->GetEngine() : nullptr;
    if (!pEngine) return false;

    // ストリームの参照カウントを増やして保持
    stream->AddRef();
    m_readStream = stream;

    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        0,
        ma_engine_get_sample_rate(pEngine)
    );
    
    // カスタムバックエンドを設定 (Vorbis サポート用)
    decoderConfig.ppCustomBackendVTables = m_engine->GetCustomBackendVTables();
    decoderConfig.customBackendCount = m_engine->GetCustomBackendCount();

    ma_result result = ma_decoder_init_ex(OnDecoderRead, OnDecoderSeek, OnDecoderTell, m_readStream, &decoderConfig, &m_decoder);
    if (result != MA_SUCCESS) {
        m_readStream->Release();
        m_readStream = nullptr;
        return false;
    }
    m_decoderInited = true;

    if (!InitSoundFromDecoder()) {
        ma_decoder_uninit(&m_decoder);
        m_decoderInited = false;
        m_readStream->Release();
        m_readStream = nullptr;
        return false;
    }
    return true;
}

// ============================================================
//  諸元取得
// ============================================================
int AudioStream::GetBitsPerSample() const
{
    if (!m_decoderInited) return 0;
    ma_format format;
    ma_uint32 channels, sampleRate;
    ma_decoder_get_data_format(const_cast<ma_decoder*>(&m_decoder),
                               &format, &channels, &sampleRate, nullptr, 0);
    switch (format) {
        case ma_format_u8:  return 8;
        case ma_format_s16: return 16;
        case ma_format_s24: return 24;
        case ma_format_s32: return 32;
        case ma_format_f32: return 32;
        default:            return 0;
    }
}

int AudioStream::GetChannels() const
{
    if (!m_decoderInited) return 0;
    ma_format format;
    ma_uint32 channels, sampleRate;
    ma_decoder_get_data_format(const_cast<ma_decoder*>(&m_decoder),
                               &format, &channels, &sampleRate, nullptr, 0);
    return (int)channels;
}

uint64_t AudioStream::GetTotalTime()
{
    if (!m_soundInited) return 0;
    float lengthInSeconds = 0;
    ma_sound_get_length_in_seconds(&m_sound, &lengthInSeconds);
    return (uint64_t)(lengthInSeconds * 1000.0f);
}

uint64_t AudioStream::GetTotalSamples()
{
    if (!m_soundInited) return 0;
    ma_uint64 length = 0;
    ma_sound_get_length_in_pcm_frames(&m_sound, &length);
    return (uint64_t)length;
}

// ============================================================
//  ループエリア設定
// ============================================================
void AudioStream::SetLoopArea(int startSample, int endSample)
{
    if (!m_soundInited) return;
    ma_data_source* pDS = ma_sound_get_data_source(&m_sound);
    if (!pDS) return;

    ma_uint64 loopEnd = (endSample < 0) ? ~(ma_uint64)0 : (ma_uint64)endSample;
    ma_data_source_set_loop_point_in_pcm_frames(pDS, (ma_uint64)startSample, loopEnd);
}

// ============================================================
//  再生制御
// ============================================================
void AudioStream::Play(bool loop)
{
    if (!m_soundInited) return;

    // 先頭に戻す
    ma_sound_seek_to_pcm_frame(&m_sound, 0);
    ma_sound_set_looping(&m_sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_reset_stop_time_and_fade(&m_sound);

    ma_result result = ma_sound_start(&m_sound);
    if (result == MA_SUCCESS) {
        m_paused = false;
        ChangeState(SoundState_Playing);
    }
}

void AudioStream::Stop()
{
    if (!m_soundInited) return;
    ma_sound_stop(&m_sound);
    m_paused = false;
    ChangeState(SoundState_Stopped);
}

// ============================================================
//  再生位置
// ============================================================
uint64_t AudioStream::GetPosition()
{
    if (!m_soundInited) return 0;
    float cursor = 0;
    ma_sound_get_cursor_in_seconds(&m_sound, &cursor);
    return (uint64_t)(cursor * 1000.0f);
}

void AudioStream::SetPosition(uint64_t pos)
{
    if (!m_soundInited || m_baseFrequency == 0) return;
    ma_uint64 frame = (ma_uint64)((double)pos / 1000.0 * m_baseFrequency);
    ma_sound_seek_to_pcm_frame(&m_sound, frame);
}

uint64_t AudioStream::GetSamplePosition()
{
    if (!m_soundInited) return 0;
    ma_uint64 cursor = 0;
    ma_sound_get_cursor_in_pcm_frames(&m_sound, &cursor);
    return (uint64_t)cursor;
}

void AudioStream::SetSamplePosition(uint64_t pos)
{
    if (!m_soundInited) return;
    ma_sound_seek_to_pcm_frame(&m_sound, (ma_uint64)pos);
}

// ============================================================
//  ポーズ
// ============================================================
bool AudioStream::GetPaused() const
{
    return m_paused;
}

void AudioStream::SetPaused(bool b)
{
    if (!m_soundInited) return;

    if (b && !m_paused) {
        ma_sound_stop(&m_sound);
        m_paused = true;
        ChangeState(SoundState_Paused);
    } else if (!b && m_paused) {
        ma_sound_reset_stop_time_and_fade(&m_sound);
        ma_sound_start(&m_sound);
        m_paused = false;
        ChangeState(SoundState_Playing);
    }
}

// ============================================================
//  ループ
// ============================================================
void AudioStream::SetLooping(bool b)
{
    if (!m_soundInited) return;
    ma_sound_set_looping(&m_sound, b ? MA_TRUE : MA_FALSE);
}

bool AudioStream::GetLooping() const
{
    if (!m_soundInited) return false;
    return ma_sound_is_looping(const_cast<ma_sound*>(&m_sound)) != MA_FALSE;
}

// ============================================================
//  音量 (0 - 100)
// ============================================================
void AudioStream::SetVolume(int v)
{
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    m_volume = v;
    if (m_soundInited) {
        ma_sound_set_volume(&m_sound, v / 100.0f);
    }
}

int AudioStream::GetVolume() const
{
    return m_volume;
}

// ============================================================
//  パン (-100 ~ 100)
// ============================================================
void AudioStream::SetPan(int v)
{
    if (v < -100) v = -100;
    if (v > 100)  v = 100;
    m_pan = v;
    if (m_soundInited) {
        ma_sound_set_pan(&m_sound, v / 100.0f);
    }
}

int AudioStream::GetPan() const
{
    return m_pan;
}

// ============================================================
//  周波数
// ============================================================
int AudioStream::GetFrequency() const
{
    return m_frequency;
}

void AudioStream::SetFrequency(int freq)
{
    if (freq <= 0 || m_baseFrequency <= 0) return;
    m_frequency = freq;
    if (m_soundInited) {
        // pitch = 目標周波数 / 元の周波数
        float pitch = (float)freq / (float)m_baseFrequency;
        ma_sound_set_pitch(&m_sound, pitch);
    }
}

// ============================================================
//  再生中判定
// ============================================================
bool AudioStream::IsPlaying() const
{
    if (!m_soundInited) return false;
    return ma_sound_is_playing(const_cast<ma_sound*>(&m_sound)) != MA_FALSE;
}
