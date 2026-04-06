/*
miniaudio libopus backend
opusfile を使用した Opus デコーダー
*/
#ifndef MA_LIBOPUS_H
#define MA_LIBOPUS_H

#include "miniaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
ma_libopus データソース
*/
typedef struct
{
    ma_data_source_base ds;
    ma_read_proc onRead;
    ma_seek_proc onSeek;
    ma_tell_proc onTell;
    void* pReadSeekTellUserData;
    ma_format format;
    ma_uint32 channels;
    ma_uint32 sampleRate;
    void* of;  /* OggOpusFile* */
} ma_libopus;

MA_API ma_result ma_libopus_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus);
MA_API ma_result ma_libopus_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus);
MA_API ma_result ma_libopus_init_memory(const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus);
MA_API void ma_libopus_uninit(ma_libopus* pOpus, const ma_allocation_callbacks* pAllocationCallbacks);
MA_API ma_result ma_libopus_read_pcm_frames(ma_libopus* pOpus, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead);
MA_API ma_result ma_libopus_seek_to_pcm_frame(ma_libopus* pOpus, ma_uint64 frameIndex);
MA_API ma_result ma_libopus_get_data_format(ma_libopus* pOpus, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap);
MA_API ma_result ma_libopus_get_cursor_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pCursor);
MA_API ma_result ma_libopus_get_length_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pLength);

/*
デコーダーバックエンド vtable
*/
extern ma_decoding_backend_vtable g_ma_decoding_backend_vtable_libopus;

#ifdef __cplusplus
}
#endif

#endif /* MA_LIBOPUS_H */
