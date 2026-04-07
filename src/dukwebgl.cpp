/**
 * WebGL 2.0 compatible bindings for QuickJS + OpenGL ES 3.0
 *
 * Based on https://github.com/mrautio/duktape-webgl
 * Adapted for GLES 3.0 (via glad/gles2.h)
 */

#include "dukwebgl.h"
#include "glad/gles2.h"
#include <quickjs.h>
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>

// ============================================================
// ヘルパー: WebGL オブジェクト（GLuint ID をラップ）
// ============================================================

static JSValue create_gl_object_uint(JSContext *ctx, GLuint id) {
    if (id == 0) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_id", JS_NewUint32(ctx, id));
    return obj;
}

static GLuint get_gl_object_id_uint(JSContext *ctx, JSValueConst val) {
    GLuint ret = 0;
    if (JS_IsObject(val)) {
        JSValue id = JS_GetPropertyStr(ctx, val, "_id");
        JS_ToUint32(ctx, &ret, id);
        JS_FreeValue(ctx, id);
    }
    return ret;
}

static JSValue create_gl_object_int(JSContext *ctx, GLint id) {
    if (id < 0) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_id", JS_NewInt32(ctx, id));
    return obj;
}

static GLint get_gl_object_id_int(JSContext *ctx, JSValueConst val) {
    GLint ret = -1;
    if (JS_IsObject(val)) {
        JSValue id = JS_GetPropertyStr(ctx, val, "_id");
        JS_ToInt32(ctx, &ret, id);
        JS_FreeValue(ctx, id);
    }
    return ret;
}

// ============================================================
// ヘルパー: バッファデータ取得
// ============================================================

// TypedArray / ArrayBuffer からバッファデータを取得するヘルパー
// TypedArray の byteOffset / byteLength を正しく考慮する
static void* qjs_get_buffer(JSContext *ctx, JSValueConst val, size_t *out_size) {
    // Try TypedArray first
    if (JS_IsObject(val)) {
        size_t offset = 0, length = 0, bpe = 0;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &offset, &length, &bpe);
        if (!JS_IsException(ab)) {
            size_t buf_size = 0;
            uint8_t *buf = JS_GetArrayBuffer(ctx, &buf_size, ab);
            JS_FreeValue(ctx, ab);
            if (buf && offset + length <= buf_size) {
                if (out_size) *out_size = length;
                return buf + offset;
            }
        }
        // Try plain ArrayBuffer
        size_t buf_size = 0;
        uint8_t *buf = JS_GetArrayBuffer(ctx, &buf_size, val);
        if (buf) {
            if (out_size) *out_size = buf_size;
            return buf;
        }
    }
    if (out_size) *out_size = 0;
    return NULL;
}

// ピクセルデータ取得ヘルパー
static void* qjs_get_pixels(JSContext *ctx, JSValueConst val) {
    void *ptr = qjs_get_buffer(ctx, val, NULL);
    if (ptr) return ptr;
    if (JS_IsObject(val)) {
        JSValue data = JS_GetPropertyStr(ctx, val, "data");
        ptr = qjs_get_buffer(ctx, data, NULL);
        JS_FreeValue(ctx, data);
    }
    return ptr;
}

// ============================================================
// ヘルパー: 引数取得ユーティリティ
// ============================================================

static uint32_t arg_uint(JSContext *ctx, JSValueConst val) {
    uint32_t v = 0; JS_ToUint32(ctx, &v, val); return v;
}
static int32_t arg_int(JSContext *ctx, JSValueConst val) {
    int32_t v = 0; JS_ToInt32(ctx, &v, val); return v;
}
static double arg_float64(JSContext *ctx, JSValueConst val) {
    double v = 0; JS_ToFloat64(ctx, &v, val); return v;
}

// ============================================================
// WebGL コンテキスト情報
// ============================================================

static JSValue js_getContextAttributes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "alpha", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "depth", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "stencil", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "antialias", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "premultipliedAlpha", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, obj, "preserveDrawingBuffer", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "powerPreference", JS_NewString(ctx, "default"));
    JS_SetPropertyStr(ctx, obj, "failIfMajorPerformanceCaveat", JS_NewBool(ctx, 0));
    return obj;
}

static JSValue js_isContextLost(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_FALSE;
}

static JSValue js_getSupportedExtensions(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static JSValue js_getExtension(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *name = JS_ToCString(ctx, argv[0]);
    if (name) {
        // GLES3 で標準サポートされている拡張は空オブジェクトを返す
        int supported =
            strcmp(name, "OES_element_index_uint") == 0 ||
            strcmp(name, "OES_vertex_array_object") == 0 ||
            strcmp(name, "OES_texture_float") == 0 ||
            strcmp(name, "OES_texture_half_float") == 0 ||
            strcmp(name, "ANGLE_instanced_arrays") == 0 ||
            strcmp(name, "EXT_blend_minmax") == 0 ||
            strcmp(name, "EXT_frag_depth") == 0 ||
            strcmp(name, "EXT_shader_texture_lod") == 0 ||
            strcmp(name, "WEBGL_depth_texture") == 0 ||
            strcmp(name, "EXT_color_buffer_float") == 0 ||
            strcmp(name, "WEBGL_draw_buffers") == 0 ||
            strcmp(name, "EXT_disjoint_timer_query") == 0 ||
            strcmp(name, "OES_standard_derivatives") == 0;
        JS_FreeCString(ctx, name);
        if (supported) return JS_NewObject(ctx);
    }
    return JS_NULL;
}

// ============================================================
// シェーダ / プログラム
// ============================================================

static JSValue js_createShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum type = (GLenum)arg_uint(ctx, argv[0]);
    GLuint shader = glCreateShader(type);
    return create_gl_object_uint(ctx, shader);
}

static JSValue js_deleteShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteShader(shader);
    return JS_UNDEFINED;
}

static JSValue js_shaderSource(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    const char *source = JS_ToCString(ctx, argv[1]);
    glShaderSource(shader, 1, &source, NULL);
    JS_FreeCString(ctx, source);
    return JS_UNDEFINED;
}

static JSValue js_compileShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    glCompileShader(shader);
    return JS_UNDEFINED;
}

static JSValue js_getShaderParameter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLint value = 0;
    glGetShaderiv(shader, pname, &value);
    switch (pname) {
    case GL_DELETE_STATUS:
    case GL_COMPILE_STATUS:
        return JS_NewBool(ctx, value == GL_TRUE ? 1 : 0);
    case GL_SHADER_TYPE:
        return JS_NewUint32(ctx, (GLuint)value);
    default:
        return JS_UNDEFINED;
    }
}

static JSValue js_getShaderInfoLog(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    GLchar infoLog[4096];
    GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof(infoLog), &length, infoLog);
    return JS_NewString(ctx, infoLog);
}

static JSValue js_getShaderSource(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    GLchar source[65536];
    GLsizei length = 0;
    glGetShaderSource(shader, sizeof(source), &length, source);
    return JS_NewString(ctx, source);
}

static JSValue js_isShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint shader = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsShader(shader));
}

static JSValue js_createProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = glCreateProgram();
    return create_gl_object_uint(ctx, program);
}

static JSValue js_deleteProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteProgram(program);
    return JS_UNDEFINED;
}

static JSValue js_attachShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint shader = get_gl_object_id_uint(ctx, argv[1]);
    glAttachShader(program, shader);
    return JS_UNDEFINED;
}

static JSValue js_detachShader(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint shader = get_gl_object_id_uint(ctx, argv[1]);
    glDetachShader(program, shader);
    return JS_UNDEFINED;
}

static JSValue js_linkProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    glLinkProgram(program);
    return JS_UNDEFINED;
}

static JSValue js_useProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    glUseProgram(program);
    return JS_UNDEFINED;
}

static JSValue js_validateProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    glValidateProgram(program);
    return JS_UNDEFINED;
}

static JSValue js_isProgram(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsProgram(program));
}

static JSValue js_getProgramParameter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLint value = 0;
    glGetProgramiv(program, pname, &value);
    switch (pname) {
    case GL_DELETE_STATUS:
    case GL_LINK_STATUS:
    case GL_VALIDATE_STATUS:
        return JS_NewBool(ctx, value == GL_TRUE ? 1 : 0);
    case GL_ATTACHED_SHADERS:
    case GL_ACTIVE_ATTRIBUTES:
    case GL_ACTIVE_UNIFORMS:
    case GL_TRANSFORM_FEEDBACK_VARYINGS:
    case GL_ACTIVE_UNIFORM_BLOCKS:
        return JS_NewInt32(ctx, value);
    case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
        return JS_NewUint32(ctx, (GLuint)value);
    default:
        return JS_UNDEFINED;
    }
}

static JSValue js_getProgramInfoLog(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLchar infoLog[4096];
    GLsizei length = 0;
    glGetProgramInfoLog(program, sizeof(infoLog), &length, infoLog);
    return JS_NewString(ctx, infoLog);
}

static JSValue js_bindAttribLocation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint index = arg_uint(ctx, argv[1]);
    const char *name = JS_ToCString(ctx, argv[2]);
    glBindAttribLocation(program, index, name);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue js_getAttribLocation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    const char *name = JS_ToCString(ctx, argv[1]);
    GLint loc = glGetAttribLocation(program, name);
    JS_FreeCString(ctx, name);
    return JS_NewInt32(ctx, loc);
}

static JSValue js_getUniformLocation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    const char *name = JS_ToCString(ctx, argv[1]);
    GLint loc = glGetUniformLocation(program, name);
    JS_FreeCString(ctx, name);
    return create_gl_object_int(ctx, loc);
}

static JSValue js_getActiveAttrib(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint index = arg_uint(ctx, argv[1]);
    GLchar name[1024];
    GLsizei length = 0;
    GLenum type;
    GLint size;
    glGetActiveAttrib(program, index, sizeof(name), &length, &size, &type, name);
    if (length <= 0) return JS_UNDEFINED;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewUint32(ctx, type));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewInt32(ctx, size));
    return obj;
}

static JSValue js_getActiveUniform(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint index = arg_uint(ctx, argv[1]);
    GLchar name[1024];
    GLsizei length = 0;
    GLenum type;
    GLint size;
    glGetActiveUniform(program, index, sizeof(name), &length, &size, &type, name);
    if (length <= 0) return JS_UNDEFINED;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewUint32(ctx, type));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewInt32(ctx, size));
    return obj;
}

// ============================================================
// バッファ
// ============================================================

static JSValue js_createBuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenBuffers(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteBuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteBuffers(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_isBuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsBuffer(id));
}

