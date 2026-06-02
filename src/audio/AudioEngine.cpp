#include "AudioEngine.h"
#include "AudioStream.h"
#include <algorithm>

#ifdef HAS_VORBIS
#include "ma_libvorbis.h"
#endif
#ifdef HAS_OPUS
#include "ma_libopus.h"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// カスタムバックエンドテーブル
static ma_decoding_backend_vtable* g_pCustomBackendVTables[] = {
#ifdef HAS_VORBIS
    &g_ma_decoding_backend_vtable_libvorbis,
#endif
#ifdef HAS_OPUS
    &g_ma_decoding_backend_vtable_libopus,
#endif
    nullptr
};
static const ma_uint32 g_customBackendCount = (sizeof(g_pCustomBackendVTables) / sizeof(g_pCustomBackendVTables[0])) - 1;

// ============================================================
//  シングルトン
// ============================================================
AudioEngine& AudioEngine::GetInstance()
{
    static AudioEngine instance;
    return instance;
}

// ============================================================
//  コンストラクタ / デストラクタ
// ============================================================
AudioEngine::AudioEngine()
    : m_resourceManagerInited(false)
    , m_engineInited(false)
    , m_masterVolume(100)
{
    // リソースマネージャーを初期化（カスタムバックエンド付き）
    ma_resource_manager_config resourceManagerConfig = ma_resource_manager_config_init();
    resourceManagerConfig.ppCustomDecodingBackendVTables = g_pCustomBackendVTables;
    resourceManagerConfig.customDecodingBackendCount = sizeof(g_pCustomBackendVTables) / sizeof(g_pCustomBackendVTables[0]);
    resourceManagerConfig.pCustomDecodingBackendUserData = NULL;
    
    if (ma_resource_manager_init(&resourceManagerConfig, &m_resourceManager) == MA_SUCCESS) {
        m_resourceManagerInited = true;
    }
    
    // エンジンを初期化（リソースマネージャーを設定）
    ma_engine_config config = ma_engine_config_init();
    if (m_resourceManagerInited) {
        config.pResourceManager = &m_resourceManager;
    }
    
    if (ma_engine_init(&config, &m_engine) == MA_SUCCESS) {
        m_engineInited = true;
    }
}

AudioEngine::~AudioEngine()
{
    StopAll();

    // ストリームが残っている場合の安全対策
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_streams.clear();
    }

    // グループノード破棄
    for (auto& pair : m_groups) {
        if (pair.second.inited) {
            ma_sound_group_uninit(&pair.second.group);
        }
    }
    m_groups.clear();

    // 動的グループ破棄
    for (ma_sound_group* g : m_dynamicGroups) {
        if (g) {
            ma_sound_group_uninit(g);
            delete g;
        }
    }
    m_dynamicGroups.clear();

    if (m_engineInited) {
        ma_engine_uninit(&m_engine);
        m_engineInited = false;
    }
    
    if (m_resourceManagerInited) {
        ma_resource_manager_uninit(&m_resourceManager);
        m_resourceManagerInited = false;
    }
}

// ============================================================
//  マスターボリューム
// ============================================================
void AudioEngine::SetMasterVolume(int v)
{
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    m_masterVolume = v;
    if (m_engineInited) {
        ma_engine_set_volume(&m_engine, v / 100.0f);
    }
}

int AudioEngine::GetMasterVolume() const
{
    return m_masterVolume;
}

// ============================================================
//  グループボリューム
// ============================================================
void AudioEngine::SetVolume(int groupId, int v)
{
    if (v < 0)   v = 0;
    if (v > 100) v = 100;

    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureGroup(groupId);
    auto it = m_groups.find(groupId);
    if (it != m_groups.end() && it->second.inited) {
        it->second.volume = v;
        ma_sound_group_set_volume(&it->second.group, v / 100.0f);
    }
}

int AudioEngine::GetVolume(int groupId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_groups.find(groupId);
    if (it != m_groups.end()) {
        return it->second.volume;
    }
    return 100;
}

// ============================================================
//  停止
// ============================================================
// ============================================================
//  ストリーム生成
// ============================================================
AudioStream* AudioEngine::CreateStream(int groupId)
{
    return new AudioStream(this, groupId);
}

void AudioEngine::StopAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 全ストリームを停止
    for (auto* stream : m_streams) {
        stream->Stop();
    }
}

void AudioEngine::StopGroup(int groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* stream : m_streams) {
        if (stream->GetGroupId() == groupId) {
            stream->Stop();
        }
    }
}

// ============================================================
//  内部ユーティリティ
// ============================================================
ma_engine* AudioEngine::GetEngine()
{
    return m_engineInited ? &m_engine : nullptr;
}

ma_decoding_backend_vtable** AudioEngine::GetCustomBackendVTables()
{
    return g_pCustomBackendVTables;
}

ma_uint32 AudioEngine::GetCustomBackendCount()
{
    return g_customBackendCount;
}

ma_sound_group* AudioEngine::GetGroup(int groupId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureGroup(groupId);
    auto it = m_groups.find(groupId);
    if (it != m_groups.end() && it->second.inited) {
        return &it->second.group;
    }
    return nullptr;
}

ma_sound_group* AudioEngine::CreateGroupNode(ma_sound_group* parent)
{
    if (!m_engineInited) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);
    ma_sound_group* group = new ma_sound_group();
    ma_result r = ma_sound_group_init(&m_engine, 0, parent, group);
    if (r != MA_SUCCESS) {
        delete group;
        return nullptr;
    }
    m_dynamicGroups.push_back(group);
    return group;
}

void AudioEngine::DestroyGroupNode(ma_sound_group* group)
{
    if (!group) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    // この group に attach されているストリームを endpoint に逃がす
    // (ma_sound_group_uninit は子ノードの再 attach までは面倒見ない)
    if (m_engineInited) {
        ma_node* endpoint = ma_engine_get_endpoint(&m_engine);
        for (AudioStream* stream : m_streams) {
            if (stream && stream->m_soundInited) {
                // 出力先が group のものを endpoint に切り替える
                // (個別判定は AudioStream 側でやってもらうのが本来だが、
                //  ma_node API では attach 先の問い合わせができないので
                //  AudioStream に group ポインタを保持する形を取る)
                if (stream->m_currentGroup == group) {
                    ma_node_attach_output_bus(&stream->m_sound, 0, endpoint, 0);
                    stream->m_currentGroup = nullptr;
                }
            }
        }
    }

    ma_sound_group_uninit(group);
    auto it = std::find(m_dynamicGroups.begin(), m_dynamicGroups.end(), group);
    if (it != m_dynamicGroups.end()) m_dynamicGroups.erase(it);
    delete group;
}

void AudioEngine::EnsureGroup(int groupId)
{
    // 呼び出し元で m_mutex をロック済み前提
    if (m_groups.find(groupId) != m_groups.end()) {
        return;
    }
    
    // 先にマップにエントリを作成してから初期化（コピーを避ける）
    GroupInfo& info = m_groups[groupId];
    info.volume = 100;
    info.inited = false;
    
    if (m_engineInited) {
        ma_result result = ma_sound_group_init(&m_engine, 0, nullptr, &info.group);
        if (result == MA_SUCCESS) {
            info.inited = true;
        }
    }
}

void AudioEngine::RegisterStream(AudioStream* stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams.push_back(stream);
}

void AudioEngine::UnregisterStream(AudioStream* stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
        if (*it == stream) {
            m_streams.erase(it);
            return;
        }
    }
}
