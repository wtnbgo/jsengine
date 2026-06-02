#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include "miniaudio.h"
#include <map>
#include <vector>
#include <mutex>

class AudioStream;

class AudioEngine
{
public:
    static AudioEngine& GetInstance();

    // コピー / ムーブ禁止
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    ~AudioEngine();

    // マスターボリューム (0 - 100)
    void SetMasterVolume(int v);
    int  GetMasterVolume() const;

    // グループボリューム (0 - 100)
    void SetVolume(int groupId, int v);
    int  GetVolume(int groupId) const;

    // ストリーム生成
    AudioStream* CreateStream(int groupId);

    // 停止
    void StopAll();
    void StopGroup(int groupId);

    // --- ポインタベースのグループ管理 (WebAudio AudioGroup 用) ---
    // 動的に作成・破棄する ma_sound_group。GetGroup(int) と違って ID マップを使わず、
    // 寿命を呼出元 (= JsAudioGroup) で管理する。
    //   parent: 親グループ。nullptr なら engine の endpoint にぶら下がる (= master 直下)
    ma_sound_group* CreateGroupNode(ma_sound_group* parent = nullptr);
    // group に attach されている全ストリームを endpoint に逃がしてから uninit する。
    void DestroyGroupNode(ma_sound_group* group);

    // --- 内部用 ---
    ma_engine*      GetEngine();
    ma_sound_group* GetGroup(int groupId);    // グループノードを取得(なければ作成)
    void RegisterStream(AudioStream* stream);
    void UnregisterStream(AudioStream* stream);
    
    // カスタムバックエンド情報取得 (デコーダ用)
    ma_decoding_backend_vtable** GetCustomBackendVTables();
    ma_uint32 GetCustomBackendCount();

private:
    AudioEngine();

    ma_resource_manager m_resourceManager;
    bool      m_resourceManagerInited;
    ma_engine m_engine;
    bool      m_engineInited;
    int       m_masterVolume;   // 0 - 100

    struct GroupInfo {
        ma_sound_group group;
        int            volume;  // 0 - 100
        bool           inited;
    };

    std::map<int, GroupInfo>     m_groups;
    std::vector<AudioStream*>   m_streams;
    // 動的に作成された ma_sound_group。ポインタ寿命を AudioEngine で所有する
    std::vector<ma_sound_group*> m_dynamicGroups;
    mutable std::mutex          m_mutex;

    void EnsureGroup(int groupId);
};

#endif // AUDIO_ENGINE_H