static JSValue js_bindBuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint buffer = get_gl_object_id_uint(ctx, argv[1]);
    glBindBuffer(target, buffer);
    return JS_UNDEFINED;
}

static JSValue js_bufferData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);

    size_t data_size = 0;
    GLvoid *data = (GLvoid *)qjs_get_buffer(ctx, argv[1], &data_size);
    if (!data) {
        // argv[1] が数値の場合はサイズ指定
        double d;
        if (JS_ToFloat64(ctx, &d, argv[1]) == 0 && !JS_IsObject(argv[1])) {
            data_size = (size_t)d;
        }
    }
    GLenum usage = (GLenum)arg_uint(ctx, argv[2]);

    if (argc > 3) {
        uint32_t src_offset = arg_uint(ctx, argv[3]);
        data = (GLvoid*)((char*)data + src_offset);
        data_size -= src_offset;
        if (argc > 4) {
            uint32_t length = arg_uint(ctx, argv[4]);
            if (length > 0 && length <= data_size) data_size = length;
        }
    }
    glBufferData(target, (GLsizeiptr)data_size, data, usage);
    return JS_UNDEFINED;
}

static JSValue js_bufferSubData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLintptr offset = (GLintptr)arg_uint(ctx, argv[1]);

    size_t data_size = 0;
    GLvoid *data = (GLvoid *)qjs_get_buffer(ctx, argv[2], &data_size);
    if (argc > 3) {
        uint32_t src_offset = arg_uint(ctx, argv[3]);
        data = (GLvoid*)((char*)data + src_offset);
        data_size -= src_offset;
        if (argc > 4) {
            uint32_t length = arg_uint(ctx, argv[4]);
            if (length > 0 && length <= data_size) data_size = length;
        }
    }
    glBufferSubData(target, offset, (GLsizeiptr)data_size, data);
    return JS_UNDEFINED;
}

// ============================================================
// テクスチャ
// ============================================================

static JSValue js_createTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenTextures(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteTextures(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_isTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsTexture(id));
}

static JSValue js_bindTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint texture = get_gl_object_id_uint(ctx, argv[1]);
    glBindTexture(target, texture);
    return JS_UNDEFINED;
}

static JSValue js_activeTexture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum texture = (GLenum)arg_uint(ctx, argv[0]);
    glActiveTexture(texture);
    return JS_UNDEFINED;
}

static JSValue js_texParameteri(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLint param = (GLint)arg_int(ctx, argv[2]);
    glTexParameteri(target, pname, param);
    return JS_UNDEFINED;
}

static JSValue js_texParameterf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLfloat param = (GLfloat)arg_float64(ctx, argv[2]);
    glTexParameterf(target, pname, param);
    return JS_UNDEFINED;
}

static JSValue js_generateMipmap(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    glGenerateMipmap(target);
    return JS_UNDEFINED;
}

static JSValue js_texImage2D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLint level = (GLint)arg_int(ctx, argv[1]);
    GLint internalformat = (GLint)arg_int(ctx, argv[2]);

    if (argc >= 9) {
        // texImage2D(target, level, internalformat, width, height, border, format, type, pixels)
        GLsizei width = (GLsizei)arg_int(ctx, argv[3]);
        GLsizei height = (GLsizei)arg_int(ctx, argv[4]);
        GLint border = (GLint)arg_int(ctx, argv[5]);
        GLenum format = (GLenum)arg_uint(ctx, argv[6]);
        GLenum type = (GLenum)arg_uint(ctx, argv[7]);
        void *pixels = qjs_get_pixels(ctx, argv[8]);
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    } else if (argc >= 6) {
        // texImage2D(target, level, internalformat, format, type, source)
        GLenum format = (GLenum)arg_uint(ctx, argv[3]);
        GLenum type = (GLenum)arg_uint(ctx, argv[4]);
        GLsizei width = 0, height = 0;
        void *pixels = NULL;
        if (JS_IsObject(argv[5])) {
            JSValue wv = JS_GetPropertyStr(ctx, argv[5], "width");
            if (!JS_IsUndefined(wv)) { width = (GLsizei)arg_int(ctx, wv); }
            JS_FreeValue(ctx, wv);
            JSValue hv = JS_GetPropertyStr(ctx, argv[5], "height");
            if (!JS_IsUndefined(hv)) { height = (GLsizei)arg_int(ctx, hv); }
            JS_FreeValue(ctx, hv);
            pixels = qjs_get_pixels(ctx, argv[5]);
        }
        glTexImage2D(target, level, internalformat, width, height, 0, format, type, pixels);
    }
    return JS_UNDEFINED;
}

static JSValue js_texSubImage2D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLint level = (GLint)arg_int(ctx, argv[1]);
    GLint xoffset = (GLint)arg_int(ctx, argv[2]);
    GLint yoffset = (GLint)arg_int(ctx, argv[3]);

    if (argc >= 9) {
        // texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels)
        GLsizei width = (GLsizei)arg_int(ctx, argv[4]);
        GLsizei height = (GLsizei)arg_int(ctx, argv[5]);
        GLenum format = (GLenum)arg_uint(ctx, argv[6]);
        GLenum type = (GLenum)arg_uint(ctx, argv[7]);
        void *pixels = qjs_get_pixels(ctx, argv[8]);
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    } else if (argc >= 7) {
        // texSubImage2D(target, level, xoffset, yoffset, format, type, source)
        GLenum format = (GLenum)arg_uint(ctx, argv[4]);
        GLenum type = (GLenum)arg_uint(ctx, argv[5]);
        GLsizei width = 0, height = 0;
        void *pixels = NULL;
        if (JS_IsObject(argv[6])) {
            JSValue wv = JS_GetPropertyStr(ctx, argv[6], "width");
            if (!JS_IsUndefined(wv)) { width = (GLsizei)arg_int(ctx, wv); }
            JS_FreeValue(ctx, wv);
            JSValue hv = JS_GetPropertyStr(ctx, argv[6], "height");
            if (!JS_IsUndefined(hv)) { height = (GLsizei)arg_int(ctx, hv); }
            JS_FreeValue(ctx, hv);
            pixels = qjs_get_pixels(ctx, argv[6]);
        }
        if (width > 0 && height > 0 && pixels) {
            glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_texImage3D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLint level = (GLint)arg_int(ctx, argv[1]);
    GLint internalformat = (GLint)arg_int(ctx, argv[2]);
    GLsizei width = (GLsizei)arg_int(ctx, argv[3]);
    GLsizei height = (GLsizei)arg_int(ctx, argv[4]);
    GLsizei depth = (GLsizei)arg_int(ctx, argv[5]);
    GLint border = (GLint)arg_int(ctx, argv[6]);
    GLenum format = (GLenum)arg_uint(ctx, argv[7]);
    GLenum type = (GLenum)arg_uint(ctx, argv[8]);
    void *pixels = qjs_get_pixels(ctx, argv[9]);
    glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels);
    return JS_UNDEFINED;
}

static JSValue js_copyTexImage2D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glCopyTexImage2D(
        (GLenum)arg_uint(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]),
        (GLenum)arg_uint(ctx, argv[2]),
        (GLint)arg_int(ctx, argv[3]), (GLint)arg_int(ctx, argv[4]),
        (GLsizei)arg_int(ctx, argv[5]), (GLsizei)arg_int(ctx, argv[6]),
        (GLint)arg_int(ctx, argv[7]));
    return JS_UNDEFINED;
}

static JSValue js_copyTexSubImage2D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glCopyTexSubImage2D(
        (GLenum)arg_uint(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]),
        (GLint)arg_int(ctx, argv[2]), (GLint)arg_int(ctx, argv[3]),
        (GLint)arg_int(ctx, argv[4]), (GLint)arg_int(ctx, argv[5]),
        (GLsizei)arg_int(ctx, argv[6]), (GLsizei)arg_int(ctx, argv[7]));
    return JS_UNDEFINED;
}

static JSValue js_pixelStorei(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum pname = (GLenum)arg_uint(ctx, argv[0]);
    GLint param = (GLint)arg_int(ctx, argv[1]);
    // WebGL 固有パラメータ（GLES3 にはない）はスキップ
    // UNPACK_FLIP_Y_WEBGL (0x9240), UNPACK_PREMULTIPLY_ALPHA_WEBGL (0x9241)
    if (pname == 0x9240 || pname == 0x9241) return JS_UNDEFINED;
    glPixelStorei(pname, param);
    return JS_UNDEFINED;
}

static JSValue js_readPixels(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint x = (GLint)arg_int(ctx, argv[0]);
    GLint y = (GLint)arg_int(ctx, argv[1]);
    GLsizei width = (GLsizei)arg_int(ctx, argv[2]);
    GLsizei height = (GLsizei)arg_int(ctx, argv[3]);
    GLenum format = (GLenum)arg_uint(ctx, argv[4]);
    GLenum type = (GLenum)arg_uint(ctx, argv[5]);
    void *pixels = qjs_get_buffer(ctx, argv[6], NULL);
    glReadPixels(x, y, width, height, format, type, pixels);
    return JS_UNDEFINED;
}

// ============================================================
// フレームバッファ / レンダーバッファ
// ============================================================

static JSValue js_createFramebuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenFramebuffers(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteFramebuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteFramebuffers(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_isFramebuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsFramebuffer(id));
}

static JSValue js_bindFramebuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint fb = get_gl_object_id_uint(ctx, argv[1]);
    glBindFramebuffer(target, fb);
    return JS_UNDEFINED;
}

static JSValue js_framebufferTexture2D(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glFramebufferTexture2D(
        (GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
        (GLenum)arg_uint(ctx, argv[2]), get_gl_object_id_uint(ctx, argv[3]),
        (GLint)arg_int(ctx, argv[4]));
    return JS_UNDEFINED;
}

static JSValue js_framebufferRenderbuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glFramebufferRenderbuffer(
        (GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
        (GLenum)arg_uint(ctx, argv[2]), get_gl_object_id_uint(ctx, argv[3]));
    return JS_UNDEFINED;
}

static JSValue js_checkFramebufferStatus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    return JS_NewUint32(ctx, glCheckFramebufferStatus(target));
}

static JSValue js_createRenderbuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenRenderbuffers(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteRenderbuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteRenderbuffers(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_isRenderbuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsRenderbuffer(id));
}

static JSValue js_bindRenderbuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint rb = get_gl_object_id_uint(ctx, argv[1]);
    glBindRenderbuffer(target, rb);
    return JS_UNDEFINED;
}

static JSValue js_renderbufferStorage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glRenderbufferStorage(
        (GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
        (GLsizei)arg_int(ctx, argv[2]), (GLsizei)arg_int(ctx, argv[3]));
    return JS_UNDEFINED;
}

