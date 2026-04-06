/*
miniaudio libvorbis backend implementation
libvorbisfile を使用した Ogg Vorbis デコーダー
*/

#include "ma_libvorbis.h"

#include <vorbis/vorbisfile.h>
#include <string.h>
#include <stdlib.h>

/* ======================================================
   コールバックベースのストリーム用 vorbisfile コールバック
   ====================================================== */

typedef struct {
    ma_read_proc onRead;
    ma_seek_proc onSeek;
    ma_tell_proc onTell;
    void* pUserData;
} ma_libvorbis_read_context;

static size_t ma_libvorbis_vf_read(void* ptr, size_t size, size_t nmemb, void* datasource)
{
    ma_libvorbis_read_context* ctx = (ma_libvorbis_read_context*)datasource;
    size_t bytesToRead = size * nmemb;
    size_t bytesRead = 0;
    
    if (ctx->onRead == NULL) {
        return 0;
    }
    
    ma_result result = ctx->onRead(ctx->pUserData, ptr, bytesToRead, &bytesRead);
    if (result != MA_SUCCESS) {
        return 0;
    }
    
    return bytesRead / size;
}

static int ma_libvorbis_vf_seek(void* datasource, ogg_int64_t offset, int whence)
{
    ma_libvorbis_read_context* ctx = (ma_libvorbis_read_context*)datasource;
    ma_seek_origin origin;
    
    if (ctx->onSeek == NULL) {
        return -1;
    }
    
    switch (whence) {
        case SEEK_SET: origin = ma_seek_origin_start;   break;
        case SEEK_CUR: origin = ma_seek_origin_current; break;
        case SEEK_END: origin = ma_seek_origin_end;     break;
        default: return -1;
    }
    
    ma_result result = ctx->onSeek(ctx->pUserData, (ma_int64)offset, origin);
    return (result == MA_SUCCESS) ? 0 : -1;
}

static int ma_libvorbis_vf_close(void* datasource)
{
    /* 何もしない - miniaudio側で管理 */
    (void)datasource;
    return 0;
}

static long ma_libvorbis_vf_tell(void* datasource)
{
    ma_libvorbis_read_context* ctx = (ma_libvorbis_read_context*)datasource;
    ma_int64 cursor = 0;
    
    if (ctx->onTell == NULL) {
        return -1;
    }
    
    ma_result result = ctx->onTell(ctx->pUserData, &cursor);
    if (result != MA_SUCCESS) {
        return -1;
    }
    
    return (long)cursor;
}

/* ======================================================
   ma_data_source インターフェース
   ====================================================== */

static ma_result ma_libvorbis_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_libvorbis_read_pcm_frames((ma_libvorbis*)pDataSource, pFramesOut, frameCount, pFramesRead);
}

static ma_result ma_libvorbis_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    return ma_libvorbis_seek_to_pcm_frame((ma_libvorbis*)pDataSource, frameIndex);
}

