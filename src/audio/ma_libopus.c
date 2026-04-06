/*
miniaudio libopus backend implementation
opusfile を使用した Opus デコーダー
*/

#include "ma_libopus.h"

#include <opusfile.h>
#include <string.h>
#include <stdlib.h>

/* ======================================================
   コールバックベースのストリーム用 opusfile コールバック
   ====================================================== */

typedef struct {
    ma_read_proc onRead;
    ma_seek_proc onSeek;
    ma_tell_proc onTell;
    void* pUserData;
} ma_libopus_read_context;

static int ma_libopus_of_read(void* _stream, unsigned char* _ptr, int _nbytes)
{
    ma_libopus_read_context* ctx = (ma_libopus_read_context*)_stream;
    size_t bytesRead = 0;
    
    if (ctx->onRead == NULL || _nbytes <= 0) {
        return 0;
    }
    
    ma_result result = ctx->onRead(ctx->pUserData, _ptr, (size_t)_nbytes, &bytesRead);
    if (result != MA_SUCCESS) {
        return -1;  /* エラー */
    }
    
    return (int)bytesRead;
}

static int ma_libopus_of_seek(void* _stream, opus_int64 _offset, int _whence)
{
    ma_libopus_read_context* ctx = (ma_libopus_read_context*)_stream;
    ma_seek_origin origin;
    
    if (ctx->onSeek == NULL) {
        return -1;
    }
    
    switch (_whence) {
        case SEEK_SET: origin = ma_seek_origin_start;   break;
        case SEEK_CUR: origin = ma_seek_origin_current; break;
        case SEEK_END: origin = ma_seek_origin_end;     break;
        default: return -1;
    }
    
    ma_result result = ctx->onSeek(ctx->pUserData, (ma_int64)_offset, origin);
    return (result == MA_SUCCESS) ? 0 : -1;
}

static opus_int64 ma_libopus_of_tell(void* _stream)
{
    ma_libopus_read_context* ctx = (ma_libopus_read_context*)_stream;
    ma_int64 cursor = 0;
    
    if (ctx->onTell == NULL) {
        return -1;
    }
    
    ma_result result = ctx->onTell(ctx->pUserData, &cursor);
    if (result != MA_SUCCESS) {
        return -1;
    }
    
    return (opus_int64)cursor;
}

static int ma_libopus_of_close(void* _stream)
{
    /* 何もしない - miniaudio側で管理 */
    (void)_stream;
    return 0;
}

/* ======================================================
   ma_data_source インターフェース
   ====================================================== */

static ma_result ma_libopus_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_libopus_read_pcm_frames((ma_libopus*)pDataSource, pFramesOut, frameCount, pFramesRead);
}

static ma_result ma_libopus_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    return ma_libopus_seek_to_pcm_frame((ma_libopus*)pDataSource, frameIndex);
}