static JSValue js_drawBuffers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (!JS_IsArray(argv[0])) return JS_ThrowTypeError(ctx, "drawBuffers: expected array");
    JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
    uint32_t length = 0;
    JS_ToUint32(ctx, &length, lenVal);
    JS_FreeValue(ctx, lenVal);
    GLenum *bufs = (GLenum*)malloc(sizeof(GLenum) * length);
    if (!bufs) return JS_ThrowInternalError(ctx, "drawBuffers: alloc failed");
    for (uint32_t i = 0; i < length; i++) {
        JSValue el = JS_GetPropertyUint32(ctx, argv[0], i);
        bufs[i] = (GLenum)arg_uint(ctx, el);
        JS_FreeValue(ctx, el);
    }
    glDrawBuffers((GLsizei)length, bufs);
    free(bufs);
    return JS_UNDEFINED;
}

static JSValue js_blitFramebuffer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlitFramebuffer(
        (GLint)arg_int(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]),
        (GLint)arg_int(ctx, argv[2]), (GLint)arg_int(ctx, argv[3]),
        (GLint)arg_int(ctx, argv[4]), (GLint)arg_int(ctx, argv[5]),
        (GLint)arg_int(ctx, argv[6]), (GLint)arg_int(ctx, argv[7]),
        (GLbitfield)arg_uint(ctx, argv[8]), (GLenum)arg_uint(ctx, argv[9]));
    return JS_UNDEFINED;
}

// ============================================================
// VAO (WebGL 2.0 / GLES 3.0)
// ============================================================

static JSValue js_createVertexArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenVertexArrays(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteVertexArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteVertexArrays(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_isVertexArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    return JS_NewBool(ctx, glIsVertexArray(id));
}

static JSValue js_bindVertexArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint vao = get_gl_object_id_uint(ctx, argv[0]);
    glBindVertexArray(vao);
    return JS_UNDEFINED;
}

// ============================================================
// 頂点属性
// ============================================================

static JSValue js_enableVertexAttribArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint index = arg_uint(ctx, argv[0]);
    glEnableVertexAttribArray(index);
    return JS_UNDEFINED;
}

static JSValue js_disableVertexAttribArray(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint index = arg_uint(ctx, argv[0]);
    glDisableVertexAttribArray(index);
    return JS_UNDEFINED;
}

static JSValue js_vertexAttribPointer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint index = arg_uint(ctx, argv[0]);
    GLint size = (GLint)arg_int(ctx, argv[1]);
    GLenum type = (GLenum)arg_uint(ctx, argv[2]);
    GLboolean normalized = JS_ToBool(ctx, argv[3]) ? GL_TRUE : GL_FALSE;
    GLsizei stride = (GLsizei)arg_int(ctx, argv[4]);
    GLintptr offset = (GLintptr)arg_int(ctx, argv[5]);
    glVertexAttribPointer(index, size, type, normalized, stride, (const void*)offset);
    return JS_UNDEFINED;
}

static JSValue js_vertexAttribIPointer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint index = arg_uint(ctx, argv[0]);
    GLint size = (GLint)arg_int(ctx, argv[1]);
    GLenum type = (GLenum)arg_uint(ctx, argv[2]);
    GLsizei stride = (GLsizei)arg_int(ctx, argv[3]);
    GLintptr offset = (GLintptr)arg_int(ctx, argv[4]);
    glVertexAttribIPointer(index, size, type, stride, (const void*)offset);
    return JS_UNDEFINED;
}

static JSValue js_vertexAttribDivisor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint index = arg_uint(ctx, argv[0]);
    GLuint divisor = arg_uint(ctx, argv[1]);
    glVertexAttribDivisor(index, divisor);
    return JS_UNDEFINED;
}

static JSValue js_vertexAttrib1f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glVertexAttrib1f(arg_uint(ctx, argv[0]), (GLfloat)arg_float64(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_vertexAttrib2f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glVertexAttrib2f(arg_uint(ctx, argv[0]),
        (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]));
    return JS_UNDEFINED;
}
static JSValue js_vertexAttrib3f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glVertexAttrib3f(arg_uint(ctx, argv[0]),
        (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]),
        (GLfloat)arg_float64(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_vertexAttrib4f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glVertexAttrib4f(arg_uint(ctx, argv[0]),
        (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]),
        (GLfloat)arg_float64(ctx, argv[3]), (GLfloat)arg_float64(ctx, argv[4]));
    return JS_UNDEFINED;
}

// ============================================================
// Uniform
// ============================================================