static ma_result ma_libvorbis_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_libvorbis_get_data_format((ma_libvorbis*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_libvorbis_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_libvorbis_get_cursor_in_pcm_frames((ma_libvorbis*)pDataSource, pCursor);
}

static ma_result ma_libvorbis_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    return ma_libvorbis_get_length_in_pcm_frames((ma_libvorbis*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_libvorbis_ds_vtable = {
    ma_libvorbis_ds_read,
    ma_libvorbis_ds_seek,
    ma_libvorbis_ds_get_data_format,
    ma_libvorbis_ds_get_cursor,
    ma_libvorbis_ds_get_length,
    NULL, /* onSetLooping */
    0     /* flags */
};

/* ======================================================
   ma_libvorbis 初期化/解放
   ====================================================== */

static ma_result ma_libvorbis_init_internal(const ma_decoding_backend_config* pConfig, ma_libvorbis* pVorbis)
{
    ma_result result;
    ma_data_source_config dataSourceConfig;

    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    memset(pVorbis, 0, sizeof(*pVorbis));

    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_libvorbis_ds_vtable;
    
    result = ma_data_source_init(&dataSourceConfig, &pVorbis->ds);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Vorbis は常に float を出力 */
    pVorbis->format = ma_format_f32;

    return MA_SUCCESS;
}

MA_API ma_result ma_libvorbis_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    ma_result result;
    OggVorbis_File* vf;
    ma_libvorbis_read_context* ctx;
    ov_callbacks callbacks;
    vorbis_info* info;
    int ovResult;

    result = ma_libvorbis_init_internal(pConfig, pVorbis);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* コンテキスト確保 */
    ctx = (ma_libvorbis_read_context*)ma_malloc(sizeof(ma_libvorbis_read_context), pAllocationCallbacks);
    if (ctx == NULL) {
        ma_data_source_uninit(&pVorbis->ds);
        return MA_OUT_OF_MEMORY;
    }
    ctx->onRead = onRead;
    ctx->onSeek = onSeek;
    ctx->onTell = onTell;
    ctx->pUserData = pReadSeekTellUserData;

    /* OggVorbis_File 確保 */
    vf = (OggVorbis_File*)ma_malloc(sizeof(OggVorbis_File), pAllocationCallbacks);
    if (vf == NULL) {
        ma_free(ctx, pAllocationCallbacks);
        ma_data_source_uninit(&pVorbis->ds);
        return MA_OUT_OF_MEMORY;
    }

    /* vorbisfile コールバック設定 */
    callbacks.read_func  = ma_libvorbis_vf_read;
    callbacks.seek_func  = ma_libvorbis_vf_seek;
    callbacks.close_func = ma_libvorbis_vf_close;
    callbacks.tell_func  = ma_libvorbis_vf_tell;

    ovResult = ov_open_callbacks(ctx, vf, NULL, 0, callbacks);
    if (ovResult < 0) {
        ma_free(vf, pAllocationCallbacks);
        ma_free(ctx, pAllocationCallbacks);
        ma_data_source_uninit(&pVorbis->ds);
        return MA_INVALID_FILE;
    }

    /* フォーマット情報取得 */
    info = ov_info(vf, -1);
    if (info == NULL) {
        ov_clear(vf);
        ma_free(vf, pAllocationCallbacks);
        ma_free(ctx, pAllocationCallbacks);
        ma_data_source_uninit(&pVorbis->ds);
        return MA_INVALID_FILE;
    }

    pVorbis->onRead = onRead;
    pVorbis->onSeek = onSeek;
    pVorbis->onTell = onTell;
    pVorbis->pReadSeekTellUserData = ctx;  /* ctx を保存 */
    pVorbis->channels = (ma_uint32)info->channels;
    pVorbis->sampleRate = (ma_uint32)info->rate;
    pVorbis->vf = vf;

    return MA_SUCCESS;
}

MA_API ma_result ma_libvorbis_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    ma_result result;
    OggVorbis_File* vf;
    vorbis_info* info;
    int ovResult;

    if (pFilePath == NULL) {
        return MA_INVALID_ARGS;
    }

    result = ma_libvorbis_init_internal(pConfig, pVorbis);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* OggVorbis_File 確保 */
    vf = (OggVorbis_File*)ma_malloc(sizeof(OggVorbis_File), pAllocationCallbacks);
    if (vf == NULL) {
        ma_data_source_uninit(&pVorbis->ds);
        return MA_OUT_OF_MEMORY;
    }

    ovResult = ov_fopen(pFilePath, vf);
    if (ovResult < 0) {
        ma_free(vf, pAllocationCallbacks);
        ma_data_source_uninit(&pVorbis->ds);
        return MA_INVALID_FILE;
    }

    /* フォーマット情報取得 */
    info = ov_info(vf, -1);
    if (info == NULL) {
        ov_clear(vf);
        ma_free(vf, pAllocationCallbacks);
        ma_data_source_uninit(&pVorbis->ds);
        return MA_INVALID_FILE;
    }

    pVorbis->channels = (ma_uint32)info->channels;
    pVorbis->sampleRate = (ma_uint32)info->rate;
    pVorbis->vf = vf;

    return MA_SUCCESS;
}

MA_API ma_result ma_libvorbis_init_memory(const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    /* メモリからの初期化は未実装 - 必要に応じて実装 */
    (void)pData;
    (void)dataSize;
    (void)pConfig;
    (void)pAllocationCallbacks;
    (void)pVorbis;
    return MA_NOT_IMPLEMENTED;
}

MA_API void ma_libvorbis_uninit(ma_libvorbis* pVorbis, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pVorbis == NULL) {
        return;
    }

    if (pVorbis->vf != NULL) {
        ov_clear((OggVorbis_File*)pVorbis->vf);
        ma_free(pVorbis->vf, pAllocationCallbacks);
        pVorbis->vf = NULL;
    }

    /* コールバックモードの場合、コンテキストを解放 */
    if (pVorbis->pReadSeekTellUserData != NULL && pVorbis->onRead != NULL) {
        ma_free(pVorbis->pReadSeekTellUserData, pAllocationCallbacks);
        pVorbis->pReadSeekTellUserData = NULL;
    }

    ma_data_source_uninit(&pVorbis->ds);
}

/* ======================================================
   PCMフレーム読み取り
   ====================================================== */

