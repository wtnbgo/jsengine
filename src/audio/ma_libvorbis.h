/*
miniaudio libvorbis backend
libvorbisfile を使用した Ogg Vorbis デコーダー
*/
#ifndef MA_LIBVORBIS_H
#define MA_LIBVORBIS_H

#include "miniaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
ma_libvorbis データソース
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
    void* vf;  /* OggVorbis_File* */
} ma_libvorbis;

MA_API ma_result ma_libvorbis_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis);
MA_API ma_result ma_libvorbis_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis);
MA_API ma_result ma_libvorbis_init_memory(const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis);
MA_API void ma_libvorbis_uninit(ma_libvorbis* pVorbis, const ma_allocation_callbacks* pAllocationCallbacks);
MA_API ma_result ma_libvorbis_read_pcm_frames(ma_libvorbis* pVorbis, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead);
MA_API ma_result ma_libvorbis_seek_to_pcm_frame(ma_libvorbis* pVorbis, ma_uint64 frameIndex);
MA_API ma_result ma_libvorbis_get_data_format(ma_libvorbis* pVorbis, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap);
MA_API ma_result ma_libvorbis_get_cursor_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pCursor);
MA_API ma_result ma_libvorbis_get_length_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pLength);

/*
デコーダーバックエンド vtable
*/
extern ma_decoding_backend_vtable g_ma_decoding_backend_vtable_libvorbis;

#ifdef __cplusplus
}
#endif

#endif /* MA_LIBVORBIS_H */