static JSValue js_uniform1i(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform1i(loc, (GLint)arg_int(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_uniform2i(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform2i(loc, (GLint)arg_int(ctx, argv[1]), (GLint)arg_int(ctx, argv[2]));
    return JS_UNDEFINED;
}
static JSValue js_uniform3i(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform3i(loc, (GLint)arg_int(ctx, argv[1]), (GLint)arg_int(ctx, argv[2]), (GLint)arg_int(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_uniform4i(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform4i(loc, (GLint)arg_int(ctx, argv[1]), (GLint)arg_int(ctx, argv[2]),
        (GLint)arg_int(ctx, argv[3]), (GLint)arg_int(ctx, argv[4]));
    return JS_UNDEFINED;
}

static JSValue js_uniform1f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform1f(loc, (GLfloat)arg_float64(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_uniform2f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform2f(loc, (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]));
    return JS_UNDEFINED;
}
static JSValue js_uniform3f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform3f(loc, (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]),
        (GLfloat)arg_float64(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_uniform4f(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLint loc = get_gl_object_id_int(ctx, argv[0]);
    glUniform4f(loc, (GLfloat)arg_float64(ctx, argv[1]), (GLfloat)arg_float64(ctx, argv[2]),
        (GLfloat)arg_float64(ctx, argv[3]), (GLfloat)arg_float64(ctx, argv[4]));
    return JS_UNDEFINED;
}

// uniform*fv / uniform*iv
#define DEFINE_UNIFORM_FV(name, cType, glFunc) \
    static JSValue js_##name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) { \
        GLint loc = get_gl_object_id_int(ctx, argv[0]); \
        size_t count = 0; \
        const cType *value = (const cType *)qjs_get_buffer(ctx, argv[1], &count); \
        if (value && loc >= 0) { \
            glFunc(loc, (GLsizei)count, value); \
        } \
        return JS_UNDEFINED; \
    }

DEFINE_UNIFORM_FV(uniform1fv, GLfloat, glUniform1fv)
DEFINE_UNIFORM_FV(uniform2fv, GLfloat, glUniform2fv)
DEFINE_UNIFORM_FV(uniform3fv, GLfloat, glUniform3fv)
DEFINE_UNIFORM_FV(uniform4fv, GLfloat, glUniform4fv)
DEFINE_UNIFORM_FV(uniform1iv, GLint, glUniform1iv)
DEFINE_UNIFORM_FV(uniform2iv, GLint, glUniform2iv)
DEFINE_UNIFORM_FV(uniform3iv, GLint, glUniform3iv)
DEFINE_UNIFORM_FV(uniform4iv, GLint, glUniform4iv)
DEFINE_UNIFORM_FV(uniform1uiv, GLuint, glUniform1uiv)
DEFINE_UNIFORM_FV(uniform2uiv, GLuint, glUniform2uiv)
DEFINE_UNIFORM_FV(uniform3uiv, GLuint, glUniform3uiv)
DEFINE_UNIFORM_FV(uniform4uiv, GLuint, glUniform4uiv)

// uniformMatrix*fv
#define DEFINE_UNIFORM_MATRIX(name, glFunc) \
    static JSValue js_##name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) { \
        GLint loc = get_gl_object_id_int(ctx, argv[0]); \
        GLboolean transpose = JS_ToBool(ctx, argv[1]) ? GL_TRUE : GL_FALSE; \
        size_t count = 0; \
        const GLfloat *value = (const GLfloat *)qjs_get_buffer(ctx, argv[2], &count); \
        if (value && loc >= 0) { \
            glFunc(loc, 1, transpose, value); \
        } \
        return JS_UNDEFINED; \
    }

DEFINE_UNIFORM_MATRIX(uniformMatrix2fv, glUniformMatrix2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3fv, glUniformMatrix3fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4fv, glUniformMatrix4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix2x3fv, glUniformMatrix2x3fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix2x4fv, glUniformMatrix2x4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3x2fv, glUniformMatrix3x2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix3x4fv, glUniformMatrix3x4fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4x2fv, glUniformMatrix4x2fv)
DEFINE_UNIFORM_MATRIX(uniformMatrix4x3fv, glUniformMatrix4x3fv)

// ============================================================
// 描画
// ============================================================

static JSValue js_drawArrays(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum mode = (GLenum)arg_uint(ctx, argv[0]);
    GLint first = (GLint)arg_int(ctx, argv[1]);
    GLsizei count = (GLsizei)arg_int(ctx, argv[2]);
    glDrawArrays(mode, first, count);
    return JS_UNDEFINED;
}

static JSValue js_drawElements(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum mode = (GLenum)arg_uint(ctx, argv[0]);
    GLsizei count = (GLsizei)arg_int(ctx, argv[1]);
    GLenum type = (GLenum)arg_uint(ctx, argv[2]);
    GLintptr offset = (GLintptr)arg_int(ctx, argv[3]);
    glDrawElements(mode, count, type, (const void*)offset);
    return JS_UNDEFINED;
}

static JSValue js_drawArraysInstanced(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum mode = (GLenum)arg_uint(ctx, argv[0]);
    GLint first = (GLint)arg_int(ctx, argv[1]);
    GLsizei count = (GLsizei)arg_int(ctx, argv[2]);
    GLsizei instanceCount = (GLsizei)arg_int(ctx, argv[3]);
    glDrawArraysInstanced(mode, first, count, instanceCount);
    return JS_UNDEFINED;
}

static JSValue js_drawElementsInstanced(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum mode = (GLenum)arg_uint(ctx, argv[0]);
    GLsizei count = (GLsizei)arg_int(ctx, argv[1]);
    GLenum type = (GLenum)arg_uint(ctx, argv[2]);
    GLintptr offset = (GLintptr)arg_int(ctx, argv[3]);
    GLsizei instanceCount = (GLsizei)arg_int(ctx, argv[4]);
    glDrawElementsInstanced(mode, count, type, (const void*)offset, instanceCount);
    return JS_UNDEFINED;
}

// ============================================================
// ステート管理
// ============================================================

static JSValue js_enable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glEnable((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_disable(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glDisable((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_isEnabled(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewBool(ctx, glIsEnabled((GLenum)arg_uint(ctx, argv[0])));
}

static JSValue js_viewport(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glViewport((GLint)arg_int(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]),
               (GLsizei)arg_int(ctx, argv[2]), (GLsizei)arg_int(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_scissor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glScissor((GLint)arg_int(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]),
              (GLsizei)arg_int(ctx, argv[2]), (GLsizei)arg_int(ctx, argv[3]));
    return JS_UNDEFINED;
}

static JSValue js_clearColor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glClearColor((GLfloat)arg_float64(ctx, argv[0]), (GLfloat)arg_float64(ctx, argv[1]),
                 (GLfloat)arg_float64(ctx, argv[2]), (GLfloat)arg_float64(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_clearDepth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glClearDepthf((GLfloat)arg_float64(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_clearStencil(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glClearStencil((GLint)arg_int(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glClear((GLbitfield)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}

static JSValue js_colorMask(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glColorMask(JS_ToBool(ctx, argv[0]), JS_ToBool(ctx, argv[1]),
                JS_ToBool(ctx, argv[2]), JS_ToBool(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_depthMask(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glDepthMask(JS_ToBool(ctx, argv[0]) ? GL_TRUE : GL_FALSE);
    return JS_UNDEFINED;
}
static JSValue js_depthFunc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glDepthFunc((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_depthRange(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glDepthRangef((GLfloat)arg_float64(ctx, argv[0]), (GLfloat)arg_float64(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue js_blendFunc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlendFunc((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_blendFuncSeparate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlendFuncSeparate((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
                        (GLenum)arg_uint(ctx, argv[2]), (GLenum)arg_uint(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_blendEquation(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlendEquation((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_blendEquationSeparate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlendEquationSeparate((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_blendColor(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBlendColor((GLfloat)arg_float64(ctx, argv[0]), (GLfloat)arg_float64(ctx, argv[1]),
                 (GLfloat)arg_float64(ctx, argv[2]), (GLfloat)arg_float64(ctx, argv[3]));
    return JS_UNDEFINED;
}

static JSValue js_stencilFunc(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilFunc((GLenum)arg_uint(ctx, argv[0]), (GLint)arg_int(ctx, argv[1]), arg_uint(ctx, argv[2]));
    return JS_UNDEFINED;
}
static JSValue js_stencilFuncSeparate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilFuncSeparate((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
                          (GLint)arg_int(ctx, argv[2]), arg_uint(ctx, argv[3]));
    return JS_UNDEFINED;
}
static JSValue js_stencilMask(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilMask(arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_stencilMaskSeparate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilMaskSeparate((GLenum)arg_uint(ctx, argv[0]), arg_uint(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_stencilOp(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilOp((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]), (GLenum)arg_uint(ctx, argv[2]));
    return JS_UNDEFINED;
}
static JSValue js_stencilOpSeparate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glStencilOpSeparate((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]),
                        (GLenum)arg_uint(ctx, argv[2]), (GLenum)arg_uint(ctx, argv[3]));
    return JS_UNDEFINED;
}

static JSValue js_cullFace(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glCullFace((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_frontFace(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glFrontFace((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_lineWidth(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glLineWidth((GLfloat)arg_float64(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue js_polygonOffset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glPolygonOffset((GLfloat)arg_float64(ctx, argv[0]), (GLfloat)arg_float64(ctx, argv[1]));
    return JS_UNDEFINED;
}
static JSValue js_sampleCoverage(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glSampleCoverage((GLfloat)arg_float64(ctx, argv[0]), JS_ToBool(ctx, argv[1]) ? GL_TRUE : GL_FALSE);
    return JS_UNDEFINED;
}

static JSValue js_hint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glHint((GLenum)arg_uint(ctx, argv[0]), (GLenum)arg_uint(ctx, argv[1]));
    return JS_UNDEFINED;
}

static JSValue js_flush(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glFlush();
    return JS_UNDEFINED;
}
static JSValue js_finish(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glFinish();
    return JS_UNDEFINED;
}

static JSValue js_getError(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewUint32(ctx, glGetError());
}

// ============================================================
// getParameter
// ============================================================

static JSValue js_getParameter(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum pname = (GLenum)arg_uint(ctx, argv[0]);
    switch (pname) {
    // GLenum returns
    case GL_ACTIVE_TEXTURE:
    case GL_CULL_FACE_MODE:
    case GL_DEPTH_FUNC:
    case GL_FRONT_FACE:
    case GL_GENERATE_MIPMAP_HINT:
    case GL_BLEND_DST_ALPHA:
    case GL_BLEND_DST_RGB:
    case GL_BLEND_EQUATION_RGB:
    case GL_BLEND_EQUATION_ALPHA:
    case GL_BLEND_SRC_ALPHA:
    case GL_BLEND_SRC_RGB:
    case GL_STENCIL_BACK_FAIL:
    case GL_STENCIL_BACK_FUNC:
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
    case GL_STENCIL_BACK_PASS_DEPTH_PASS:
    case GL_STENCIL_FAIL:
    case GL_STENCIL_FUNC:
    case GL_STENCIL_PASS_DEPTH_FAIL:
    case GL_STENCIL_PASS_DEPTH_PASS:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        return JS_NewUint32(ctx, (GLenum)v);
    }
    // GLint returns
    case GL_ALPHA_BITS:
    case GL_BLUE_BITS:
    case GL_DEPTH_BITS:
    case GL_GREEN_BITS:
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    case GL_MAX_RENDERBUFFER_SIZE:
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_VARYING_VECTORS:
    case GL_MAX_VERTEX_ATTRIBS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_UNIFORM_VECTORS:
    case GL_PACK_ALIGNMENT:
    case GL_RED_BITS:
    case GL_SAMPLE_BUFFERS:
    case GL_SAMPLES:
    case GL_STENCIL_BACK_REF:
    case GL_STENCIL_BITS:
    case GL_STENCIL_CLEAR_VALUE:
    case GL_STENCIL_REF:
    case GL_SUBPIXEL_BITS:
    case GL_UNPACK_ALIGNMENT:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        return JS_NewInt32(ctx, v);
    }
    // GLfloat returns
    case GL_DEPTH_CLEAR_VALUE:
    case GL_LINE_WIDTH:
    case GL_POLYGON_OFFSET_FACTOR:
    case GL_POLYGON_OFFSET_UNITS:
    case GL_SAMPLE_COVERAGE_VALUE:
    {
        GLfloat v = 0; glGetFloatv(pname, &v);
        return JS_NewFloat64(ctx, v);
    }
    // GLboolean returns
    case GL_BLEND:
    case GL_CULL_FACE:
    case GL_DEPTH_TEST:
    case GL_DEPTH_WRITEMASK:
    case GL_DITHER:
    case GL_POLYGON_OFFSET_FILL:
    case GL_SAMPLE_ALPHA_TO_COVERAGE:
    case GL_SAMPLE_COVERAGE:
    case GL_SAMPLE_COVERAGE_INVERT:
    case GL_SCISSOR_TEST:
    case GL_STENCIL_TEST:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        return JS_NewBool(ctx, v == GL_TRUE ? 1 : 0);
    }
    // string returns
    case GL_VENDOR:
    case GL_RENDERER:
    case GL_VERSION:
    case GL_SHADING_LANGUAGE_VERSION:
        return JS_NewString(ctx, (const char*)glGetString(pname));
    // Int32Array (4要素) returns — plain JS array で返す
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    {
        GLint v[4] = {0};
        glGetIntegerv(pname, v);
        JSValue arr = JS_NewArray(ctx);
        for (int i = 0; i < 4; i++)
            JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, v[i]));
        return arr;
    }
    // Float32Array (4要素) returns — plain JS array で返す
    case GL_COLOR_CLEAR_VALUE:
    case GL_BLEND_COLOR:
    case GL_DEPTH_RANGE:
    {
        GLfloat v[4] = {0};
        glGetFloatv(pname, v);
        JSValue arr = JS_NewArray(ctx);
        for (int i = 0; i < 4; i++)
            JS_SetPropertyUint32(ctx, arr, i, JS_NewFloat64(ctx, v[i]));
        return arr;
    }
    // Uint8Array (4要素) returns — plain JS array で返す
    case GL_COLOR_WRITEMASK:
    {
        GLboolean v[4] = {0};
        glGetBooleanv(pname, v);
        JSValue arr = JS_NewArray(ctx);
        for (int i = 0; i < 4; i++)
            JS_SetPropertyUint32(ctx, arr, i, JS_NewBool(ctx, v[i] ? 1 : 0));
        return arr;
    }
    // WebGL2 追加 GLint returns
    case GL_MAX_3D_TEXTURE_SIZE:
    case GL_MAX_ARRAY_TEXTURE_LAYERS:
    case GL_MAX_COLOR_ATTACHMENTS:
    case GL_MAX_DRAW_BUFFERS:
    case GL_MAX_ELEMENTS_INDICES:
    case GL_MAX_ELEMENTS_VERTICES:
    case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
    case GL_MAX_SAMPLES:
    case GL_MAX_UNIFORM_BLOCK_SIZE:
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
    case GL_MAX_VARYING_COMPONENTS:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        return JS_NewInt32(ctx, v);
    }
    // バインディング参照 (object として返す)
    case GL_CURRENT_PROGRAM:
    case GL_ARRAY_BUFFER_BINDING:
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
    case GL_FRAMEBUFFER_BINDING:
    case GL_RENDERBUFFER_BINDING:
    case GL_TEXTURE_BINDING_2D:
    case GL_TEXTURE_BINDING_CUBE_MAP:
    {
        GLint v = 0; glGetIntegerv(pname, &v);
        if (v == 0) return JS_NULL;
        return create_gl_object_uint(ctx, (GLuint)v);
    }
    default:
    {
        // 未知のパラメータは GLint として試行
        GLint v = 0;
        glGetIntegerv(pname, &v);
        if (glGetError() == GL_NO_ERROR) {
            return JS_NewInt32(ctx, v);
        }
        return JS_UNDEFINED;
    }
    }
}

// ============================================================
// Uniform Block (WebGL2 / GLES3)
// ============================================================

static JSValue js_getUniformBlockIndex(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    const char *name = JS_ToCString(ctx, argv[1]);
    GLuint idx = glGetUniformBlockIndex(program, name);
    JS_FreeCString(ctx, name);
    return JS_NewUint32(ctx, idx);
}

static JSValue js_uniformBlockBinding(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    GLuint blockIndex = arg_uint(ctx, argv[1]);
    GLuint blockBinding = arg_uint(ctx, argv[2]);
    glUniformBlockBinding(program, blockIndex, blockBinding);
    return JS_UNDEFINED;
}

static JSValue js_bindBufferBase(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint index = arg_uint(ctx, argv[1]);
    GLuint buffer = get_gl_object_id_uint(ctx, argv[2]);
    glBindBufferBase(target, index, buffer);
    return JS_UNDEFINED;
}

static JSValue js_bindBufferRange(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint index = arg_uint(ctx, argv[1]);
    GLuint buffer = get_gl_object_id_uint(ctx, argv[2]);
    GLintptr offset = (GLintptr)arg_int(ctx, argv[3]);
    GLsizeiptr size = (GLsizeiptr)arg_int(ctx, argv[4]);
    glBindBufferRange(target, index, buffer, offset, size);
    return JS_UNDEFINED;
}

// ============================================================
// Transform Feedback (WebGL2 / GLES3)
// ============================================================

static JSValue js_createTransformFeedback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenTransformFeedbacks(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteTransformFeedback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteTransformFeedbacks(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_bindTransformFeedback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint id = get_gl_object_id_uint(ctx, argv[1]);
    glBindTransformFeedback(target, id);
    return JS_UNDEFINED;
}

static JSValue js_beginTransformFeedback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glBeginTransformFeedback((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}

static JSValue js_endTransformFeedback(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    glEndTransformFeedback();
    return JS_UNDEFINED;
}

static JSValue js_transformFeedbackVaryings(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint program = get_gl_object_id_uint(ctx, argv[0]);
    // arg1 is array of strings
    if (!JS_IsArray(argv[1])) return JS_ThrowTypeError(ctx, "transformFeedbackVaryings: expected array");
    JSValue lenVal = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t count = 0;
    JS_ToInt32(ctx, &count, lenVal);
    JS_FreeValue(ctx, lenVal);
    const char **varyings = (const char**)malloc(sizeof(char*) * count);
    if (!varyings) return JS_ThrowInternalError(ctx, "transformFeedbackVaryings: alloc failed");
    for (int32_t i = 0; i < count; i++) {
        JSValue el = JS_GetPropertyUint32(ctx, argv[1], i);
        varyings[i] = JS_ToCString(ctx, el);
        JS_FreeValue(ctx, el);
    }
    GLenum bufferMode = (GLenum)arg_uint(ctx, argv[2]);
    glTransformFeedbackVaryings(program, count, varyings, bufferMode);
    // free all strings
    for (int32_t i = 0; i < count; i++) JS_FreeCString(ctx, varyings[i]);
    free(varyings);
    return JS_UNDEFINED;
}

// ============================================================
// Query (WebGL2 / GLES3)
// ============================================================

static JSValue js_createQuery(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenQueries(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteQuery(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteQueries(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_beginQuery(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum target = (GLenum)arg_uint(ctx, argv[0]);
    GLuint id = get_gl_object_id_uint(ctx, argv[1]);
    glBeginQuery(target, id);
    return JS_UNDEFINED;
}

static JSValue js_endQuery(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    glEndQuery((GLenum)arg_uint(ctx, argv[0]));
    return JS_UNDEFINED;
}

// ============================================================
// Sampler (WebGL2 / GLES3)
// ============================================================

static JSValue js_createSampler(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id;
    glGenSamplers(1, &id);
    return create_gl_object_uint(ctx, id);
}

static JSValue js_deleteSampler(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint id = get_gl_object_id_uint(ctx, argv[0]);
    glDeleteSamplers(1, &id);
    return JS_UNDEFINED;
}

static JSValue js_bindSampler(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint unit = arg_uint(ctx, argv[0]);
    GLuint sampler = get_gl_object_id_uint(ctx, argv[1]);
    glBindSampler(unit, sampler);
    return JS_UNDEFINED;
}

static JSValue js_samplerParameteri(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint sampler = get_gl_object_id_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLint param = (GLint)arg_int(ctx, argv[2]);
    glSamplerParameteri(sampler, pname, param);
    return JS_UNDEFINED;
}

static JSValue js_samplerParameterf(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLuint sampler = get_gl_object_id_uint(ctx, argv[0]);
    GLenum pname = (GLenum)arg_uint(ctx, argv[1]);
    GLfloat param = (GLfloat)arg_float64(ctx, argv[2]);
    glSamplerParameterf(sampler, pname, param);
    return JS_UNDEFINED;
}

// ============================================================
// clearBuffer (WebGL2 / GLES3)
// ============================================================

static JSValue js_clearBufferfv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum buffer = (GLenum)arg_uint(ctx, argv[0]);
    GLint drawbuffer = (GLint)arg_int(ctx, argv[1]);
    const GLfloat *value = (const GLfloat *)qjs_get_buffer(ctx, argv[2], NULL);
    glClearBufferfv(buffer, drawbuffer, value);
    return JS_UNDEFINED;
}

static JSValue js_clearBufferiv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum buffer = (GLenum)arg_uint(ctx, argv[0]);
    GLint drawbuffer = (GLint)arg_int(ctx, argv[1]);
    const GLint *value = (const GLint *)qjs_get_buffer(ctx, argv[2], NULL);
    glClearBufferiv(buffer, drawbuffer, value);
    return JS_UNDEFINED;
}

static JSValue js_clearBufferuiv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum buffer = (GLenum)arg_uint(ctx, argv[0]);
    GLint drawbuffer = (GLint)arg_int(ctx, argv[1]);
    const GLuint *value = (const GLuint *)qjs_get_buffer(ctx, argv[2], NULL);
    glClearBufferuiv(buffer, drawbuffer, value);
    return JS_UNDEFINED;
}

static JSValue js_clearBufferfi(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    GLenum buffer = (GLenum)arg_uint(ctx, argv[0]);
    GLint drawbuffer = (GLint)arg_int(ctx, argv[1]);
    GLfloat depth = (GLfloat)arg_float64(ctx, argv[2]);
    GLint stencil = (GLint)arg_int(ctx, argv[3]);
    glClearBufferfi(buffer, drawbuffer, depth, stencil);
    return JS_UNDEFINED;
}

// ============================================================
// 定数登録
// ============================================================

static void bind_constants(JSContext *ctx, JSValue gl) {

#define PUSH_CONST(name) \
    JS_SetPropertyStr(ctx, gl, #name, JS_NewUint32(ctx, GL_##name))

    // データ型
    PUSH_CONST(BYTE);
    PUSH_CONST(UNSIGNED_BYTE);
    PUSH_CONST(SHORT);
    PUSH_CONST(UNSIGNED_SHORT);
    PUSH_CONST(INT);
    PUSH_CONST(UNSIGNED_INT);
    PUSH_CONST(FLOAT);
    PUSH_CONST(HALF_FLOAT);

    // プリミティブ
    PUSH_CONST(POINTS);
    PUSH_CONST(LINES);
    PUSH_CONST(LINE_LOOP);
    PUSH_CONST(LINE_STRIP);
    PUSH_CONST(TRIANGLES);
    PUSH_CONST(TRIANGLE_STRIP);
    PUSH_CONST(TRIANGLE_FAN);

    // ブレンド
    PUSH_CONST(ZERO);
    PUSH_CONST(ONE);
    PUSH_CONST(SRC_COLOR);
    PUSH_CONST(ONE_MINUS_SRC_COLOR);
    PUSH_CONST(DST_COLOR);
    PUSH_CONST(ONE_MINUS_DST_COLOR);
    PUSH_CONST(SRC_ALPHA);
    PUSH_CONST(ONE_MINUS_SRC_ALPHA);
    PUSH_CONST(DST_ALPHA);
    PUSH_CONST(ONE_MINUS_DST_ALPHA);
    PUSH_CONST(CONSTANT_COLOR);
    PUSH_CONST(ONE_MINUS_CONSTANT_COLOR);
    PUSH_CONST(CONSTANT_ALPHA);
    PUSH_CONST(ONE_MINUS_CONSTANT_ALPHA);
    PUSH_CONST(SRC_ALPHA_SATURATE);
    PUSH_CONST(FUNC_ADD);
    PUSH_CONST(FUNC_SUBTRACT);
    PUSH_CONST(FUNC_REVERSE_SUBTRACT);
    PUSH_CONST(MIN);
    PUSH_CONST(MAX);

    // バッファ
    PUSH_CONST(ARRAY_BUFFER);
    PUSH_CONST(ELEMENT_ARRAY_BUFFER);
    PUSH_CONST(UNIFORM_BUFFER);
    PUSH_CONST(TRANSFORM_FEEDBACK_BUFFER);
    PUSH_CONST(COPY_READ_BUFFER);
    PUSH_CONST(COPY_WRITE_BUFFER);
    PUSH_CONST(PIXEL_PACK_BUFFER);
    PUSH_CONST(PIXEL_UNPACK_BUFFER);
    PUSH_CONST(STREAM_DRAW);
    PUSH_CONST(STREAM_READ);
    PUSH_CONST(STREAM_COPY);
    PUSH_CONST(STATIC_DRAW);
    PUSH_CONST(STATIC_READ);
    PUSH_CONST(STATIC_COPY);
    PUSH_CONST(DYNAMIC_DRAW);
    PUSH_CONST(DYNAMIC_READ);
    PUSH_CONST(DYNAMIC_COPY);
    PUSH_CONST(ARRAY_BUFFER_BINDING);
    PUSH_CONST(ELEMENT_ARRAY_BUFFER_BINDING);
    PUSH_CONST(BUFFER_SIZE);
    PUSH_CONST(BUFFER_USAGE);

    // クリアビット
    PUSH_CONST(DEPTH_BUFFER_BIT);
    PUSH_CONST(STENCIL_BUFFER_BIT);
    PUSH_CONST(COLOR_BUFFER_BIT);

    // 有効/無効
    PUSH_CONST(BLEND);
    PUSH_CONST(CULL_FACE);
    PUSH_CONST(DEPTH_TEST);
    PUSH_CONST(DITHER);
    PUSH_CONST(POLYGON_OFFSET_FILL);
    PUSH_CONST(SAMPLE_ALPHA_TO_COVERAGE);
    PUSH_CONST(SAMPLE_COVERAGE);
    PUSH_CONST(SCISSOR_TEST);
    PUSH_CONST(STENCIL_TEST);
    PUSH_CONST(RASTERIZER_DISCARD);

    // 面
    PUSH_CONST(FRONT);
    PUSH_CONST(BACK);
    PUSH_CONST(FRONT_AND_BACK);
    PUSH_CONST(CW);
    PUSH_CONST(CCW);

    // 深度 / ステンシル
    PUSH_CONST(NEVER);
    PUSH_CONST(LESS);
    PUSH_CONST(EQUAL);
    PUSH_CONST(LEQUAL);
    PUSH_CONST(GREATER);
    PUSH_CONST(NOTEQUAL);
    PUSH_CONST(GEQUAL);
    PUSH_CONST(ALWAYS);
    PUSH_CONST(KEEP);
    PUSH_CONST(REPLACE);
    PUSH_CONST(INCR);
    PUSH_CONST(DECR);
    PUSH_CONST(INVERT);
    PUSH_CONST(INCR_WRAP);
    PUSH_CONST(DECR_WRAP);

    // シェーダ
    PUSH_CONST(VERTEX_SHADER);
    PUSH_CONST(FRAGMENT_SHADER);
    PUSH_CONST(COMPILE_STATUS);
    PUSH_CONST(LINK_STATUS);
    PUSH_CONST(VALIDATE_STATUS);
    PUSH_CONST(DELETE_STATUS);
    PUSH_CONST(SHADER_TYPE);
    PUSH_CONST(ATTACHED_SHADERS);
    PUSH_CONST(ACTIVE_ATTRIBUTES);
    PUSH_CONST(ACTIVE_UNIFORMS);
    PUSH_CONST(ACTIVE_UNIFORM_BLOCKS);
    PUSH_CONST(TRANSFORM_FEEDBACK_BUFFER_MODE);
    PUSH_CONST(TRANSFORM_FEEDBACK_VARYINGS);
    PUSH_CONST(MAX_VERTEX_ATTRIBS);
    PUSH_CONST(MAX_VERTEX_UNIFORM_VECTORS);
    PUSH_CONST(MAX_VARYING_VECTORS);
    PUSH_CONST(MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_TEXTURE_IMAGE_UNITS);
    PUSH_CONST(MAX_FRAGMENT_UNIFORM_VECTORS);
    PUSH_CONST(CURRENT_PROGRAM);

    // uniform 型
    PUSH_CONST(FLOAT_VEC2);
    PUSH_CONST(FLOAT_VEC3);
    PUSH_CONST(FLOAT_VEC4);
    PUSH_CONST(INT_VEC2);
    PUSH_CONST(INT_VEC3);
    PUSH_CONST(INT_VEC4);
    PUSH_CONST(BOOL);
    PUSH_CONST(BOOL_VEC2);
    PUSH_CONST(BOOL_VEC3);
    PUSH_CONST(BOOL_VEC4);
    PUSH_CONST(FLOAT_MAT2);
    PUSH_CONST(FLOAT_MAT3);
    PUSH_CONST(FLOAT_MAT4);
    PUSH_CONST(SAMPLER_2D);
    PUSH_CONST(SAMPLER_3D);
    PUSH_CONST(SAMPLER_CUBE);
    PUSH_CONST(SAMPLER_2D_SHADOW);

    // テクスチャ
    PUSH_CONST(TEXTURE_2D);
    PUSH_CONST(TEXTURE_3D);
    PUSH_CONST(TEXTURE_CUBE_MAP);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_X);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_X);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_Y);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_Y);
    PUSH_CONST(TEXTURE_CUBE_MAP_POSITIVE_Z);
    PUSH_CONST(TEXTURE_CUBE_MAP_NEGATIVE_Z);
    PUSH_CONST(TEXTURE_2D_ARRAY);
    PUSH_CONST(TEXTURE0);
    PUSH_CONST(TEXTURE1);
    PUSH_CONST(TEXTURE2);
    PUSH_CONST(TEXTURE3);
    PUSH_CONST(TEXTURE4);
    PUSH_CONST(TEXTURE5);
    PUSH_CONST(TEXTURE6);
    PUSH_CONST(TEXTURE7);
    PUSH_CONST(TEXTURE8);
    PUSH_CONST(TEXTURE9);
    PUSH_CONST(TEXTURE10);
    PUSH_CONST(TEXTURE11);
    PUSH_CONST(TEXTURE12);
    PUSH_CONST(TEXTURE13);
    PUSH_CONST(TEXTURE14);
    PUSH_CONST(TEXTURE15);
    PUSH_CONST(TEXTURE16);
    PUSH_CONST(TEXTURE17);
    PUSH_CONST(TEXTURE18);
    PUSH_CONST(TEXTURE19);
    PUSH_CONST(TEXTURE20);
    PUSH_CONST(TEXTURE21);
    PUSH_CONST(TEXTURE22);
    PUSH_CONST(TEXTURE23);
    PUSH_CONST(TEXTURE24);
    PUSH_CONST(TEXTURE25);
    PUSH_CONST(TEXTURE26);
    PUSH_CONST(TEXTURE27);
    PUSH_CONST(TEXTURE28);
    PUSH_CONST(TEXTURE29);
    PUSH_CONST(TEXTURE30);
    PUSH_CONST(TEXTURE31);
    PUSH_CONST(ACTIVE_TEXTURE);
    PUSH_CONST(TEXTURE_MIN_FILTER);
    PUSH_CONST(TEXTURE_MAG_FILTER);
    PUSH_CONST(TEXTURE_WRAP_S);
    PUSH_CONST(TEXTURE_WRAP_T);
    PUSH_CONST(TEXTURE_WRAP_R);
    PUSH_CONST(TEXTURE_MIN_LOD);
    PUSH_CONST(TEXTURE_MAX_LOD);
    PUSH_CONST(TEXTURE_BASE_LEVEL);
    PUSH_CONST(TEXTURE_MAX_LEVEL);
    PUSH_CONST(TEXTURE_COMPARE_MODE);
    PUSH_CONST(TEXTURE_COMPARE_FUNC);
    PUSH_CONST(NEAREST);
    PUSH_CONST(LINEAR);
    PUSH_CONST(NEAREST_MIPMAP_NEAREST);
    PUSH_CONST(LINEAR_MIPMAP_NEAREST);
    PUSH_CONST(NEAREST_MIPMAP_LINEAR);
    PUSH_CONST(LINEAR_MIPMAP_LINEAR);
    PUSH_CONST(REPEAT);
    PUSH_CONST(CLAMP_TO_EDGE);
    PUSH_CONST(MIRRORED_REPEAT);
    PUSH_CONST(MAX_TEXTURE_SIZE);
    PUSH_CONST(MAX_CUBE_MAP_TEXTURE_SIZE);

    // ピクセルフォーマット
    PUSH_CONST(ALPHA);
    PUSH_CONST(RGB);
    PUSH_CONST(RGBA);
    PUSH_CONST(LUMINANCE);
    PUSH_CONST(LUMINANCE_ALPHA);
    PUSH_CONST(DEPTH_COMPONENT);
    PUSH_CONST(DEPTH_COMPONENT16);
    PUSH_CONST(DEPTH_COMPONENT24);
    PUSH_CONST(DEPTH_COMPONENT32F);
    PUSH_CONST(DEPTH_STENCIL);
    PUSH_CONST(DEPTH24_STENCIL8);
    PUSH_CONST(DEPTH32F_STENCIL8);
    PUSH_CONST(R8);
    PUSH_CONST(RG8);
    PUSH_CONST(RGB8);
    PUSH_CONST(RGBA8);
    PUSH_CONST(R16F);
    PUSH_CONST(RG16F);
    PUSH_CONST(RGB16F);
    PUSH_CONST(RGBA16F);
    PUSH_CONST(R32F);
    PUSH_CONST(RG32F);
    PUSH_CONST(RGB32F);
    PUSH_CONST(RGBA32F);
    PUSH_CONST(R8I);
    PUSH_CONST(R8UI);
    PUSH_CONST(R16I);
    PUSH_CONST(R16UI);
    PUSH_CONST(R32I);
    PUSH_CONST(R32UI);
    PUSH_CONST(RG8I);
    PUSH_CONST(RG8UI);
    PUSH_CONST(RG16I);
    PUSH_CONST(RG16UI);
    PUSH_CONST(RG32I);
    PUSH_CONST(RG32UI);
    PUSH_CONST(RGBA8I);
    PUSH_CONST(RGBA8UI);
    PUSH_CONST(RGBA16I);
    PUSH_CONST(RGBA16UI);
    PUSH_CONST(RGBA32I);
    PUSH_CONST(RGBA32UI);
    PUSH_CONST(RED);
    PUSH_CONST(RED_INTEGER);
    PUSH_CONST(RG);
    PUSH_CONST(RG_INTEGER);
    PUSH_CONST(RGB_INTEGER);
    PUSH_CONST(RGBA_INTEGER);
    PUSH_CONST(SRGB8);
    PUSH_CONST(SRGB8_ALPHA8);
    PUSH_CONST(RGB10_A2);
    PUSH_CONST(RGB10_A2UI);
    PUSH_CONST(R11F_G11F_B10F);
    PUSH_CONST(RGB9_E5);
    PUSH_CONST(UNSIGNED_SHORT_5_6_5);
    PUSH_CONST(UNSIGNED_SHORT_4_4_4_4);
    PUSH_CONST(UNSIGNED_SHORT_5_5_5_1);
    PUSH_CONST(UNSIGNED_INT_2_10_10_10_REV);
    PUSH_CONST(UNSIGNED_INT_10F_11F_11F_REV);
    PUSH_CONST(UNSIGNED_INT_5_9_9_9_REV);
    PUSH_CONST(UNSIGNED_INT_24_8);
    PUSH_CONST(FLOAT_32_UNSIGNED_INT_24_8_REV);

    // フレームバッファ
    PUSH_CONST(FRAMEBUFFER);
    PUSH_CONST(READ_FRAMEBUFFER);
    PUSH_CONST(DRAW_FRAMEBUFFER);
    PUSH_CONST(RENDERBUFFER);
    PUSH_CONST(COLOR_ATTACHMENT0);
    PUSH_CONST(COLOR_ATTACHMENT1);
    PUSH_CONST(COLOR_ATTACHMENT2);
    PUSH_CONST(COLOR_ATTACHMENT3);
    PUSH_CONST(COLOR_ATTACHMENT4);
    PUSH_CONST(COLOR_ATTACHMENT5);
    PUSH_CONST(COLOR_ATTACHMENT6);
    PUSH_CONST(COLOR_ATTACHMENT7);
    PUSH_CONST(COLOR_ATTACHMENT8);
    PUSH_CONST(COLOR_ATTACHMENT9);
    PUSH_CONST(COLOR_ATTACHMENT10);
    PUSH_CONST(COLOR_ATTACHMENT11);
    PUSH_CONST(COLOR_ATTACHMENT12);
    PUSH_CONST(COLOR_ATTACHMENT13);
    PUSH_CONST(COLOR_ATTACHMENT14);
    PUSH_CONST(COLOR_ATTACHMENT15);
    PUSH_CONST(DEPTH_ATTACHMENT);
    PUSH_CONST(STENCIL_ATTACHMENT);
    PUSH_CONST(DEPTH_STENCIL_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_COMPLETE);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
    PUSH_CONST(FRAMEBUFFER_INCOMPLETE_DIMENSIONS);
    PUSH_CONST(FRAMEBUFFER_UNSUPPORTED);
    PUSH_CONST(FRAMEBUFFER_BINDING);
    PUSH_CONST(RENDERBUFFER_BINDING);
    PUSH_CONST(MAX_RENDERBUFFER_SIZE);
    PUSH_CONST(NONE);

    // ピクセルストア
    PUSH_CONST(PACK_ALIGNMENT);
    PUSH_CONST(UNPACK_ALIGNMENT);
    PUSH_CONST(PACK_ROW_LENGTH);
    PUSH_CONST(PACK_SKIP_PIXELS);
    PUSH_CONST(PACK_SKIP_ROWS);
    PUSH_CONST(UNPACK_ROW_LENGTH);
    PUSH_CONST(UNPACK_IMAGE_HEIGHT);
    PUSH_CONST(UNPACK_SKIP_PIXELS);
    PUSH_CONST(UNPACK_SKIP_ROWS);
    PUSH_CONST(UNPACK_SKIP_IMAGES);

    // エラー
    PUSH_CONST(NO_ERROR);
    PUSH_CONST(INVALID_ENUM);
    PUSH_CONST(INVALID_VALUE);
    PUSH_CONST(INVALID_OPERATION);
    PUSH_CONST(INVALID_FRAMEBUFFER_OPERATION);
    PUSH_CONST(OUT_OF_MEMORY);

    // Transform Feedback
    PUSH_CONST(INTERLEAVED_ATTRIBS);
    PUSH_CONST(SEPARATE_ATTRIBS);

    // Misc
    PUSH_CONST(VENDOR);
    PUSH_CONST(RENDERER);
    PUSH_CONST(VERSION);
    PUSH_CONST(SHADING_LANGUAGE_VERSION);
    PUSH_CONST(GENERATE_MIPMAP_HINT);
    PUSH_CONST(FASTEST);
    PUSH_CONST(NICEST);
    PUSH_CONST(DONT_CARE);

    // Compare mode
    PUSH_CONST(COMPARE_REF_TO_TEXTURE);

    // color
    PUSH_CONST(COLOR);
    PUSH_CONST(DEPTH);
    PUSH_CONST(STENCIL);

    // getParameter で使う追加定数
    PUSH_CONST(VIEWPORT);
    PUSH_CONST(SCISSOR_BOX);
    PUSH_CONST(COLOR_CLEAR_VALUE);
    PUSH_CONST(COLOR_WRITEMASK);
    PUSH_CONST(BLEND_COLOR);
    PUSH_CONST(STENCIL_BACK_FAIL);
    PUSH_CONST(STENCIL_BACK_FUNC);
    PUSH_CONST(STENCIL_BACK_PASS_DEPTH_FAIL);
    PUSH_CONST(STENCIL_BACK_PASS_DEPTH_PASS);
    PUSH_CONST(STENCIL_BACK_REF);
    PUSH_CONST(STENCIL_BACK_VALUE_MASK);
    PUSH_CONST(STENCIL_BACK_WRITEMASK);
    PUSH_CONST(STENCIL_CLEAR_VALUE);
    PUSH_CONST(STENCIL_VALUE_MASK);
    PUSH_CONST(STENCIL_WRITEMASK);

    // WebGL2 追加定数
    PUSH_CONST(MAX_3D_TEXTURE_SIZE);
    PUSH_CONST(MAX_ARRAY_TEXTURE_LAYERS);
    PUSH_CONST(MAX_COLOR_ATTACHMENTS);
    PUSH_CONST(MAX_DRAW_BUFFERS);
    PUSH_CONST(MAX_ELEMENTS_INDICES);
    PUSH_CONST(MAX_ELEMENTS_VERTICES);
    PUSH_CONST(MAX_FRAGMENT_UNIFORM_COMPONENTS);
    PUSH_CONST(MAX_SAMPLES);
    PUSH_CONST(MAX_UNIFORM_BLOCK_SIZE);
    PUSH_CONST(MAX_UNIFORM_BUFFER_BINDINGS);
    PUSH_CONST(MAX_VERTEX_UNIFORM_COMPONENTS);
    PUSH_CONST(MAX_VARYING_COMPONENTS);
    PUSH_CONST(MAX_VERTEX_OUTPUT_COMPONENTS);
    PUSH_CONST(MAX_FRAGMENT_INPUT_COMPONENTS);
    PUSH_CONST(TEXTURE_BINDING_2D);
    PUSH_CONST(TEXTURE_BINDING_CUBE_MAP);
    PUSH_CONST(TEXTURE_BINDING_3D);
    PUSH_CONST(TEXTURE_BINDING_2D_ARRAY);
    PUSH_CONST(UNPACK_IMAGE_HEIGHT);
    PUSH_CONST(UNPACK_SKIP_IMAGES);
    PUSH_CONST(VERTEX_ARRAY_BINDING);

    // WebGL 固有定数（GLES ヘッダに無い）
    JS_SetPropertyStr(ctx, gl, "UNPACK_FLIP_Y_WEBGL", JS_NewUint32(ctx, 0x9240));
    JS_SetPropertyStr(ctx, gl, "UNPACK_PREMULTIPLY_ALPHA_WEBGL", JS_NewUint32(ctx, 0x9241));

    // OES_element_index_uint (UNSIGNED_INT は既に上で登録済み)

    // sync
    PUSH_CONST(SYNC_GPU_COMMANDS_COMPLETE);
    PUSH_CONST(ALREADY_SIGNALED);
    PUSH_CONST(TIMEOUT_EXPIRED);
    PUSH_CONST(CONDITION_SATISFIED);
    PUSH_CONST(WAIT_FAILED);

#undef PUSH_CONST
}

// ============================================================
// バインド登録: dukwebgl_bind
// ============================================================

#define BIND_FUNC(gl, jsName, cFunc, nargs) \
    JS_SetPropertyStr(ctx, gl, #jsName, JS_NewCFunction(ctx, cFunc, #jsName, nargs))

void dukwebgl_bind(JSContext *ctx) {
    // gl オブジェクトを直接作成
    JSValue gl = JS_NewObject(ctx);

    // 定数登録
    bind_constants(ctx, gl);

    // コンテキスト情報
    BIND_FUNC(gl, getContextAttributes, js_getContextAttributes, 0);
    BIND_FUNC(gl, isContextLost, js_isContextLost, 0);
    BIND_FUNC(gl, getSupportedExtensions, js_getSupportedExtensions, 0);
    BIND_FUNC(gl, getExtension, js_getExtension, 1);
    BIND_FUNC(gl, getParameter, js_getParameter, 1);
    BIND_FUNC(gl, getError, js_getError, 0);

    // シェーダ
    BIND_FUNC(gl, createShader, js_createShader, 1);
    BIND_FUNC(gl, deleteShader, js_deleteShader, 1);
    BIND_FUNC(gl, shaderSource, js_shaderSource, 2);
    BIND_FUNC(gl, compileShader, js_compileShader, 1);
    BIND_FUNC(gl, getShaderParameter, js_getShaderParameter, 2);
    BIND_FUNC(gl, getShaderInfoLog, js_getShaderInfoLog, 1);
    BIND_FUNC(gl, getShaderSource, js_getShaderSource, 1);
    BIND_FUNC(gl, isShader, js_isShader, 1);

    // プログラム
    BIND_FUNC(gl, createProgram, js_createProgram, 0);
    BIND_FUNC(gl, deleteProgram, js_deleteProgram, 1);
    BIND_FUNC(gl, attachShader, js_attachShader, 2);
    BIND_FUNC(gl, detachShader, js_detachShader, 2);
    BIND_FUNC(gl, linkProgram, js_linkProgram, 1);
    BIND_FUNC(gl, useProgram, js_useProgram, 1);
    BIND_FUNC(gl, validateProgram, js_validateProgram, 1);
    BIND_FUNC(gl, isProgram, js_isProgram, 1);
    BIND_FUNC(gl, getProgramParameter, js_getProgramParameter, 2);
    BIND_FUNC(gl, getProgramInfoLog, js_getProgramInfoLog, 1);
    BIND_FUNC(gl, bindAttribLocation, js_bindAttribLocation, 3);
    BIND_FUNC(gl, getAttribLocation, js_getAttribLocation, 2);
    BIND_FUNC(gl, getUniformLocation, js_getUniformLocation, 2);
    BIND_FUNC(gl, getActiveAttrib, js_getActiveAttrib, 2);
    BIND_FUNC(gl, getActiveUniform, js_getActiveUniform, 2);

    // バッファ
    BIND_FUNC(gl, createBuffer, js_createBuffer, 0);
    BIND_FUNC(gl, deleteBuffer, js_deleteBuffer, 1);
    BIND_FUNC(gl, isBuffer, js_isBuffer, 1);
    BIND_FUNC(gl, bindBuffer, js_bindBuffer, 2);
    BIND_FUNC(gl, bufferData, js_bufferData, 5);
    BIND_FUNC(gl, bufferSubData, js_bufferSubData, 5);

    // テクスチャ
    BIND_FUNC(gl, createTexture, js_createTexture, 0);
    BIND_FUNC(gl, deleteTexture, js_deleteTexture, 1);
    BIND_FUNC(gl, isTexture, js_isTexture, 1);
    BIND_FUNC(gl, bindTexture, js_bindTexture, 2);
    BIND_FUNC(gl, activeTexture, js_activeTexture, 1);
    BIND_FUNC(gl, texParameteri, js_texParameteri, 3);
    BIND_FUNC(gl, texParameterf, js_texParameterf, 3);
    BIND_FUNC(gl, generateMipmap, js_generateMipmap, 1);
    BIND_FUNC(gl, texImage2D, js_texImage2D, 10);
    BIND_FUNC(gl, texSubImage2D, js_texSubImage2D, 10);
    BIND_FUNC(gl, texImage3D, js_texImage3D, 10);
    BIND_FUNC(gl, copyTexImage2D, js_copyTexImage2D, 8);
    BIND_FUNC(gl, copyTexSubImage2D, js_copyTexSubImage2D, 8);
    BIND_FUNC(gl, pixelStorei, js_pixelStorei, 2);
    BIND_FUNC(gl, readPixels, js_readPixels, 7);

    // フレームバッファ / レンダーバッファ
    BIND_FUNC(gl, createFramebuffer, js_createFramebuffer, 0);
    BIND_FUNC(gl, deleteFramebuffer, js_deleteFramebuffer, 1);
    BIND_FUNC(gl, isFramebuffer, js_isFramebuffer, 1);
    BIND_FUNC(gl, bindFramebuffer, js_bindFramebuffer, 2);
    BIND_FUNC(gl, framebufferTexture2D, js_framebufferTexture2D, 5);
    BIND_FUNC(gl, framebufferRenderbuffer, js_framebufferRenderbuffer, 4);
    BIND_FUNC(gl, checkFramebufferStatus, js_checkFramebufferStatus, 1);
    BIND_FUNC(gl, createRenderbuffer, js_createRenderbuffer, 0);
    BIND_FUNC(gl, deleteRenderbuffer, js_deleteRenderbuffer, 1);
    BIND_FUNC(gl, isRenderbuffer, js_isRenderbuffer, 1);
    BIND_FUNC(gl, bindRenderbuffer, js_bindRenderbuffer, 2);
    BIND_FUNC(gl, renderbufferStorage, js_renderbufferStorage, 4);
    BIND_FUNC(gl, drawBuffers, js_drawBuffers, 1);
    BIND_FUNC(gl, blitFramebuffer, js_blitFramebuffer, 10);

    // VAO
    BIND_FUNC(gl, createVertexArray, js_createVertexArray, 0);
    BIND_FUNC(gl, deleteVertexArray, js_deleteVertexArray, 1);
    BIND_FUNC(gl, isVertexArray, js_isVertexArray, 1);
    BIND_FUNC(gl, bindVertexArray, js_bindVertexArray, 1);

    // 頂点属性
    BIND_FUNC(gl, enableVertexAttribArray, js_enableVertexAttribArray, 1);
    BIND_FUNC(gl, disableVertexAttribArray, js_disableVertexAttribArray, 1);
    BIND_FUNC(gl, vertexAttribPointer, js_vertexAttribPointer, 6);
    BIND_FUNC(gl, vertexAttribIPointer, js_vertexAttribIPointer, 5);
    BIND_FUNC(gl, vertexAttribDivisor, js_vertexAttribDivisor, 2);
    BIND_FUNC(gl, vertexAttrib1f, js_vertexAttrib1f, 2);
    BIND_FUNC(gl, vertexAttrib2f, js_vertexAttrib2f, 3);
    BIND_FUNC(gl, vertexAttrib3f, js_vertexAttrib3f, 4);
    BIND_FUNC(gl, vertexAttrib4f, js_vertexAttrib4f, 5);

    // Uniform (scalar)
    BIND_FUNC(gl, uniform1i, js_uniform1i, 2);
    BIND_FUNC(gl, uniform2i, js_uniform2i, 3);
    BIND_FUNC(gl, uniform3i, js_uniform3i, 4);
    BIND_FUNC(gl, uniform4i, js_uniform4i, 5);
    BIND_FUNC(gl, uniform1f, js_uniform1f, 2);
    BIND_FUNC(gl, uniform2f, js_uniform2f, 3);
    BIND_FUNC(gl, uniform3f, js_uniform3f, 4);
    BIND_FUNC(gl, uniform4f, js_uniform4f, 5);

    // Uniform (vector)
    BIND_FUNC(gl, uniform1fv, js_uniform1fv, 2);
    BIND_FUNC(gl, uniform2fv, js_uniform2fv, 2);
    BIND_FUNC(gl, uniform3fv, js_uniform3fv, 2);
    BIND_FUNC(gl, uniform4fv, js_uniform4fv, 2);
    BIND_FUNC(gl, uniform1iv, js_uniform1iv, 2);
    BIND_FUNC(gl, uniform2iv, js_uniform2iv, 2);
    BIND_FUNC(gl, uniform3iv, js_uniform3iv, 2);
    BIND_FUNC(gl, uniform4iv, js_uniform4iv, 2);
    BIND_FUNC(gl, uniform1uiv, js_uniform1uiv, 2);
    BIND_FUNC(gl, uniform2uiv, js_uniform2uiv, 2);
    BIND_FUNC(gl, uniform3uiv, js_uniform3uiv, 2);
    BIND_FUNC(gl, uniform4uiv, js_uniform4uiv, 2);

    // Uniform (matrix)
    BIND_FUNC(gl, uniformMatrix2fv, js_uniformMatrix2fv, 3);
    BIND_FUNC(gl, uniformMatrix3fv, js_uniformMatrix3fv, 3);
    BIND_FUNC(gl, uniformMatrix4fv, js_uniformMatrix4fv, 3);
    BIND_FUNC(gl, uniformMatrix2x3fv, js_uniformMatrix2x3fv, 3);
    BIND_FUNC(gl, uniformMatrix2x4fv, js_uniformMatrix2x4fv, 3);
    BIND_FUNC(gl, uniformMatrix3x2fv, js_uniformMatrix3x2fv, 3);
    BIND_FUNC(gl, uniformMatrix3x4fv, js_uniformMatrix3x4fv, 3);
    BIND_FUNC(gl, uniformMatrix4x2fv, js_uniformMatrix4x2fv, 3);
    BIND_FUNC(gl, uniformMatrix4x3fv, js_uniformMatrix4x3fv, 3);

    // 描画
    BIND_FUNC(gl, drawArrays, js_drawArrays, 3);
    BIND_FUNC(gl, drawElements, js_drawElements, 4);
    BIND_FUNC(gl, drawArraysInstanced, js_drawArraysInstanced, 4);
    BIND_FUNC(gl, drawElementsInstanced, js_drawElementsInstanced, 5);

    // ステート
    BIND_FUNC(gl, enable, js_enable, 1);
    BIND_FUNC(gl, disable, js_disable, 1);
    BIND_FUNC(gl, isEnabled, js_isEnabled, 1);
    BIND_FUNC(gl, viewport, js_viewport, 4);
    BIND_FUNC(gl, scissor, js_scissor, 4);
    BIND_FUNC(gl, clearColor, js_clearColor, 4);
    BIND_FUNC(gl, clearDepth, js_clearDepth, 1);
    BIND_FUNC(gl, clearStencil, js_clearStencil, 1);
    BIND_FUNC(gl, clear, js_clear, 1);
    BIND_FUNC(gl, colorMask, js_colorMask, 4);
    BIND_FUNC(gl, depthMask, js_depthMask, 1);
    BIND_FUNC(gl, depthFunc, js_depthFunc, 1);
    BIND_FUNC(gl, depthRange, js_depthRange, 2);
    BIND_FUNC(gl, blendFunc, js_blendFunc, 2);
    BIND_FUNC(gl, blendFuncSeparate, js_blendFuncSeparate, 4);
    BIND_FUNC(gl, blendEquation, js_blendEquation, 1);
    BIND_FUNC(gl, blendEquationSeparate, js_blendEquationSeparate, 2);
    BIND_FUNC(gl, blendColor, js_blendColor, 4);
    BIND_FUNC(gl, stencilFunc, js_stencilFunc, 3);
    BIND_FUNC(gl, stencilFuncSeparate, js_stencilFuncSeparate, 4);
    BIND_FUNC(gl, stencilMask, js_stencilMask, 1);
    BIND_FUNC(gl, stencilMaskSeparate, js_stencilMaskSeparate, 2);
    BIND_FUNC(gl, stencilOp, js_stencilOp, 3);
    BIND_FUNC(gl, stencilOpSeparate, js_stencilOpSeparate, 4);
    BIND_FUNC(gl, cullFace, js_cullFace, 1);
    BIND_FUNC(gl, frontFace, js_frontFace, 1);
    BIND_FUNC(gl, lineWidth, js_lineWidth, 1);
    BIND_FUNC(gl, polygonOffset, js_polygonOffset, 2);
    BIND_FUNC(gl, sampleCoverage, js_sampleCoverage, 2);
    BIND_FUNC(gl, hint, js_hint, 2);
    BIND_FUNC(gl, flush, js_flush, 0);
    BIND_FUNC(gl, finish, js_finish, 0);

    // clearBuffer (WebGL2)
    BIND_FUNC(gl, clearBufferfv, js_clearBufferfv, 3);
    BIND_FUNC(gl, clearBufferiv, js_clearBufferiv, 3);
    BIND_FUNC(gl, clearBufferuiv, js_clearBufferuiv, 3);
    BIND_FUNC(gl, clearBufferfi, js_clearBufferfi, 4);

    // Uniform Block
    BIND_FUNC(gl, getUniformBlockIndex, js_getUniformBlockIndex, 2);
    BIND_FUNC(gl, uniformBlockBinding, js_uniformBlockBinding, 3);
    BIND_FUNC(gl, bindBufferBase, js_bindBufferBase, 3);
    BIND_FUNC(gl, bindBufferRange, js_bindBufferRange, 5);

    // Transform Feedback
    BIND_FUNC(gl, createTransformFeedback, js_createTransformFeedback, 0);
    BIND_FUNC(gl, deleteTransformFeedback, js_deleteTransformFeedback, 1);
    BIND_FUNC(gl, bindTransformFeedback, js_bindTransformFeedback, 2);
    BIND_FUNC(gl, beginTransformFeedback, js_beginTransformFeedback, 1);
    BIND_FUNC(gl, endTransformFeedback, js_endTransformFeedback, 0);
    BIND_FUNC(gl, transformFeedbackVaryings, js_transformFeedbackVaryings, 3);

    // Query
    BIND_FUNC(gl, createQuery, js_createQuery, 0);
    BIND_FUNC(gl, deleteQuery, js_deleteQuery, 1);
    BIND_FUNC(gl, beginQuery, js_beginQuery, 2);
    BIND_FUNC(gl, endQuery, js_endQuery, 1);

    // Sampler
    BIND_FUNC(gl, createSampler, js_createSampler, 0);
    BIND_FUNC(gl, deleteSampler, js_deleteSampler, 1);
    BIND_FUNC(gl, bindSampler, js_bindSampler, 2);
    BIND_FUNC(gl, samplerParameteri, js_samplerParameteri, 3);
    BIND_FUNC(gl, samplerParameterf, js_samplerParameterf, 3);

    // gl としてグローバル登録
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "gl", gl);

    // WebGL2RenderingContext コンストラクタ（互換性のため）
    const char *ctor_src = "(function WebGL2RenderingContext(){})";
    JSValue ctor = JS_Eval(ctx, ctor_src, strlen(ctor_src), "<webgl>", JS_EVAL_TYPE_GLOBAL);
    JS_SetPropertyStr(ctx, global, "WebGL2RenderingContext", ctor);

    JS_FreeValue(ctx, global);
}

#undef BIND_FUNC