MA_API ma_result ma_libvorbis_read_pcm_frames(ma_libvorbis* pVorbis, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    OggVorbis_File* vf;
    ma_uint64 totalFramesRead = 0;
    float* pOutput = (float*)pFramesOut;

    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (pVorbis == NULL || pVorbis->vf == NULL) {
        return MA_INVALID_ARGS;
    }

    vf = (OggVorbis_File*)pVorbis->vf;

    while (totalFramesRead < frameCount) {
        float** pcm;
        int currentSection;
        long samplesRead;
        ma_uint64 framesToRead = frameCount - totalFramesRead;
        
        /* ov_read_float は最大でもある程度のサンプル数しか返さない */
        if (framesToRead > 8192) {
            framesToRead = 8192;
        }

        samplesRead = ov_read_float(vf, &pcm, (int)framesToRead, &currentSection);
        
        if (samplesRead <= 0) {
            if (samplesRead == 0) {
                /* EOF */
                break;
            }
            /* エラー */
            return MA_ERROR;
        }

        /* インターリーブ形式に変換 */
        if (pOutput != NULL) {
            for (long i = 0; i < samplesRead; i++) {
                for (ma_uint32 ch = 0; ch < pVorbis->channels; ch++) {
                    pOutput[(totalFramesRead + i) * pVorbis->channels + ch] = pcm[ch][i];
                }
            }
        }

        totalFramesRead += (ma_uint64)samplesRead;
    }

    if (pFramesRead != NULL) {
        *pFramesRead = totalFramesRead;
    }

    return (totalFramesRead > 0) ? MA_SUCCESS : MA_AT_END;
}

/* ======================================================
   シーク
   ====================================================== */

MA_API ma_result ma_libvorbis_seek_to_pcm_frame(ma_libvorbis* pVorbis, ma_uint64 frameIndex)
{
    OggVorbis_File* vf;
    int result;

    if (pVorbis == NULL || pVorbis->vf == NULL) {
        return MA_INVALID_ARGS;
    }

    vf = (OggVorbis_File*)pVorbis->vf;
    result = ov_pcm_seek(vf, (ogg_int64_t)frameIndex);
    
    return (result == 0) ? MA_SUCCESS : MA_ERROR;
}

/* ======================================================
   フォーマット情報取得
   ====================================================== */

MA_API ma_result ma_libvorbis_get_data_format(ma_libvorbis* pVorbis, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    if (pFormat != NULL) {
        *pFormat = pVorbis->format;
    }
    if (pChannels != NULL) {
        *pChannels = pVorbis->channels;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = pVorbis->sampleRate;
    }
    if (pChannelMap != NULL) {
        ma_channel_map_init_standard(ma_standard_channel_map_vorbis, pChannelMap, channelMapCap, pVorbis->channels);
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_libvorbis_get_cursor_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pCursor)
{
    OggVorbis_File* vf;
    ogg_int64_t cursor;

    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;

    if (pVorbis == NULL || pVorbis->vf == NULL) {
        return MA_INVALID_ARGS;
    }

    vf = (OggVorbis_File*)pVorbis->vf;
    cursor = ov_pcm_tell(vf);
    
    if (cursor < 0) {
        return MA_ERROR;
    }

    *pCursor = (ma_uint64)cursor;
    return MA_SUCCESS;
}

MA_API ma_result ma_libvorbis_get_length_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pLength)
{
    OggVorbis_File* vf;
    ogg_int64_t length;

    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;

    if (pVorbis == NULL || pVorbis->vf == NULL) {
        return MA_INVALID_ARGS;
    }

    vf = (OggVorbis_File*)pVorbis->vf;
    length = ov_pcm_total(vf, -1);
    
    if (length < 0) {
        return MA_ERROR;
    }

    *pLength = (ma_uint64)length;
    return MA_SUCCESS;
}

/* ======================================================
   デコーダーバックエンド vtable 実装
   ====================================================== */

static ma_result ma_decoding_backend_init__libvorbis(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(ma_libvorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;
    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libvorbis(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(ma_libvorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init_file(pFilePath, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;
    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_memory__libvorbis(void* pUserData, const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(ma_libvorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init_memory(pData, dataSize, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;
    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libvorbis(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    (void)pUserData;

    ma_libvorbis_uninit((ma_libvorbis*)pBackend, pAllocationCallbacks);
    ma_free(pBackend, pAllocationCallbacks);
}

ma_decoding_backend_vtable g_ma_decoding_backend_vtable_libvorbis = {
    ma_decoding_backend_init__libvorbis,
    ma_decoding_backend_init_file__libvorbis,
    NULL,  /* onInitFileW - 未実装 */
    ma_decoding_backend_init_memory__libvorbis,
    ma_decoding_backend_uninit__libvorbis
};
