#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include "miniaudio.h"
#include <cstdint>
#include <functional>
#include <vector>

class AudioEngine;

class IAudioReadStream {
public:
    virtual int AddRef(void) = 0;
    virtual int Release(void) = 0;
    virtual size_t Read(void *buf, size_t size) = 0;
    virtual int64_t Tell() const = 0;
    virtual void Seek(int64_t offset, int origin) = 0;
    virtual size_t Size() const = 0;
};

enum SoundState
{
    SoundState_None,     // サウンドが開かれていない状態
    SoundState_Ready,    // サウンドが開かれて再生可能な状態
    SoundState_Playing,  // サウンドが再生中の状態
    SoundState_Paused,   // サウンドが一時停止中の状態
    SoundState_Stopped   // サウンドが停止している状態
};

class AudioStream
{
    friend class AudioEngine;
public:
    ~AudioStream();

    // コピー / ムーブ禁止
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    // コールバック登録
    void addStatusCallback(std::function<void(SoundState)> callback);

    // ファイルオープン (メモリ上のデータから)
    bool Open(const char* data, size_t size);

    // ファイルオープン (ファイル名から)
    bool Open(const char* filename);

    // ファイルオープン (ストリームから)
    bool Open(IAudioReadStream* stream);

    // 開いたファイルの諸元
    int GetBitsPerSample() const;
    int GetChannels() const;

    uint64_t GetTotalTime();     // 総再生時間 (ms)
    uint64_t GetTotalSamples();  // 総サンプル数

    // ループエリア設定 (sample)  endが -1 の場合は末尾
    void SetLoopArea(int startSample, int endSample);

    // 再生制御
    void Play(bool loop = false);
    void Stop();

    // 再生位置 (ms)
    uint64_t GetPosition();
    void     SetPosition(uint64_t pos);

    // 再生位置 (sample)
    uint64_t GetSamplePosition();
    void     SetSamplePosition(uint64_t pos);

    // ポーズ
    bool GetPaused() const;
    void SetPaused(bool b);

    // ループ
    void SetLooping(bool b);
    bool GetLooping() const;

    // 音量 (0 - 100)
    void SetVolume(int v);
    int  GetVolume() const;

    // パン (-100 ~ 100)
    void SetPan(int v);
    int  GetPan() const;

    // 周波数
    int  GetFrequency() const;
    void SetFrequency(int freq);

    // 再生中判定
    bool IsPlaying() const;

    // グループID取得 (内部用)
    int GetGroupId() const { return m_groupId; }

private:
    /**
     * AudioStreamはAudioEngine::CreateStream()経由で作成される。
     */
    AudioStream(AudioEngine* engine, int groupId);

    void CleanupDecoder();
    bool InitSoundFromDecoder();

    void ChangeState(SoundState newState);
    static void OnSoundEnd(void* pUserData, ma_sound* pSound);

    // miniaudio デコーダ用ブリッジコールバック
    static ma_result OnDecoderRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead);
    static ma_result OnDecoderSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin);
    static ma_result OnDecoderTell(ma_decoder* pDecoder, ma_int64* pCursor);

    AudioEngine*  m_engine;
    int           m_groupId;

    ma_decoder    m_decoder;
    bool          m_decoderInited;

    ma_sound      m_sound;
    bool          m_soundInited;

    SoundState    m_state;
    bool          m_paused;
    int           m_volume;      // 0 - 100
    int           m_pan;         // -100 ~ 100
    int           m_frequency;   // 元のサンプルレート
    int           m_baseFrequency;

    IAudioReadStream* m_readStream;  // Open(IAudioReadStream*) 時に保持

    std::vector<std::function<void(SoundState)>> m_callbacks;
};

#endif // AUDIO_STREAM_H