static ma_result ma_libopus_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_libopus_get_data_format((ma_libopus*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_libopus_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_libopus_get_cursor_in_pcm_frames((ma_libopus*)pDataSource, pCursor);
}

static ma_result ma_libopus_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    return ma_libopus_get_length_in_pcm_frames((ma_libopus*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_libopus_ds_vtable = {
    ma_libopus_ds_read,
    ma_libopus_ds_seek,
    ma_libopus_ds_get_data_format,
    ma_libopus_ds_get_cursor,
    ma_libopus_ds_get_length,
    NULL, /* onSetLooping */
    0     /* flags */
};

/* ======================================================
   ma_libopus 初期化/解放
   ====================================================== */

static ma_result ma_libopus_init_internal(const ma_decoding_backend_config* pConfig, ma_libopus* pOpus)
{
    ma_result result;
    ma_data_source_config dataSourceConfig;

    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    memset(pOpus, 0, sizeof(*pOpus));

    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_libopus_ds_vtable;
    
    result = ma_data_source_init(&dataSourceConfig, &pOpus->ds);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Opus は常に float を出力 */
    pOpus->format = ma_format_f32;
    
    /* Opus は常に 48kHz */
    pOpus->sampleRate = 48000;

    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus)
{
    ma_result result;
    OggOpusFile* of;
    ma_libopus_read_context* ctx;
    OpusFileCallbacks callbacks;
    int opusError;
    const OpusHead* head;

    result = ma_libopus_init_internal(pConfig, pOpus);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* コンテキスト確保 */
    ctx = (ma_libopus_read_context*)ma_malloc(sizeof(ma_libopus_read_context), pAllocationCallbacks);
    if (ctx == NULL) {
        ma_data_source_uninit(&pOpus->ds);
        return MA_OUT_OF_MEMORY;
    }
    ctx->onRead = onRead;
    ctx->onSeek = onSeek;
    ctx->onTell = onTell;
    ctx->pUserData = pReadSeekTellUserData;

    /* opusfile コールバック設定 */
    callbacks.read  = ma_libopus_of_read;
    callbacks.seek  = ma_libopus_of_seek;
    callbacks.tell  = ma_libopus_of_tell;
    callbacks.close = ma_libopus_of_close;

    of = op_open_callbacks(ctx, &callbacks, NULL, 0, &opusError);
    if (of == NULL) {
        ma_free(ctx, pAllocationCallbacks);
        ma_data_source_uninit(&pOpus->ds);
        return MA_INVALID_FILE;
    }

    /* フォーマット情報取得 */
    head = op_head(of, -1);
    if (head == NULL) {
        op_free(of);
        ma_free(ctx, pAllocationCallbacks);
        ma_data_source_uninit(&pOpus->ds);
        return MA_INVALID_FILE;
    }

    pOpus->onRead = onRead;
    pOpus->onSeek = onSeek;
    pOpus->onTell = onTell;
    pOpus->pReadSeekTellUserData = ctx;  /* ctx を保存 */
    pOpus->channels = (ma_uint32)head->channel_count;
    pOpus->of = of;

    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus)
{
    ma_result result;
    OggOpusFile* of;
    const OpusHead* head;
    int opusError;

    if (pFilePath == NULL) {
        return MA_INVALID_ARGS;
    }

    result = ma_libopus_init_internal(pConfig, pOpus);
    if (result != MA_SUCCESS) {
        return result;
    }

    of = op_open_file(pFilePath, &opusError);
    if (of == NULL) {
        ma_data_source_uninit(&pOpus->ds);
        return MA_INVALID_FILE;
    }

    /* フォーマット情報取得 */
    head = op_head(of, -1);
    if (head == NULL) {
        op_free(of);
        ma_data_source_uninit(&pOpus->ds);
        return MA_INVALID_FILE;
    }

    pOpus->channels = (ma_uint32)head->channel_count;
    pOpus->of = of;

    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_init_memory(const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus)
{
    /* メモリからの初期化は未実装 - 必要に応じて実装 */
    (void)pData;
    (void)dataSize;
    (void)pConfig;
    (void)pAllocationCallbacks;
    (void)pOpus;
    return MA_NOT_IMPLEMENTED;
}

MA_API void ma_libopus_uninit(ma_libopus* pOpus, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pOpus == NULL) {
        return;
    }

    if (pOpus->of != NULL) {
        op_free((OggOpusFile*)pOpus->of);
        pOpus->of = NULL;
    }

    /* コールバックモードの場合、コンテキストを解放 */
    if (pOpus->pReadSeekTellUserData != NULL && pOpus->onRead != NULL) {
        ma_free(pOpus->pReadSeekTellUserData, pAllocationCallbacks);
        pOpus->pReadSeekTellUserData = NULL;
    }

    ma_data_source_uninit(&pOpus->ds);
}

/* ======================================================
   PCMフレーム読み取り
   ====================================================== */

MA_API ma_result ma_libopus_read_pcm_frames(ma_libopus* pOpus, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    OggOpusFile* of;
    ma_uint64 totalFramesRead = 0;
    float* pOutput = (float*)pFramesOut;

    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (pOpus == NULL || pOpus->of == NULL) {
        return MA_INVALID_ARGS;
    }

    of = (OggOpusFile*)pOpus->of;

    while (totalFramesRead < frameCount) {
        int samplesRead;
        int framesToRead = (int)(frameCount - totalFramesRead);
        float* pDest = (pOutput != NULL) ? pOutput + totalFramesRead * pOpus->channels : NULL;

        /* op_read_float は最大でもある程度のサンプル数しか返さない */
        if (framesToRead > 8192) {
            framesToRead = 8192;
        }

        /* op_read_float はインターリーブ形式で出力 */
        samplesRead = op_read_float(of, pDest, framesToRead * (int)pOpus->channels, NULL);
        
        if (samplesRead <= 0) {
            if (samplesRead == 0) {
                /* EOF */
                break;
            }
            /* エラー */
            return MA_ERROR;
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

MA_API ma_result ma_libopus_seek_to_pcm_frame(ma_libopus* pOpus, ma_uint64 frameIndex)
{
    OggOpusFile* of;
    int result;

    if (pOpus == NULL || pOpus->of == NULL) {
        return MA_INVALID_ARGS;
    }

    of = (OggOpusFile*)pOpus->of;
    result = op_pcm_seek(of, (ogg_int64_t)frameIndex);
    
    return (result == 0) ? MA_SUCCESS : MA_ERROR;
}

/* ======================================================
   フォーマット情報取得
   ====================================================== */

MA_API ma_result ma_libopus_get_data_format(ma_libopus* pOpus, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    if (pFormat != NULL) {
        *pFormat = pOpus->format;
    }
    if (pChannels != NULL) {
        *pChannels = pOpus->channels;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = pOpus->sampleRate;
    }
    if (pChannelMap != NULL) {
        /* Opus は Vorbis と同じチャンネルマッピングを使用 */
        ma_channel_map_init_standard(ma_standard_channel_map_vorbis, pChannelMap, channelMapCap, pOpus->channels);
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_get_cursor_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pCursor)
{
    OggOpusFile* of;
    ogg_int64_t cursor;

    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;

    if (pOpus == NULL || pOpus->of == NULL) {
        return MA_INVALID_ARGS;
    }

    of = (OggOpusFile*)pOpus->of;
    cursor = op_pcm_tell(of);
    
    if (cursor < 0) {
        return MA_ERROR;
    }

    *pCursor = (ma_uint64)cursor;
    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_get_length_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pLength)
{
    OggOpusFile* of;
    ogg_int64_t length;

    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;

    if (pOpus == NULL || pOpus->of == NULL) {
        return MA_INVALID_ARGS;
    }

    of = (OggOpusFile*)pOpus->of;
    length = op_pcm_total(of, -1);
    
    if (length < 0) {
        return MA_ERROR;
    }

    *pLength = (ma_uint64)length;
    return MA_SUCCESS;
}

/* ======================================================
   デコーダーバックエンド vtable 実装
   ====================================================== */

static ma_result ma_decoding_backend_init__libopus(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libopus* pOpus;

    (void)pUserData;

    pOpus = (ma_libopus*)ma_malloc(sizeof(ma_libopus), pAllocationCallbacks);
    if (pOpus == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libopus_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pOpus);
    if (result != MA_SUCCESS) {
        ma_free(pOpus, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pOpus;
    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libopus(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libopus* pOpus;

    (void)pUserData;

    pOpus = (ma_libopus*)ma_malloc(sizeof(ma_libopus), pAllocationCallbacks);
    if (pOpus == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libopus_init_file(pFilePath, pConfig, pAllocationCallbacks, pOpus);
    if (result != MA_SUCCESS) {
        ma_free(pOpus, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pOpus;
    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_memory__libopus(void* pUserData, const void* pData, size_t dataSize, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libopus* pOpus;

    (void)pUserData;

    pOpus = (ma_libopus*)ma_malloc(sizeof(ma_libopus), pAllocationCallbacks);
    if (pOpus == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libopus_init_memory(pData, dataSize, pConfig, pAllocationCallbacks, pOpus);
    if (result != MA_SUCCESS) {
        ma_free(pOpus, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pOpus;
    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libopus(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    (void)pUserData;

    ma_libopus_uninit((ma_libopus*)pBackend, pAllocationCallbacks);
    ma_free(pBackend, pAllocationCallbacks);
}

ma_decoding_backend_vtable g_ma_decoding_backend_vtable_libopus = {
    ma_decoding_backend_init__libopus,
    ma_decoding_backend_init_file__libopus,
    NULL,  /* onInitFileW - 未実装 */
    ma_decoding_backend_init_memory__libopus,
    ma_decoding_backend_uninit__libopus
};
